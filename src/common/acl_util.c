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

/**
 * Utility functions that may be used when interacting with Access Control
 * Lists
 */

#include <daos_security.h>
#include <gurt/common.h>
#include <gurt/debug.h>
#include <pwd.h>
#include <grp.h>

/*
 * String values for special principal types
 */
#define PRINCIPAL_OWNER_STR	"OWNER@"
#define PRINCIPAL_OWNER_GRP_STR	"GROUP@"
#define PRINCIPAL_EVERYONE_STR	"EVERYONE@"

/*
 * Characters representing access flags
 */
#define FLAG_GROUP_CH		'G'
#define FLAG_SUCCESS_CH		'S'
#define FLAG_FAIL_CH		'F'
#define FLAG_POOL_INHERIT_CH	'P'

/*
 * Characters representing access types
 */
#define ACCESS_ALLOW_CH		'A'
#define ACCESS_AUDIT_CH		'U'
#define ACCESS_ALARM_CH		'L'

/*
 * Characters representing permissions
 */
#define PERM_READ_CH		'r'
#define PERM_WRITE_CH		'w'

static size_t DEFAULT_BUF_LEN = 1024;

/*
 * States used to parse a formatted ACE string
 */
enum ace_str_state {
	ACE_ACCESS_TYPES,
	ACE_FLAGS,
	ACE_IDENTITY,
	ACE_PERMS,
	ACE_DONE,
	ACE_INVALID
};

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
	char		*new_buf = NULL;
	size_t		buflen = DEFAULT_BUF_LEN;

	if (name == NULL) {
		D_INFO("name pointer was NULL!\n");
		return -DER_INVAL;
	}

	/*
	 * No platform-agnostic way to fetch the max buflen - so let's try a
	 * sane value and double until it's big enough.
	 */
	do {
		D_REALLOC(new_buf, buf, buflen);
		if (new_buf == NULL) {
			D_ERROR("Couldn't allocate memory for getpwuid_r\n");
			return -DER_NOMEM;
		}

		buf = new_buf;

		rc = getpwuid_r(uid, &user, buf, buflen, &result);

		buflen *= 2;
	} while (rc == ERANGE);

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
	char		*new_buf = NULL;
	size_t		buflen = DEFAULT_BUF_LEN;

	if (name == NULL) {
		D_INFO("name pointer was NULL!\n");
		return -DER_INVAL;
	}

	/*
	 * No platform-agnostic way to fetch the max buflen - so let's try a
	 * sane value and double until it's big enough.
	 */
	do {
		D_REALLOC(new_buf, buf, buflen);
		if (new_buf == NULL) {
			D_ERROR("Couldn't allocate memory for getgrgid_r\n");
			return -DER_NOMEM;
		}

		buf = new_buf;

		rc = getgrgid_r(gid, &grp, buf, buflen, &result);

		buflen *= 2;
	} while (rc == ERANGE);

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
	char		*new_buf = NULL;
	size_t		buflen = DEFAULT_BUF_LEN;
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

	do {
		D_REALLOC(new_buf, buf, buflen);
		if (new_buf == NULL) {
			D_ERROR("Couldn't alloc buffer for getpwnam_r\n");
			return -DER_NOMEM;
		}

		buf = new_buf;

		rc = getpwnam_r(username, &passwd, buf, buflen, &result);

		buflen *= 2;
	} while (rc == ERANGE);

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
	char		*new_buf = NULL;
	size_t		buflen = DEFAULT_BUF_LEN;
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

	do {
		D_REALLOC(new_buf, buf, buflen);
		if (new_buf == NULL) {
			D_ERROR("Couldn't alloc buffer for getgrnam_r\n");
			return -DER_NOMEM;
		}

		buf = new_buf;

		rc = getgrnam_r(grpname, &grp, buf, buflen, &result);

		buflen *= 2;
	} while (rc == ERANGE);

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

static enum ace_str_state
process_access_types(const char *str, uint8_t *access_types)
{
	size_t len;
	size_t i;

	len = strnlen(str, DAOS_ACL_MAX_ACE_STR_LEN);
	for (i = 0; i < len; i++) {
		switch (str[i]) {
		case ACCESS_ALLOW_CH:
			*access_types |= DAOS_ACL_ACCESS_ALLOW;
			break;
		case ACCESS_AUDIT_CH:
			*access_types |= DAOS_ACL_ACCESS_AUDIT;
			break;
		case ACCESS_ALARM_CH:
			*access_types |= DAOS_ACL_ACCESS_ALARM;
			break;
		default:
			D_INFO("Invalid access type '%c'\n", str[i]);
			return ACE_INVALID;
		}
	}

	return ACE_FLAGS;
}

static enum ace_str_state
process_flags(const char *str, uint16_t *flags)
{
	size_t len;
	size_t i;

	len = strnlen(str, DAOS_ACL_MAX_ACE_STR_LEN);
	for (i = 0; i < len; i++) {
		switch (str[i]) {
		case FLAG_GROUP_CH:
			*flags |= DAOS_ACL_FLAG_GROUP;
			break;
		case FLAG_SUCCESS_CH:
			*flags |= DAOS_ACL_FLAG_ACCESS_SUCCESS;
			break;
		case FLAG_FAIL_CH:
			*flags |= DAOS_ACL_FLAG_ACCESS_FAIL;
			break;
		case FLAG_POOL_INHERIT_CH:
			*flags |= DAOS_ACL_FLAG_POOL_INHERIT;
			break;
		default:
			D_INFO("Invalid flag '%c'\n", str[i]);
			return ACE_INVALID;
		}
	}

	return ACE_IDENTITY;
}

static enum ace_str_state
process_perms(const char *str, uint64_t *perms)
{
	size_t len;
	size_t i;

	len = strnlen(str, DAOS_ACL_MAX_ACE_STR_LEN);
	for (i = 0; i < len; i++) {
		switch (str[i]) {
		case PERM_READ_CH:
			*perms |= DAOS_ACL_PERM_READ;
			break;
		case PERM_WRITE_CH:
			*perms |= DAOS_ACL_PERM_WRITE;
			break;
		default:
			D_INFO("Invalid permission '%c'\n", str[i]);
			return ACE_INVALID;
		}
	}

	return ACE_DONE;
}

static struct daos_ace *
get_ace_from_identity(const char *identity, uint16_t flags)
{
	enum daos_acl_principal_type type;

	if (strncmp(identity, PRINCIPAL_OWNER_STR,
		    DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0)
		type = DAOS_ACL_OWNER;
	else if (strncmp(identity, PRINCIPAL_OWNER_GRP_STR,
			 DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0)
		type = DAOS_ACL_OWNER_GROUP;
	else if (strncmp(identity, PRINCIPAL_EVERYONE_STR,
			 DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0)
		type = DAOS_ACL_EVERYONE;
	else if (flags & DAOS_ACL_FLAG_GROUP)
		type = DAOS_ACL_GROUP;
	else
		type = DAOS_ACL_USER;

	return daos_ace_create(type, identity);
}

/*
 * This helper function modifies the input string during processing.
 */
static int
create_ace_from_mutable_str(char *str, struct daos_ace **ace)
{
	struct daos_ace			*new_ace = NULL;
	char				*pch;
	char				*field;
	char				delimiter[] = ":";
	enum ace_str_state		state = ACE_ACCESS_TYPES;
	uint16_t			flags = 0;
	uint8_t				access_types = 0;
	uint64_t			perms = 0;
	int				rc = 0;

	pch = strpbrk(str, delimiter);
	field = str;
	while (state != ACE_INVALID) {
		/*
		 * We need to do one round with pch == NULL to pick up the last
		 * field in the string.
		 */
		if (pch != NULL)
			*pch = '\0';

		switch (state) {
		case ACE_ACCESS_TYPES:
			state = process_access_types(field, &access_types);
			break;
		case ACE_FLAGS:
			state = process_flags(field, &flags);
			break;
		case ACE_IDENTITY:
			new_ace = get_ace_from_identity(field, flags);
			if (new_ace == NULL) {
				D_ERROR("Couldn't alloc ACE structure\n");
				D_GOTO(error, rc = -DER_NOMEM);
			}
			state = ACE_PERMS;
			break;
		case ACE_PERMS:
			state = process_perms(field, &perms);
			break;
		case ACE_DONE:
		default:
			D_INFO("Bad state: %u\n", state);
			state = ACE_INVALID;
		}

		if (pch == NULL)
			break;
		field = pch + 1;
		pch = strpbrk(field, delimiter);
	}

	if (state != ACE_DONE) {
		D_INFO("Invalid ACE string\n");
		D_GOTO(error, rc = -DER_INVAL);
	}

	new_ace->dae_access_flags = flags;
	new_ace->dae_access_types = access_types;

	if (access_types & DAOS_ACL_ACCESS_ALLOW)
		new_ace->dae_allow_perms = perms;
	if (access_types & DAOS_ACL_ACCESS_AUDIT)
		new_ace->dae_audit_perms = perms;
	if (access_types & DAOS_ACL_ACCESS_ALARM)
		new_ace->dae_alarm_perms = perms;

	*ace = new_ace;

	return 0;

error:
	daos_ace_free(new_ace);
	return rc;
}

int
daos_ace_from_str(const char *str, struct daos_ace **ace)
{
	int		rc;
	size_t		len;
	char		*tmpstr;
	struct daos_ace	*new_ace = NULL;

	if (str == NULL || ace == NULL) {
		D_INFO("Invalid input ptr, str=%p, ace=%p\n", str, ace);
		return -DER_INVAL;
	}

	len = strnlen(str, DAOS_ACL_MAX_ACE_STR_LEN + 1);
	if (len > DAOS_ACL_MAX_ACE_STR_LEN) {
		D_INFO("Input string is too long\n");
		return -DER_INVAL;
	}

	/* Will be mangling the string during processing */
	D_STRNDUP(tmpstr, str, len);
	if (tmpstr == NULL) {
		D_ERROR("Couldn't allocate temporary string\n");
		return -DER_NOMEM;
	}

	rc = create_ace_from_mutable_str(tmpstr, &new_ace);
	D_FREE(tmpstr);
	if (rc != 0)
		return rc;

	if (!daos_ace_is_valid(new_ace)) {
		D_INFO("Finished building ACE but it's not valid\n");
		daos_ace_free(new_ace);
		return -DER_INVAL;
	}

	*ace = new_ace;

	return 0;
}

static const char *
get_principal_name_str(struct daos_ace *ace)
{
	switch (ace->dae_principal_type) {
	case DAOS_ACL_OWNER:
		return PRINCIPAL_OWNER_STR;
	case DAOS_ACL_OWNER_GROUP:
		return PRINCIPAL_OWNER_GRP_STR;
	case DAOS_ACL_EVERYONE:
		return PRINCIPAL_EVERYONE_STR;
	case DAOS_ACL_USER:
	case DAOS_ACL_GROUP:
	default:
		break;
	}

	return ace->dae_principal;
}

static int
write_char(char **pen, char ch, ssize_t *remaining_len)
{
	if (*remaining_len <= 1) /* leave a null termination char in buffer */
		return -DER_TRUNC;

	**pen = ch;
	(*remaining_len)--;
	(*pen)++;

	return 0;
}

static uint64_t
get_perms(struct daos_ace *ace)
{
	if (ace->dae_access_types & DAOS_ACL_ACCESS_ALLOW)
		return ace->dae_allow_perms;
	if (ace->dae_access_types & DAOS_ACL_ACCESS_AUDIT)
		return ace->dae_audit_perms;
	if (ace->dae_access_types & DAOS_ACL_ACCESS_ALARM)
		return ace->dae_alarm_perms;

	return 0;
}

static bool
perms_unified(struct daos_ace *ace)
{
	uint64_t perms_union;

	perms_union = ace->dae_allow_perms |
		      ace->dae_audit_perms |
		      ace->dae_alarm_perms;

	if ((ace->dae_access_types & DAOS_ACL_ACCESS_ALLOW) &&
	    (ace->dae_allow_perms != perms_union))
		return false;

	if ((ace->dae_access_types & DAOS_ACL_ACCESS_AUDIT) &&
	    (ace->dae_audit_perms != perms_union))
		return false;

	if ((ace->dae_access_types & DAOS_ACL_ACCESS_ALARM) &&
	    (ace->dae_alarm_perms != perms_union))
		return false;

	return true;
}

int
daos_ace_to_str(struct daos_ace *ace, char *buf, size_t buf_len)
{
	ssize_t		remaining_len = buf_len;
	char		*pen = buf;
	uint64_t	perms;
	ssize_t		written;
	int		rc = 0;

	if (ace == NULL || buf == NULL || buf_len == 0) {
		D_INFO("Invalid input, ace=%p, buf=%p, buf_len=%lu\n",
		       ace, buf, buf_len);
		return -DER_INVAL;
	}

	if (!daos_ace_is_valid(ace)) {
		D_INFO("ACE structure is not valid\n");
		return -DER_INVAL;
	}


	if (!perms_unified(ace)) {
		D_INFO("Can't create string for ACE with different perms for "
		       "different access types\n");
		return -DER_INVAL;
	}

	memset(buf, 0, buf_len);

	if (ace->dae_access_types & DAOS_ACL_ACCESS_ALLOW)
		rc = write_char(&pen, ACCESS_ALLOW_CH, &remaining_len);
	if (ace->dae_access_types & DAOS_ACL_ACCESS_AUDIT)
		rc = write_char(&pen, ACCESS_AUDIT_CH, &remaining_len);
	if (ace->dae_access_types & DAOS_ACL_ACCESS_ALARM)
		rc = write_char(&pen, ACCESS_ALARM_CH, &remaining_len);

	rc = write_char(&pen, ':', &remaining_len);

	if (ace->dae_access_flags & DAOS_ACL_FLAG_GROUP)
		rc = write_char(&pen, FLAG_GROUP_CH, &remaining_len);
	if (ace->dae_access_flags & DAOS_ACL_FLAG_ACCESS_SUCCESS)
		rc = write_char(&pen, FLAG_SUCCESS_CH, &remaining_len);
	if (ace->dae_access_flags & DAOS_ACL_FLAG_ACCESS_FAIL)
		rc = write_char(&pen, FLAG_FAIL_CH, &remaining_len);
	if (ace->dae_access_flags & DAOS_ACL_FLAG_POOL_INHERIT)
		rc = write_char(&pen, FLAG_POOL_INHERIT_CH, &remaining_len);

	written = snprintf(pen, remaining_len, ":%s:",
			   get_principal_name_str(ace));
	if (written > remaining_len) {
		remaining_len = 0;
	} else {
		pen += written;
		remaining_len -= written;
	}

	perms = get_perms(ace);
	if (perms & DAOS_ACL_PERM_READ)
		rc = write_char(&pen, PERM_READ_CH, &remaining_len);
	if (perms & DAOS_ACL_PERM_WRITE)
		rc = write_char(&pen, PERM_WRITE_CH, &remaining_len);

	return rc;
}

int
daos_acl_from_strs(const char **ace_strs, size_t ace_nr, struct daos_acl **acl)
{
	struct daos_ace	**tmp_aces;
	struct daos_acl	*tmp_acl;
	size_t		i;
	int		rc;

	if (ace_strs == NULL || ace_nr == 0) {
		D_ERROR("No ACE strings provided\n");
		return -DER_INVAL;
	}

	D_ALLOC_ARRAY(tmp_aces, ace_nr);
	if (tmp_aces == NULL)
		return -DER_NOMEM;

	for (i = 0; i < ace_nr; i++) {
		rc = daos_ace_from_str(ace_strs[i], &(tmp_aces[i]));
		if (rc != 0) {
			D_ERROR("Failed to convert string '%s' to ACE, "
				"err=%d\n", ace_strs[i], rc);
			D_GOTO(out, rc);
		}
	}

	tmp_acl = daos_acl_create(tmp_aces, ace_nr);
	if (tmp_acl == NULL) {
		D_ERROR("Failed to allocate ACL\n");
		D_GOTO(out, rc = -DER_NOMEM);
	}

	*acl = tmp_acl;
	rc = 0;

out:
	for (i = 0; i < ace_nr; i++)
		daos_ace_free(tmp_aces[i]);
	D_FREE(tmp_aces);
	return rc;
}

