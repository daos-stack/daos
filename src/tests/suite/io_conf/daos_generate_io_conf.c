/**
 * (C) Copyright 2018 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/**
 * This file is part of daos, to generate the epoch io test.
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"

static int obj_num = 1;
static int dkey_num = 10;
static int akey_num = 10;
static int rank_size = 8;
static int tgt_size = 8;
static int iod_size = 1;
static int oid_type = DAOS_OC_R3S_SPEC_RANK;

enum op {
	UPDATE,
	FETCH,
	PUNCH,
	MAX_OPS,
};

struct current_status {
	int		cur_obj_num;
	int		cur_dkey_num;
	int		cur_akey_num;
	int		cur_rank;
	daos_epoch_t	cur_eph;
};

enum rec_types {
	ARRAY_REC = 1 << 0,
	PUNCH_REC = 1 << 1,
};

#define MAX_REC_NUM		5
#define MAX_DISTANCE		10
#define EXTENT_SIZE		10
#define MAX_OFFSET		1048576
#define SINGLE_REC_RATE		20
#define MAX_EPOCH_TIMES		10

static struct option long_ops[] = {
	{ "obj_num",	required_argument,	NULL,	'o' },
	{ "dkey_num",	required_argument,	NULL,	'd' },
	{ "akey_num",	required_argument,	NULL,	'a' },
	{ "rec_size",	required_argument,	NULL,	's' },
	{ "rank_size",	required_argument,	NULL,	'g' },
	{ "tgt_size",	required_argument,	NULL,	't' },
	{ "oid_type",	required_argument,	NULL,	'O' },
	{ "help",	no_argument,		NULL,	'h' },
	{ NULL,		0,			NULL,	0   },
};

void print_usage(void)
{
	fprintf(stdout, "daos_generate_io_conf -g <rank_size> -t <tgt_size>"
		"-o [obj_num] -d [dkey_num] -a [akey_num] -s [rec_size]"
		"<file_name>\n");
}

/* Generate the ioconf
 *
 * update --epoch 1 --recx "[0, 2] [3, 8] [12, 18]"
 * update --epoch 1 --single
 * update --epoch 2 --recx "[1, 3] [5, 10] [12, 14] [100, 108]"
 * update --epoch 3 --recx "[0, 8] [13, 17] [90, 104]"
 * update --epoch 4 --recx "[1, 20] [80, 96] [110, 120]"
 * update --epoch 4 --single
 *
 * fail --rank %d --tgt %d
 * fetch --epoch 1 --recx "[0, 2] [3, 8] [12, 18]"
 * fetch --epoch 2 --recx "[0, 4] [5, 7] [13, 15] [100, 108]"
 * fetch --epoch 2 --single
 */
int
generate_io_conf_rec(int fd, struct current_status *status)
{
	char		line[256];
	char		rec_buf[256];
	unsigned int	offset = 0;
	int		rec_type[MAX_EPOCH_TIMES] = { 0 };
	daos_epoch_t	eph;
	int		dist;
	int		rec_num;
	int		rec_length = 0;
	int		extent_size;
	int		epoch_times;
	int		inject_fail_idx;
	int		i;
	int		rc1;
	int		rc = 0;
	int		tgt;

	sprintf(line, "iod_size %d\n", iod_size);
	rc1 = write(fd, line, strlen(line));
	if (rc1 <= 0)
		return -1;

	rec_num = rand() % MAX_REC_NUM + 1;
	dist = rand() % MAX_DISTANCE;
	extent_size = (rand() % EXTENT_SIZE + 1) * EXTENT_SIZE;
	offset = rand() % MAX_OFFSET;
	epoch_times = rand() % MAX_EPOCH_TIMES + 1;

	/* create rec string */
	for (i = 0; i < rec_num; i++) {
		char rec[32];
		int length;
		int end;

		end = offset + extent_size;

		length = sprintf(rec, "[%d, %d]", offset, end);

		sprintf(rec_buf + rec_length, "%s ", rec);
		rec_length += length + 1;
		offset = end + dist;
	}

	eph = status->cur_eph;
	inject_fail_idx = rand() % epoch_times;
	tgt = rand() % tgt_size;
	for (i = 0; i < epoch_times; i++) {
		daos_epoch_t	eph_idx;
		daos_epoch_t	op_eph;
		char		expect_line[32] = { 0 };
		int		op;

		if (rand() % 100 > SINGLE_REC_RATE) {
			sprintf(line, "update --epoch "DF_U64" --recx \"%s\"\n",
				eph + i, rec_buf);
			rec_type[i] |= ARRAY_REC;
		} else {
			sprintf(line, "update --epoch "DF_U64" --single\n",
				eph + i);
		}

		/* Clear punch flags for later epoch */
		for (eph_idx = i; eph_idx < eph + MAX_EPOCH_TIMES; eph_idx++)
			rec_type[i] &= ~PUNCH_REC;

		rc1 = write(fd, line, strlen(line));
		if (rc1 <= 0) {
			rc = -1;
			break;
		}

		/* Insert other operations */
		op = rand() % (MAX_OPS - 1) + 1;
		op_eph = rand() % (i + 1) + eph;

		if (inject_fail_idx == i) {
			sprintf(line, "exclude --rank %u --tgt %d\n",
				status->cur_rank, tgt);
			rc1 = write(fd, line, strlen(line));
			if (rc1 <= 0) {
				rc = -1;
				break;
			}
		}

		switch (op) {
		case FETCH:
			if (rec_type[op_eph - eph] & PUNCH_REC)
				sprintf(expect_line, "-x 0");

			if (rec_type[op_eph - eph] & ARRAY_REC) /* ARRAY type */
				sprintf(line, "fetch --epoch "DF_U64" -v --recx"
					" \"%s\" %s\n", op_eph, rec_buf,
					expect_line);
			else
				sprintf(line, "fetch --epoch "DF_U64" -v"
					" --single %s\n", op_eph, expect_line);
			break;
		case PUNCH:
			rec_type[i] |= PUNCH_REC;
			sprintf(line, "punch --epoch "DF_U64"\n", eph + i);
			break;
		default:
			break;
		}

		rc1 = write(fd, line, strlen(line));
		if (rc1 <= 0) {
			rc = -1;
			break;
		}
	}

	sprintf(line, "add --rank %u --tgt %d\n", status->cur_rank, tgt);
	rc1 = write(fd, line, strlen(line));
	if (rc1 <= 0)
		rc = -1;

	sprintf(line, "pool --query\n");
	rc1 = write(fd, line, strlen(line));
	if (rc1 <= 0)
		rc = -1;

	status->cur_eph += i;
	return rc;
}

int
generate_io_conf_akey(int fd, struct current_status *status)
{
	char akey[64];
	int rc = 0;

	while (status->cur_akey_num < akey_num) {
		sprintf(akey, "akey akey_%d\n",
			status->cur_akey_num);
		rc = write(fd, akey, strlen(akey));
		if (rc <= 0) {
			rc = -1;
			break;
		}
		rc = generate_io_conf_rec(fd, status);
		if (rc)
			break;
		status->cur_akey_num++;
	}

	return rc;
}

int
generate_io_conf_dkey(int fd, struct current_status *status)
{
	char dkey[64];
	int rc = 0;

	while (status->cur_dkey_num < dkey_num) {
		sprintf(dkey, "dkey dkey_%d\n",
			status->cur_dkey_num);
		rc = write(fd, dkey, strlen(dkey));
		if (rc <= 0) {
			rc = -1;
			break;
		}
		rc = generate_io_conf_akey(fd, status);
		if (rc)
			break;
		status->cur_akey_num = 0;
		status->cur_dkey_num++;
	}

	return rc;
}

int
generate_io_conf_obj(int fd, struct current_status *status)
{
	char oid_buf[64];
	int rc = 0;

	while (status->cur_obj_num < obj_num) {
		d_rank_t	rank;

		rank = rand() % rank_size;

		/* Fill the dkey first */
		sprintf(oid_buf, "oid --type %d --rank %d\n", oid_type, rank);
		rc = write(fd, oid_buf, strlen(oid_buf));
		if (rc <= 0) {
			rc = -1;
			break;
		}

		status->cur_rank = rank;

		rc = generate_io_conf_dkey(fd, status);
		if (rc)
			break;

		status->cur_dkey_num = 0;
		status->cur_akey_num = 0;
		status->cur_obj_num++;
	}

	return rc;
}

int
main(int argc, char **argv)
{
	char	*fname = NULL;
	struct current_status status = { 0 };
	int	fd;
	int	rc;

	while ((rc = getopt_long(argc, argv, "a:d:o:s:g:t:O:h",
				 long_ops, NULL)) != -1) {
		char	*endp;

		switch (rc) {
		case 'a':
			akey_num = strtoul(optarg, &endp, 0);
			break;
		case 'd':
			dkey_num = strtoul(optarg, &endp, 0);
			break;
		case 'o':
			obj_num = strtoul(optarg, &endp, 0);
			break;
		case 's':
			iod_size = strtoul(optarg, &endp, 0);
			break;
		case 'g':
			rank_size = strtoul(optarg, &endp, 0);
			break;
		case 't':
			tgt_size = strtoul(optarg, &endp, 0);
			break;
		case 'O':
			oid_type = strtoul(optarg, &endp, 0);
			break;
		case 'h':
			print_usage();
			return 0;
		default:
			fprintf(stderr, "Unknown option %c\n", rc);
			print_usage();
			return -1;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "Bad parameters.\n");
		print_usage();
		return -1;
	}

	fname = argv[optind];
	fd = open(fname, O_RDWR|O_TRUNC|O_CREAT, 0666);

	/* Prepare the header, only support daos */
	rc = write(fd, "test_lvl daos\n", strlen("test_lvl daos\n"));
	if (rc <= 0) {
		rc = -1;
		goto out;
	}

	rc = generate_io_conf_obj(fd, &status);
	if (rc)
		goto out;
out:
	close(fd);
	return rc;
}
