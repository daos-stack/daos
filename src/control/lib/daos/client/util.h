#ifndef __DAOS_CLIENT_UTIL_H__
#define __DAOS_CLIENT_UTIL_H__

//#define D_LOGFAC	DD_FAC(client)
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <daos/common.h>

/* cgo is unable to work directly with preprocessor macros
 * so we have to provide these glue helpers.
 */
static inline uint64_t
daos_prop_co_status_val(uint32_t status, uint32_t flag, uint32_t ver)
{
	return DAOS_PROP_CO_STATUS_VAL(status, flag, ver);
}

static void
daos_free(void *ptr)
{
	D_FREE(ptr);
}

/* cgo is unable to work directly with unions, so we have
 *  to provide these glue helpers.
 */
static inline char *
get_dpe_str(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return NULL;

	return dpe->dpe_str;
}

static inline uint64_t
get_dpe_val(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return 0;

	return dpe->dpe_val;
}

static inline void *
get_dpe_val_ptr(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return NULL;

	return dpe->dpe_val_ptr;
}

static inline bool
dpe_is_negative(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return 0;

	return dpe->dpe_flags & DAOS_PROP_ENTRY_NOT_SET;
}

static inline void
set_dpe_str(struct daos_prop_entry *dpe, d_string_t str)
{
	if (dpe == NULL)
		return;

	dpe->dpe_str = str;
}

static inline void
set_dpe_val(struct daos_prop_entry *dpe, uint64_t val)
{
	if (dpe == NULL)
		return;

	dpe->dpe_val = val;
}

static inline void
set_dpe_val_ptr(struct daos_prop_entry *dpe, void *val_ptr)
{
	if (dpe == NULL)
		return;

	dpe->dpe_val_ptr = val_ptr;
}

/*static inline uint32_t
get_rebuild_state(struct daos_rebuild_status *drs)
{
	if (drs == NULL)
		return 0;

	return drs->rs_state;
}*/

#endif /* __DAOS_CLIENT_UTIL_H__ */