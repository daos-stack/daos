

#define D_LOGFAC DD_FAC(dfuse)

#include "dfuse.h"

int
main()
{
	struct dfuse_info *dfuse_info                             = NULL;
	char               pool_name[DAOS_PROP_LABEL_MAX_LEN + 1] = {};
	uuid_t             cont_uuid                              = {};
	struct dfuse_pool *dfp                                    = NULL;
	struct dfuse_cont *dfs                                    = NULL;
	int                rc;
	int                rc2;

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
	rc = dfuse_cont_open(dfuse_info, dfp, &cont_uuid, &dfs);
	if (rc != 0) {
		DFUSE_TRA_DEBUG(dfuse_info, "dfuse_cont_open() failed: %d", rc);
		D_GOTO(out_daos, rc = daos_errno2der(rc));
	}

	rc = dfuse_fs_start(dfuse_info, dfs);
	if (rc != 0) {
		DFUSE_TRA_DEBUG(dfuse_info, "dfuse_fs_start() failed:" DF_RC, DP_RC(rc));
		D_GOTO(out_daos, rc);
	}

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
