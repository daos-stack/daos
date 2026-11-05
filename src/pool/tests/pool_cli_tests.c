/**
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * Unit tests for pool client (dc_pool) internals.
 */
#define D_LOGFAC DD_FAC(tests)

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include <daos/common.h>
#include <daos/pool.h>
#include <daos/tls.h>
#include <daos/tests_lib.h>
#include "../cli_internal.h"

/*
 * Enabled ranks of a 1600-rank production pool (204 rank ranges, ~1.4 KB as a string) whose
 * "daos pool query -e" and "daos pool autotest" output included a spurious
 * "pool_print_range_list() ...: snprintf failed, 4" ERR message (DAOS-19768).
 */
static const char *large_rank_ranges =
    "1-3,8,10-14,17-18,21-23,25,28-34,36-37,39,41-42,44,46,48,50-54,58-61,63-71,73-74,76-79,"
    "81-85,88-91,94-97,99-101,103,105-107,110-115,117-129,131-133,138-139,141,143-145,"
    "148-149,153-154,156,158,160-168,170-171,173-175,177,179-180,182-183,185,188,190,194-196,"
    "198-200,202-205,207-209,211,213-214,217-228,231-234,237,240,242-247,249-251,255-259,"
    "261-262,265,268-271,273-287,289-290,292-298,300-302,304-324,326-328,330,332,334-339,341,"
    "345,347-352,356,358-359,361-365,369,371,374,376,378-381,383,385-388,390,392-393,396,"
    "399-403,405,407,409-415,417-418,422,424,427,429-433,436,439,441-446,452-454,457-460,"
    "464-466,469-471,473-475,477,482-483,485-488,491,493-494,498-500,504-505,507,509-511,"
    "513-924,926-927,929,931-933,936-937,940,942-951,953-955,959-961,963-964,966-967,969,"
    "971-972,976,978-979,981-985,988-994,996,999,1001,1004,1008,1011-1021,1023-1029,"
    "1031-1053,1055-1072,1074,1076-1077,1079-1084,1086,1089,1093-1094,1096,1098-1102,"
    "1104-1109,1111-1112,1115-1116,1119,1121-1125,1128-1129,1134-1135,1142,1144-1156,"
    "1158-1163,1166-1173,1175-1187,1189-1199,1202,1205-1206,1208,1215-1216,1218,1220-1224,"
    "1227,1229,1231,1233-1234,1236-1239,1241-1242,1244-1248,1251-1254,1256,1260-1262,"
    "1269-1270,1272,1274,1277-1285,1287-1290,1293-1297,1300-1308,1310-1317,1320-1322,"
    "1324-1329,1332-1334,1337-1345,1347-1349,1352-1360,1363-1368,1370-1371,1373-1376,1378,"
    "1380-1394,1396,1398-1411,1413-1423,1425-1564,1567,1570,1572-1573,1577-1578,1580-1581,"
    "1583,1586-1592,1595-1598";

static const char *pool_uuid_str = "be8f5632-fda1-4985-b240-7039321a3fcb";

static char        log_path[64];

/* Parse a "lo-hi,rank,..." string into a rank list. */
static d_rank_list_t *
ranks_from_str(const char *str)
{
	d_rank_list_t *ranks;
	char          *buf;
	char          *tok;
	char          *saveptr = NULL;
	unsigned int   lo;
	unsigned int   hi;
	unsigned int   r;
	int            rc;

	ranks = d_rank_list_alloc(0);
	assert_non_null(ranks);
	buf = strdup(str);
	assert_non_null(buf);

	for (tok = strtok_r(buf, ",", &saveptr); tok != NULL; tok = strtok_r(NULL, ",", &saveptr)) {
		if (sscanf(tok, "%u-%u", &lo, &hi) != 2) {
			assert_int_equal(sscanf(tok, "%u", &lo), 1);
			hi = lo;
		}
		for (r = lo; r <= hi; r++) {
			rc = d_rank_list_append(ranks, r);
			assert_rc_equal(rc, 0);
		}
	}

	free(buf);
	return ranks;
}

/*
 * Build a rank list whose d_rank_list_to_str() string is exactly len bytes long:
 * "[" + n 4-digit ranks each followed by "," + one w-digit rank + "]", w in [5, 9].
 */
static d_rank_list_t *
ranks_with_str_len(size_t len)
{
	d_rank_list_t *ranks;
	size_t         n;
	size_t         w;
	size_t         i;
	d_rank_t       last = 1;
	int            rc;

	assert_true(len >= 12);
	n = (len - 7) / 5;
	w = len - 2 - 5 * n;
	assert_true(w >= 5 && w <= 9);

	ranks = d_rank_list_alloc(0);
	assert_non_null(ranks);
	for (i = 0; i < n; i++) {
		rc = d_rank_list_append(ranks, 1000 + 2 * i);
		assert_rc_equal(rc, 0);
	}
	for (i = 1; i < w; i++)
		last *= 10;
	rc = d_rank_list_append(ranks, last);
	assert_rc_equal(rc, 0);

	return ranks;
}

static void
shuffle_ranks(d_rank_list_t *ranks)
{
	unsigned int i;

	srandom(19768);
	for (i = ranks->rl_nr - 1; i > 0; i--) {
		unsigned int j   = random() % (i + 1);
		d_rank_t     tmp = ranks->rl_ranks[i];

		ranks->rl_ranks[i] = ranks->rl_ranks[j];
		ranks->rl_ranks[j] = tmp;
	}
}

/* Route all client logging at log_mask (and DD_MASK=dd_mask, if not NULL) to a new file. */
static void
log_open(const char *log_mask, const char *dd_mask)
{
	int fd;
	int rc;

	snprintf(log_path, sizeof(log_path), "/tmp/pool_cli_tests.XXXXXX");
	fd = mkstemp(log_path);
	assert_true(fd >= 0);
	close(fd);

	setenv("D_LOG_FILE", log_path, 1);
	setenv("D_LOG_MASK", log_mask, 1);
	if (dd_mask != NULL)
		setenv("DD_MASK", dd_mask, 1);
	else
		unsetenv("DD_MASK");
	unsetenv("DD_SUBSYS");
	unsetenv("DD_STDERR");
	unsetenv("D_LOG_FILE_APPEND_PID");
	unsetenv("D_LOG_FILE_APPEND_RANK");

	rc = daos_debug_init(NULL);
	assert_rc_equal(rc, 0);
}

/* Flush and close the log, and return its contents (caller frees). */
static char *
log_close(void)
{
	FILE *fp;
	char *buf;
	long  len;

	daos_debug_fini();

	fp = fopen(log_path, "r");
	assert_non_null(fp);
	assert_int_equal(fseek(fp, 0, SEEK_END), 0);
	len = ftell(fp);
	assert_true(len >= 0);
	rewind(fp);
	buf = calloc(1, len + 1);
	assert_non_null(buf);
	assert_int_equal(fread(buf, 1, len, fp), len);
	fclose(fp);
	unlink(log_path);

	return buf;
}

static void
log_ranks(d_rank_list_t *ranks, bool enabled)
{
	uuid_t uuid;

	assert_int_equal(uuid_parse(pool_uuid_str, uuid), 0);
	dc_pool_log_ranks(uuid, ranks, enabled);
}

/* The priority is the first field of a log file line, or follows the facility name. */
static int
count_err_lines(const char *log)
{
	const char *line;
	const char *eol;
	int         nr = 0;

	for (line = log; *line != '\0'; line = (*eol == '\n') ? eol + 1 : eol) {
		eol = strchrnul(line, '\n');
		if (strncmp(line, "ERR ", strlen("ERR ")) == 0 ||
		    memmem(line, eol - line, " ERR ", strlen(" ERR ")) != NULL)
			nr++;
	}
	return nr;
}

/*
 * Check the dc_pool_log_ranks() lines in log: numbered parts 1..N of at most
 * DC_POOL_RANKS_LOG_CHUNK bytes, split after a ',', that reassemble to d_rank_list_to_str().
 * Return N.
 */
static int
check_ranks_log(const char *log, d_rank_list_t *ranks, bool enabled)
{
	char        prefix[128];
	char       *expected;
	char       *joined;
	size_t      joined_len = 0;
	const char *pos;
	int         nparts = 0;
	int         rc;

	rc = d_rank_list_to_str(ranks, &expected);
	assert_rc_equal(rc, 0);
	assert_non_null(expected);

	joined = calloc(1, strlen(expected) + 1);
	assert_non_null(joined);

	snprintf(prefix, sizeof(prefix), "%.8s: %s ranks (nr=%u, part ", pool_uuid_str,
		 enabled ? "ENABLED" : "DISABLED", ranks->rl_nr);

	for (pos = strstr(log, prefix); pos != NULL; pos = strstr(pos, prefix)) {
		const char *eol;
		const char *payload;
		size_t      chunk;
		int         part;
		int         total;

		pos += strlen(prefix);
		assert_int_equal(sscanf(pos, "%d/%d)", &part, &total), 2);
		nparts++;
		assert_int_equal(part, nparts);

		payload = strstr(pos, "): ");
		assert_non_null(payload);
		payload += strlen("): ");
		eol = strchr(payload, '\n');
		assert_non_null(eol);
		chunk = eol - payload;

		assert_true(chunk > 0 && chunk <= DC_POOL_RANKS_LOG_CHUNK);
		assert_true(joined_len + chunk <= strlen(expected));
		memcpy(joined + joined_len, payload, chunk);
		joined_len += chunk;

		if (part < total)
			assert_int_equal(payload[chunk - 1], ',');
		else
			assert_int_equal(part, total);
		pos = eol;
	}

	assert_true(nparts > 0);
	assert_string_equal(joined, expected);

	free(joined);
	D_FREE(expected);
	return nparts;
}

/* Default client log mask: nothing is logged, in particular no ERR message. */
static void
test_log_ranks_debug_disabled(void **state)
{
	d_rank_list_t *ranks = ranks_from_str(large_rank_ranges);
	char          *log;

	log_open("ERR", NULL);
	log_ranks(ranks, true);
	log = log_close();

	assert_null(strstr(log, "ranks (nr="));
	assert_int_equal(count_err_lines(log), 0);

	free(log);
	d_rank_list_free(ranks);
}

/* DAOS-19768: a rank list too long for one log line is logged completely, without ERR. */
static void
test_log_ranks_large(void **state)
{
	d_rank_list_t *ranks = ranks_from_str(large_rank_ranges);
	char          *log;
	int            nparts;

	assert_int_equal(ranks->rl_nr, 1280);
	shuffle_ranks(ranks);

	log_open("DEBUG", "md");
	log_ranks(ranks, true);
	log = log_close();

	assert_int_equal(count_err_lines(log), 0);
	nparts = check_ranks_log(log, ranks, true);
	assert_true(nparts > 1);

	free(log);
	d_rank_list_free(ranks);
}

static void
test_log_ranks_small(void **state)
{
	d_rank_list_t *ranks = ranks_from_str("0-15,20");
	char          *log;

	log_open("DEBUG", "md");
	log_ranks(ranks, false);
	log = log_close();

	assert_int_equal(check_ranks_log(log, ranks, false), 1);
	assert_non_null(strstr(log, "DISABLED ranks (nr=17, part 1/1): [0-15,20]\n"));

	free(log);
	d_rank_list_free(ranks);
}

static void
test_log_ranks_empty(void **state)
{
	d_rank_list_t *ranks = d_rank_list_alloc(0);
	char          *log;

	assert_non_null(ranks);

	log_open("DEBUG", "md");
	log_ranks(ranks, true);
	log = log_close();

	assert_int_equal(check_ranks_log(log, ranks, true), 1);
	assert_non_null(strstr(log, "ENABLED ranks (nr=0, part 1/1): []\n"));

	free(log);
	d_rank_list_free(ranks);
}

static void
test_log_ranks_chunk_boundary(void **state)
{
	const struct {
		size_t len;
		int    nparts;
	} cases[] = {
	    {DC_POOL_RANKS_LOG_CHUNK - 1, 1},
	    {DC_POOL_RANKS_LOG_CHUNK, 1},
	    {DC_POOL_RANKS_LOG_CHUNK + 1, 2},
	    {DC_POOL_RANKS_LOG_CHUNK * 2, 3},
	};
	int i;

	for (i = 0; i < ARRAY_SIZE(cases); i++) {
		d_rank_list_t *ranks = ranks_with_str_len(cases[i].len);
		char          *str;
		char          *log;
		int            rc;

		rc = d_rank_list_to_str(ranks, &str);
		assert_rc_equal(rc, 0);
		assert_int_equal(strlen(str), cases[i].len);
		D_FREE(str);

		log_open("DEBUG", "md");
		log_ranks(ranks, true);
		log = log_close();

		assert_int_equal(check_ranks_log(log, ranks, true), cases[i].nparts);

		free(log);
		d_rank_list_free(ranks);
	}
}

/* No two ranks are contiguous, so every rank is its own range: 50000 ranges. */
static void
test_log_ranks_many_ranges(void **state)
{
	d_rank_list_t *ranks = d_rank_list_alloc(0);
	char          *log;
	d_rank_t       r;
	int            rc;

	assert_non_null(ranks);
	for (r = 0; r < 100000; r += 2) {
		rc = d_rank_list_append(ranks, r);
		assert_rc_equal(rc, 0);
	}

	log_open("DEBUG", "md");
	log_ranks(ranks, false);
	log = log_close();

	assert_int_equal(count_err_lines(log), 0);
	assert_true(check_ranks_log(log, ranks, false) > 500);

	free(log);
	d_rank_list_free(ranks);
}

int
main(int argc, char **argv)
{
	const struct CMUnitTest tests[] = {
	    cmocka_unit_test(test_log_ranks_debug_disabled),
	    cmocka_unit_test(test_log_ranks_large),
	    cmocka_unit_test(test_log_ranks_small),
	    cmocka_unit_test(test_log_ranks_empty),
	    cmocka_unit_test(test_log_ranks_chunk_boundary),
	    cmocka_unit_test(test_log_ranks_many_ranges),
	};

	return cmocka_run_group_tests_name("pool_cli_tests", tests, NULL, NULL);
}
