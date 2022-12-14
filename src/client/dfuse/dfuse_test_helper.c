

#define D_LOGFAC DD_FAC(dfuse)

#include "dfuse.h"

static void
show_help(char *name)
{
	printf("\n");
}

static void
show_version(char *name)
{
	fprintf(stdout, "DFuse test helper\n");
	fprintf(stdout, "%s version %s, libdaos %d.%d.%d\n", name, DAOS_VERSION,
		DAOS_API_VERSION_MAJOR, DAOS_API_VERSION_MINOR, DAOS_API_VERSION_FIX);
	fprintf(stdout, "Using fuse %s\n", fuse_pkgversion());
#if HAVE_CACHE_READDIR
	fprintf(stdout, "Kernel readdir support enabled\n");
#endif
};

int
main(int argc, char **argv)
{
	struct dfuse_info *dfuse_info                             = NULL;
	uuid_t             cont_uuid                              = {};
	char               pool_name[DAOS_PROP_LABEL_MAX_LEN + 1] = {};
	char               cont_name[DAOS_PROP_LABEL_MAX_LEN + 1] = {};
	struct dfuse_pool *dfp                                    = NULL;
	struct dfuse_cont *dfs                                    = NULL;
	int                rc;
	int                rc2;
	int                c;

	struct option      long_options[] = {{"pool", required_argument, 0, 'p'},
					     {"container", required_argument, 0, 'c'},
					     {"version", no_argument, 0, 'v'},
					     {"help", no_argument, 0, 'h'},
					     {0, 0, 0, 0}};

	while (1) {
		c = getopt_long(argc, argv, "Mm:St:o:fhv", long_options, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'p':
			strncpy(pool_name, optarg, DAOS_PROP_LABEL_MAX_LEN);
			break;
		case 'c':
			strncpy(cont_name, optarg, DAOS_PROP_LABEL_MAX_LEN);
			break;
		case 'h':
			show_help(argv[0]);
			D_GOTO(out_debug, rc = -DER_SUCCESS);
			break;
		case 'v':
			show_version(argv[0]);

			D_GOTO(out_debug, rc = -DER_SUCCESS);
			break;
		case '?':
			show_help(argv[0]);
			D_GOTO(out_debug, rc = -DER_INVAL);
			break;
		}
	}

	/* TODO: Check positional args. */

	rc = daos_debug_init(DAOS_LOG_DEFAULT);
	if (rc != 0)
		D_GOTO(out, rc);

	D_ALLOC_PTR(dfuse_info);
	if (dfuse_info == NULL)
		D_GOTO(out_debug_fini, rc = -DER_NOMEM);

	DFUSE_TRA_ROOT(dfuse_info, "dfuse_info");

	rc = daos_init();
	if (rc != -DER_SUCCESS) {
		DFUSE_TRA_DEBUG(dfuse_info, "daos_init() failed:" DF_RC, DP_RC(rc));
		D_GOTO(out_debug, rc);
	}

	rc = dfuse_fs_init(dfuse_info);
	if (rc != 0) {
		DFUSE_TRA_DEBUG(dfuse_info, "dfuse_fs_init() failed:" DF_RC, DP_RC(rc));
		D_GOTO(out_fini, rc);
	}

	rc = dfuse_pool_connect(dfuse_info, pool_name, &dfp);
	if (rc != 0) {
		DFUSE_TRA_DEBUG(dfuse_info, "dfuse_pool_connect() failed: %d", rc);
		D_GOTO(out_daos, rc = daos_errno2der(rc));
	}

	if (cont_name[0] && uuid_parse(cont_name, cont_uuid) < 0)
		D_GOTO(out_daos, rc = -DER_INVAL);

	rc = dfuse_cont_open(dfuse_info, dfp, &cont_uuid, &dfs);
	if (rc != 0) {
		DFUSE_TRA_DEBUG(dfuse_info, "dfuse_cont_open() failed: %d", rc);
		D_GOTO(out_pool, rc = daos_errno2der(rc));
	}

	rc = dfuse_fs_start(dfuse_info, dfs);
	if (rc != 0) {
		DFUSE_TRA_DEBUG(dfuse_info, "dfuse_fs_start() failed:" DF_RC, DP_RC(rc));
		D_GOTO(out_cont, rc);
	}

	/* Mock up opendir */

	struct dfuse_inode_entry *ie;
	struct dfuse_obj_hdl     *oh;
	ino_t                     ino = 1;
	d_list_t                 *rlink;
	char                     *reply_buff;
	size_t                    size = 1024;

	rlink = d_hash_rec_find(&dfuse_info->dpi_iet, &ino, sizeof(ino));
	ie    = container_of(rlink, struct dfuse_inode_entry, ie_htl);

	d_hash_rec_decref(&dfuse_info->dpi_iet, rlink);

	D_ALLOC_PTR(oh);
	if (oh == NULL)
		D_GOTO(out_stop, rc = -DER_NOMEM);

	DFUSE_TRA_UP(oh, ie, "open handle");

	dfuse_open_handle_init(oh, ie);

	D_ALLOC(reply_buff, size);
	if (reply_buff == NULL) {
		D_FREE(oh);
		D_GOTO(out_stop, rc = -DER_NOMEM);
	}

	rc = dfuse_do_readdir(dfuse_info, 0, oh, reply_buff, &size, 0, false);
	if (rc != 0)
		DFUSE_TRA_ERROR(oh, "Reply was: %d (%s)", rc, strerror(rc));

	rc = dfuse_do_readdir(dfuse_info, 0, oh, reply_buff, &size, 3, false);
	if (rc != 0)
		DFUSE_TRA_ERROR(oh, "Reply was: %d (%s)", rc, strerror(rc));

	rc = dfuse_do_readdir(dfuse_info, 0, oh, reply_buff, &size, 0, true);
	if (rc != 0)
		DFUSE_TRA_ERROR(oh, "Reply was: %d (%s)", rc, strerror(rc));

	D_FREE(reply_buff);
	D_FREE(oh->doh_rd);
	D_FREE(oh);

out_stop:
	d_hash_rec_decref(&dfuse_info->dpi_pool_table, &dfp->dfp_entry);

	rc = dfuse_fs_stop(dfuse_info);
	if (rc != 0) {
		DFUSE_TRA_DEBUG(dfuse_info, "dfuse_fs_stop() failed:" DF_RC, DP_RC(rc));
	}
	rc2 = dfuse_fs_fini(dfuse_info);
	if (rc2 != 0) {
		DFUSE_TRA_DEBUG(dfuse_info, "dfuse_fs_fini() failed:" DF_RC, DP_RC(rc2));
	}
	if (rc == -DER_SUCCESS)
		rc = rc2;

	goto out_fini;
out_cont:
	d_hash_rec_decref(&dfp->dfp_cont_table, &dfs->dfs_entry);
out_pool:
	d_hash_rec_decref(&dfuse_info->dpi_pool_table, &dfp->dfp_entry);
out_daos:
	rc2 = dfuse_fs_fini(dfuse_info);
	if (rc == -DER_SUCCESS)
		rc = rc2;
out_fini:
	daos_fini();
out_debug:
	DFUSE_TRA_DEBUG(dfuse_info, "Exiting: " DF_RC, DP_RC(rc));
	D_FREE(dfuse_info);
out_debug_fini:
	daos_debug_fini();
out:
	return 0;
}
