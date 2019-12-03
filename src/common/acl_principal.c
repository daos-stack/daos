/*
 * (C) Copyright 2019 Intel Corporation.
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
 * provided in Contract No. 8F-30005.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */
/*
 * daos_acl: This file contains functions related to working with the principals
 * from Access Control Lists.
 */
#include <daos/common.h>
#include <daos_security.h>
#include <gurt/common.h>
#include <pwd.h>
#include <grp.h>

#define DEFAULT_BUF_LEN 1024

/*
 * No platform-agnostic way to fetch the max buflen - so let's try a
 * sane value and double until it's big enough.
 *
 * Assumes _func is in the getpw*_r family of functions.
 */
#define TRY_UNTIL_BUF_SIZE_OK(_func, _arg1, _arg2, _buf, _result)	\
({									\
	int	__rc = 0;						\
	char	*new_buf = NULL;					\
	size_t	buflen = DEFAULT_BUF_LEN;				\
									\
	do {								\
		D_REALLOC(new_buf, _buf, buflen);			\
		if (new_buf == NULL) {					\
			__rc = -DER_NOMEM;				\
			break;						\
		}							\
		_buf = new_buf;						\
									\
		__rc = _func(_arg1, _arg2, _buf, buflen, _result);	\
									\
		buflen *= 2;						\
	} while (__rc == ERANGE);					\
	__rc;								\
})

/*
 * States used to parse a principal name
 */
enum validity_state {
	STATE_START,
	STATE_NAME,
	STATE_DOMAIN,
	STATE_DONE,
	STATE_INVALID
};

static enum validity_state
next_validity_state(enum validity_state current_state, char ch)
{
	switch (current_state) {
	case STATE_START:
		if (ch == '@')
			return STATE_INVALID;
		return STATE_NAME;
	case STATE_NAME:
		if (ch == '@')
			return STATE_DOMAIN;
		if (ch == '\0')
			return STATE_INVALID;
		break;
	case STATE_DOMAIN:
		if (ch == '\0')
			return STATE_DONE;
		if (ch == '@')
			return STATE_INVALID;
		break;
	case STATE_DONE:
	case STATE_INVALID:
		break;
	default:
		D_ASSERTF(false, "Invalid state: %d\n", current_state);
	}

	return current_state;
}

bool
daos_acl_principal_is_valid(const char *name)
{
	size_t			len;
	size_t			i;
	enum validity_state	state = STATE_START;

	if (name == NULL) {
		D_INFO("Name was NULL\n");
		return false;
	}

	len = strnlen(name, DAOS_ACL_MAX_PRINCIPAL_BUF_LEN);
	if (len == 0 || len > DAOS_ACL_MAX_PRINCIPAL_LEN) {
		D_INFO("Invalid len: %lu\n", len);
		return false;
	}

	for (i = 0; i < (len + 1); i++) {
		state = next_validity_state(state, name[i]);
		if (state == STATE_INVALID) {
			D_INFO("Name was badly formatted: %s\n", name);
			return false;
		}
	}

	return true;
}

static int
local_name_to_principal_name(const char *local_name, char **name)
{
	D_ASPRINTF(*name, "%s@", local_name);
	if (*name == NULL) {
		D_ERROR("Failed to allocate string for name\n");
		return -DER_NOMEM;
	}

	return 0;
}

int
daos_acl_uid_to_principal(uid_t uid, char **name)
{
	int		rc;
	struct passwd	user;
	struct passwd	*result = NULL;
	char		*buf = NULL;

	if (name == NULL) {
		D_INFO("name pointer was NULL!\n");
		return -DER_INVAL;
	}

	rc = TRY_UNTIL_BUF_SIZE_OK(getpwuid_r, uid, &user, buf, &result);
	if (rc == -DER_NOMEM)
		D_GOTO(out, rc);
	if (rc != 0) {
		D_ERROR("Error from getpwuid_r: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc = d_errno2der(rc));
	}

	if (result == NULL) {
		D_INFO("No user for uid %u\n", uid);
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	rc = local_name_to_principal_name(result->pw_name, name);

out:
	D_FREE(buf);
	return rc;
}

int
daos_acl_gid_to_principal(gid_t gid, char **name)
{
	int		rc;
	struct group	grp;
	struct group	*result = NULL;
	char		*buf = NULL;

	if (name == NULL) {
		D_INFO("name pointer was NULL!\n");
		return -DER_INVAL;
	}

	rc = TRY_UNTIL_BUF_SIZE_OK(getgrgid_r, gid, &grp, buf, &result);
	if (rc == -DER_NOMEM)
		D_GOTO(out, rc);
	if (rc != 0) {
		D_ERROR("Error from getgrgid_r: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc = d_errno2der(rc));
	}

	if (result == NULL) {
		D_INFO("No group for gid %u\n", gid);
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	rc = local_name_to_principal_name(result->gr_name, name);

out:
	D_FREE(buf);
	return rc;
}

/*
 * Extracts the user/group name from the "name@[domain]" formatted principal
 * string.
 */
static int
get_id_name_from_principal(const char *principal, char *name)
{
	int num_matches;

	if (!daos_acl_principal_is_valid(principal)) {
		D_INFO("Invalid name format\n");
		return -DER_INVAL;
	}

	num_matches = sscanf(principal, "%[^@]", name);
	if (num_matches == 0) {
		/*
		 * This is a surprise - if it's formatted properly, we should
		 * be able to extract the name.
		 */
		D_ERROR("Couldn't extract ID name from '%s'\n", principal);
		return -DER_INVAL;
	}

	return 0;
}

int
daos_acl_principal_to_uid(const char *principal, uid_t *uid)
{
	char		username[DAOS_ACL_MAX_PRINCIPAL_BUF_LEN];
	char		*buf = NULL;
	struct passwd	passwd;
	struct passwd	*result = NULL;
	int		rc;

	if (uid == NULL) {
		D_INFO("NULL uid pointer\n");
		return -DER_INVAL;
	}

	rc = get_id_name_from_principal(principal, username);
	if (rc != 0)
		return rc;

	rc = TRY_UNTIL_BUF_SIZE_OK(getpwnam_r, username, &passwd, buf, &result);
	if (rc == -DER_NOMEM)
		D_GOTO(out, rc);
	if (rc != 0) {
		D_ERROR("Error from getpwnam_r: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc = d_errno2der(rc));
	}

	if (result == NULL) {
		D_INFO("User '%s' not found\n", username);
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	*uid = result->pw_uid;
	rc = 0;
out:
	D_FREE(buf);
	return rc;
}

int
daos_acl_principal_to_gid(const char *principal, gid_t *gid)
{
	char		grpname[DAOS_ACL_MAX_PRINCIPAL_BUF_LEN];
	char		*buf = NULL;
	struct group	grp;
	struct group	*result = NULL;
	int		rc;

	if (gid == NULL) {
		D_INFO("NULL gid pointer\n");
		return -DER_INVAL;
	}

	rc = get_id_name_from_principal(principal, grpname);
	if (rc != 0)
		return rc;

	rc = TRY_UNTIL_BUF_SIZE_OK(getgrnam_r, grpname, &grp, buf, &result);
	if (rc == -DER_NOMEM)
		D_GOTO(out, rc);
	if (rc != 0) {
		D_ERROR("Error from getgrnam_r: "DF_RC"\n", DP_RC(rc));
		D_GOTO(out, rc = d_errno2der(rc));
	}

	if (result == NULL) {
		D_INFO("Group '%s' not found\n", grpname);
		D_GOTO(out, rc = -DER_NONEXIST);
	}

	*gid = result->gr_gid;
	rc = 0;
out:
	D_FREE(buf);
	return rc;
}
