/*
 * (C) Copyright 2019-2020 Intel Corporation.
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
 * \file
 *
 * DAOS API methods for security and access control
 */

#ifndef __DAOS_SECURITY_H__
#define __DAOS_SECURITY_H__

#if defined(__cplusplus)
extern "C" {
#endif

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

/**
 * Version of the ACL structure format
 */
#define	DAOS_ACL_VERSION		(1)

/**
 * Maximum length of the user@domain principal string, not including null
 * terminator.
 */
#define DAOS_ACL_MAX_PRINCIPAL_LEN	(255)
/** DAOS_ACL_MAX_PRINCIPAL_LEN including NULL terminator */
#define DAOS_ACL_MAX_PRINCIPAL_BUF_LEN	(DAOS_ACL_MAX_PRINCIPAL_LEN + 1)

/**
 * String values for the special principal types
 */
#define DAOS_ACL_PRINCIPAL_OWNER	"OWNER@"
#define DAOS_ACL_PRINCIPAL_OWNER_GRP	"GROUP@"
#define DAOS_ACL_PRINCIPAL_EVERYONE	"EVERYONE@"

/**
 * Maximum length of daos_acl::dal_ace (dal_len's value).
 */
#define DAOS_ACL_MAX_ACE_LEN		(65536)

/**
 * Maximum length of an ACE provided in string format:
 *	<access>:<flags>:<principal>:<perms>
 */
#define DAOS_ACL_MAX_ACE_STR_LEN	(DAOS_ACL_MAX_PRINCIPAL_LEN + 64)

/**
 * Header for the Access Control List, followed by the table of variable-length
 * Access Control Entries.
 * The entry list may be walked by inspecting the principal length and
 * calculating the entry's overall length from that.
 */
struct daos_acl {
	/** Version of the table format */
	uint16_t	dal_ver;
	/** reserved for 64-bit alignment */
	uint16_t	dal_reserv;
	/** length of entries list in bytes */
	uint32_t	dal_len;
	/** table of variable-length Access Control Entries (struct daos_ace) */
	uint8_t		dal_ace[];
};

/**
 * Type of principal for the Access Control Entry.
 * OWNER, OWNER_GROUP, and EVERYONE are special principals that do not need
 * a principal name string.
 */
enum daos_acl_principal_type {
	DAOS_ACL_OWNER,		/** Owner of the object */
	DAOS_ACL_USER,		/** Individual user */
	DAOS_ACL_OWNER_GROUP,	/** Owning group */
	DAOS_ACL_GROUP,		/** Group */
	DAOS_ACL_EVERYONE,	/** Anyone else */

	NUM_DAOS_ACL_TYPES	/** Must be last */
};

/**
 * Bits representing access types to set permissions for
 */
enum daos_acl_access_type {
	DAOS_ACL_ACCESS_ALLOW = (1U << 0),	/** allow access */
	DAOS_ACL_ACCESS_AUDIT = (1U << 1),	/** log the access for review */
	DAOS_ACL_ACCESS_ALARM = (1U << 2)	/** notify of the access */
};

/**
 * Mask of all valid access bits
 */
#define DAOS_ACL_ACCESS_ALL	(DAOS_ACL_ACCESS_ALLOW |		\
				 DAOS_ACL_ACCESS_AUDIT |		\
				 DAOS_ACL_ACCESS_ALARM)

/**
 * Bits representing access flags
 */
enum daos_acl_flags {
	/** This represents a group, not a user */
	DAOS_ACL_FLAG_GROUP		= (1U << 0),
	/** Containers should inherit access controls from this pool */
	DAOS_ACL_FLAG_POOL_INHERIT	= (1U << 1),
	/** Audit/alarm should occur on failed access */
	DAOS_ACL_FLAG_ACCESS_FAIL	= (1U << 2),
	/** Audit/alarm should occur on successful access */
	DAOS_ACL_FLAG_ACCESS_SUCCESS	= (1U << 3)
};

/**
 * Mask of all valid flag bits
 */
#define DAOS_ACL_FLAG_ALL	(DAOS_ACL_FLAG_GROUP |			\
				 DAOS_ACL_FLAG_POOL_INHERIT |		\
				 DAOS_ACL_FLAG_ACCESS_FAIL |		\
				 DAOS_ACL_FLAG_ACCESS_SUCCESS)

/**
 * Bits representing the specific permissions that may be set
 */
enum daos_acl_perm {
	DAOS_ACL_PERM_READ		= (1U << 0),
	DAOS_ACL_PERM_WRITE		= (1U << 1),
	DAOS_ACL_PERM_CREATE_CONT	= (1U << 2),
	DAOS_ACL_PERM_DEL_CONT		= (1U << 3),
	DAOS_ACL_PERM_GET_PROP		= (1U << 4),
	DAOS_ACL_PERM_SET_PROP		= (1U << 5),
	DAOS_ACL_PERM_GET_ACL		= (1U << 6),
	DAOS_ACL_PERM_SET_ACL		= (1U << 7),
	DAOS_ACL_PERM_SET_OWNER		= (1U << 8),
};

/**
 * Mask of all valid permissions for DAOS pools
 */
#define DAOS_ACL_PERM_POOL_ALL	(DAOS_ACL_PERM_READ |			\
				 DAOS_ACL_PERM_GET_PROP |		\
				 DAOS_ACL_PERM_WRITE |			\
				 DAOS_ACL_PERM_CREATE_CONT |		\
				 DAOS_ACL_PERM_DEL_CONT)

/**
 * Mask of all valid permissions for DAOS containers
 */
#define DAOS_ACL_PERM_CONT_ALL	(DAOS_ACL_PERM_READ |			\
				 DAOS_ACL_PERM_WRITE |			\
				 DAOS_ACL_PERM_DEL_CONT |		\
				 DAOS_ACL_PERM_GET_PROP |		\
				 DAOS_ACL_PERM_SET_PROP |		\
				 DAOS_ACL_PERM_GET_ACL |		\
				 DAOS_ACL_PERM_SET_ACL |		\
				 DAOS_ACL_PERM_SET_OWNER)

/**
 * Mask of all valid permission bits in DAOS
 */
#define DAOS_ACL_PERM_ALL	(DAOS_ACL_PERM_POOL_ALL |		\
				 DAOS_ACL_PERM_CONT_ALL)

/**
 * Access Control Entry for a given principal.
 * Each principal has at most one ACE that lists all their permissions in a
 * given Access Control List.
 */
struct daos_ace {
	/** Bitmap of daos_acl_access_type */
	uint8_t		dae_access_types;
	/** daos_acl_principal_type */
	uint8_t		dae_principal_type;
	/** Length of the principal string */
	uint16_t	dae_principal_len;
	/** Bitmap of daos_acl_flags */
	uint16_t	dae_access_flags;
	/** Reserved for 64-bit alignment */
	uint16_t	dae_reserv;
	/** Bitmap of daos_acl_perm for the ALLOW access */
	uint64_t	dae_allow_perms;
	/** Bitmap of daos_acl_perm for AUDIT access */
	uint64_t	dae_audit_perms;
	/** Bitmap of daos_acl_perm for ALARM access */
	uint64_t	dae_alarm_perms;
	/**
	 * Null-terminated string representing the principal name for specific
	 * user/group.
	 * Actual bytes allocated MUST be rounded up for 64-bit alignment.
	 * Empty for special principals OWNER, OWNER_GROUP, and EVERYONE.
	 */
	char		dae_principal[];
};

/**
 * Allocate an DAOS Access Control List.
 *
 * \param[in]	aces		Array of pointers to ACEs to be put in the ACL.
 * \param[in]	num_aces	Number of ACEs in array
 *
 * \return	allocated daos_acl pointer, NULL if failed
 */
struct daos_acl *
daos_acl_create(struct daos_ace *aces[], uint16_t num_aces);

/**
 * Allocate a new copy of a DAOS Access Control List.
 *
 * \param[in]	acl	ACL structure to be copied
 *
 * \return	Newly allocated copy of the ACL, or NULL if the ACL can't be
 *		allocated
 */
struct daos_acl *
daos_acl_dup(struct daos_acl *acl);

/**
 * Free a DAOS Access Control List.
 *
 * \param[in]	acl	ACL pointer to be freed
 */
void
daos_acl_free(struct daos_acl *acl);

/**
 * Get the total size of the DAOS Access Control List in bytes.
 * This includes the size of the header as well as the ACE list.
 *
 * \param[in]	acl	ACL to get the size of
 *
 * \return	Size of ACL in bytes
 *		-DER_INVAL		Invalid input
 */
ssize_t
daos_acl_get_size(struct daos_acl *acl);

/**
 * Get the next Access Control Entry in the Access Control List, for iterating
 * over the list.
 *
 * \param[in]	acl		ACL to traverse
 * \param[in]	current_ace	Current ACE, to determine the next one, or NULL
 *				for the first ACE
 *
 * \return	Pointer to the next ACE in the ACL, or NULL if at the end
 */
struct daos_ace *
daos_acl_get_next_ace(struct daos_acl *acl, struct daos_ace *current_ace);

/**
 * Search the Access Control List for an Access Control Entry for a specific
 * principal.
 *
 * \param[in]	acl		ACL to search
 * \param[in]	type		Principal type to search for
 * \param[in]	principal	Principal name, if type is USER or GROUP. NULL
 *				otherwise.
 * \param[out]	ace		Pointer to matching ACE within ACL (not a copy)
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NONEXIST	Matching ACE not found
 */
int
daos_acl_get_ace_for_principal(struct daos_acl *acl,
			       enum daos_acl_principal_type type,
			       const char *principal, struct daos_ace **ace);

/**
 * Insert an Access Control Entry in the appropriate location in the ACE
 * list. The expected order is: Owner, Users, Assigned Group, Groups, Everyone.
 *
 * The ACL structure may be reallocated to make room for the new ACE. If so the
 * old structure will be freed.
 *
 * If the new ACE is an update of an existing entry, it will replace the old
 * entry.
 *
 * \param[in]	acl	ACL to modify
 * \param[in]	new_ace	ACE to be added
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NOMEM	Failed to allocate required memory
 */
int
daos_acl_add_ace(struct daos_acl **acl, struct daos_ace *new_ace);

/**
 * Remove an Access Control Entry from the list.
 *
 * When the entry is removed, the ACL is reallocated, and the old structure
 * is freed.
 *
 * \param[in]	acl			Original ACL
 * \param[in]	type			Principal type of the ACE to remove
 * \param[in]	principal_name		Principal name of the ACE to remove
 *					(NULL if type isn't user/group)
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NOMEM	Failed to allocate required memory
 *		-DER_NONEXIST	Requested ACE was not in the ACL
 */
int
daos_acl_remove_ace(struct daos_acl **acl,
		    enum daos_acl_principal_type type,
		    const char *principal_name);

/**
 * Print the Access Control List to stdout in a detailed human-readable format,
 * for debug purposes.
 *
 * \param	acl	Access Control List to print
 */
void
daos_acl_dump(struct daos_acl *acl);

/**
 * Parse and sanity check the entire Access Control List for valid values and
 * internal consistency.
 *
 * \param	acl	Access Control List to sanity check
 *
 * \return	0		ACL is valid
 *		-DER_INVAL	ACL is not valid
 *		-DER_NOMEM	Ran out of memory while checking
 */
int
daos_acl_validate(struct daos_acl *acl);

/**
 * Check that the Access Control List is valid for use with a DAOS pool.
 *
 * This includes the checks in daos_acl_validate().
 *
 * \param	acl	Access Control List to sanity check
 *
 * \return	0		ACL is valid
 *		-DER_INVAL	ACL is not valid
 *		-DER_NOMEM	Ran out of memory while checking
 */
int
daos_acl_pool_validate(struct daos_acl *acl);

/**
 * Check that the Access Control List is valid for use with a DAOS container.
 *
 * This includes the checks in daos_acl_validate().
 *
 * \param	acl	Access Control List to sanity check
 *
 * \return	0		ACL is valid
 *		-DER_INVAL	ACL is not valid
 *		-DER_NOMEM	Ran out of memory while checking
 */
int
daos_acl_cont_validate(struct daos_acl *acl);

/**
 * Allocate a new Access Control Entry with an appropriately aligned principal
 * name, if applicable.
 *
 * Only User and Group types use principal name.
 *
 * \param[in]	type			Type of principal for the ACE
 * \param[in]	principal_name		Principal name will be added to the end
 *					of the structure. For types that don't
 *					use it, it is ignored. OK to pass NULL.
 *
 * \return	New ACE structure with an appropriately packed principal name,
 *			length, and type set.
 */
struct daos_ace *
daos_ace_create(enum daos_acl_principal_type type, const char *principal_name);

/**
 * Free an Access Control Entry allocated by daos_ace_alloc().
 *
 * \param[in]	ace	ACE to be freed
 */
void
daos_ace_free(struct daos_ace *ace);

/**
 * Get the length in bytes of an Access Control Entry.
 * The entries have variable length.
 *
 * \param[in]	ace	ACE to get the size of
 *
 * \return	Size of ACE in bytes
 *		-DER_INVAL		Invalid input
 */
ssize_t
daos_ace_get_size(struct daos_ace *ace);

/**
 * Print the Access Control Entry to stdout in a human-readable format.
 *
 * \param	ace	Access Control Entry to print
 * \param	tabs	Number of tabs to indent at top level
 */
void
daos_ace_dump(struct daos_ace *ace, uint32_t tabs);

/**
 * Sanity check the Access Control Entry structure for valid values and internal
 * consistency.
 *
 * \param	ace	Access Control Entry to be checked
 *
 * \return	True if the ACE is valid, false otherwise
 */
bool
daos_ace_is_valid(struct daos_ace *ace);

/**
 * Sanity check that the principal is a properly-formatted name string for use
 * in an Access Control List.
 *
 * The check is not very strict. It verifies that the name is in the
 * name@[domain] format, but does not make assumptions about legal characters
 * in the name or verify that the principal actually exists
 *
 * \param	name	Principal name to be validated
 *
 * \return	true if the name is properly formatted
 *		false otherwise
 */
bool
daos_acl_principal_is_valid(const char *name);

/**
 * Convert a local uid to a properly-formatted principal name for use with the
 * Access Control List API.
 *
 * \param[in]	uid	UID to convert
 * \param[out]	name	Newly allocated null-terminated string containing the
 *			formatted principal name
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NONEXIST	UID not found
 *		-DER_NOMEM	Could not allocate memory
 */
int
daos_acl_uid_to_principal(uid_t uid, char **name);

/**
 * Convert a local gid to a properly-formatted principal name for use with the
 * Access Control List API.
 *
 * \param[in]	gid	GID to convert
 * \param[out]	name	Newly allocated null-terminated string containing the
 *			formatted principal name
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NONEXIST	GID not found
 *		-DER_NOMEM	Could not allocate memory
 */
int
daos_acl_gid_to_principal(gid_t gid, char **name);

/**
 * Convert the name of a user principal from an Access Control List to its
 * corresponding local UID.
 *
 * \param[in]	principal	Principal name
 * \param[out]	uid		UID of the principal
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NONEXIST	User not found
 *		-DER_NOMEM	Could not allocate memory
 */
int
daos_acl_principal_to_uid(const char *principal, uid_t *uid);

/**
 * Convert the name of a group principal from an Access Control List to its
 * corresponding local GID.
 *
 * \param[in]	principal	Principal name
 * \param[out]	gid		GID of the principal
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NONEXIST	Group not found
 *		-DER_NOMEM	Could not allocate memory
 */
int
daos_acl_principal_to_gid(const char *principal, gid_t *gid);

/**
 * Get the principal name string from an Access Control Entry.
 *
 * \param[in]	ace	Access Control Entry
 *
 * \return	Either the string from the principal name field, or one of the
 *		special principal names: OWNER@, GROUP@, EVERYONE@
 */
const char *
daos_ace_get_principal_str(struct daos_ace *ace);

/**
 * Convert an Access Control Entry formatted as a string to a daos_ace
 * structure.
 *
 * \param[in]	str	String defining an ACE
 * \param[out]	ace	Newly allocated ACE structure
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NOMEM	Could not allocate memory
 */
int
daos_ace_from_str(const char *str, struct daos_ace **ace);

/**
 * Convert an Access Control Entry in the form of a daos_ace structure to a
 * compact string.
 *
 * Limitation: A valid ACE with different permissions for different access types
 * cannot be formatted as a single string, and will be rejected as invalid.
 *
 * \param[in]	ace		ACE structure
 * \param[out]	buf		Buffer to write the string
 * \param[out]	buf_len		Size of buffer
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 */
int
daos_ace_to_str(struct daos_ace *ace, char *buf, size_t buf_len);

/**
 * Convert an Access Control Entry string to a verbose string.
 *
 * \param[in]	ace_str		ACE string
 * \param[out]	buf		Output buffer
 * \param[in]	buf_len		Length of output buffer
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input string
 *		-DER_TRUNC	Output didn't fit in buffer
 */
int
daos_ace_str_get_verbose(const char *ace_str, char *buf, size_t buf_len);

/**
 * Convert a list of Access Control Entries formatted as strings to a daos_acl
 * structure.
 *
 * \param[in]	ace_strs	Array of strings defining ACEs
 * \param[in]	ace_nr		Length of ace_strs
 * \param[out]	acl		Newly allocated ACL structure
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NOMEM	Could not allocate memory
 */
int
daos_acl_from_strs(const char **ace_strs, size_t ace_nr, struct daos_acl **acl);

/**
 * Convert an Access Control List (daos_acl) to a list of Access Control Entries
 * formatted as strings.
 *
 * Each entry in ace_strs is dynamically allocated. So is the array itself. It
 * is the caller's responsibility to free all of them.
 *
 * \param[in]	acl		DAOS ACL
 * \param[out]	ace_strs	Newly allocated array of strings
 * \param[out]	ace_nr		Length of ace_strs
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NOMEM	Could not allocate memory
 */
int
daos_acl_to_strs(struct daos_acl *acl, char ***ace_strs, size_t *ace_nr);

/**
 * Convert a formatted principal string to an ACL principal type and name
 * suitable for creating or looking up an Access Control Entry.
 *
 * The format of the input string is:
 * - For named user: "u:username@"
 * - For named group: "g:groupname@"
 * - For special types: "OWNER@", "GROUP@", or "EVERYONE@"
 *
 * \param[in]	principal_str	Formatted principal string
 * \param[out]	type		Type determined from the string
 * \param[out]	name		Newly-allocated name string. Caller is
 *				responsible for freeing.
 *				Result may be NULL if the principal is one of
 *				the special types.
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NOMEM	Could not allocate memory
 */
int
daos_acl_principal_from_str(const char *principal_str,
			    enum daos_acl_principal_type *type,
			    char **name);

/**
 * Print the Access Control List to a stream in the ACL file format.
 *
 * \param[in]	stream	Open stream to which the ACL should be printed
 * \param[in]	acl	Access Control List to print
 * \param[in]	verbose	Include verbose comment for each ACE in output
 *
 * \return	0		Success
 *		-DER_INVAL	Invalid input
 *		-DER_NOMEM	Could not allocate memory
 *		-DER_IO		Failed to write to stream
 */
int
daos_acl_to_stream(FILE *stream, struct daos_acl *acl, bool verbose);

#if defined(__cplusplus)
}
#endif
#endif /* __DAOS_SECURITY_H__ */
