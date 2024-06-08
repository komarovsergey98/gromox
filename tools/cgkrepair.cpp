// SPDX-License-Identifier: AGPL-3.0-or-later
// SPDX-FileCopyrightText: 2021–2024 grommunio GmbH
// This file is part of Gromox.
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <libHX/option.h>
#include <gromox/exmdb_rpc.hpp>
#include <gromox/scope.hpp>
#include "genimport.hpp"

namespace exmdb_client = exmdb_client_remote;
using namespace gromox;
static constexpr cpid_t codepage = CP_UTF8;
static unsigned int g_dry_run;
static char *g_primail;

static constexpr HXoption g_options_table[] = {
	{nullptr, 'e', HXTYPE_STRING, &g_primail, nullptr, nullptr, 0, "Primary e-mail address of store", "ADDR"},
	{nullptr, 'n', HXTYPE_NONE, &g_dry_run, nullptr, nullptr, 0, "Perform a dry run"},
	HXOPT_AUTOHELP,
	HXOPT_TABLEEND,
};

static inline bool change_key_size_ok(const BINARY &b)
{
	/* Not much else to do. MS-OXCFXICS v24 §2.2.2.2 */
	return b.cb >= 16 && b.cb <= 24;
}

static inline bool change_key_gc_ok(const BINARY &b)
{
	static constexpr uint8_t
		/* cf. rop_util_make_user_guid and _domain_guid */
		gx_pvt_mbox[] = {0xFB, 0x0A, 0x09, 0x00, 0x91, 0x92, 0x49, 0x88, 0x6A, 0xA7, 0x38, 0xCE},
		gx_pub_mbox[] = {0xFB, 0x0A, 0xF6, 0x7D, 0x91, 0x92, 0x49, 0x88, 0x6A, 0xA7, 0x38, 0xCE};
	/*
	 * Don't bother with CHANGE_KEYs not generated by Gromox.
	 * All Gromox PR_CHANGE_KEYs are built from Gromox database GUIDs;
	 * all these GUIDs have a fixed 128-bit segment.
	 */
	if (b.cb != 22)
		return true;
	if (memcmp(&b.pb[4], gx_pvt_mbox, std::size(gx_pvt_mbox)) != 0 &&
	    memcmp(&b.pb[4], gx_pub_mbox, std::size(gx_pub_mbox)) != 0)
		return true;
	/*
	 * Someone once decided to declare CHANGE_NUMBER_BEGIN to be 1<<47.
	 * Oddly enough, that now gives us the possibility to check for it.
	 */
	return b.pb[16] & 0x80U;
}

static inline int pcl_ok(const BINARY *b)
{
	return b != nullptr ? PCL().deserialize(b) : true;
}

static int repair_folder(uint64_t fid)
{
	tpropval_array_ptr props(tpropval_array_init());
	if (props == nullptr)
		return -ENOMEM;
	uint64_t change_num = 0;
	if (!exmdb_client::allocate_cn(g_storedir, &change_num)) {
		fprintf(stderr, "exm: allocate_cn(fld) RPC failed\n");
		return -EIO;
	}
	auto ret = exm_set_change_keys(props.get(), change_num);
	if (ret < 0)
		return ret;
	PROBLEM_ARRAY problems;
	if (!exmdb_client::set_folder_properties(g_storedir,
	    codepage, fid, props.get(), &problems)) {
		fprintf(stderr, "exm: set_folder_properties RPC failed\n");
		return -EIO;
	}
	printf(" (new key: %llxh)\n", static_cast<unsigned long long>(rop_util_get_gc_value(change_num)));
	return 0;
}

static int repair_mbox()
{
	static constexpr uint32_t tags[] =
		{PidTagFolderId, PR_CHANGE_KEY, PR_PREDECESSOR_CHANGE_LIST};
	static constexpr PROPTAG_ARRAY ptags = {3, deconst(tags)};
	uint32_t table_id = 0, row_num = 0;
	uint64_t root_fld = g_public_folder ? rop_util_make_eid_ex(1, PRIVATE_FID_ROOT) :
	                    rop_util_make_eid_ex(1, PUBLIC_FID_ROOT);
	/*
	 * This does not return the root entry itself, just its subordinates.
	 * Might want to refine later.
	 */
	if (!exmdb_client::load_hierarchy_table(g_storedir, root_fld,
	    nullptr, TABLE_FLAG_DEPTH | TABLE_FLAG_NONOTIFICATIONS,
	    nullptr, &table_id, &row_num)) {
		fprintf(stderr, "exm: load_hierarchy_table RPC failed\n");
		return -EIO;
	}
	TARRAY_SET tset{};
	if (!exmdb_client::query_table(g_storedir, nullptr, codepage, table_id,
	    &ptags, 0, row_num, &tset)) {
		fprintf(stderr, "exm: query_table RPC failed\n");
		return -EIO;
	}
	exmdb_client::unload_table(g_storedir, table_id);

	printf("Hierarchy discovery: %u folders\n", tset.count);
	for (size_t i = 0; i < tset.count; ++i) {
		auto fid = tset.pparray[i]->get<uint64_t>(PidTagFolderId);
		if (fid == nullptr)
			continue;
		auto ckey = tset.pparray[i]->get<BINARY>(PR_CHANGE_KEY);
		auto pcl  = tset.pparray[i]->get<BINARY>(PR_PREDECESSOR_CHANGE_LIST);
		if (ckey == nullptr)
			continue;
		auto k1   = change_key_size_ok(*ckey);
		auto k2   = change_key_gc_ok(*ckey);
		auto k3   = pcl_ok(pcl);
		if (k3 < 0)
			return k3;

		printf("%llxh", static_cast<unsigned long long>(rop_util_get_gc_value(*fid)));
		auto goodpoints = k1 + k2 + k3;
		if (goodpoints == 3) {
			printf(" (ok)\n");
			continue;
		}
		printf(" (problems:%c%c%c)", !k1 ? 'Z' : '-', !k2 ? 'B' : '-', !k3 ? 'P' : '-');
		if (g_dry_run) {
			putc('\n', stdout);
			continue;
		}
		auto ret = repair_folder(*fid);
		if (ret != 0)
			return ret;
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (HX_getopt5(g_options_table, argv, &argc, &argv,
	    HXOPT_USAGEONERR) != HXOPT_ERR_SUCCESS)
		return EXIT_FAILURE;
	auto cl_0 = make_scope_exit([=]() { HX_zvecfree(argv); });
	if (g_primail == nullptr) {
		fprintf(stderr, "Usage: cgkrepair -e primary_mailaddr\n");
		return EXIT_FAILURE;
	}
	gi_setup_early(g_primail);
	if (gi_setup() != EXIT_SUCCESS)
		return EXIT_FAILURE;
	auto cl_1 = make_scope_exit(gi_shutdown);
	auto ret = repair_mbox();
	if (ret == -ENOMEM) {
		fprintf(stderr, "Insufficient system memory.\n");
		ret = EXIT_FAILURE;
	} else if (ret != 0) {
		fprintf(stderr, "The operation did not complete.\n");
		ret = EXIT_FAILURE;
	} else if (ret == 0) {
		ret = EXIT_SUCCESS;
	}
	return ret;
}
