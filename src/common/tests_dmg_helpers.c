/**
 * (C) Copyright 2020-2024 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

#include <pwd.h>
#include <grp.h>
#include <linux/limits.h>
#include <json-c/json.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/mman.h>

#include <daos/common.h>
#include <daos/tests_lib.h>
#include <daos.h>
#include <daos_srv/bio.h>

/*
 * D_LOG_DMG_STDERR_ENV provides the environment variable that can be used to enable test binaries
 * to log the stderr output from executing dmg. This can be useful for debugging.
 */
#define D_LOG_DMG_STDERR_ENV "D_TEST_LOG_DMG_STDERR"

static bool
is_stderr_logging_enabled(void)
{
	int  rc;
	bool enabled = false;

	rc = d_getenv_bool(D_LOG_DMG_STDERR_ENV, &enabled);
	if (rc == 0)
		return enabled;
	return false;
}

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
	va_end(ap);
	if (arg == NULL || rc < 0) {
		D_ERROR("failed to create arg\n");
		cmd_free_args(args, *argcount);
		return NULL;
	}

	D_REALLOC_ARRAY(tmp, args, *argcount, *argcount + 1);
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
	size_t		size, old;
	int		i;
	void		*addr;

	if (cmd_base == NULL)
		return NULL;

	old = size = strnlen(cmd_base, ARG_MAX - 1) + 1;
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

		D_REALLOC(tmp, cmd_str, old, size);
		if (tmp == NULL) {
			D_FREE(cmd_str);
			return NULL;
		}
		strncat(tmp, args[i], size);
		cmd_str = tmp;
		old = size;
	}

	addr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED) {
		D_ERROR("mmap() failed : %s\n", strerror(errno));
		D_FREE(cmd_str);
		return NULL;
	}
	memcpy(addr, cmd_str, size);
	D_FREE(cmd_str);

	return (char *)addr;
}

static void
log_stderr_pipe(int fd)
{
	char	buf[512];
	char	*full_msg = NULL;
	ssize_t	len = 0;

	D_DEBUG(DB_TEST, "reading from stderr pipe\n");
	while (1) {
		ssize_t n;
		ssize_t old_len = len;
		char	*tmp;

		n = read(fd, buf, sizeof(buf));
		if (n == 0)
			break;
		if (n < 0) {
			D_ERROR("read from stderr pipe failed: %s\n", strerror(errno));
			break;
		}

		len = len + n;
		D_REALLOC(tmp, full_msg, old_len, len);
		if (tmp == NULL) {
			D_ERROR("reading from stderr pipe: can't realloc tmp with size %ld\n",
				len);
			break;
		}

		full_msg = tmp;
		strncpy(&full_msg[old_len], buf, n);
	}


	D_DEBUG(DB_TEST, "done reading stderr pipe\n");
	close(fd);

	if (full_msg == NULL) {
		D_INFO("no stderr output\n");
		return;
	}

	D_DEBUG(DB_TEST, "stderr: %s\n", full_msg);
	D_FREE(full_msg);
}

static int
run_cmd(const char *command, int *outputfd)
{
	int  rc       = 0;
	int  child_rc = 0;
	int  child_pid;
	int  stdoutfd[2];
	int  stderrfd[2];
	bool log_stderr;

	D_DEBUG(DB_TEST, "dmg cmd: %s\n", command);

	log_stderr = is_stderr_logging_enabled();
	if (log_stderr)
		D_DEBUG(DB_TEST, "dmg stderr output will be logged\n");

	/* Create pipes */
	if (pipe(stdoutfd) == -1) {
		rc = daos_errno2der(errno);
		D_ERROR("failed to create stdout pipe: %s\n", strerror(errno));
		return rc;
	}

	if (pipe(stderrfd) == -1) {
		rc = daos_errno2der(errno);
		D_ERROR("failed to create stderr pipe: %s\n", strerror(errno));
		close(stdoutfd[0]);
		close(stdoutfd[1]);
		return rc;
	}

	D_DEBUG(DB_TEST, "forking to run dmg command\n");

	child_pid = fork();
	if (child_pid == -1) {
		rc = daos_errno2der(errno);
		D_ERROR("failed to fork: %s\n", strerror(errno));
		return rc;
	} else if (child_pid == 0) {
		/* child doesn't need the read end of the pipes */
		close(stdoutfd[0]);
		close(stderrfd[0]);

		if (dup2(stdoutfd[1], STDOUT_FILENO) == -1)
			_exit(errno);

		if (dup2(stderrfd[1], STDERR_FILENO) == -1)
			_exit(errno);

		close(stdoutfd[1]);
		close(stderrfd[1]);

		rc = system(command);
		if (rc == -1)
			_exit(errno);
		_exit(rc);
	}

	/* parent doesn't need the write end of the pipes */
	close(stdoutfd[1]);
	close(stderrfd[1]);

	D_DEBUG(DB_TEST, "waiting for dmg to finish executing\n");
	if (wait(&child_rc) == -1) {
		D_ERROR("wait failed: %s\n", strerror(errno));
		return daos_errno2der(errno);
	}
	D_DEBUG(DB_TEST, "dmg command finished\n");

	if (child_rc != 0) {
		D_ERROR("child process failed, rc=%d (%s)\n", child_rc, strerror(child_rc));
		close(stdoutfd[0]);
		if (log_stderr)
			log_stderr_pipe(stderrfd[0]); /* closes the pipe after reading */
		else
			close(stderrfd[0]);
		return daos_errno2der(child_rc);
	}

	close(stderrfd[0]);
	*outputfd = stdoutfd[0];
	return 0;
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
	int			stdoutfd = 0;
	int			rc = 0;
	const char		*debug_flags = "-d --log-file=/tmp/suite_dmg.log";

	if (dmg_config_file == NULL)
		D_ASPRINTF(cmd_base, "dmg -j -i %s %s ", debug_flags, dmg_cmd);
	else
		D_ASPRINTF(cmd_base, "dmg -j %s -o %s %s ", debug_flags,
			   dmg_config_file, dmg_cmd);
	if (cmd_base == NULL)
		return -DER_NOMEM;
	cmd_str = cmd_string(cmd_base, args, argcount);
	D_FREE(cmd_base);
	if (cmd_str == NULL)
		return -DER_NOMEM;

	rc = run_cmd(cmd_str, &stdoutfd);
	if (rc != 0)
		goto out;

	/* If the caller doesn't care about output, don't bother parsing it. */
	if (json_out == NULL)
		goto out_close;

	fp = fdopen(stdoutfd, "r");
	if (fp == NULL) {
		D_ERROR("fdopen failed: %s\n", strerror(errno));
		D_GOTO(out_close, rc = daos_errno2der(errno));
	}

	char	*jbuf = NULL, *temp;
	size_t	size = 0;
	size_t	total = 0;
	size_t	n;

	D_DEBUG(DB_TEST, "reading json from stdout\n");
	while (1) {
		if (total + JSON_CHUNK_SIZE + 1 > size) {
			size = total + JSON_CHUNK_SIZE + 1;

			if (size >= JSON_MAX_INPUT) {
				D_ERROR("JSON input too large (size=%lu)\n", size);
				D_GOTO(out_jbuf, rc = -DER_REC2BIG);
			}

			D_REALLOC(temp, jbuf, total, size);
			if (temp == NULL)
				D_GOTO(out_jbuf, rc = -DER_NOMEM);
			jbuf = temp;
		}

		n = fread(jbuf + total, 1, JSON_CHUNK_SIZE, fp);
		if (n == 0)
			break;

		total += n;
	}
	D_DEBUG(DB_TEST, "read %lu bytes\n", total);

	if (total == 0) {
		D_ERROR("dmg output is empty\n");
		D_GOTO(out_jbuf, rc = -DER_INVAL);
	}

	D_REALLOC(temp, jbuf, total, total + 1);
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

		D_ERROR("failed to parse JSON at offset %d: %s (failed character: %c)\n",
			fail_off, json_tokener_error_desc(jerr), aterr[0]);
		D_GOTO(out_tokener, rc = -DER_INVAL);
	}

out_tokener:
	json_tokener_free(tok);
out_jbuf:
	D_FREE(jbuf);

	if (fclose(fp) == -1) {
		D_ERROR("failed to close fp: %s\n", strerror(errno));
		if (rc == 0)
			rc = daos_errno2der(errno);
	}
out_close:
	close(stdoutfd);
out:
	if (munmap(cmd_str, strlen(cmd_str) + 1) == -1)
		D_ERROR("munmap() failed : %s\n", strerror(errno));

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
	int			i, rc;

	if (json_pool == NULL || pool_info == NULL)
		return -DER_INVAL;

	if (!json_object_object_get_ex(json_pool, "uuid", &tmp)) {
		D_ERROR("unable to extract pool UUID from JSON\n");
		return -DER_INVAL;
	}
	uuid_str = json_object_get_string(tmp);
	if (uuid_str == NULL) {
		D_ERROR("unable to extract UUID string from JSON\n");
		return -DER_INVAL;
	}
	rc = uuid_parse(uuid_str, pool_info->mgpi_uuid);
	if (rc != 0) {
		D_ERROR("failed parsing uuid_str\n");
		return -DER_INVAL;
	}

	if (!json_object_object_get_ex(json_pool, "svc_ldr", &tmp)) {
		D_ERROR("unable to extract pool leader from JSON\n");
		return -DER_INVAL;
	}
	pool_info->mgpi_ldr = json_object_get_int(tmp);

	if (!json_object_object_get_ex(json_pool, "svc_reps", &tmp)) {
		D_ERROR("unable to parse pool svcreps from JSON\n");
		return -DER_INVAL;
	}

	n_svcranks = json_object_array_length(tmp);
	if (n_svcranks <= 0) {
		D_ERROR("unexpected svc_reps length: %d\n", n_svcranks);
		return -DER_INVAL;
	}
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

static int
parse_dmg_string(struct json_object *obj, const char *key, char **tgt)
{
	struct json_object	*tmp;
	const char		*str;

	if (!json_object_object_get_ex(obj, key, &tmp)) {
		D_ERROR("Unable to extract %s from check query result\n", key);
		return -DER_INVAL;
	}

	str = json_object_get_string(tmp);
	if (str == NULL) {
		D_ERROR("Got empty %s from check query result\n", key);
		return -DER_INVAL;
	}

	D_STRNDUP(*tgt, str, strlen(str));
	if (*tgt == NULL) {
		D_ERROR("Failed to dup %s from check query result\n", key);
		return -DER_NOMEM;
	}

	return 0;
}

static int
parse_dmg_uuid(struct json_object *obj, const char *key, uuid_t uuid)
{
	struct json_object	*tmp;
	const char		*str;
	int			 rc;

	if (!json_object_object_get_ex(obj, key, &tmp)) {
		D_ERROR("Unable to extract %s from check query result\n", key);
		return -DER_INVAL;
	}

	str = json_object_get_string(tmp);
	if (str == NULL) {
		D_ERROR("Got empty %s from check query result\n", key);
		return -DER_INVAL;
	}

	rc = uuid_parse(str, uuid);
	if (rc != 0)
		D_ERROR("Failed to parse uuid %s from check query result\n", str);

	return rc;
}

int
dmg_pool_set_prop(const char *dmg_config_file,
		  const char *prop_name, const char *prop_value,
		  const uuid_t pool_uuid)
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	uuid_unparse_lower(pool_uuid, uuid_str);
	args = cmd_push_arg(args, &argcount, "%s ", uuid_str);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	args = cmd_push_arg(args, &argcount, "%s:%s",
			    prop_name, prop_value);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("pool set-prop", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0) {
		D_ERROR("dmg failed\n");
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
dmg_pool_get_prop(const char *dmg_config_file, const char *label,
		  const uuid_t uuid, const char *name, char **value)
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			len;
	int			rc = 0;

	D_ASSERT(name != NULL);
	D_ASSERT(value != NULL);

	if (label != NULL) {
		args = cmd_push_arg(args, &argcount, "%s %s", label, name);
	} else {
		uuid_unparse_lower(uuid, uuid_str);
		args = cmd_push_arg(args, &argcount, "%s %s", uuid_str, name);
	}
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("pool get-prop", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0) {
		D_ERROR("pool get-prop for %s failed: %d\n", label != NULL ? label : uuid_str, rc);
		goto out_json;
	}

	D_ASSERT(dmg_out != NULL);

	if (json_object_is_type(dmg_out, json_type_null)) {
		D_ERROR("Cannot find the property %s for %s\n",
			name, label != NULL ? label : uuid_str);
		D_GOTO(out_json, rc = -DER_ENOENT);
	}

	len = json_object_array_length(dmg_out);
	D_ASSERTF(len >= 1, "Invalid prop entries count: %d\n", len);

	rc = parse_dmg_string(json_object_array_get_idx(dmg_out, 0), "value", value);

out_json:
	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);

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
	char			tmp_name[] = "/tmp/acl_XXXXXX";
	FILE			*tmp_file = NULL;
	daos_mgmt_pool_info_t	pool_info = {};
	struct json_object	*dmg_out = NULL;
	bool			 has_label = false;
	int			fd = -1, rc = 0;

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
			fd = mkstemp(tmp_name);
			if (fd < 0) {
				D_ERROR("failed to create tmpfile file\n");
				D_GOTO(out_cmd, rc = -DER_NOMEM);
			}
			tmp_file = fdopen(fd, "w");
			if (tmp_file == NULL) {
				D_ERROR("failed to associate stream: %s\n",
					strerror(errno));
				close(fd);
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

		entry = daos_prop_entry_get(prop, DAOS_PROP_PO_LABEL);
		if (entry != NULL) {
			args = cmd_push_arg(args, &argcount, "%s ",
					    entry->dpe_str);
			if (args == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
			has_label = true;
		}

		entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SCRUB_MODE);
		if (entry != NULL) {
			const char *scrub_str = NULL;

			switch (entry->dpe_val) {
			case DAOS_SCRUB_MODE_OFF:
				scrub_str = "off";
				break;
			case DAOS_SCRUB_MODE_LAZY:
				scrub_str = "lazy";
				break;
			case DAOS_SCRUB_MODE_TIMED:
				scrub_str = "timed";
				break;
			default:
				break;
			}

			if (scrub_str) {
				args = cmd_push_arg(args, &argcount, "--properties=scrub:%s ",
						    scrub_str);
				if (args == NULL)
					D_GOTO(out, rc = -DER_NOMEM);
			}
		}

		entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_OPS_ENABLED);
		if (entry != NULL) {
			args = cmd_push_arg(args, &argcount, "--properties=svc_ops_enabled:%zu ",
					    entry->dpe_val);
			if (args == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SVC_OPS_ENTRY_AGE);
		if (entry != NULL) {
			args = cmd_push_arg(args, &argcount, "--properties=svc_ops_entry_age:%zu ",
					    entry->dpe_val);
			if (args == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}

		entry = daos_prop_entry_get(prop, DAOS_PROP_PO_SPACE_RB);
		if (entry != NULL) {
			args = cmd_push_arg(args, &argcount, "--properties=space_rb:%zu ",
					    entry->dpe_val);
			if (args == NULL)
				D_GOTO(out, rc = -DER_NOMEM);
		}
	}

	if (!has_label) {
		char	 path[] = "/tmp/test_XXXXXX";
		int	 tmp_fd;
		char	*label = &path[5];

		/* pool label is required, generate a unique one randomly */
		tmp_fd = mkstemp(path);
		if (tmp_fd < 0) {
			D_ERROR("failed to generate unique label: %s\n",
				strerror(errno));
			D_GOTO(out_cmd, rc = d_errno2der(errno));
		}
		close(tmp_fd);
		unlink(path);

		args = cmd_push_arg(args, &argcount, "%s ", label);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
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
		D_ERROR("dmg failed\n");
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

	if (pool_info.mgpi_svc->rl_nr == 0) {
		D_ERROR("unexpected zero-length pool svc ranks list\n");
		rc = -DER_INVAL;
		goto out_svc;
	}
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
	if (fd >= 0)
		unlink(tmp_name);
	return rc;
}

int
dmg_pool_destroy(const char *dmg_config_file, const uuid_t uuid, const char *grp, int force)
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	uuid_unparse_lower(uuid, uuid_str);
	args = cmd_push_arg(args, &argcount, "%s ", uuid_str);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	/* Always perform recursive destroy. */
	args = cmd_push_arg(args, &argcount, " --recursive ");
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (force != 0) {
		args = cmd_push_arg(args, &argcount, " --force ");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = daos_dmg_json_pipe("pool destroy", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0) {
		D_ERROR("dmg failed\n");
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
dmg_pool_evict(const char *dmg_config_file, const uuid_t uuid, const char *grp)
{
	char                uuid_str[DAOS_UUID_STR_SIZE];
	int                 argcount = 0;
	char              **args     = NULL;
	struct json_object *dmg_out  = NULL;
	int                 rc       = 0;

	uuid_unparse_lower(uuid, uuid_str);
	args = cmd_push_arg(args, &argcount, "%s ", uuid_str);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("pool evict", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0) {
		DL_ERROR(rc, "dmg failed");
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
dmg_pool_update_ace(const char *dmg_config_file, const uuid_t uuid, const char *grp,
		    const char *ace)
{
	char                uuid_str[DAOS_UUID_STR_SIZE];
	int                 argcount = 0;
	char              **args     = NULL;
	struct json_object *dmg_out  = NULL;
	int                 rc       = 0;

	uuid_unparse_lower(uuid, uuid_str);
	args = cmd_push_arg(args, &argcount, "%s ", uuid_str);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	args = cmd_push_arg(args, &argcount, "%s", "--entry=");
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	args = cmd_push_arg(args, &argcount, "%s", ace);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("pool update-acl", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0) {
		DL_ERROR(rc, "dmg failed");
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
dmg_pool_delete_ace(const char *dmg_config_file, const uuid_t uuid, const char *grp,
		    const char *principal)
{
	char                uuid_str[DAOS_UUID_STR_SIZE];
	int                 argcount = 0;
	char              **args     = NULL;
	struct json_object *dmg_out  = NULL;
	int                 rc       = 0;

	uuid_unparse_lower(uuid, uuid_str);
	args = cmd_push_arg(args, &argcount, "%s ", uuid_str);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	args = cmd_push_arg(args, &argcount, "%s", "--principal=");
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	args = cmd_push_arg(args, &argcount, "%s", principal);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("pool delete-acl", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0) {
		DL_ERROR(rc, "dmg failed");
		goto out_json;
	}

out_json:
	if (dmg_out != NULL)
		json_object_put(dmg_out);
	cmd_free_args(args, argcount);
out:
	return rc;
}

static int
dmg_pool_target(const char *cmd, const char *dmg_config_file, const uuid_t uuid,
		const char *grp, d_rank_t rank, int tgt_idx)
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	uuid_unparse_lower(uuid, uuid_str);
	args = cmd_push_arg(args, &argcount, "%s ", uuid_str);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (grp != NULL) {
		args = cmd_push_arg(args, &argcount, "--sys=%s ", grp);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (tgt_idx >= 0) {
		args = cmd_push_arg(args, &argcount, "--target-idx=%d ", tgt_idx);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	args = cmd_push_arg(args, &argcount, "--rank=%d ", rank);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe(cmd, dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0) {
		D_ERROR("dmg failed\n");
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
dmg_pool_exclude(const char *dmg_config_file, const uuid_t uuid,
		 const char *grp, d_rank_t rank, int tgt_idx)
{
	return dmg_pool_target("pool exclude", dmg_config_file, uuid, grp, rank, tgt_idx);
}

int
dmg_pool_reintegrate(const char *dmg_config_file, const uuid_t uuid,
		     const char *grp, d_rank_t rank, int tgt_idx)
{
	return dmg_pool_target("pool reintegrate", dmg_config_file, uuid, grp, rank, tgt_idx);
}

int
dmg_pool_drain(const char *dmg_config_file, const uuid_t uuid,
	       const char *grp, d_rank_t rank, int tgt_idx)
{
	return dmg_pool_target("pool drain", dmg_config_file, uuid, grp, rank, tgt_idx);
}

int
dmg_pool_extend(const char *dmg_config_file, const uuid_t uuid,
		const char *grp, d_rank_t *ranks, int rank_nr)
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	d_rank_list_t		rank_list = { 0 };
	char			*rank_str = NULL;
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	rank_list.rl_ranks = ranks;
	rank_list.rl_nr = rank_nr;

	rank_str = d_rank_list_to_str(&rank_list);
	if (rank_str == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_unparse_lower(uuid, uuid_str);
	args = cmd_push_arg(args, &argcount, "%s ", uuid_str);
	if (args == NULL)
		D_GOTO(out_rankstr, rc = -DER_NOMEM);

	if (grp != NULL) {
		args = cmd_push_arg(args, &argcount, "--sys=%s ", grp);
		if (args == NULL)
			D_GOTO(out_rankstr, rc = -DER_NOMEM);
	}

	args = cmd_push_arg(args, &argcount, "--ranks=%s ", rank_str);
	if (args == NULL)
		D_GOTO(out_rankstr, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("pool extend", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0) {
		D_ERROR("dmg failed\n");
		goto out_json;
	}

out_json:
	if (dmg_out != NULL)
		json_object_put(dmg_out);
	cmd_free_args(args, argcount);
out_rankstr:
	D_FREE(rank_str);
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
		D_ERROR("dmg failed\n");
		goto out_json;
	}

	json_object_object_get_ex(dmg_out, "pools", &pool_list);
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
	struct json_object      *ctrlr  = NULL;
	struct json_object	*target = NULL;
	struct json_object	*targets;
	int			tgts_len;
	int			i, j;
	int			rc;
	char			*tmp_var;
	char			*saved_ptr;

	for (i = 0; i < dev_length; i++) {
		dev = json_object_array_get_idx(smd_dev, i);

		tmp_var =  strtok_r(host, ":", &saved_ptr);
		if (tmp_var == NULL) {
			D_ERROR("Hostname is empty\n");
			return -DER_INVAL;
		}

		snprintf(devices[*disks].host, sizeof(devices[*disks].host),
			 "%s", tmp_var + 1);

		if (!json_object_object_get_ex(dev, "uuid", &tmp)) {
			D_ERROR("unable to extract uuid from JSON\n");
			return -DER_INVAL;
		}

		rc = uuid_parse(json_object_get_string(tmp), devices[*disks].device_id);
		if (rc != 0) {
			D_ERROR("failed parsing uuid_str\n");
			return -DER_INVAL;
		}

		if (!json_object_object_get_ex(dev, "tgt_ids",
					       &targets)) {
			D_ERROR("unable to extract tgtids from JSON\n");
			return -DER_INVAL;
		}

		if (targets != NULL)
			tgts_len = json_object_array_length(targets);
		else
			tgts_len = 0;

		for (j = 0; j < tgts_len; j++) {
			target = json_object_array_get_idx(targets, j);
			devices[*disks].tgtidx[j] = atoi(
				json_object_to_json_string(target));
		}
		devices[*disks].n_tgtidx = tgts_len;

		if (!json_object_object_get_ex(dev, "rank", &tmp)) {
			D_ERROR("unable to extract rank from JSON\n");
			return -DER_INVAL;
		}
		devices[*disks].rank = atoi(json_object_to_json_string(tmp));

		if (!json_object_object_get_ex(dev, "ctrlr", &ctrlr)) {
			D_ERROR("unable to extract ctrlr obj from JSON\n");
			return -DER_INVAL;
		}

		if (!json_object_object_get_ex(ctrlr, "dev_state", &tmp)) {
			D_ERROR("unable to extract state from JSON\n");
			return -DER_INVAL;
		}

		snprintf(devices[*disks].state, sizeof(devices[*disks].state), "%s",
			 json_object_to_json_string(tmp));
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
	char		*host;
	int			dev_length = 0;
	int			rc = 0;
	int			*disk;

	if (ndisks != NULL)
		*ndisks = 0;

	D_ALLOC_PTR(disk);
	rc = daos_dmg_json_pipe("storage query list-devices", dmg_config_file,
				NULL, 0, &dmg_out);
	if (rc != 0) {
		D_FREE(disk);
		D_ERROR("dmg failed\n");
		goto out_json;
	}

	if (!json_object_object_get_ex(dmg_out, "host_storage_map",
				       &storage_map)) {
		D_ERROR("unable to extract host_storage_map from JSON\n");
		D_GOTO(out, rc = -DER_INVAL);
	}

	json_object_object_foreach(storage_map, key, val) {
		D_DEBUG(DB_TEST, "key:\"%s\",val=%s\n", key,
			json_object_to_json_string(val));

		if (!json_object_object_get_ex(val, "hosts", &hosts)) {
			D_ERROR("unable to extract hosts from JSON\n");
			D_GOTO(out, rc = -DER_INVAL);
		}

		D_ALLOC(host, strlen(json_object_to_json_string(hosts)) + 1);
		strcpy(host, json_object_to_json_string(hosts));

		json_object_object_foreach(val, key1, val1) {
			D_DEBUG(DB_TEST, "key1:\"%s\",val1=%s\n", key1,
				json_object_to_json_string(val1));

			json_object_object_get_ex(val1, "smd_info", &smd_info);
			if (smd_info != NULL) {
				if (!json_object_object_get_ex(
					smd_info, "devices", &smd_dev)) {
					D_ERROR("unable to extract devices\n");
					D_FREE(host);
					D_GOTO(out, rc = -DER_INVAL);
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
					if (rc != 0) {
						D_FREE(host);
						goto out_json;
					}
				}
			}
		}
		D_FREE(host);
	}

out_json:
	if (dmg_out != NULL)
		json_object_put(dmg_out);

out:
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
		D_ERROR("dmg command failed\n");
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
dmg_storage_query_device_health(const char *dmg_config_file, char *host,
				char *stats, const uuid_t uuid)
{
	struct json_object	*dmg_out = NULL;
	struct json_object	*storage_map = NULL;
	struct json_object	*smd_info = NULL;
	struct json_object	*storage_info = NULL;
	struct json_object       *health_stats = NULL;
	struct json_object	*devices = NULL;
	struct json_object	*dev_info = NULL;
	struct json_object       *ctrlr_info   = NULL;
	struct json_object	*tmp = NULL;
	char			uuid_str[DAOS_UUID_STR_SIZE];
	int			argcount = 0;
	char			**args = NULL;
	int			rc = 0;

	uuid_unparse_lower(uuid, uuid_str);
	args = cmd_push_arg(args, &argcount, " --uuid=%s ", uuid_str);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	args = cmd_push_arg(args, &argcount, " --host-list=%s ", host);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("storage query device-health ", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0) {
		D_ERROR("dmg command failed\n");
		goto out_json;
	}
	if (!json_object_object_get_ex(dmg_out, "host_storage_map",
				       &storage_map)) {
		D_ERROR("unable to extract host_storage_map from JSON\n");
		D_GOTO(out_json, rc = -DER_INVAL);
	}

	json_object_object_foreach(storage_map, key, val) {
		D_DEBUG(DB_TEST, "key:\"%s\",val=%s\n", key,
			json_object_to_json_string(val));

		if (!json_object_object_get_ex(val, "storage", &storage_info)) {
			D_ERROR("unable to extract storage info from JSON\n");
			D_GOTO(out_json, rc = -DER_INVAL);
		}
		if (!json_object_object_get_ex(storage_info, "smd_info",
					       &smd_info)) {
			D_ERROR("unable to extract smd_info from JSON\n");
			D_GOTO(out_json, rc = -DER_INVAL);
		}
		if (!json_object_object_get_ex(smd_info, "devices", &devices)) {
			D_ERROR("unable to extract devices list from JSON\n");
			D_GOTO(out_json, rc = -DER_INVAL);
		}

		dev_info = json_object_array_get_idx(devices, 0);
		if (!json_object_object_get_ex(dev_info, "ctrlr", &ctrlr_info)) {
			D_ERROR("unable to extract ctrlr details from JSON\n");
			D_GOTO(out_json, rc = -DER_INVAL);
		}
		json_object_object_get_ex(ctrlr_info, "health_stats", &health_stats);
		if (health_stats != NULL) {
			json_object_object_get_ex(health_stats, stats, &tmp);
			strcpy(stats, json_object_to_json_string(tmp));
		}
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

int dmg_system_stop_rank(const char *dmg_config_file, d_rank_t rank, int force)
{
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	if (rank != CRT_NO_RANK) {
		args = cmd_push_arg(args, &argcount, " -r %d ", rank);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (force != 0) {
		args = cmd_push_arg(args, &argcount, " --force ");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = daos_dmg_json_pipe("system stop", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg failed\n");

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);
out:
	return rc;
}

int dmg_system_start_rank(const char *dmg_config_file, d_rank_t rank)
{
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	if (rank != CRT_NO_RANK) {
		args = cmd_push_arg(args, &argcount, " -r %d ", rank);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = daos_dmg_json_pipe("system start", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg failed\n");

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);
out:
	return rc;
}

int dmg_system_reint_rank(const char *dmg_config_file, d_rank_t rank)
{
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	if (rank == CRT_NO_RANK)
		D_GOTO(out, rc = -DER_INVAL);

	args = cmd_push_arg(args, &argcount, " -r %d ", rank);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("system clear-exclude", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg system clear-exclude failed\n");

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);

out:
	return rc;
}

int dmg_system_exclude_rank(const char *dmg_config_file, d_rank_t rank)
{
	int			argcount = 0;
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			rc = 0;

	if (rank == CRT_NO_RANK)
		D_GOTO(out, rc = -DER_INVAL);

	args = cmd_push_arg(args, &argcount, " -r %d ", rank);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("system exclude", dmg_config_file,
				args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg system exclude failed\n");

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);

out:
	return rc;
}

int
dmg_server_set_logmasks(const char *dmg_config_file, const char *masks, const char *streams,
			const char *subsystems)
{
	int                 argcount = 0;
	char              **args     = NULL;
	struct json_object *dmg_out  = NULL;
	int                 rc       = 0;

	/* engine log_mask */
	if (masks != NULL) {
		args = cmd_push_arg(args, &argcount, " --masks=%s", masks);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	/* DD_MASK environment variable (aka streams) */
	if (streams != NULL) {
		args = cmd_push_arg(args, &argcount, " --streams=%s", streams);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	/* DD_SUBSYS environment variable */
	if (subsystems != NULL) {
		args = cmd_push_arg(args, &argcount, " --subsystems=%s", subsystems);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	/* If none of masks, streams, subsystems are specified, restore original engine config */
	rc = daos_dmg_json_pipe("server set-logmasks", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg failed\n");

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);
out:
	return rc;
}

const char *
daos_target_state_enum_to_str(int state)
{
	switch (state) {
	case DAOS_TS_UNKNOWN: return "UNKNOWN";
	case DAOS_TS_DOWN_OUT: return "DOWNOUT";
	case DAOS_TS_DOWN: return "DOWN";
	case DAOS_TS_UP: return "UP";
	case DAOS_TS_UP_IN: return "UPIN";
	case DAOS_TS_DRAIN: return "DRAIN";
	}

	return "Undefined State";
}

int
dmg_fault_inject(const char *dmg_config_file, uuid_t uuid, bool mgmt, const char *fault)
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			argcount = 0;
	int			rc = 0;

	if (mgmt)
		args = cmd_push_arg(args, &argcount, " mgmt-svc pool");
	else
		args = cmd_push_arg(args, &argcount, " pool-svc");
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	uuid_unparse_lower(uuid, uuid_str);
	args = cmd_push_arg(args, &argcount, " %s", uuid_str);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	args = cmd_push_arg(args, &argcount, " %s", fault);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("faults", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg %s fault injection for " DF_UUID " with %s got failure: %d\n",
			mgmt ? "mgmt" : "pool", DP_UUID(uuid), fault, rc);

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);

out:
	return rc;
}

int
dmg_check_switch(const char *dmg_config_file, bool enable)
{
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			argcount = 0;
	int			rc = 0;

	if (enable)
		args = cmd_push_arg(args, &argcount, " enable");
	else
		args = cmd_push_arg(args, &argcount, " disable");
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	rc = daos_dmg_json_pipe("check", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg check switch to %s failed: %d\n", enable ? "enable" : "disable", rc);

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);

out:
	return rc;
}

int
dmg_check_start(const char *dmg_config_file, uint32_t flags, uint32_t pool_nr, uuid_t uuids[],
		const char *policies)
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			argcount = 0;
	int			rc = 0;
	int			i;

	if (flags & TCSF_DRYRUN) {
		args = cmd_push_arg(args, &argcount, " -n");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (flags & TCSF_RESET) {
		args = cmd_push_arg(args, &argcount, " -r");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (flags & TCSF_FAILOUT) {
		args = cmd_push_arg(args, &argcount, " --failout=on");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (flags & TCSF_AUTO) {
		args = cmd_push_arg(args, &argcount, " --auto=on");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (flags & TCSF_ORPHAN) {
		args = cmd_push_arg(args, &argcount, " -O");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (flags & TCSF_NO_FAILOUT) {
		args = cmd_push_arg(args, &argcount, " --failout=off");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (flags & TCSF_NO_AUTO) {
		args = cmd_push_arg(args, &argcount, " --auto=off");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (policies != NULL) {
		args = cmd_push_arg(args, &argcount, " --policies=%s", policies);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	for (i = 0; i < pool_nr; i++) {
		uuid_unparse_lower(uuids[i], uuid_str);
		args = cmd_push_arg(args, &argcount, " %s", uuid_str);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = daos_dmg_json_pipe("check start", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg check start with flags %x, policies %s failed: %d\n", flags,
			policies != NULL ? policies : "(null)", rc);

	if (dmg_out != NULL)
		json_object_put(dmg_out);

out:
	cmd_free_args(args, argcount);

	return rc;
}

int
dmg_check_stop(const char *dmg_config_file, uint32_t pool_nr, uuid_t uuids[])
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			argcount = 0;
	int			rc = 0;
	int			i;

	for (i = 0; i < pool_nr; i++) {
		uuid_unparse_lower(uuids[i], uuid_str);
		args = cmd_push_arg(args, &argcount, " %s", uuid_str);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = daos_dmg_json_pipe("check stop", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg check stop failed: %d\n", rc);

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);

out:
	return rc;
}

static int
check_query_reports_cmp(const void *p1, const void *p2)
{
	const struct daos_check_report_info	*dcri1 = p1;
	const struct daos_check_report_info	*dcri2 = p2;

	if (dcri1->dcri_class > dcri2->dcri_class)
		return 1;

	if (dcri1->dcri_class < dcri2->dcri_class)
		return -1;

	return 0;
}

static int
parse_check_query_pool(struct json_object *obj, uuid_t uuid, struct daos_check_info *dci)
{
	struct daos_check_pool_info	*dcpi;
	struct json_object		*pool;
	char				 uuid_str[DAOS_UUID_STR_SIZE];
	int				 rc;

	uuid_unparse_lower(uuid, uuid_str);

	/* The queried pool may not exist. */
	if (!json_object_object_get_ex(obj, uuid_str, &pool)) {
		D_WARN("Do not find the pool %s in check query result, may not exist\n", uuid_str);
		return 0;
	}

	dcpi = &dci->dci_pools[dci->dci_pool_nr];

	rc = parse_dmg_uuid(pool, "uuid", dcpi->dcpi_uuid);
	if (rc != 0)
		return rc;

	rc = parse_dmg_string(pool, "status", &dcpi->dcpi_status);
	if (rc != 0)
		return rc;

	rc = parse_dmg_string(pool, "phase", &dcpi->dcpi_phase);
	if (rc == 0)
		dci->dci_pool_nr++;

	return rc;
}

static int
parse_check_query_report(struct json_object *obj, struct daos_check_report_info *dcri)
{
	struct json_object	*tmp;
	int			 rc;
	int			 i;

	rc = parse_dmg_uuid(obj, "pool_uuid", dcri->dcri_uuid);
	if (rc != 0)
		return rc;

	if (!json_object_object_get_ex(obj, "seq", &tmp)) {
		D_ERROR("Unable to extract seq for pool " DF_UUID " from check query result\n",
			DP_UUID(dcri->dcri_uuid));
		return -DER_INVAL;
	}

	dcri->dcri_seq = json_object_get_int64(tmp);

	if (!json_object_object_get_ex(obj, "class", &tmp)) {
		D_ERROR("Unable to extract class for pool " DF_UUID " from check query result\n",
			DP_UUID(dcri->dcri_uuid));
		return -DER_INVAL;
	}

	dcri->dcri_class = json_object_get_int(tmp);

	if (!json_object_object_get_ex(obj, "action", &tmp)) {
		D_ERROR("Unable to extract action for pool " DF_UUID " from check query result\n",
			DP_UUID(dcri->dcri_uuid));
		return -DER_INVAL;
	}

	dcri->dcri_act = json_object_get_int(tmp);

	if (!json_object_object_get_ex(obj, "result", &tmp))
		dcri->dcri_result = 0;
	else
		dcri->dcri_result = json_object_get_int(tmp);

	/* Not interaction. */
	if (!json_object_object_get_ex(obj, "act_choices", &tmp))
		return 0;

	dcri->dcri_option_nr = json_object_array_length(tmp);
	D_ASSERTF(dcri->dcri_option_nr > 0,
		  "Invalid options count for pool " DF_UUID " in check query result: %d\n",
		  DP_UUID(dcri->dcri_uuid), dcri->dcri_option_nr);

	for (i = 0; i < dcri->dcri_option_nr; i++)
		dcri->dcri_options[i] = json_object_get_int(json_object_array_get_idx(tmp, i));

	return 0;
}

static int
parse_check_query_info(struct json_object *query_output, uint32_t pool_nr, uuid_t uuids[],
		       struct daos_check_info *dci)
{
	struct json_object	*obj;
	int			 i;
	int			 rc;

	rc = parse_dmg_string(query_output, "status", &dci->dci_status);
	if (rc != 0)
		return rc;

	rc = parse_dmg_string(query_output, "scan_phase", &dci->dci_phase);
	if (rc != 0)
		return rc;

	dci->dci_pool_nr = 0;

	if (pool_nr <= 0)
		goto reports;

	if (!json_object_object_get_ex(query_output, "pools", &obj)) {
		D_ERROR("Unable to extract pools from check query result\n");
		return -DER_INVAL;
	}

	if (json_object_is_type(obj, json_type_null))
		goto reports;

	D_ALLOC_ARRAY(dci->dci_pools, pool_nr);
	if (dci->dci_pools == NULL) {
		D_ERROR("Failed to allocate pools (len %d) for check query result\n", pool_nr);
		return -DER_NOMEM;
	}

	for (i = 0; i < pool_nr; i++) {
		rc = parse_check_query_pool(obj, uuids[i], dci);
		if (rc != 0)
			return rc;
	}

reports:
	if (!json_object_object_get_ex(query_output, "reports", &obj)) {
		D_ERROR("Unable to extract reports from check query result\n");
		return -DER_INVAL;
	}

	if (json_object_is_type(obj, json_type_null)) {
		dci->dci_report_nr = 0;
		return 0;
	}

	dci->dci_report_nr = json_object_array_length(obj);
	D_ASSERTF(dci->dci_report_nr > 0,
		  "Invalid reports count pool in check query result: %d\n", dci->dci_report_nr);

	D_ALLOC_ARRAY(dci->dci_reports, dci->dci_report_nr);
	if (dci->dci_reports == NULL) {
		D_ERROR("Failed to allocate reports (len %d) for check query result\n",
			dci->dci_report_nr);
		return -DER_NOMEM;
	}

	for (i = 0; i < dci->dci_report_nr; i++) {
		rc = parse_check_query_report(json_object_array_get_idx(obj, i),
					      &dci->dci_reports[i]);
		if (rc != 0)
			return rc;
	}

	/* Sort the inconsistency reports for easy verification. */
	if (dci->dci_report_nr > 1)
		qsort(dci->dci_reports, dci->dci_report_nr, sizeof(dci->dci_reports[0]),
		      check_query_reports_cmp);

	return 0;
}

int
dmg_check_query(const char *dmg_config_file, uint32_t pool_nr, uuid_t uuids[],
		struct daos_check_info *dci)
{
	char			uuid_str[DAOS_UUID_STR_SIZE];
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			argcount = 0;
	int			rc = 0;
	int			i;

	for (i = 0; i < pool_nr; i++) {
		uuid_unparse_lower(uuids[i], uuid_str);
		args = cmd_push_arg(args, &argcount, " %s", uuid_str);
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = daos_dmg_json_pipe("check query", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg check query failed: %d\n", rc);
	else
		rc = parse_check_query_info(dmg_out, pool_nr, uuids, dci);

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);

out:
	return rc;
}

int
dmg_check_repair(const char *dmg_config_file, uint64_t seq, uint32_t opt, bool for_all)
{
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			argcount = 0;
	int			rc = 0;

	args = cmd_push_arg(args, &argcount, " %Lu %u", seq, opt);
	if (args == NULL)
		D_GOTO(out, rc = -DER_NOMEM);

	if (for_all) {
		args = cmd_push_arg(args, &argcount, " -f");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = daos_dmg_json_pipe("check repair", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg check repair with seq %lu, opt %u, for_all %s, failed: %d\n",
			(unsigned long)seq, opt, for_all ? "yes" : "no", rc);

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);

out:
	return rc;
}
int
dmg_check_set_policy(const char *dmg_config_file, uint32_t flags, const char *policies)
{
	char			**args = NULL;
	struct json_object	*dmg_out = NULL;
	int			argcount = 0;
	int			rc = 0;

	if (flags & TCPF_RESET) {
		args = cmd_push_arg(args, &argcount, " -d");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	if (flags & TCPF_INTERACT) {
		args = cmd_push_arg(args, &argcount, " -a");
		if (args == NULL)
			D_GOTO(out, rc = -DER_NOMEM);
	}

	rc = daos_dmg_json_pipe("check set-policy", dmg_config_file, args, argcount, &dmg_out);
	if (rc != 0)
		D_ERROR("dmg check set-policy with flags %x, policies %s failed: %d\n", flags,
			policies != NULL ? policies : "(null)", rc);

	if (dmg_out != NULL)
		json_object_put(dmg_out);

	cmd_free_args(args, argcount);

out:
	return rc;
}
