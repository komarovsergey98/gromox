// SPDX-License-Identifier: AGPL-3.0-or-later, OR GPL-2.0-or-later WITH linking exception
// SPDX-FileCopyrightText: 2020–2021 grommunio GmbH
// This file is part of Gromox.
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <libHX/defs.h>
#include <libHX/scope.hpp>
#include <gromox/exmdb_common_util.hpp>
#include <gromox/exmdb_server.hpp>
#include <gromox/fileio.h>
#include <gromox/mail_func.hpp>
#include <gromox/mapidefs.h>
#include <gromox/rop_util.hpp>
#include <gromox/tie.hpp>

using namespace gromox;

namespace {
struct instbody_delete : public stdlib_delete {
	using stdlib_delete::operator();
	inline void operator()(BINARY *x) const { rop_util_free_binary(x); }
};
}

unsigned int exmdb_body_autosynthesis;

/* Get an arbitrary body, no fallbacks. */
static int instance_get_raw(MESSAGE_CONTENT *mc, BINARY *&bin, unsigned int tag)
{
	auto cid = mc->proplist.get<const char>(tag);
	if (cid == nullptr)
		return 0;
	uint32_t length = 0;
	auto content = instance_read_cid_content(cid, &length, tag);
	if (content == nullptr)
		return -1;
	bin = cu_alloc<BINARY>();
	if (bin == nullptr)
		return -1;
	bin->cb = length;
	bin->pv = content;
	return 1;
}

/* Get uncompressed RTF body, no fallbacks. */
static int instance_get_rtf(MESSAGE_CONTENT *mc, BINARY *&bin)
{
	auto ret = instance_get_raw(mc, bin, ID_TAG_RTFCOMPRESSED);
	if (ret <= 0)
		return ret;
	BINARY rtf_comp = *bin;
	ssize_t unc_size = rtfcp_uncompressed_size(&rtf_comp);
	if (unc_size < 0)
		return -1;
	bin->pv = common_util_alloc(unc_size);
	if (bin->pv == nullptr)
		return -1;
	size_t unc_size2 = unc_size;
	if (!rtfcp_uncompress(&rtf_comp, bin->pc, &unc_size2))
		return -1;
	bin->cb = unc_size2;
	return 1;
}

static int instance_conv_htmlfromhigher(MESSAGE_CONTENT *mc, BINARY *&bin)
{
	auto ret = instance_get_rtf(mc, bin);
	if (ret <= 0)
		return ret;
	std::string outbuf;
	auto at = attachment_list_init();
	auto at_clean = HX::make_scope_exit([&]() { attachment_list_free(at); });
	if (!rtf_to_html(bin->pc, bin->cb, "utf-8", outbuf, at))
		return -1;
	bin->cb = outbuf.size() < UINT32_MAX ? outbuf.size() : UINT32_MAX;
	bin->pv = common_util_alloc(bin->cb);
	if (bin->pv == nullptr)
		return -1;
	memcpy(bin->pv, outbuf.c_str(), bin->cb);
	return 1;
}

/* Always yields UTF-8 */
static int instance_conv_textfromhigher(MESSAGE_CONTENT *mc, BINARY *&bin)
{
	auto ret = instance_get_raw(mc, bin, ID_TAG_HTML);
	if (exmdb_body_autosynthesis && ret == 0)
		ret = instance_conv_htmlfromhigher(mc, bin);
	if (ret <= 0)
		return ret;
	std::string plainbuf;
	ret = html_to_plain(bin->pc, bin->cb, plainbuf);
	if (ret < 0)
		return 0;
	auto cpraw = mc->proplist.get<const uint32_t>(PR_INTERNET_CPID);
	cpid_t orig_cpid = cpraw != nullptr ? static_cast<cpid_t>(*cpraw) : CP_UTF8;
	if (ret != CP_UTF8 && orig_cpid != CP_UTF8) {
		bin->pv = common_util_convert_copy(TRUE, orig_cpid, plainbuf.c_str());
		return bin->pv != nullptr ? 1 : -1;
	}
	/* Original already was UTF-8, or conversion to UTF-8 happened by htmltoplain */
	bin->pv = common_util_alloc(plainbuf.size() + 1);
	if (bin->pv == nullptr)
		return -1;
	memcpy(bin->pv, plainbuf.c_str(), plainbuf.size() + 1);
	return 1;
}

static int instance_conv_htmlfromlower(MESSAGE_CONTENT *mc,
    cpid_t cpid, BINARY *&bin)
{
	auto ret = instance_get_raw(mc, bin, ID_TAG_BODY);
	if (ret == 0) {
		ret = instance_get_raw(mc, bin, ID_TAG_BODY_STRING8);
		if (ret > 0) {
			bin->pc = common_util_convert_copy(true, cpid, bin->pc);
			if (bin->pc == nullptr)
				return -1;
		}
	}
	if (ret <= 0)
		return ret;
	std::unique_ptr<char[], instbody_delete> htmlout(plain_to_html(bin->pc));
	if (htmlout == nullptr)
		return -1;
	bin->pc = common_util_convert_copy(false, cpid, htmlout.get());
	if (bin->pc == nullptr)
		return -1;
	/* instance_get_raw / instance_read_cid_content guaranteed trailing \0 */
	bin->cb = strlen(bin->pc);
	return 1;
}

static int instance_conv_rtfcpfromlower(MESSAGE_CONTENT *mc,
    cpid_t cpid, BINARY *&bin)
{
	auto ret = instance_conv_htmlfromlower(mc, cpid, bin);
	if (ret <= 0)
		return ret;
	std::unique_ptr<char[], instbody_delete> rtfout;
	size_t rtflen = 0;
	if (html_to_rtf(bin->pc, bin->cb, cpid, &unique_tie(rtfout), &rtflen) != ecSuccess)
		return -1;
	std::unique_ptr<BINARY, instbody_delete> rtfcpbin(rtfcp_compress(rtfout.get(), rtflen));
	if (rtfcpbin == nullptr)
		return -1;
	bin->cb = rtfcpbin->cb;
	bin->pv = common_util_alloc(rtfcpbin->cb);
	if (bin->pv == nullptr)
		return -1;
	memcpy(bin->pv, rtfcpbin->pv, rtfcpbin->cb);
	return 1;
}

/* Get any plaintext body, fallback to autogeneration. */
static int instance_get_body_unspec(MESSAGE_CONTENT *mc, TPROPVAL_ARRAY *pval)
{
	BINARY *bin = nullptr;
	auto ret = instance_get_raw(mc, bin, ID_TAG_BODY);
	auto unicode_body = ret > 0;
	if (ret == 0)
		ret = instance_get_raw(mc, bin, ID_TAG_BODY_STRING8);
	if (exmdb_body_autosynthesis && ret == 0) {
		ret = instance_conv_textfromhigher(mc, bin);
		if (ret > 0)
			unicode_body = true;
	}
	if (ret <= 0)
		return ret;

	/* Strictly required to respond with the same proptag as was requested */
	auto tpv = cu_alloc<TYPED_PROPVAL>();
	if (tpv == nullptr)
		return -1;
	tpv->type   = unicode_body ? PT_UNICODE : PT_STRING8;
	tpv->pvalue = bin->pv;
	pval->emplace_back(CHANGE_PROP_TYPE(PR_BODY, PT_UNSPECIFIED), tpv);
	return 1;
}

/* Get UTF plaintext body, fallback to autogeneration. */
static int instance_get_body_utf8(MESSAGE_CONTENT *mc, cpid_t cpid,
    TPROPVAL_ARRAY *pval)
{
	BINARY *bin = nullptr;
	int ret = instance_get_raw(mc, bin, ID_TAG_BODY);
	if (ret == 0) {
		ret = instance_get_raw(mc, bin, ID_TAG_BODY_STRING8);
		if (ret > 0) {
			bin->pc = common_util_convert_copy(true, cpid, bin->pc);
			if (bin->pc == nullptr)
				return -1;
		}
	}
	if (exmdb_body_autosynthesis && ret == 0)
		ret = instance_conv_textfromhigher(mc, bin);
	if (ret <= 0)
		return ret;
	pval->emplace_back(PR_BODY_W, bin->pc);
	return 1;
}

/* Get 8-bit plaintext body, fallback to autogeneration. */
static int instance_get_body_8bit(MESSAGE_CONTENT *mc, cpid_t cpid,
    TPROPVAL_ARRAY *pval)
{
	BINARY *bin = nullptr;
	auto ret = instance_get_raw(mc, bin, ID_TAG_BODY_STRING8);
	if (ret == 0) {
		ret = instance_get_raw(mc, bin, ID_TAG_BODY);
		if (ret > 0) {
			bin->pc = common_util_convert_copy(false, cpid, bin->pc);
			if (bin->pc == nullptr)
				return -1;
		}
	}
	if (ret == 0) {
		ret = instance_conv_textfromhigher(mc, bin);
		if (ret > 0) {
			bin->pc = common_util_convert_copy(false, cpid, bin->pc);
			if (bin->pc == nullptr)
				return -1;
		}
	}
	if (ret <= 0)
		return ret;
	pval->emplace_back(PR_BODY_A, bin->pc);
	return 1;
}

static int instance_get_html(MESSAGE_CONTENT *mc, cpid_t cpid,
    TPROPVAL_ARRAY *pval)
{
	BINARY *bin = nullptr;
	auto ret = instance_get_raw(mc, bin, ID_TAG_HTML);
	if (exmdb_body_autosynthesis) {
		if (ret == 0)
			ret = instance_conv_htmlfromhigher(mc, bin);
		if (ret == 0)
			ret = instance_conv_htmlfromlower(mc, cpid, bin);
	}
	if (ret <= 0)
		return ret;
	pval->emplace_back(PR_HTML, bin);
	return 1;
}

static int instance_get_html_unspec(MESSAGE_CONTENT *mc, cpid_t cpid,
    TPROPVAL_ARRAY *pval)
{
	auto ret = instance_get_html(mc, cpid, pval);
	if (ret <= 0)
		return ret;
	auto tpv = cu_alloc<TYPED_PROPVAL>();
	if (tpv == nullptr)
		return -1;
	/* Overwrite what instance_get_html has written to. */
	auto &pv = pval->ppropval[pval->count-1];
	tpv->type   = PT_BINARY;
	tpv->pvalue = pv.pvalue;
	pv.proptag  = CHANGE_PROP_TYPE(PR_HTML, PT_UNSPECIFIED);
	pv.pvalue   = tpv;
	return 1;
}

/* Get RTFCP, fallback to autogeneration. */
static int instance_get_rtfcp(MESSAGE_CONTENT *mc, cpid_t cpid,
    TPROPVAL_ARRAY *pval)
{
	BINARY *bin = nullptr;
	auto ret = instance_get_raw(mc, bin, ID_TAG_RTFCOMPRESSED);
	if (exmdb_body_autosynthesis && ret == 0)
		ret = instance_conv_rtfcpfromlower(mc, cpid, bin);
	if (ret <= 0)
		return ret;
	pval->emplace_back(PR_RTF_COMPRESSED, bin);
	return 1;
}

int instance_get_message_body(MESSAGE_CONTENT *mc, unsigned int tag,
    cpid_t cpid, TPROPVAL_ARRAY *pv)
{
	switch (tag) {
	case PR_BODY_A:
		return instance_get_body_8bit(mc, cpid, pv);
	case PR_BODY_W:
		return instance_get_body_utf8(mc, cpid, pv);
	case CHANGE_PROP_TYPE(PR_BODY, PT_UNSPECIFIED):
		return instance_get_body_unspec(mc, pv);
	case PR_HTML:
		return instance_get_html(mc, cpid, pv);
	case CHANGE_PROP_TYPE(PR_HTML, PT_UNSPECIFIED):
		return instance_get_html_unspec(mc, cpid, pv);
	case PR_RTF_COMPRESSED:
		return instance_get_rtfcp(mc, cpid, pv);
	}
	return -1;
}
