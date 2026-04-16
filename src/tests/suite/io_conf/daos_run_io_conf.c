/**
 * (C) Copyright 2018-2022 Intel Corporation.
 * (C) Copyright 2026 Hewlett Packard Enterprise Development LP
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of daos, to generate the epoch io test.
 */
#define D_LOGFAC	DD_FAC(tests)

#include "daos_iotest.h"

#define CMD_LINE_LEN_MAX  (1024)
#define CMD_LINE_ARGC_MAX (16)
#define CMD_LINE_DBG      0

static struct option long_ops[] = {
	{ "help",	no_argument,		NULL,	'h' },
	{ NULL,		0,			NULL,	0   },
};

void print_usage(void)
{
	fprintf(stdout, "-n|--dmg_config\n");
	fprintf(stdout, "daos_run_io_conf <io_conf_file>\n");
}

static int
cmd_line_get(FILE *fp, char *line)
{
	char *p;
	int   i;

	D_ASSERT(line != NULL && fp != NULL);
	do {
		if (fgets(line, CMD_LINE_LEN_MAX - 1, fp) == NULL)
			return -DER_ENOENT;
		for (p = line, i = 0; isspace(*p) && i < CMD_LINE_LEN_MAX - 1; p++, i++)
			;
		if (i == CMD_LINE_LEN_MAX - 1)
			continue;
		if (*p != '\0' && *p != '#' && *p != '\n')
			break;
	} while (1);

	squeeze_spaces(line);

	return 0;
}

static int
io_conf_run(test_arg_t *arg, const char *io_conf)
{
	struct test_op_record *op = NULL;
	FILE                  *fp;
	char                   cmd_line[CMD_LINE_LEN_MAX - 1] = {};
	int                    rc                             = 0;
	/*Array for snapshot epoch*/
	daos_epoch_t           sn_epoch[DTS_MAX_EPOCH_TIMES] = {};

	if (io_conf == NULL || strlen(io_conf) == 0) {
		print_message("invalid io_conf.\n");
		return -DER_INVAL;
	}

	fp = fopen(io_conf, "r");
	if (fp == NULL) {
		print_message("failed to open io_conf %s, %d(%s).\n", io_conf, errno,
			      strerror(errno));
		return daos_errno2der(errno);
	}

	int line_nr = 0;

	do {
		size_t cmd_size;

		memset(cmd_line, 0, CMD_LINE_LEN_MAX - 1);
		if (cmd_line_get(fp, cmd_line) != 0)
			break;

		cmd_size = strnlen(cmd_line, CMD_LINE_LEN_MAX - 1);
		if (cmd_size == 0)
			continue;
		if (cmd_size >= CMD_LINE_LEN_MAX - 1) {
			print_message("bad cmd_line, exit.\n");
			break;
		}
		rc = cmd_line_parse(arg, cmd_line, &op);
		if (rc != 0) {
			print_message("bad cmd_line %s, exit.\n", cmd_line);
			break;
		}

		if (op != NULL) {
			op->snap_epoch = &sn_epoch[op->tx];
			print_message("will run cmd_line %s, line_nr %d\n", cmd_line, ++line_nr);
			rc = cmd_line_run(arg, op);
			if (rc) {
				print_message("run cmd_line %s failed, "
					      "rc %d.\n",
					      cmd_line, rc);
				break;
			}
		}
	} while (1);

	fclose(fp);
	return rc;
}

static int
cmd_line_parse(test_arg_t *arg, const char *cmd_line, struct test_op_record **op)
{
	char                   cmd[CMD_LINE_LEN_MAX + 1]   = {0};
	struct test_op_record *op_rec                      = NULL;
	char                  *argv[CMD_LINE_ARGC_MAX + 1] = {0};
	char                  *dkey                        = NULL;
	char                  *akey                        = NULL;
	size_t                 cmd_size;
	int                    argc = 0;
	int                    rc   = 0;

	strncpy(cmd, cmd_line, CMD_LINE_LEN_MAX);
#if CMD_LINE_DBG
	print_message("parsing cmd: %s.\n", cmd);
#endif
	cmd_size = strnlen(cmd, CMD_LINE_LEN_MAX);
	if (cmd_size == 0)
		return 0;
	if (cmd_size >= CMD_LINE_LEN_MAX) {
		print_message("bad cmd_line.\n");
		return -1;
	}
	rc = cmd_parse_argv(cmd, &argc, argv);
	if (rc != 0) {
		print_message("bad format %s.\n", cmd);
		D_GOTO(out, rc = -DER_INVAL);
	}

	if (argc < 2)
		D_GOTO(out, rc = -DER_INVAL);

	if (strcmp(argv[0], "test_lvl") == 0) {
		if (strcmp(argv[1], "daos") == 0) {
			arg->eio_args.op_lvl = TEST_LVL_DAOS;
		} else if (strcmp(argv[1], "vos") == 0) {
			arg->eio_args.op_lvl = TEST_LVL_VOS;
			print_message("vos level test not supported now.\n");
			rc = -DER_INVAL;
		} else {
			print_message("bad test_lvl %s.\n", argv[1]);
			rc = -DER_INVAL;
		}
	} else if (strcmp(argv[0], "dkey") == 0) {
		dkey = argv[1];
		D_FREE(arg->eio_args.op_dkey);
		D_STRNDUP(arg->eio_args.op_dkey, dkey, strlen(dkey));
	} else if (strcmp(argv[0], "akey") == 0) {
		akey = argv[1];
		D_FREE(arg->eio_args.op_akey);
		D_STRNDUP(arg->eio_args.op_akey, akey, strlen(akey));
	} else if (strcmp(argv[0], "iod_size") == 0) {
		arg->eio_args.op_iod_size = atoi(argv[1]);
	} else if (strcmp(argv[0], "obj_class") == 0) {
		if (strcmp(argv[1], "ec") == 0) {
			print_message("the test is for EC object.\n");
			arg->eio_args.op_ec = 1;
			if ((argc == 3 && strcmp(argv[2], "OC_EC_2P2G1") == 0) || argc == 2) {
				print_message("EC obj class OC_EC_2P2G1\n");
				dts_ec_obj_class = OC_EC_2P2G1;
				dts_ec_grp_size  = 4;
			} else if (argc == 3 && strcmp(argv[2], "OC_EC_4P2G1") == 0) {
				print_message("EC obj class OC_EC_4P2G1\n");
				dts_ec_obj_class = OC_EC_4P2G1;
				dts_ec_grp_size  = 6;
			} else {
				print_message("bad parameter");
				D_GOTO(out, rc = -DER_INVAL);
			}
			arg->eio_args.op_oid =
			    daos_test_oid_gen(arg->coh, dts_ec_obj_class, 0, 0, arg->myrank);
		} else if (strcmp(argv[1], "replica") == 0) {
			arg->eio_args.op_ec = 0;
			arg->eio_args.op_oid =
			    daos_test_oid_gen(arg->coh, dts_obj_class, 0, 0, arg->myrank);
			print_message("the test is for replica object.\n");
		} else {
			print_message("bad obj_class %s.\n", argv[1]);
			rc = -DER_INVAL;
		}
	} else if (strcmp(argv[0], "fail_shard_fetch") == 0) {
		uint16_t shard[4] = {0};
		uint64_t fail_val;
		int      i;

		if (argc < 2 || argc > 6) {
			print_message("bad parameter");
			D_GOTO(out, rc = -DER_INVAL);
		}
		if (strcmp(argv[1], "set") == 0) {
			for (i = 0; i < argc - 2; i++) {
				shard[i] = atoi(argv[i + 2]);
				print_message("will fail fetch from shard %d\n", shard[i]);
			}
			fail_val        = daos_shard_fail_value(shard, argc - 2);
			arg->fail_loc   = DAOS_FAIL_SHARD_OPEN | DAOS_FAIL_ALWAYS;
			arg->fail_value = fail_val;
		} else if (strcmp(argv[1], "clear") == 0) {
			arg->fail_loc   = 0;
			arg->fail_value = 0;
		} else {
			print_message("bad parameter");
			D_GOTO(out, rc = -DER_INVAL);
		}
	} else if (strcmp(argv[0], "oid") == 0) {
		rc = cmd_parse_oid(arg, argc, argv);
	} else if (strcmp(argv[0], "update") == 0) {
		rc = cmd_parse_update_fetch(arg, argc, argv, TEST_OP_UPDATE, &op_rec);
	} else if (strcmp(argv[0], "fetch") == 0) {
		rc = cmd_parse_update_fetch(arg, argc, argv, TEST_OP_FETCH, &op_rec);
	} else if (strcmp(argv[0], "exclude") == 0) {
		rc = cmd_parse_add_exclude(arg, argc, argv, TEST_OP_EXCLUDE, &op_rec);
	} else if (strcmp(argv[0], "add") == 0) {
		rc = cmd_parse_add_exclude(arg, argc, argv, TEST_OP_ADD, &op_rec);
	} else if (strcmp(argv[0], "pool") == 0) {
		rc = cmd_parse_pool(arg, argc, argv, &op_rec);
	} else if (strcmp(argv[0], "punch") == 0) {
		rc = cmd_parse_punch(arg, argc, argv, &op_rec);
	} else {
		print_message("unknown cmd %s.\n", argv[0]);
		rc = -DER_INVAL;
	}

out:
	if (rc == 0)
		*op = op_rec;
	return rc;
}

#define AKEY_PATH_LEN (PATH_MAX - 10)
/* replay the OPs which epoch <= /a epoch in key_rec's op_queue */
static int
test_op_queue_replay(test_arg_t *arg, struct test_key_record *key_rec, daos_epoch_t epoch)
{
	struct test_op_record *op_rec;
	char                   akey_dir[AKEY_PATH_LEN] = {0};
	char                   array_path[PATH_MAX]    = {0};
	char                   single_path[PATH_MAX]   = {0};
	int                    rc                      = 0;

#if CMD_LINE_DBG
	print_message("replay %s/%s, epoch " DF_U64 ", replayed_epoch " DF_U64 "\n",
		      key_rec->or_dkey, key_rec->or_akey, epoch, key_rec->or_replayed_epoch);
#endif
	/* replay from beginning if read epoch behind replayed epoch,
	 * so verify from low epoch to high epoch will be faster.
	 */
	if (epoch < key_rec->or_replayed_epoch) {
		D_ASSERT(key_rec->or_fd_array != 0);
		close(key_rec->or_fd_array);
		key_rec->or_fd_array = 0;

		D_ASSERT(key_rec->or_fd_single != 0);
		close(key_rec->or_fd_single);
		key_rec->or_fd_single = 0;

		key_rec->or_replayed_epoch = 0;
	}

	if (key_rec->or_replayed_epoch == 0) {
		snprintf(akey_dir, AKEY_PATH_LEN, "%s/%s/%s", test_io_work_dir, key_rec->or_dkey,
			 key_rec->or_akey);
		test_rmdir(akey_dir, true);
		rc = epoch_io_mkdir(akey_dir);
		if (rc) {
			print_message("failed to mkdir %s, rc %d.\n", akey_dir, rc);
			return rc;
		}
		snprintf(array_path, PATH_MAX, "%s/array", akey_dir);
		snprintf(single_path, PATH_MAX, "%s/single", akey_dir);
		key_rec->or_fd_array = open(array_path, O_CREAT | O_TRUNC | O_RDWR, 0666);
		if (key_rec->or_fd_array == -1) {
			print_message("failed to open %s, %d(%s)\n", array_path, errno,
				      strerror(errno));
			return daos_errno2der(errno);
		}
		key_rec->or_fd_single = open(single_path, O_CREAT | O_TRUNC | O_RDWR, 0666);
		if (key_rec->or_fd_single == -1) {
			print_message("failed to open %s, %d(%s)\n", single_path, errno,
				      strerror(errno));
			close(key_rec->or_fd_array);
			key_rec->or_fd_array = 0;
			return daos_errno2der(errno);
		}
	}

	d_list_for_each_entry(op_rec, &key_rec->or_queue, or_queue_link) {
		if (op_rec->tx < key_rec->or_replayed_epoch)
			continue;
		if (op_rec->tx > epoch)
			break;
		rc = op_dict[op_rec->or_op].op_cb[TEST_LVL_FIO](arg, op_rec, NULL, NULL);
		if (rc == 0) {
			key_rec->or_replayed_epoch = op_rec->tx;
		} else {
			print_message("op_dict[%d].op_cb[%d] failed, rc %d.\n", op_rec->or_op,
				      TEST_LVL_FIO, rc);
			close(key_rec->or_fd_array);
			key_rec->or_fd_array = 0;
			close(key_rec->or_fd_single);
			key_rec->or_fd_single      = 0;
			key_rec->or_replayed_epoch = 0;
			return rc;
		}
	}

	return rc;
}

static int
cmd_line_run(test_arg_t *arg, struct test_op_record *op_rec)
{
	int         op  = op_rec->or_op;
	int         lvl = arg->eio_args.op_lvl;
	char       *buf = NULL, *f_buf = NULL;
	daos_size_t size = 0, f_size = 0;
	int         rc = 0;

	D_ASSERT(op >= TEST_OP_MIN && op <= TEST_OP_MAX);
	D_ASSERT(lvl == TEST_LVL_DAOS || lvl == TEST_LVL_VOS);

	/* for modification OP, just go through DAOS stack and return */
	if (test_op_is_modify(op) || op == TEST_OP_POOL_QUERY || op == TEST_OP_ADD ||
	    op == TEST_OP_EXCLUDE)
		return op_dict[op].op_cb[lvl](arg, op_rec, NULL, 0);

	/* for verification OP, firstly retrieve it through DAOS stack */
	rc = op_dict[op].op_cb[lvl](arg, op_rec, &buf, &size);
	if (rc) {
		print_message("op_dict[%d].op_cb[%d] failed, rc %d.\n", op, lvl, rc);
		D_GOTO(out, rc);
	}

	if (arg->eio_args.op_no_verify)
		D_GOTO(out, rc = 0);

	/* then replay the modification OPs in the queue, retrieve it through
	 * fio and compare the result data.
	 */
	rc = test_op_queue_replay(arg, op_rec->or_key_rec, op_rec->tx);
	if (rc) {
		print_message("test_op_queue_replay epoch %d failed,"
			      "rc %d.\n",
			      op_rec->tx, rc);
		D_GOTO(out, rc);
	}

	rc = op_dict[op].op_cb[TEST_LVL_FIO](arg, op_rec, &f_buf, &f_size);
	if (rc) {
		print_message("op_dict[%d].op_cb[%d] failed, rc %d.\n", op, TEST_LVL_FIO, rc);
		D_GOTO(out, rc);
	}

	if (size != f_size) {
		print_message("size mismatch (" DF_U64 " vs " DF_U64 ").\n", size, f_size);
		D_GOTO(out, rc = -DER_MISMATCH);
	}
	if (memcmp(buf, f_buf, size) != 0) {
		int i, j;

		print_message("data verification failed.\n");
		/* print first 8 mismatched data */
		for (i = 0, j = 0; i < size && j < 8; i++) {
			if (buf[i] != f_buf[i]) {
				print_message("offset %d expect %d, got %d.\n", i, f_buf[i],
					      buf[i]);
				j++;
			}
		}
		rc = -DER_MISMATCH;
	}

out:
	D_FREE(buf);
	D_FREE(f_buf);
	return rc;
}

#define POOL_SIZE	(10ULL << 30)
int
main(int argc, char **argv)
{
	test_arg_t		*arg;
	struct epoch_io_args	*eio_arg;
	char			*fname = NULL;
	void			*state = NULL;
	int			rc;

	par_init(&argc, &argv);
	rc = daos_init();
	if (rc) {
		fprintf(stderr, "daos init failed: rc %d\n", rc);
		goto out_mpi;
	}

	while ((rc = getopt_long(argc, argv, "h:n:", long_ops, NULL)) != -1) {
		switch (rc) {
		case 'h':
			print_usage();
			goto out_fini;
		case 'n':
			dmg_config_file = optarg;
			printf("dmg_config_file = %s\n", dmg_config_file);
			break;
		default:
			fprintf(stderr, "Unknown option %c\n", rc);
			print_usage();
			rc = -1;
			goto out_fini;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "Bad parameters.\n");
		print_usage();
		rc = -1;
		goto out_fini;
	}

	fname = argv[optind];

	rc = obj_setup(&state);
	if (rc) {
		fprintf(stderr, "obj setup failed: rc %d\n", rc);
		goto out_fini;
	}

	arg = state;
	arg->dmg_config = dmg_config_file;
	eio_arg = &arg->eio_args;
	D_INIT_LIST_HEAD(&eio_arg->op_list);
	eio_arg->op_lvl = TEST_LVL_DAOS;
	eio_arg->op_iod_size = 1;
	eio_arg->op_oid = dts_oid_gen(arg->myrank);
	rc = daos_obj_set_oid_by_class(&eio_arg->op_oid, 0, dts_obj_class, 0);
	if (rc) {
		fprintf(stderr, "oid setup failed: rc %d\n", rc);
		goto out_fini;
	}
	arg->eio_args.op_no_verify = 1;	/* No verification for now */

	par_barrier(PAR_COMM_WORLD);

	rc = io_conf_run(arg, fname);
	if (rc)
		fprintf(stderr, "io_conf_run failed: rc %d\n", rc);

	test_teardown(&state);

	par_barrier(PAR_COMM_WORLD);
	fprintf(stdout, "daos_run_io_conf completed successfully\n");
out_fini:
	daos_fini();
out_mpi:
	par_fini();
	return rc;
}
