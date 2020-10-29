/**
 * (C) Copyright 2020 Intel Corporation.
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

#include <pwd.h>
#include <grp.h>
#include <linux/limits.h>
#include <json-c/json.h>

#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos.h>

static void
cmd_free_args(char **args, int argcount)
{
	int i;

	for (i = 0; i < argcount; i++)
		D_FREE(args[i]);

	D_FREE(args);
}

static char **
cmd_push_arg(char *args[], int *argcount, const char *fmt, ...)
{
	char		**tmp = NULL;
	char		*arg = NULL;
	va_list		ap;
	int		rc;

	va_start(ap, fmt);
	rc = vasprintf(&arg, fmt, ap);
	if (arg == NULL || rc < 0) {
		D_ERROR("failed to create arg\n");
		cmd_free_args(args, *argcount);
		return NULL;
	}
	va_end(ap);

	D_REALLOC(tmp, args, sizeof(char *) * (*argcount + 1));
	if (tmp == NULL) {
		D_ERROR("realloc failed\n");
		D_FREE(arg);
		cmd_free_args(args, *argcount);
		return NULL;
	}

	tmp[*argcount] = arg;
	(*argcount)++;

	return tmp;
}

static char *
cmd_string(const char *cmd_base, char *args[], int argcount)
{
	char		*tmp = NULL;
	char		*cmd_str = NULL;
	size_t		size;
	int		i;

	if (cmd_base == NULL)
		return NULL;

	size = strnlen(cmd_base, ARG_MAX - 1) + 1;
	D_STRNDUP(cmd_str, cmd_base, size);
	if (cmd_str == NULL)
		return NULL;

	for (i = 0; i < argcount; i++) {
		size += strnlen(args[i], ARG_MAX - 1) + 1;
		if (size >= ARG_MAX) {
			D_ERROR("arg list too long\n");
			D_FREE(cmd_str);
			return NULL;
		}

		D_REALLOC(tmp, cmd_str, size);
		if (tmp == NULL) {
			D_FREE(cmd_str);
			return NULL;
		}
		strncat(tmp, args[i], size);
		cmd_str = tmp;
	}

	return cmd_str;
}

#ifndef HAVE_JSON_TOKENER_GET_PARSE_END
#define json_tokener_get_parse_end(tok) ((tok)->char_offset)
#endif

#define JSON_CHUNK_SIZE 4096
#define JSON_MAX_INPUT (1 << 20) /* 1MB is plenty */

/* JSON output handling for dmg command */
static int
daos_dmg_json_pipe(const char *dmg_cmd, const char *dmg_config_file,
		   char *args[], int argcount,
		   struct json_object **json_out)
{
	char			*cmd_str = NULL;
	char			*cmd_base = NULL;
	struct	json_object	*obj = NULL;
	int			parse_depth = JSON_TOKENER_DEFAULT_DEPTH;
	json_tokener		*tok = NULL;
	FILE			*fp = NULL;
	int			pc_rc, rc = 0;

	if (dmg_config_file == NULL)
		D_ASPRINTF(cmd_base, "dmg -j -i %s ", dmg_cmd);
	else
		D_ASPRINTF(cmd_base, "dmg -j -o %s %s ",
			   dmg_config_file, dmg_cmd);
	if (cmd_base == NULL)
		return -DER_NOMEM;
	cmd_str = cmd_string(cmd_base, args, argcount);
	D_FREE(cmd_base);
	if (cmd_str == NULL)
		return -DER_NOMEM;

	D_DEBUG(DB_TEST, "running %s\n", cmd_str);
	fp = popen(cmd_str, "r");
	if (!fp) {
		D_ERROR("failed to invoke %s\n", cmd_str);
		D_GOTO(out, rc = -DER_IO);
	}

	/* If the caller doesn't care about output, don't bother parsing it. */
	if (json_out == NULL)
		goto out_pclose;

	char	*jbuf = NULL, *temp;
	size_t	size = 0;
	size_t	total = 0;
	size_t	n;

	while (1) {
		if (total + JSON_CHUNK_SIZE + 1 > size) {
			size = total + JSON_CHUNK_SIZE + 1;

			if (size >= JSON_MAX_INPUT) {
				D_ERROR("JSON input too large\n");
				D_GOTO(out_jbuf, rc = -DER_REC2BIG);
			}

			D_REALLOC(temp, jbuf, size);
			if (temp == NULL)
				D_GOTO(out_jbuf, rc = -DER_NOMEM);
			jbuf = temp;
		}

		n = fread(jbuf + total, 1, JSON_CHUNK_SIZE, fp);
		if (n == 0)
			break;

		total += n;
	}

	D_REALLOC(temp, jbuf, total + 1);
	if (temp == NULL)
		D_GOTO(out_jbuf, rc = -DER_NOMEM);
	jbuf = temp;
	jbuf[total] = '\0';

	tok = json_tokener_new_ex(parse_depth);
	if (tok == NULL)
		D_GOTO(out_jbuf, rc = -DER_NOMEM);

	obj = json_tokener_parse_ex(tok, jbuf, total);
	if (obj == NULL) {
		enum json_tokener_error jerr = json_tokener_get_error(tok);
		int fail_off = json_tokener_get_parse_end(tok);
		char *aterr = &jbuf[fail_off];

		D_ERROR("failed to parse JSON at offset %d: %s %c\n",
			fail_off, json_tokener_error_desc(jerr), aterr[0]);
		D_GOTO(out_tokener, rc = -DER_INVAL);
	}

out_tokener:
	json_tokener_free(tok);
out_jbuf:
	D_FREE(jbuf);
out_pclose:
	pc_rc = pclose(fp);
	if (pc_rc != 0) {
		D_ERROR("%s exited with %d\n", cmd_str, pc_rc % 0xFF);
		if (rc == 0)
			rc = -DER_MISC;
	}
out:
	D_FREE(cmd_str);

	if (obj != NULL) {
		struct json_object *tmp;
		int flags = JSON_C_TO_STRING_PRETTY | JSON_C_TO_STRING_SPACED;

		D_DEBUG(DB_TEST, "parsed output:\n%s\n",
			json_object_to_json_string_ext(obj, flags));

		json_object_object_get_ex(obj, "error", &tmp);

		if (tmp && !json_object_is_type(tmp, json_type_null)) {
			const char *err_str;

			err_str = json_object_get_string(tmp);
			D_ERROR("dmg error: %s\n", err_str);
			*json_out = json_object_get(tmp);

			if (json_object_object_get_ex(obj, "status", &tmp))
				rc = json_object_get_int(tmp);
		} else {
			if (json_object_object_get_ex(obj, "response", &tmp))
				*json_out = json_object_get(tmp);
		}

		json_object_put(obj);
	}

	return rc;
}

static int
parse_pool_info(struct json_object *json_pool, daos_mgmt_pool_info_t *pool_info)
{
	struct json_object	*tmp, *rank;
	int			n_svcranks;
	const char		*uuid_str;
	int			i;

	if (json_pool == NULL || pool_info == NULL)
		return -DER_INVAL;

	if (!json_object_object_get_ex(json_pool, "UUID", &tmp)) {
		D_ERROR("unable to extract pool UUID from JSON\n");
		return -DER_INVAL;
	}
	uuid_str = json_object_get_string(tmp);
	uuid_parse(uuid_str, pool_info->mgpi_uuid);

	if (!json_object_object_get_ex(json_pool, "Svcreps", &tmp)) {
		D_ERROR("unable to parse pool svcreps from JSON\n");
		return -DER_INVAL;
	}

	n_svcranks = json_object_array_length(tmp);
	if (pool_info->mgpi_svc == NULL) {
		pool_info->mgpi_svc = d_rank_list_alloc(n_svcranks);
		if (pool_info->mgpi_svc == NULL) {
			D_ERROR("failed to allocate rank list\n");
			return -DER_NOMEM;
		}
	}

	for (i = 0; i < n_svcranks; i++) {
		rank = json_object_array_get_idx(tmp, i);
		pool_info->mgpi_svc->rl_ranks[i] =
			json_object_get_int(rank);
	}

	return 0;
}

static char *
rank_list_to_string(const d_rank_list_t *rank_list)
{
	char		*ranks_str = NULL;
	int		 width;
	int		 i;
	int		 idx = 0;

	if (rank_list == NULL)
		return NULL;

	width = 0;
	for (i = 0; i < rank_list->rl_nr; i++)
		width += snprintf(NULL, 0, "%d,", rank_list->rl_ranks[i]);
	width++;
	D_ALLOC(ranks_str, width);
	if (ranks_str == NULL)
		return NULL;
	for (i = 0; i < rank_list->rl_nr; i++)
		idx += sprintf(&ranks_str[idx], "%d,", rank_list->rl_ranks[i]);
	ranks_str[width - 1] = '\0';
	ranks_str[width - 2] = '\0';

	return ranks_str;
}

static int
print_acl_entry(FILE *outstream, struct daos_prop_entry *acl_entry)
{
	struct daos_acl		*acl = NULL;
	char			**acl_str = NULL;
	size_t			nr_acl_str = 0;
	size_t			i;
	int			rc = 0;

	if (outstream == NULL || acl_entry == NULL)
		return -DER_INVAL;

	/*
	 * Validate the ACL before we start printing anything out.
	 */
	if (acl_entry->dpe_val_ptr != NULL) {
		acl = acl_entry->dpe_val_ptr;
		rc = daos_acl_to_strs(acl, &acl_str, &nr_acl_str);
		if (rc != 0) {
			D_ERROR("invalid ACL\n");
			goto out;
		}
	}

	for (i = 0; i < nr_acl_str; i++)
		fprintf(outstream, "%s\n", acl_str[i]);

	for (i = 0; i < nr_acl_str; i++)
		D_FREE(acl_str[i]);

	D_FREE(acl_str);

out:
	return rc;
}

int
dmg_pool_create(const char *dmg_config_file,
		uid_t uid, gid_t gid, const char *grp,
		const d_rank_list_t *tgts,
		daos_size_t scm_size, daos_size_t nvme_size,
		daos_prop_t *prop,
		d_rank_list_t *svc, uuid_t uuid)
{
	int			argcount = 0;
	char			**args = NULL;
	struct passwd		*passwd = NULL;
	struct group		*group = NULL;
	struct daos_prop_entry	*entry;
	char			tmp_buf[L_tmpnam], *tmp_name = tmp_buf;
	FILE			*tmp_file = NULL;
	daos_mgmt_pool_info_t	pool_info = {};
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	if (grp != NULL) {
		args = cmd_push_arg(args, &argcount,
				    "--sys=%s ", grp);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (tgts != NULL) {
		char *ranks_str = rank_list_to_string(tgts);

		if (ranks_str == NULL) {
			D_ERROR("failed to create rank string\n");
			D_GOTO(out_cmd, rc = -DER_NOMEM);
		}
		args = cmd_push_arg(args, &argcount,
				    "--ranks=%s ", ranks_str);
		D_FREE(ranks_str);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	passwd = getpwuid(uid);
	if (passwd == NULL) {
		D_ERROR("unable to resolve %d to passwd entry\n", uid);
		D_GOTO(out_cmd, rc = -DER_INVAL);
	}

	args = cmd_push_arg(args, &argcount,
			    "--user=%s ", passwd->pw_name);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	group = getgrgid(gid);
	if (group == NULL) {
		D_ERROR("unable to resolve %d to group name\n", gid);
		D_GOTO(out_cmd, rc = -DER_INVAL);
	}

	args = cmd_push_arg(args, &argcount,
			    "--group=%s ", group->gr_name);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	args = cmd_push_arg(args, &argcount,
			    "--scm-size=%"PRIu64"b ", scm_size);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (nvme_size > 0) {
		args = cmd_push_arg(args, &argcount,
				    "--nvme-size=%"PRIu64"b ", nvme_size);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (prop != NULL) {
		entry = daos_prop_entry_get(prop, DAOS_PROP_PO_ACL);
		if (entry != NULL) {
			tmp_name = tmpnam(tmp_buf);
			if (tmp_name == NULL) {
				D_ERROR("failed to create tmpfile name\n");
				D_GOTO(out_cmd, rc = -DER_NOMEM);
			}
			tmp_file = fopen(tmp_name, "w");
			if (tmp_file == NULL) {
				D_ERROR("failed to open %s\n", tmp_name);
				D_GOTO(out_cmd, rc = -DER_MISC);
			}

			rc = print_acl_entry(tmp_file, entry);
			fclose(tmp_file);
			if (rc != 0) {
				D_ERROR("failed to write ACL to tmpfile\n");
				goto out_cmd;
			}
			args = cmd_push_arg(args, &argcount,
					    "--acl-file=%s ", tmp_name);
			if (args == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
	}

	if (svc != NULL) {
		args = cmd_push_arg(args, &argcount,
				    "--nsvc=%d", svc->rl_nr);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = daos_dmg_json_pipe("pool create", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0) {
		D_ERROR("dmg failed");
		goto out_json;
	}

	rc = parse_pool_info(dmg_out, &pool_info);
	if (rc != 0) {
		D_ERROR("failed to parse pool info\n");
		goto out_json;
	}

	uuid_copy(uuid, pool_info.mgpi_uuid);
	if (svc == NULL)
		goto out_svc;

	rc = d_rank_list_copy(svc, pool_info.mgpi_svc);
	if (rc != 0) {
		D_ERROR("failed to dup svc rank list\n");
		goto out_svc;
	}

out_svc:
	d_rank_list_free(pool_info.mgpi_svc);
out_json:
	if (dmg_out != NULL)
		json_object_put(dmg_out);
out_cmd:
	cmd_free_args(args, argcount);
out:
	if (tmp_file != NULL)
		unlink(tmp_name);
	return rc;
}

int
dmg_pool_destroy(const char *dmg_config_file,
		 const uuid_t uuid, const char *grp, int force)
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	uuid_unparse_lower(uuid, uuid_str);
	args = cmd_push_arg(args, &argcount,
			    "--pool=%s ", uuid_str);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (force != 0) {
		args = cmd_push_arg(args, &argcount, "--force");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = daos_dmg_json_pipe("pool destroy", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0) {
		D_ERROR("dmg failed");
		goto out_json;
	}

out_json:
	if (dmg_out != NULL)
		json_object_put(dmg_out);
	cmd_free_args(args, argcount);
out:
	return rc;
}

int
dmg_pool_list(const char *dmg_config_file, const char *group,
	      daos_size_t *npools, daos_mgmt_pool_info_t *pools)
{
	daos_size_t		npools_in;
	struct json_object	*dmg_out = NULL;
	struct json_object	*pool_list = NULL;
	struct json_object	*pool = NULL;
	int			rc = 0;
	int			i;

	if (npools == NULL)
		return -DER_INVAL;
	npools_in = *npools;

	rc = daos_dmg_json_pipe("pool list", dmg_config_file,
				NULL, 0, &dmg_out);
	if (rc != 0) {
		D_ERROR("dmg failed");
		goto out_json;
	}

	json_object_object_get_ex(dmg_out, "Pools", &pool_list);
	if (pool_list == NULL)
		*npools = 0;
	else
		*npools = json_object_array_length(pool_list);

	if (pools == NULL)
		goto out_json;
	else if (npools_in < *npools)
		D_GOTO(out_json, rc = -DER_TRUNC);

	for (i = 0; i < *npools; i++) {
		pool = json_object_array_get_idx(pool_list, i);
		if (pool == NULL)
			D_GOTO(out_json, rc = -DER_INVAL);

		rc = parse_pool_info(pool, &pools[i]);
		if (rc != 0)
			goto out_json;
	}

out_json:
	if (dmg_out != NULL)
		json_object_put(dmg_out);

	return rc;
}

static int
parse_device_info(struct json_object *smd_dev, device_list *devices,
		  char *host, int dev_length, int *disks)
{
	struct json_object	*tmp;
	struct json_object	*dev = NULL;
	int			i;

	for (i = 0; i < dev_length; i++) {
		dev = json_object_array_get_idx(smd_dev, i);
		strcpy(devices[*disks].host, strtok(host, ":") + 1);
		if (!json_object_object_get_ex(dev, "uuid", &tmp)) {
			D_ERROR("unable to extract uuid from JSON\n");
			return -DER_INVAL;
		}
		uuid_parse(json_object_get_string(tmp),
			   devices[*disks].device_id);

		if (!json_object_object_get_ex(dev, "state", &tmp)) {
			D_ERROR("unable to extract state from JSON\n");
			return -DER_INVAL;
		}
		strcpy(devices[*disks].state, json_object_to_json_string(tmp));

		if (!json_object_object_get_ex(dev, "rank", &tmp)) {
			D_ERROR("unable to extract rank from JSON\n");
			return -DER_INVAL;
		}
		devices[*disks].rank = atoi(json_object_to_json_string(tmp));
		*disks = *disks + 1;
	}

	return 0;
}

int
dmg_storage_device_list(const char *dmg_config_file, int *ndisks,
			device_list *devices)
{
	struct json_object	*dmg_out = NULL;
	struct json_object	*storage_map = NULL;
	struct json_object	*hosts = NULL;
	struct json_object	*smd_info = NULL;
	struct json_object	*smd_dev = NULL;
	char		host[100];
	int			dev_length = 0;
	int			rc = 0;
	int			*disk;

	if (ndisks != NULL)
		*ndisks = 0;

	D_ALLOC_PTR(disk);
	rc = daos_dmg_json_pipe("storage query list-devices", dmg_config_file,
				NULL, 0, &dmg_out);
	if (rc != 0) {
		D_ERROR("dmg failed");
		goto out_json;
	}

	if (!json_object_object_get_ex(dmg_out, "host_storage_map",
				       &storage_map)) {
		D_ERROR("unable to extract host_storage_map from JSON\n");
		return -DER_INVAL;
	}

	json_object_object_foreach(storage_map, key, val) {
		D_DEBUG(DB_TEST, "key:\"%s\",val=%s\n", key,
			json_object_to_json_string(val));

		if (!json_object_object_get_ex(val, "hosts", &hosts)) {
			D_ERROR("unable to extract hosts from JSON\n");
			return -DER_INVAL;
		}

		strcpy(host, json_object_to_json_string(hosts));

		json_object_object_foreach(val, key1, val1) {
			D_DEBUG(DB_TEST, "key1:\"%s\",val1=%s\n", key1,
				json_object_to_json_string(val1));

			json_object_object_get_ex(val1, "smd_info", &smd_info);
			if (smd_info != NULL) {
				if (!json_object_object_get_ex(
					smd_info, "devices", &smd_dev)) {
					D_ERROR("unable to extract devices\n");
					return -DER_INVAL;
				}

				if (smd_dev != NULL)
					dev_length = json_object_array_length(
						smd_dev);

				if (ndisks != NULL)
					*ndisks = *ndisks + dev_length;

				if (devices != NULL) {
					rc = parse_device_info(smd_dev, devices,
							       host, dev_length,
							       disk);
					if (rc != 0)
						goto out_json;
				}
			}
		}
	}

out_json:
	if (dmg_out != NULL)
		json_object_put(dmg_out);

	D_FREE(disk);
	return rc;
}

int
dmg_storage_set_nvme_fault(const char *dmg_config_file,
			   char *host, const uuid_t uuid, int force)
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	uuid_unparse_lower(uuid, uuid_str);
	args = cmd_push_arg(args, &argcount, " --uuid=%s ", uuid_str);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (force != 0) {
		args = cmd_push_arg(args, &argcount, " --force ");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	args = cmd_push_arg(args, &argcount, " --host-list=%s ", host);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("storage set nvme-faulty ", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0) {
		D_ERROR("dmg command failed");
		goto out_json;
	}

out_json:
	if (dmg_out != NULL)
		json_object_put(dmg_out);
	cmd_free_args(args, argcount);
out:
	return rc;
}

int verify_blobstore_state(int state, const char *state_str)
{
	if (strcasecmp(state_str, "FAULTY") == 0) {
		if (state == BIO_BS_STATE_FAULTY)
			return 0;
	}

	if (strcasecmp(state_str, "NORMAL") == 0) {
		if (state == BIO_BS_STATE_NORMAL)
			return 0;
	}

	if (strcasecmp(state_str, "TEARDOWN") == 0) {
		if (state == BIO_BS_STATE_TEARDOWN)
			return 0;
	}

	if (strcasecmp(state_str, "OUT") == 0) {
		if (state == BIO_BS_STATE_OUT)
			return 0;
	}

	if (strcasecmp(state_str, "SETUP") == 0) {
		if (state == BIO_BS_STATE_SETUP)
			return 0;
	}

	return 1;
}
