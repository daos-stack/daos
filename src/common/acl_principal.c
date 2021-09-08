/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/*
 * daos_acl: This file contains functions related to working with the principals
 * from Access Control Lists.
 */

#include <daos_security.h>
#include <gurt/common.h>
#include <pwd.h>
#include <grp.h>

#define DEFAULT_BUF_LEN	1024
#define USER_PREFIX	"u:"
#define USER_PREFIX_LEN	(sizeof(USER_PREFIX) - 1)
#define GRP_PREFIX	"g:"
#define GRP_PREFIX_LEN	(sizeof(GRP_PREFIX) - 1)

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
	size_t	oldlen = 0; /* default to clearing buffer */		\
									\
	do {								\
		D_REALLOC(new_buf, _buf, oldlen, buflen);		\
		if (new_buf == NULL) {					\
			__rc = -DER_NOMEM;				\
			break;						\
		}							\
		_buf = new_buf;						\
									\
		__rc = _func(_arg1, _arg2, _buf, buflen, _result);	\
									\
		oldlen = buflen;					\
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
	if (*name == NULL)
		return -DER_NOMEM;

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
		D_ERROR("Error from getpwuid_r: %d\n", rc);
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
		D_ERROR("Error from getgrgid_r: %d\n", rc);
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
		D_ERROR("Error from getpwnam_r: %d\n", rc);
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
		D_ERROR("Error from getgrnam_r: %d\n", rc);
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

static bool
is_special_type(enum daos_acl_principal_type type)
{
	switch (type) {
	case DAOS_ACL_OWNER:
	case DAOS_ACL_OWNER_GROUP:
	case DAOS_ACL_EVERYONE:
		return true;
	case DAOS_ACL_USER:
	case DAOS_ACL_GROUP:
	default:
		break;
	}

	return false;
}

static int
get_principal_type_from_str(const char *principal_str,
			    enum daos_acl_principal_type *type)
{
	/*
	 * Named user or group will be designated by prefix
	 */
	if (strncmp(principal_str, USER_PREFIX, USER_PREFIX_LEN) == 0) {
		*type = DAOS_ACL_USER;
		return 0;
	}

	if (strncmp(principal_str, GRP_PREFIX, GRP_PREFIX_LEN) == 0) {
		*type = DAOS_ACL_GROUP;
		return 0;
	}

	if (strncmp(principal_str, DAOS_ACL_PRINCIPAL_EVERYONE,
		    DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0) {
		*type = DAOS_ACL_EVERYONE;
		return 0;
	}

	if (strncmp(principal_str, DAOS_ACL_PRINCIPAL_OWNER,
		    DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0) {
		*type = DAOS_ACL_OWNER;
		return 0;
	}

	if (strncmp(principal_str, DAOS_ACL_PRINCIPAL_OWNER_GRP,
		    DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0) {
		*type = DAOS_ACL_OWNER_GROUP;
		return 0;
	}

	return -DER_INVAL;
}

static const char *
get_start_of_name(const char *principal_str, enum daos_acl_principal_type type)
{
	size_t idx;

	if (type == DAOS_ACL_USER)
		idx = USER_PREFIX_LEN;
	else
		idx = GRP_PREFIX_LEN;

	return &principal_str[idx];
}

int
daos_acl_principal_from_str(const char *principal_str,
			    enum daos_acl_principal_type *type,
			    char **name)
{
	int		rc;
	const char	*p_name;

	if (principal_str == NULL || type == NULL || name == NULL) {
		D_INFO("Null input: principal_str=%p, type=%p, name=%p\n",
		       principal_str, type, name);
		return -DER_INVAL;
	}

	rc = get_principal_type_from_str(principal_str, type);
	if (rc != 0) {
		D_INFO("Badly-formatted principal string\n");
		return rc;
	}

	/*
	 * Nothing else to do for special types.
	 */
	if (is_special_type(*type)) {
		*name = NULL;
		return 0;
	}

	/*
	 * Make sure the name is something sane before we go through the
	 * trouble of allocating it.
	 */
	p_name = get_start_of_name(principal_str, *type);
	if (!daos_acl_principal_is_valid(p_name)) {
		D_INFO("Invalid principal name\n");
		return -DER_INVAL;
	}

	D_STRNDUP(*name, p_name, DAOS_ACL_MAX_PRINCIPAL_LEN);
	if (*name == NULL)
		return -DER_NOMEM;

	return 0;
}
