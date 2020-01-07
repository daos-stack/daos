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

	if (strncmp(identity, DAOS_ACL_PRINCIPAL_OWNER,
		    DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0)
		type = DAOS_ACL_OWNER;
	else if (strncmp(identity, DAOS_ACL_PRINCIPAL_OWNER_GRP,
			 DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0)
		type = DAOS_ACL_OWNER_GROUP;
	else if (strncmp(identity, DAOS_ACL_PRINCIPAL_EVERYONE,
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
			if (!daos_acl_principal_is_valid(field)) {
				state = ACE_INVALID;
				break;
			}

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

const char *
daos_ace_get_principal_str(struct daos_ace *ace)
{
	switch (ace->dae_principal_type) {
	case DAOS_ACL_OWNER:
		return DAOS_ACL_PRINCIPAL_OWNER;
	case DAOS_ACL_OWNER_GROUP:
		return DAOS_ACL_PRINCIPAL_OWNER_GRP;
	case DAOS_ACL_EVERYONE:
		return DAOS_ACL_PRINCIPAL_EVERYONE;
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
			   daos_ace_get_principal_str(ace));
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

	if (acl == NULL) {
		D_ERROR("NULL ACL pointer\n");
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

static int
alloc_str_for_ace(struct daos_ace *current, char **result)
{
	int	rc;
	char	buf[DAOS_ACL_MAX_ACE_STR_LEN];

	rc = daos_ace_to_str(current, buf, sizeof(buf));
	if (rc != 0) {
		D_ERROR("Couldn't convert ACE to string: %d\n", rc);
		return rc;
	}

	D_ASPRINTF(*result, "%s", buf);
	if (*result == NULL)
		return -DER_NOMEM;

	return 0;
}

static void
free_strings(char **str, size_t str_count)
{
	int i;

	for (i = 0; i < str_count; i++)
		D_FREE(str[i]);
}

static int
convert_aces_to_strs(struct daos_acl *acl, size_t ace_nr, char **result)
{
	struct daos_ace	*current = NULL;
	size_t		i;
	int		rc;

	for (i = 0; i < ace_nr; i++) {
		current = daos_acl_get_next_ace(acl, current);
		rc = alloc_str_for_ace(current, &(result[i]));
		if (rc != 0) {
			free_strings(result, i);
			return rc;
		}
	}

	return 0;
}

int
daos_acl_to_strs(struct daos_acl *acl, char ***ace_strs, size_t *ace_nr)
{
	size_t		ace_count = 0;
	char		**result = NULL;
	struct daos_ace *current;
	int		rc;

	if (ace_strs == NULL || ace_nr == NULL) {
		D_ERROR("Null output params: ace_strs=%p, ace_nr=%p\n",
			ace_strs, ace_nr);
		return -DER_INVAL;
	}

	if (daos_acl_validate(acl) != 0) {
		D_ERROR("ACL is not valid\n");
		return -DER_INVAL;
	}

	current = daos_acl_get_next_ace(acl, NULL);
	while (current != NULL) {
		ace_count++;
		current = daos_acl_get_next_ace(acl, current);
	}

	if (ace_count > 0) {
		D_ALLOC_ARRAY(result, ace_count);
		if (result == NULL)
			return -DER_NOMEM;

		rc = convert_aces_to_strs(acl, ace_count, result);
		if (rc != 0) {
			D_FREE(result);
			return rc;
		}
	}

	*ace_strs = result;
	*ace_nr = ace_count;

	return 0;
}
