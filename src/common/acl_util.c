/*
 * (C) Copyright 2019-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/**
 * Utility functions that may be used when interacting with Access Control
 * Lists
 */
#include <daos/common.h>
#include <daos_security.h>
#include <gurt/common.h>
#include <gurt/debug.h>

#define ACE_FIELD_DELIM		':'

/*
 * Characters representing access flags
 */
#define FLAG_GROUP_CH		'G'
#define FLAG_ACCESS_SUCCESS_CH	'S'
#define FLAG_ACCESS_FAIL_CH	'F'
#define FLAG_POOL_INHERIT_CH	'P'

/*
 * Verbose strings representing access flags
 */
#define FLAG_GROUP_STR		"Group"
#define FLAG_ACCESS_SUCCESS_STR	"Access-Success"
#define FLAG_ACCESS_FAIL_STR	"Access-Failure"
#define FLAG_POOL_INHERIT_STR	"Pool-Inherit"

/*
 * Characters representing access types
 */
#define ACCESS_ALLOW_CH		'A'
#define ACCESS_AUDIT_CH		'U'
#define ACCESS_ALARM_CH		'L'

/*
 * Verbose strings representing access types
 */
#define ACCESS_ALLOW_STR	"Allow"
#define ACCESS_AUDIT_STR	"Audit"
#define ACCESS_ALARM_STR	"Alarm"

/*
 * Characters representing permissions
 */
#define PERM_READ_CH		'r'
#define PERM_WRITE_CH		'w'
#define PERM_CREATE_CONT_CH	'c'
#define PERM_DEL_CONT_CH	'd'
#define PERM_GET_PROP_CH	't'
#define PERM_SET_PROP_CH	'T'
#define PERM_GET_ACL_CH		'a'
#define PERM_SET_ACL_CH		'A'
#define PERM_SET_OWNER_CH	'o'

/*
 * Verbose strings representing permissions
 */
#define PERM_READ_STR		"Read"
#define PERM_WRITE_STR		"Write"
#define PERM_CREATE_CONT_STR	"Create-Container"
#define PERM_DEL_CONT_STR	"Delete-Container"
#define PERM_GET_PROP_STR	"Get-Prop"
#define PERM_SET_PROP_STR	"Set-Prop"
#define PERM_GET_ACL_STR	"Get-ACL"
#define PERM_SET_ACL_STR	"Set-ACL"
#define PERM_SET_OWNER_STR	"Set-Owner"

/*
 * States used to parse a formatted ACE string
 */
enum ace_str_state {
	ACE_FIRST_ACCESS_TYPE,
	ACE_ACCESS_TYPES,
	ACE_FIRST_FLAG,
	ACE_FLAGS,
	ACE_IDENTITY,
	ACE_FIRST_PERM,
	ACE_PERMS,
	ACE_DONE,
	ACE_INVALID,
	ACE_TRUNC,
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
		case FLAG_ACCESS_SUCCESS_CH:
			*flags |= DAOS_ACL_FLAG_ACCESS_SUCCESS;
			break;
		case FLAG_ACCESS_FAIL_CH:
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
		case PERM_CREATE_CONT_CH:
			*perms |= DAOS_ACL_PERM_CREATE_CONT;
			break;
		case PERM_DEL_CONT_CH:
			*perms |= DAOS_ACL_PERM_DEL_CONT;
			break;
		case PERM_GET_PROP_CH:
			*perms |= DAOS_ACL_PERM_GET_PROP;
			break;
		case PERM_SET_PROP_CH:
			*perms |= DAOS_ACL_PERM_SET_PROP;
			break;
		case PERM_GET_ACL_CH:
			*perms |= DAOS_ACL_PERM_GET_ACL;
			break;
		case PERM_SET_ACL_CH:
			*perms |= DAOS_ACL_PERM_SET_ACL;
			break;
		case PERM_SET_OWNER_CH:
			*perms |= DAOS_ACL_PERM_SET_OWNER;
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
	char				delimiter[2];
	enum ace_str_state		state = ACE_ACCESS_TYPES;
	uint16_t			flags = 0;
	uint8_t				access_types = 0;
	uint64_t			perms = 0;
	int				rc = 0;

	/* delimiter needs to be a string for strtok */
	snprintf(delimiter, sizeof(delimiter), "%c", ACE_FIELD_DELIM);

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

	D_ASSERT(new_ace != NULL);
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
write_char(char ch, char **pen, size_t *remaining_len)
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

#define	WRITE_BIT(bits, bit_name)					\
	do {								\
		if (bits & DAOS_ACL_ ## bit_name)			\
			rc = write_char(bit_name ## _CH, &pen,		\
					&remaining_len);		\
	} while (0)

#define	WRITE_DIVIDER(_pen, _remaining_len)				\
	do {								\
		rc = write_char(ACE_FIELD_DELIM, _pen, _remaining_len);	\
	} while (0)


static int
write_str(const char *str, char **pen, size_t *remaining_len)
{
	int	written;

	written = snprintf(*pen, *remaining_len, "%s", str);
	if (written >= *remaining_len) {
		*remaining_len = 0;
		return -DER_TRUNC;
	}

	*pen += written;
	*remaining_len -= written;

	return 0;
}

int
daos_ace_to_str(struct daos_ace *ace, char *buf, size_t buf_len)
{
	size_t		remaining_len = buf_len;
	char		*pen = buf;
	uint64_t	perms;
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

	/* Access Type */
	WRITE_BIT(ace->dae_access_types, ACCESS_ALLOW);
	WRITE_BIT(ace->dae_access_types, ACCESS_AUDIT);
	WRITE_BIT(ace->dae_access_types, ACCESS_ALARM);

	WRITE_DIVIDER(&pen, &remaining_len);

	/* Flags */
	WRITE_BIT(ace->dae_access_flags, FLAG_GROUP);
	WRITE_BIT(ace->dae_access_flags, FLAG_ACCESS_SUCCESS);
	WRITE_BIT(ace->dae_access_flags, FLAG_ACCESS_FAIL);
	WRITE_BIT(ace->dae_access_flags, FLAG_POOL_INHERIT);

	WRITE_DIVIDER(&pen, &remaining_len);

	/* Principal */
	rc = write_str(daos_ace_get_principal_str(ace), &pen, &remaining_len);

	WRITE_DIVIDER(&pen, &remaining_len);

	/* Permissions */
	perms = get_perms(ace);
	WRITE_BIT(perms, PERM_READ);
	WRITE_BIT(perms, PERM_WRITE);
	WRITE_BIT(perms, PERM_CREATE_CONT);
	WRITE_BIT(perms, PERM_DEL_CONT);
	WRITE_BIT(perms, PERM_GET_PROP);
	WRITE_BIT(perms, PERM_SET_PROP);
	WRITE_BIT(perms, PERM_GET_ACL);
	WRITE_BIT(perms, PERM_SET_ACL);
	WRITE_BIT(perms, PERM_SET_OWNER);

	return rc;
}

static const char *
get_verbose_principal_str(const char *name)
{
	if (strncmp(name, DAOS_ACL_PRINCIPAL_OWNER,
		    DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0)
		return "Owner";
	if (strncmp(name, DAOS_ACL_PRINCIPAL_OWNER_GRP,
		    DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0)
		return "Owner-Group";
	if (strncmp(name, DAOS_ACL_PRINCIPAL_EVERYONE,
		    DAOS_ACL_MAX_PRINCIPAL_BUF_LEN) == 0)
		return "Everyone";

	return name;
}

static enum ace_str_state
process_verbose_access_ch(const char ch, char **pen, size_t *remaining_len,
			  bool first)
{
	const char	*str;
	int		rc = 0;

	switch (ch) {
	case ACCESS_ALLOW_CH:
		str = ACCESS_ALLOW_STR;
		break;
	case ACCESS_AUDIT_CH:
		str = ACCESS_AUDIT_STR;
		break;
	case ACCESS_ALARM_CH:
		str = ACCESS_ALARM_STR;
		break;
	case ACE_FIELD_DELIM:
		if (first)
			return ACE_INVALID;
		WRITE_DIVIDER(pen, remaining_len);
		if (rc == -DER_TRUNC)
			return ACE_TRUNC;
		return ACE_FIRST_FLAG;
	case '\0':
	default:
		return ACE_INVALID;
	}

	if (!first)
		write_char('/', pen, remaining_len);
	rc = write_str(str, pen, remaining_len);
	if (rc == -DER_TRUNC)
		return ACE_TRUNC;

	return ACE_ACCESS_TYPES;
}

static enum ace_str_state
process_verbose_flag_ch(const char ch, char **pen, size_t *remaining_len,
			bool first)
{
	const char	*str;
	int		rc = 0;

	switch (ch) {
	case FLAG_GROUP_CH:
		str = FLAG_GROUP_STR;
		break;
	case FLAG_ACCESS_FAIL_CH:
		str = FLAG_ACCESS_FAIL_STR;
		break;
	case FLAG_ACCESS_SUCCESS_CH:
		str = FLAG_ACCESS_SUCCESS_STR;
		break;
	case ACE_FIELD_DELIM:
		WRITE_DIVIDER(pen, remaining_len);
		if (rc == -DER_TRUNC)
			return ACE_TRUNC;
		return ACE_IDENTITY;
	case '\0':
	default:
		return ACE_INVALID;
	}

	if (!first)
		write_char('/', pen, remaining_len);
	rc = write_str(str, pen, remaining_len);
	if (rc == -DER_TRUNC)
		return ACE_TRUNC;

	return ACE_FLAGS;
}

static enum ace_str_state
process_verbose_perms_ch(const char ch, char **pen, size_t *remaining_len,
			 bool first)
{
	const char	*str;
	int		rc = 0;

	switch (ch) {
	case PERM_READ_CH:
		str = PERM_READ_STR;
		break;
	case PERM_WRITE_CH:
		str = PERM_WRITE_STR;
		break;
	case PERM_CREATE_CONT_CH:
		str = PERM_CREATE_CONT_STR;
		break;
	case PERM_DEL_CONT_CH:
		str = PERM_DEL_CONT_STR;
		break;
	case PERM_GET_PROP_CH:
		str = PERM_GET_PROP_STR;
		break;
	case PERM_SET_PROP_CH:
		str = PERM_SET_PROP_STR;
		break;
	case PERM_GET_ACL_CH:
		str = PERM_GET_ACL_STR;
		break;
	case PERM_SET_ACL_CH:
		str = PERM_SET_ACL_STR;
		break;
	case PERM_SET_OWNER_CH:
		str = PERM_SET_OWNER_STR;
		break;
	case '\0':
		if (first) {
			rc = write_str("No-Access", pen, remaining_len);
			if (rc == -DER_TRUNC)
				return ACE_TRUNC;
		}
		return ACE_DONE;
	case ACE_FIELD_DELIM:
	default:
		return ACE_INVALID;
	}

	if (!first)
		write_char('/', pen, remaining_len);
	rc = write_str(str, pen, remaining_len);
	if (rc == -DER_TRUNC)
		return ACE_TRUNC;

	return ACE_PERMS;
}

static enum ace_str_state
process_verbose_identity(const char *pch, char **pen, size_t *remaining_len,
			 const char **end)
{
	char	identity[DAOS_ACL_MAX_PRINCIPAL_BUF_LEN];
	size_t	len;
	char	delim[2];
	int	rc;

	snprintf(delim, sizeof(delim), "%c", ACE_FIELD_DELIM);

	*end = strpbrk(pch, delim);
	if (*end == NULL)
		return ACE_INVALID;

	len = *end - pch;
	strncpy(identity, pch, len);
	identity[len] = '\0';

	if (!daos_acl_principal_is_valid(identity))
		return ACE_INVALID;

	rc = write_str(get_verbose_principal_str(identity), pen, remaining_len);
	if (rc == -DER_TRUNC)
		return ACE_TRUNC;

	WRITE_DIVIDER(pen, remaining_len);
	if (rc == -DER_TRUNC)
		return ACE_TRUNC;

	return ACE_FIRST_PERM;
}

int
daos_ace_str_get_verbose(const char *ace_str, char *buf, size_t buf_len)
{
	const char		*pch = ace_str;
	enum ace_str_state	state = ACE_FIRST_ACCESS_TYPE;
	char			*pen = buf;
	size_t			remaining_len = buf_len;
	bool			first = false;


	if (buf == NULL || buf_len == 0) {
		D_ERROR("Empty or NULL buffer\n");
		return -DER_INVAL;
	}

	if (ace_str == NULL) {
		D_ERROR("NULL ACE string\n");
		return -DER_INVAL;
	}

	memset(buf, 0, buf_len);

	while (state != ACE_INVALID &&
	       state != ACE_DONE &&
	       state != ACE_TRUNC) {
		switch (state) {
		case ACE_FIRST_ACCESS_TYPE:
			first = true;
			/* fall through */
		case ACE_ACCESS_TYPES:
			state = process_verbose_access_ch(*pch, &pen,
							  &remaining_len,
							  first);
			break;
		case ACE_FIRST_FLAG:
			first = true;
			/* fall through */
		case ACE_FLAGS:
			state = process_verbose_flag_ch(*pch, &pen,
							&remaining_len,
							first);
			break;
		case ACE_IDENTITY:
			state = process_verbose_identity(pch, &pen,
							 &remaining_len, &pch);
			break;
		case ACE_FIRST_PERM:
			first = true;
			/* fall through */
		case ACE_PERMS:
			state = process_verbose_perms_ch(*pch,  &pen,
							 &remaining_len,
							 first);
			break;
		default:
			D_INFO("Bad state: %u\n", state);
			state = ACE_INVALID;
		}

		pch++;
		first = false;
	}

	if (state == ACE_INVALID) {
		D_INFO("Invalid ACE string\n");
		return -DER_INVAL;
	}

	if (state == ACE_TRUNC) {
		D_INFO("String was truncated\n");
		return -DER_TRUNC;
	}

	return 0;
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

	rc = daos_acl_validate(tmp_acl);
	if (rc != 0) {
		D_ERROR("Resulting ACL was invalid\n");
		daos_acl_free(tmp_acl);
		D_GOTO(out, rc);
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
		D_ERROR("Couldn't convert ACE to string: "DF_RC"\n", DP_RC(rc));
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

static int
verbose_str_to_stream(FILE *stream, const char *ace_str)
{
	char	verbose_str[DAOS_ACL_MAX_ACE_STR_LEN * 2];
	int	rc;

	rc = daos_ace_str_get_verbose(ace_str, verbose_str,
				      sizeof(verbose_str));
	/* String may have been truncated - that's OK */
	if (rc != 0 && rc != -DER_TRUNC) {
		D_ERROR("failed verbose translation for ACE string '%s': %d\n",
			ace_str, rc);
		return rc;
	}

	return D_FPRINTF(stream, "# %s\n", verbose_str);
}

int
daos_acl_to_stream(FILE *stream, struct daos_acl *acl, bool verbose)
{
	int	rc = 0;
	char	**aces = NULL;
	size_t	aces_nr, i;

	if (stream == NULL) {
		D_ERROR("Invalid stream\n");
		return -DER_INVAL;
	}

	if (acl != NULL) {
		rc = daos_acl_to_strs(acl, &aces, &aces_nr);
		if (rc != 0)
			return rc;
	}

	rc = D_FPRINTF(stream, "# Entries:\n");
	if (rc != 0)
		goto out;

	if (acl == NULL || acl->dal_len == 0) {
		rc = D_FPRINTF(stream, "#   None\n");
		goto out;
	}

	for (i = 0; i < aces_nr; i++) {
		if (verbose) {
			rc = verbose_str_to_stream(stream, aces[i]);
			if (rc != 0)
				goto out;
		}
		rc = D_FPRINTF(stream, "%s\n", aces[i]);
		if (rc != 0)
			goto out;
	}

out:
	if (aces != NULL) {
		free_strings(aces, aces_nr);
		D_FREE(aces);
	}
	return rc;
}
