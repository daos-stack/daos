/*
 * (C) Copyright 2016-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of gurt, it implements the debug subsystem based on clog.
 */

#include <stdlib.h>
#include <stdio.h>

#include <gurt/common.h>

static pthread_mutex_t d_log_lock = PTHREAD_MUTEX_INITIALIZER;
static int d_log_refcount;

D_FOREACH_GURT_FAC(D_LOG_INSTANTIATE_FAC, D_NOOP)

/* An alternative assert function. Set with d_register_alt_assert() */
void (*d_alt_assert)(const int, const char*, const char*, const int);

D_FOREACH_GURT_DB(D_LOG_INSTANTIATE_DB, D_NOOP)

/** Configurable debug bits (project-specific) */
static d_dbug_t DB_OPT1;
static d_dbug_t DB_OPT2;
static d_dbug_t DB_OPT3;
static d_dbug_t DB_OPT4;
static d_dbug_t DB_OPT5;
static d_dbug_t DB_OPT6;
static d_dbug_t DB_OPT7;
static d_dbug_t DB_OPT8;
static d_dbug_t DB_OPT9;
static d_dbug_t DB_OPT10;

#define DBG_ENV_MAX_LEN	(128)

#define DBG_DICT_ENTRY(bit, name, longname)				\
	{ .db_bit = bit, .db_name = name, .db_name_size = sizeof(name),	\
	  .db_lname = longname, .db_lname_size = sizeof(longname) }

#define D_INIT_DB(bit, name, longname, mask, arg)	\
	DBG_DICT_ENTRY(&bit, #name, #longname),

struct d_debug_bit d_dbg_bit_dict[] = {
	/* load common debug bits into dict */
	D_FOREACH_GURT_DB(D_INIT_DB, D_NOOP)
	/* set by d_log_dbg_bit_alloc() */
	DBG_DICT_ENTRY(&DB_OPT1, NULL, NULL),
	DBG_DICT_ENTRY(&DB_OPT2, NULL, NULL),
	DBG_DICT_ENTRY(&DB_OPT3, NULL, NULL),
	DBG_DICT_ENTRY(&DB_OPT4, NULL, NULL),
	DBG_DICT_ENTRY(&DB_OPT5, NULL, NULL),
	DBG_DICT_ENTRY(&DB_OPT6, NULL, NULL),
	DBG_DICT_ENTRY(&DB_OPT7, NULL, NULL),
	DBG_DICT_ENTRY(&DB_OPT8, NULL, NULL),
	DBG_DICT_ENTRY(&DB_OPT9, NULL, NULL),
	DBG_DICT_ENTRY(&DB_OPT10, NULL, NULL),
};

#define NUM_DBG_BIT_ENTRIES	ARRAY_SIZE(d_dbg_bit_dict)

#define DBG_GRP_DICT_ENTRY()					\
	{ .dg_mask = 0, .dg_name = NULL, .dg_name_size = 0 }

struct d_debug_grp d_dbg_grp_dict[] = {
	DBG_GRP_DICT_ENTRY(),
	DBG_GRP_DICT_ENTRY(),
	DBG_GRP_DICT_ENTRY(),
	DBG_GRP_DICT_ENTRY(),
	DBG_GRP_DICT_ENTRY(),
	DBG_GRP_DICT_ENTRY(),
	DBG_GRP_DICT_ENTRY(),
	DBG_GRP_DICT_ENTRY(),
	DBG_GRP_DICT_ENTRY(),
	DBG_GRP_DICT_ENTRY(),
};

#define NUM_DBG_GRP_ENTRIES	ARRAY_SIZE(d_dbg_grp_dict)

#define PRI_DICT_ENTRY(prio, name, longname, mask, arg)	\
	{ .dd_prio = prio, .dd_name = #name, .dd_name_size = sizeof(#name) },

static struct d_debug_priority d_dbg_prio_dict[] = {
	D_FOREACH_PRIO_MASK(PRI_DICT_ENTRY, D_NOOP)
};

#define NUM_DBG_PRIO_ENTRIES	ARRAY_SIZE(d_dbg_prio_dict)

struct d_debug_data d_dbglog_data = {
	/* count of alloc'd debug bits */
	.dbg_bit_cnt		= 0,
	/* count of alloc'd debug groups */
	.dbg_grp_cnt		= 0,
	/* 0 means we should use the mask provided by facilities */
	.dd_mask		= 0,
	/* optional priority output to stderr */
	.dd_prio_err		= 0,
};

#define BIT_CNT_TO_BIT_MASK(cnt)	(1 << (DLOG_DPRISHIFT + cnt))

/**
 * Reset optional debug bit
 *
 * \param[in]	name	debug mask short name
 *
 * \return		0 on success, -1 on error
 */
int
d_log_dbg_bit_dealloc(char *name)
{
	struct d_debug_bit	*d;
	int			 i;
	size_t			 name_sz;

	if (name == NULL)
		return -1;

	name_sz = strlen(name) + 1;

	for (i = 0; i < NUM_DBG_BIT_ENTRIES; i++) {
		d = &d_dbg_bit_dict[i];
		if (d->db_name != NULL) {
			if (strncasecmp(d->db_name, name, name_sz) == 0) {
				d->db_name = NULL;
				d->db_lname = NULL;
				d->db_name_size = 0;
				d->db_lname_size = 0;
				*d->db_bit = 0;

				D_ASSERT(d_dbglog_data.dbg_bit_cnt > 0);
				d_dbglog_data.dbg_bit_cnt--;

				return 0;
			}
		}
	}

	D_PRINT_ERR("Failed to dealloc debug mask:%s\n", name);

	return -1;
}

/**
 * Allocate optional debug bit, register name and return available bit
 *
 * \param[in]	name	debug mask short name
 * \param[in]	lname	debug mask long name
 * \param[out]	dbgbit	alloc'd debug bit
 *
 * \return		0 on success, -1 on error
 */
int
d_log_dbg_bit_alloc(d_dbug_t *dbgbit, char *name, char *lname)
{
	size_t		   name_sz;
	size_t		   lname_sz;
	int		   i;
	d_dbug_t	   bit = 0;
	struct d_debug_bit *d;

	if (name == NULL || dbgbit == NULL)
		return -1;

	name_sz = strlen(name) + 1;
	if (lname != NULL)
		lname_sz = strlen(lname) + 1;
	else
		lname_sz = 0;

	/**
	 * Allocate debug bit in gurt for given debug mask name.
	 * Currently only 10 configurable debug mask options.
	 * dbg_bit_cnt = [0-15]
	 */
	if (d_dbglog_data.dbg_bit_cnt >= (NUM_DBG_BIT_ENTRIES - 1)) {
		D_PRINT_ERR("Cannot allocate debug bit, all available debug "
			    "mask bits currently allocated.\n");
		return -1;
	}

	/**
	 * DB_ALL = DLOG_DBG, does not require a specific bit
	 */
	if (strncasecmp(name, DB_ALL_BITS, name_sz) != 0) {
		bit = BIT_CNT_TO_BIT_MASK(d_dbglog_data.dbg_bit_cnt);
		d_dbglog_data.dbg_bit_cnt++;
	}

	for (i = 0; i < NUM_DBG_BIT_ENTRIES; i++) {
		/**
		 * Debug bit name already present in struct,
		 * still need to assign available bit
		 */
		d = &d_dbg_bit_dict[i];
		if (d->db_name != NULL) {
			if (strncasecmp(d->db_name, name, name_sz) == 0) {
				if (*d->db_bit == 0) {
					/* DB_ALL = DLOG_DBG */
					if (strncasecmp(name, DB_ALL_BITS,
							name_sz) == 0)
						*dbgbit = DLOG_DBG;
					else
						*dbgbit = bit;
					*d->db_bit = bit;
				} else /* debug bit already assigned */
					*dbgbit = *d->db_bit;
				return 0;
			}
		/* Allocate configurable debug bit along with name */
		} else {
			d->db_name = name;
			d->db_lname = lname;
			d->db_name_size = name_sz;
			d->db_lname_size = lname_sz;
			*d->db_bit = bit;

			*dbgbit = bit;
			return 0;
		}
	}

	return -1;
}

/**
 * Reset optional debug group
 *
 * \param[in]	grpname	debug mask group name
 *
 * \return		0 on success, -1 on error
 */
int
d_log_dbg_grp_dealloc(char *name)
{
	struct d_debug_grp	*g;
	int			 i;
	size_t			 name_sz;

	if (name == NULL)
		return -1;

	name_sz = strlen(name) + 1;

	for (i = 0; i < NUM_DBG_GRP_ENTRIES; i++) {
		g = &d_dbg_grp_dict[i];
		if (g->dg_name != NULL) {
			if (strncasecmp(g->dg_name, name, name_sz) == 0) {
				g->dg_name = NULL;
				g->dg_name_size = 0;
				g->dg_mask = 0;

				D_ASSERT(d_dbglog_data.dbg_bit_cnt > 0);
				d_dbglog_data.dbg_grp_cnt--;

				return 0;
			}
		}
	}

	D_PRINT_ERR("Failed to dealloc debug group mask:%s\n", name);

	return -1;
}

static void
debug_mask_load(const char *mask_name)
{
	char			*mask_str;
	char			*cur;
	char			*saved_ptr;
	int			 i;
	struct d_debug_bit	*d;
	struct d_debug_grp	*g;

	/** Must not use D_ macros internally to avoid caching log mask
	 *  during mask resync
	 */
	mask_str = strndup(mask_name, DBG_ENV_MAX_LEN);
	if (mask_str == NULL) {
		D_PRINT_ERR("D_STRNDUP of debug mask failed");
		return;
	}

	cur = strtok_r(mask_str, DD_SEP, &saved_ptr);
	d_dbglog_data.dd_mask = 0;
	while (cur != NULL) {
		for (i = 0; i < NUM_DBG_BIT_ENTRIES; i++) {
			d = &d_dbg_bit_dict[i];
			if (d->db_name != NULL &&
			    strncasecmp(cur, d->db_name,
					d->db_name_size) == 0) {
				d_dbglog_data.dd_mask |= *d->db_bit;
				break;
			}
			if (d->db_lname != NULL &&
			    strncasecmp(cur, d->db_lname,
					d->db_lname_size) == 0) {
				d_dbglog_data.dd_mask |= *d->db_bit;
				break;
			}
		}
		/* check if DD_MASK entry is a group name */
		for (i = 0; i < NUM_DBG_GRP_ENTRIES; i++) {
			g = &d_dbg_grp_dict[i];
			if (g->dg_name != NULL &&
			    strncasecmp(cur, g->dg_name,
					g->dg_name_size) == 0) {
				d_dbglog_data.dd_mask |= g->dg_mask;
				break;
			}
		}
		cur = strtok_r(NULL, DD_SEP, &saved_ptr);
	}
	/** Must not use D_ macros internally to avoid caching log mask
	 *  during mask resync
	 */
	free(mask_str);
}

/**
 * Create an identifier/group name for multiple debug bits
 *
 * \param[in]	dbgmask		group mask
 * \param[in]	grpname		debug mask group name
 * \param[in]	flags		bit flags. e.g. D_LOG_SET_AS_DEFAULT sets
 *				grpname as the default mask. See
 *				\ref d_log_flag_bits for supported flags.
 *
 * \return			0 on success, -1 on error
 */
int
d_log_dbg_grp_alloc(d_dbug_t dbgmask, char *grpname, uint32_t flags)
{
	int			 i;
	size_t			 name_sz;
	struct d_debug_grp	*g;
	bool			 set_as_default;

	if (grpname == NULL || dbgmask == 0)
		return -1;

	name_sz = strlen(grpname) + 1;
	set_as_default = flags & D_LOG_SET_AS_DEFAULT;

	/**
	 * Allocate debug group in gurt for given debug mask name.
	 * Currently only 10 configurable debug group options.
	 */
	if (d_dbglog_data.dbg_grp_cnt > NUM_DBG_GRP_ENTRIES) {
		D_PRINT_ERR("Cannot allocate debug group, all available debug "
			    "group currently allocated.\n");
		return -1;
	}
	d_dbglog_data.dbg_grp_cnt++;

	for (i = 0; i < NUM_DBG_GRP_ENTRIES; i++) {
		g = &d_dbg_grp_dict[i];
		if (g->dg_name == NULL) {
			g->dg_name = grpname;
			g->dg_name_size = name_sz;
			g->dg_mask = dbgmask;
			if (set_as_default)
				debug_mask_load(grpname);

			return 0;
		}
	}

	/* no empty group entries available */
	return -1;
}

/** Load the priority stderr from the environment variable. */
static void
debug_prio_err_load_env(void)
{
	char	*env;
	int	i;

	env = getenv(DD_STDERR_ENV);
	if (env == NULL)
		return;

	for (i = 0; i < NUM_DBG_PRIO_ENTRIES; i++) {
		if (d_dbg_prio_dict[i].dd_name != NULL &&
		    strncasecmp(env, d_dbg_prio_dict[i].dd_name,
				d_dbg_prio_dict[i].dd_name_size) == 0) {
			d_dbglog_data.dd_prio_err = d_dbg_prio_dict[i].dd_prio;
			break;
		}
	}
	/* invalid DD_STDERR option */
	if (d_dbglog_data.dd_prio_err == 0)
		D_PRINT_ERR("DD_STDERR = %s - invalid option\n", env);
}

void
d_log_sync_mask_ex(const char *log_mask, const char *dd_mask)
{
	D_MUTEX_LOCK(&d_log_lock);

	if (dd_mask != NULL)
		debug_mask_load(dd_mask);

	if (log_mask != NULL)
		d_log_setmasks(log_mask, -1);

	D_MUTEX_UNLOCK(&d_log_lock);
}

/** Load the debug mask from the environment variable. */
void
d_log_sync_mask(void)
{
	d_log_sync_mask_ex(getenv(D_LOG_MASK_ENV), getenv(DD_MASK_ENV));
}

/**
 * Setup the clog facility names and mask.
 *
 * \param[in] masks	 masks in d_log_setmasks() format, or NULL.
 */

static inline int
setup_clog_facnamemask(void)
{
	return D_LOG_REGISTER_FAC(D_FOREACH_GURT_FAC);
}

/**
 * Cleanup gurt mask bits. Names of gurt debug masks are already defined
 * and should not be reset.
 */
static void
cleanup_dbg_namebit(void)
{
	struct d_debug_bit *d;
	int		    i;

	for (i = 0; i < NUM_DBG_BIT_ENTRIES; i++) {
		d = &d_dbg_bit_dict[i];
		if (d->db_name != NULL) {
			/**
			 * DB_ALL is a special case, does not require a
			 * specific bit, therefore is not considered in bit cnt.
			 */
			if (strncasecmp(d->db_name, DB_ALL_BITS,
					d->db_name_size) != 0) {
				*d->db_bit = 0;

				D_ASSERT(d_dbglog_data.dbg_bit_cnt > 0);
				d_dbglog_data.dbg_bit_cnt--;
			}
		}
	}
}

/**
 * Setup the debug names and mask bits.
 *
 * \return	 -DER_UNINIT on error, 0 on success
 */
static inline int
setup_dbg_namebit(void)
{
	struct d_debug_bit *d;
	d_dbug_t	    allocd_dbg_bit;
	int		    i;
	int		    rc;

	D_ASSERT(d_dbglog_data.dbg_bit_cnt == 0);

	for (i = 0; i < NUM_DBG_BIT_ENTRIES; i++) {
		d = &d_dbg_bit_dict[i];
		if (d->db_name != NULL) {
			/* register gurt debug bit masks */
			rc = d_log_dbg_bit_alloc(&allocd_dbg_bit, d->db_name,
						 d->db_lname);
			if (rc < 0) {
				D_PRINT_ERR("Debug bit for %s not allocated\n",
					    d->db_name);
				return -DER_UNINIT;
			}

			*d->db_bit = allocd_dbg_bit;
		}
	}

	return 0;
}

int
d_log_init_adv(char *log_tag, char *log_file, unsigned int flavor,
	       d_dbug_t def_mask, d_dbug_t err_mask, d_log_id_cb_t id_cb)
{
	int rc = 0;

	D_MUTEX_LOCK(&d_log_lock);
	d_log_refcount++;
	if (d_log_refcount > 1) /* Already initialized */
		D_GOTO(out, 0);

	/* Load priority error from environment variable (DD_STDERR)
	 * A Priority error will be output to stderr by the debug system.
	 */
	debug_prio_err_load_env();
	if (d_dbglog_data.dd_prio_err != 0)
		err_mask = d_dbglog_data.dd_prio_err;

	rc = d_log_open(log_tag, 0, def_mask, err_mask, log_file, flavor,
			id_cb);
	if (rc != 0) {
		D_PRINT_ERR("d_log_open failed: %d\n", rc);
		D_GOTO(out, rc = -DER_UNINIT);
	}

	rc = setup_dbg_namebit();
	if (rc != 0)
		D_GOTO(out, rc = -DER_UNINIT);

	rc = setup_clog_facnamemask();
	if (rc != 0)
		D_GOTO(out, rc = -DER_UNINIT);
out:
	if (rc != 0) {
		D_PRINT_ERR("ddebug_init failed, rc: %d.\n", rc);
		d_log_refcount--;
	}
	D_MUTEX_UNLOCK(&d_log_lock);
	return rc;
}

int
d_log_init(void)
{
	char	*log_file;
	int	 flags = DLOG_FLV_LOGPID | DLOG_FLV_FAC | DLOG_FLV_TAG;
	int	 rc;

	log_file = getenv(D_LOG_FILE_ENV);
	if (log_file == NULL || strlen(log_file) == 0) {
		flags |= DLOG_FLV_STDOUT;
		log_file = NULL;
	}

	rc = d_log_init_adv("CaRT", log_file, flags, DLOG_WARN, DLOG_EMERG,
			    NULL);
	if (rc != DER_SUCCESS) {
		D_PRINT_ERR("d_log_init_adv failed, rc: %d.\n", rc);
		D_GOTO(out, rc);
	}

	d_log_sync_mask();
out:
	return rc;
}

void d_log_fini(void)
{
	D_MUTEX_LOCK(&d_log_lock);
	D_ASSERT(d_log_refcount > 0);
	d_log_refcount--;
	if (d_log_refcount == 0) {
		cleanup_dbg_namebit();
		d_log_close();
	}

	D_MUTEX_UNLOCK(&d_log_lock);
}

/**
 * Get allocated debug mask bit from bit name.
 *
 * \param[in] bitname	short name of debug bit
 * \param[out] dbgbit	bit mask allocated for given name
 *
 * \return		0 on success, -1 on error
 */
int d_log_getdbgbit(d_dbug_t *dbgbit, char *bitname)
{
	int		   i;
	int		   num_dbg_bit_entries;
	struct d_debug_bit *d;

	if (bitname == NULL)
		return 0;

	num_dbg_bit_entries = ARRAY_SIZE(d_dbg_bit_dict);
	for (i = 0; i < num_dbg_bit_entries; i++) {
		d = &d_dbg_bit_dict[i];
		if (d->db_name != NULL &&
		    strncasecmp(bitname, d->db_name, d->db_name_size) == 0) {
			*dbgbit = *d->db_bit;
			return 0;
		}
	}

	return -1;
}

int d_register_alt_assert(void (*alt_assert)(const int, const char*,
					     const char*, const int))
{
	if (alt_assert != NULL) {
		d_alt_assert = alt_assert;
		return 0;
	}
	return -DER_INVAL;
}
