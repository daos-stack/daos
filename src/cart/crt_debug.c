/* Copyright (C) 2017-2018 Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted for any purpose (including commercial purposes)
 * provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions, and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions, and the following disclaimer in the
 *    documentation and/or materials provided with the distribution.
 *
 * 3. In addition, redistributions of modified forms of the source or binary
 *    code must carry prominent notices stating that the original code was
 *    changed and the date of the change.
 *
 *  4. All publications or advertising materials mentioning features or use of
 *     this software are asked, but not required, to acknowledge that it was
 *     developed by Intel Corporation and credit the contributors.
 *
 * 5. Neither the name of Intel Corporation, nor the name of any Contributor
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <crt_internal.h>

#define CART_FAC_MAX_LEN (128)

#define DECLARE_FAC(name) int DD_FAC(name)

DECLARE_FAC(rpc);
DECLARE_FAC(bulk);
DECLARE_FAC(corpc);
DECLARE_FAC(grp);
DECLARE_FAC(lm);
DECLARE_FAC(hg);
DECLARE_FAC(pmix);
DECLARE_FAC(self_test);
DECLARE_FAC(iv);
DECLARE_FAC(ctl);

#define D_INIT_LOG_FAC(name, lname, idp)	\
	d_init_log_facility(idp, name, lname)

struct cart_debug_fac {
	/** name of the facility */
	char		*df_name;
	char		*df_lname;
	/** pointer to the facility ID */
	int		*df_idp;
	/** facility is enabled */
	int		df_enabled;
	size_t		df_name_size;
	size_t		df_lname_size;
};

#define CART_FAC_DICT_ENTRY(name, lname, idp, enabled)		\
	{ .df_name = name, .df_lname = lname, .df_idp = idp,	\
	  .df_enabled = enabled, .df_name_size = sizeof(name),	\
	  .df_lname_size = sizeof(lname) }

/** dictionary for all facilities */
static struct cart_debug_fac debug_fac_dict[] = {
	CART_FAC_DICT_ENTRY("RPC", "rpc", &d_rpc_logfac, 1),
	CART_FAC_DICT_ENTRY("BULK", "bulk", &d_bulk_logfac, 1),
	CART_FAC_DICT_ENTRY("CORPC", "corpc", &d_corpc_logfac, 1),
	CART_FAC_DICT_ENTRY("GRP", "group", &d_grp_logfac, 1),
	CART_FAC_DICT_ENTRY("LM", "livenessmap", &d_lm_logfac, 1),
	CART_FAC_DICT_ENTRY("HG", "mercury", &d_hg_logfac, 1),
	CART_FAC_DICT_ENTRY("PMIX", "pmix", &d_pmix_logfac, 1),
	CART_FAC_DICT_ENTRY("ST", "self_test", &d_self_test_logfac, 1),
	CART_FAC_DICT_ENTRY("IV", "iv", &d_iv_logfac, 1),
};

/** Load enabled debug facilities from the environment variable. */
static void
debug_fac_load_env(void)
{
	char	*fac_env;
	char	*fac_str;
	char	*cur;
	int	i;
	int	num_dbg_fac_entries;

	fac_env = getenv(DD_FAC_ENV);
	if (fac_env == NULL)
		return;

	D_STRNDUP(fac_str, fac_env, CART_FAC_MAX_LEN);
	if (fac_str == NULL) {
		fprintf(stderr, "D_STRNDUP of fac mask failed");
		return;
	}

	/* Disable all facilities. */
	num_dbg_fac_entries = ARRAY_SIZE(debug_fac_dict);
	for (i = 0; i < num_dbg_fac_entries; i++)
		debug_fac_dict[i].df_enabled = 0;

	cur = strtok(fac_str, DD_SEP);
	while (cur != NULL) {
		for (i = 0; i < num_dbg_fac_entries; i++) {
			if (debug_fac_dict[i].df_name != NULL &&
			    strncasecmp(cur, debug_fac_dict[i].df_name,
					debug_fac_dict[i].df_name_size)
					== 0) {
				debug_fac_dict[i].df_enabled = 1;
				break;
			} else if (debug_fac_dict[i].df_lname != NULL &&
				   strncasecmp(cur, debug_fac_dict[i].df_lname,
				   debug_fac_dict[i].df_lname_size) == 0) {
				debug_fac_dict[i].df_enabled = 1;
				break;
			} else if (strncasecmp(cur, DD_FAC_ALL,
						strlen(DD_FAC_ALL)) == 0) {
				debug_fac_dict[i].df_enabled = 1;
			}
		}
		cur = strtok(NULL, DD_SEP);
	}
	D_FREE(fac_str);
}

int
crt_setup_log_fac(void)
{
	int	i;
	int	num_dbg_fac_entries;

	num_dbg_fac_entries = ARRAY_SIZE(debug_fac_dict);

	debug_fac_load_env();
	for (i = 0; i < num_dbg_fac_entries; i++) {
		if (debug_fac_dict[i].df_enabled) {
			D_INIT_LOG_FAC(debug_fac_dict[i].df_name,
				       debug_fac_dict[i].df_lname,
				       debug_fac_dict[i].df_idp);
		}
	}

	d_log_sync_mask();

	return 0;
}
