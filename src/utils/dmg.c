#include <getopt.h>
#include <stdio.h>
#include <daos_mgmt.h>
#include <daos/common.h>

typedef int (*command_hdlr_t)(int, char *[]);

static int
create_hdlr(int argc, char *argv[])
{
	struct option		options[] = {
		{"gid",		1,	NULL,	'g'},
		{"group",	1,	NULL,	'G'},
		{"mode",	1,	NULL,	'm'},
		{"size",	1,	NULL,	's'},
		{"uid",		1,	NULL,	'u'}
	};
	unsigned int		mode = 0644;
	unsigned int		uid = geteuid();
	unsigned int		gid = getegid();
	daos_size_t		size = 256 << 20;
	char		       *group = "daos_server_group";
	daos_rank_t		ranks[13];
	daos_rank_list_t	svc;
	uuid_t			uuid;
	char			uuid_string[37];
	int			rc;

	while ((rc = getopt_long(argc, argv, "", options, NULL)) != -1) {
		switch (rc) {
		case 'g':
			gid = atoi(optarg);
			break;
		case 'G':
			group = optarg;
			break;
		case 'm':
			mode = strtoul(optarg, NULL /* endptr */, 0 /* base */);
			break;
		case 's':
			size = strtoul(optarg, NULL /* endptr */, 0 /* base */);
			break;
		case 'u':
			gid = atoi(optarg);
			break;
		default:
			return 2;
		}
	}

	svc.rl_nr.num = ARRAY_SIZE(ranks);
	svc.rl_nr.num_out = 0;
	svc.rl_ranks = ranks;

	rc = dmg_pool_create(mode, uid, gid, group, NULL /* tgts */, "pmem",
			     size, &svc, uuid, NULL /* ev */);
	if (rc != 0) {
		D_ERROR("failed to create pool: %d\n", rc);
		return rc;
	}

	uuid_unparse_lower(uuid, uuid_string);
	printf("%s\n", uuid_string);

	return 0;
}

static int
help_hdlr(int argc, char *argv[])
{
	printf("\
usage: dmg COMMAND [OPTIONS]\n\
commands:\n\
  create	create a pool\n\
  destroy	destroy a pool [NOT IMPLEMENTED YET]\n\
  help		print this message and exit\n\
create options:\n\
  --gid=GID	pool GID\n\
  --group=STR	pool server process group\n\
  --mode=MODE	pool mode\n\
  --size=BYTES	target size in bytes\n\
  --uid=UID	pool UID\n\
  --uuid=UUID	pool UUID\n\
destroy options:\n\
  --uuid=UUID	pool UUID\n\
  \n");
	return 0;
}

int
main(int argc, char *argv[])
{
	command_hdlr_t		hdlr = NULL;
	int			rc = 0;

	if (argc == 1 || strcmp(argv[1], "help") == 0)
		hdlr = help_hdlr;
	else if (strcmp(argv[1], "create") == 0)
		hdlr = create_hdlr;
#if 0
	else if (strcmp(argv[1], "destroy") == 0)
		hdlr = destroy_hdlr;
#endif

	if (hdlr == NULL || hdlr == help_hdlr) {
		help_hdlr(argc, argv);
		return hdlr == NULL ? 2 : 0;
	}

	rc = dmg_init();
	if (rc != 0) {
		D_ERROR("failed to initialize dmg: %d\n", rc);
		return 1;
	}

	rc = hdlr(argc, argv);

	dmg_fini();

	if (rc < 0) {
		return 1;
	} else if (rc > 0) {
		help_hdlr(argc, argv);
		return 2;
	}

	return 0;
}
