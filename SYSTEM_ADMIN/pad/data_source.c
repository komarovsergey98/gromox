#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "data_source.h"
#include <gromox/system_log.h>
#include <mysql/mysql.h>


static char g_host[256];
static int g_port;
static char g_user[256];
static char *g_password;
static char g_password_buff[256];
static char g_db_name[256];

static void data_source_encode_squote(const char *in, char *out);

void data_source_init(const char *host, int port, const char *user,
	const char *password, const char *db_name)
{
	strcpy(g_host, host);
	g_port = port;
	strcpy(g_user, user);
	if (NULL == password || '\0' == password[0]) {
		g_password = NULL;
	} else {
		strcpy(g_password_buff, password);
		g_password = g_password_buff;
	}
	strcpy(g_db_name, db_name);
}

BOOL data_source_get_maildir(const char *username, char *path_buff)
{
	int i;
	char temp_name[512];
	char sql_string[1024];
	MYSQL_RES *pmyres;
	MYSQL_ROW myrow;
	MYSQL *pmysql;
	
	data_source_encode_squote(username, temp_name);
	
	i = 0;
	
RETRYING:
	if (i > 3) {
		return FALSE;
	}
	
	if (NULL == (pmysql = mysql_init(NULL)) ||
		NULL == mysql_real_connect(pmysql, g_host, g_user, g_password,
		g_db_name, g_port, NULL, 0)) {
		if (NULL != pmysql) {
			system_log_info("[data_source]: Failed to connect to mysql server, "
				"reason: %s", mysql_error(pmysql));
		}
		i ++;
		sleep(1);
		goto RETRYING;
	}

	snprintf(sql_string, 1024, "SELECT maildir, address_type, address_status "
		"FROM users WHERE username='%s'", temp_name);
	
	if (0 != mysql_query(pmysql, sql_string) ||
		NULL == (pmyres = mysql_store_result(pmysql))) {
		system_log_info("[data_source]: fail to query mysql server, "
			"reason: %s", mysql_error(pmysql));
		mysql_close(pmysql);
		i ++;
		sleep(1);
		goto RETRYING;
	}
	
	myrow = mysql_fetch_row(pmyres);

	if (1 != mysql_num_rows(pmyres) ||
		0 != atoi(myrow[1]) ||
		0 != atoi(myrow[2])) {
		 mysql_free_result(pmyres);
		 mysql_close(pmysql);
		 path_buff[0] = '\0';
		 return TRUE;
	}

	
	strcpy(path_buff, myrow[0]);
	mysql_free_result(pmyres);
	mysql_close(pmysql);
	return TRUE;
}

static void data_source_encode_squote(const char *in, char *out)
{
	int len, i, j;

	len = strlen(in);
	for (i=0, j=0; i<len; i++, j++) {
		if ('\'' == in[i] || '\\' == in[i]) {
			out[j] = '\\';
			j ++;
		}
		out[j] = in[i];
	}
	out[j] = '\0';
}


