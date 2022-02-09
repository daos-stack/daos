/*
 * (C) Copyright 2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */
/**
 * This file is part of GURT. It implements some miscellaneous functions which
 * not belong to other parts.
 */
#define D_LOGFAC	DD_FAC(misc)
#define M_TAG		DM_TAG(GURT)

#include <stdarg.h>
#include <math.h>
#include <gurt/common.h>

#ifndef DAOS_BUILD_RELEASE	/* enable memory counters */

/** TLS memory allocation counter */
struct dm_tls_counter {
	/** already allocated size */
	int64_t		 mtc_size;
	/** total number of calls of allocation */
	int64_t		 mtc_count;
	/** true if this TLS counter is registered on \a dm_counters */
	bool		 mtc_registered;
};

static __thread bool dm_tls_enabled;
/** thread-local counters */
static __thread struct dm_tls_counter dm_tls_counters[M_TAG_MAX];

static pthread_spinlock_t dm_lock;

/** Assuming no more than 128 xstreams */
#define DM_TLS_MAX	128

/** All the defined counters */
struct dm_counters {
	/** counter tag, see \a dm_tag */
	int			 mc_tag;
	/** index of the last TLS counter in \a mc_tls_cntrs */
	int			 mc_last;
	/** counter string name */
	char			*mc_name;
	/** all the registered tls counters */
	struct dm_tls_counter	*mc_tls_cntrs[DM_TLS_MAX];
	/** counter shared by non-service threads */
	struct dm_tls_counter	 mc_cntr;
};

static struct dm_counters	dm_counters[] = {
	/* implicit module tags used by D_ALLOC (alphabetical order) */
	{
		.mc_tag		= M_AGG,
		.mc_name	= "aggregation",
	},
	{
		.mc_tag		= M_BIO,
		.mc_name	= "bio",
	},
	{
		.mc_tag		= M_CLI,
		.mc_name	= "client",
	},
	{
		.mc_tag		= M_CRT,
		.mc_name	= "cart",
	},
	{
		.mc_tag		= M_CRT_IV,
		.mc_name	= "cart_iv",
	},
	{
		.mc_tag		= M_CRT_RPC,
		.mc_name	= "cart_rpc",
	},
	{
		.mc_tag		= M_CONT,
		.mc_name	= "cont",
	},
	{
		.mc_tag		= M_CSUM,
		.mc_name	= "csum",
	},
	{
		.mc_tag		= M_DTX,
		.mc_name	= "dtx",
	},
	{
		.mc_tag		= M_EC,
		.mc_name	= "ec",
	},
	{
		.mc_tag		= M_EC_AGG,
		.mc_name	= "ec_agg",
	},
	{
		.mc_tag		= M_EC_RECOV,
		.mc_name	= "ec_recov",
	},
	{
		.mc_tag		= M_ENG,
		.mc_name	= "engine",
	},
	{
		.mc_tag		= M_GURT,
		.mc_name	= "gurt",
	},
	{
		.mc_tag		= M_IO,
		.mc_name	= "io",
	},
	{
		.mc_tag		= M_IO_ARG,
		.mc_name	= "io_arg",
	},
	{
		.mc_tag		= M_IV,
		.mc_name	= "incast",
	},
	{
		.mc_tag		= M_LIB,
		.mc_name	= "lib",
	},
	{
		.mc_tag		= M_MGMT,
		.mc_name	= "management",
	},
	{
		.mc_tag		= M_OBJ,
		.mc_name	= "obj",
	},
	{
		.mc_tag		= M_PL,
		.mc_name	= "pl",
	},
	{
		.mc_tag		= M_POOL,
		.mc_name	= "pool",
	},
	{
		.mc_tag		= M_PROP,
		.mc_name	= "prop",
	},
	{
		.mc_tag		= M_RDB,
		.mc_name	= "rdb",
	},
	{
		.mc_tag		= M_RECOV,
		.mc_name	= "rebuild",
	},
	{
		.mc_tag		= M_RSVC,
		.mc_name	= "rsvc",
	},
	{
		.mc_tag		= M_SCHED,
		.mc_name	= "abt_sched",
	},
	{
		.mc_tag		= M_SEC,
		.mc_name	= "security",
	},
	{
		.mc_tag		= M_SWIM,
		.mc_name	= "swim",
	},
	{
		.mc_tag		= M_TSE,
		.mc_name	= "task",
	},
	{
		.mc_tag		= M_TEST,
		.mc_name	= "test",
	},
	{
		.mc_tag		= M_UTIL,
		.mc_name	= "utility",
	},
	{
		.mc_tag		= M_VEA,
		.mc_name	= "vea",
	},
	{
		.mc_tag		= M_VOS,
		.mc_name	= "vos",
	},
	{
		.mc_tag		= M_VOS_DTX,
		.mc_name	= "vos_dtx",
	},
	{
		.mc_tag		= M_VOS_LRU,
		.mc_name	= "vos_lru",
	},
	{
		.mc_tag		= M_VOS_TS,
		.mc_name	= "vos_ts",
	},
	/* the end */
	{
		.mc_tag		= M_TAG_MAX,
		.mc_name	= NULL,
	},
};

int
dm_init(void)
{
	int	rc;

	rc = D_SPIN_INIT(&dm_lock, PTHREAD_PROCESS_PRIVATE);
	if (rc)
		return -DER_NOMEM;

	return 0;
}

void
dm_fini(void)
{
	D_SPIN_DESTROY(&dm_lock);
}

void
dm_use_tls_counter(void)
{
	dm_tls_enabled = true;
}

void
dm_cntr_lock(void)
{
	if (!dm_tls_enabled)
		D_SPIN_LOCK(&dm_lock);
}

void
dm_cntr_unlock(void)
{
	if (!dm_tls_enabled)
		D_SPIN_UNLOCK(&dm_lock);
}

/* register thread-local counter to the counter table, so we can
 * collect the summary, see dm_tag_query().
 */
static void
dm_counter_register(int tag, struct dm_tls_counter *mtc)
{
	struct dm_counters	*mc;

	mc = &dm_counters[tag];
	D_ASSERTF(tag == mc->mc_tag, "Mismatched tag %s: %d/%d\n",
		  mc->mc_name, mc->mc_tag, tag);

	D_SPIN_LOCK(&dm_lock);

	D_ASSERT(mc->mc_last < DM_TLS_MAX);
	D_ASSERT(mc->mc_tls_cntrs[mc->mc_last] == NULL);

	mc->mc_tls_cntrs[mc->mc_last] = mtc;
	mc->mc_last++;
	mtc->mtc_registered = true;

	D_SPIN_UNLOCK(&dm_lock);
}

char *
dm_mem_tag_query(int tag, int64_t *size_p, int64_t *count_p)
{
	struct dm_counters  *mc;
	int64_t		     size;
	int64_t		     count;
	int		     i;

	if (tag >= M_TAG_MAX)
		return NULL;

	/* summarize total memory consumption from thread-local counters */
	mc = &dm_counters[tag];
	D_ASSERT(mc->mc_tag == tag);

	D_SPIN_LOCK(&dm_lock);
	size = mc->mc_cntr.mtc_size;
	count = mc->mc_cntr.mtc_count;

	for (i = size = count = 0; i < mc->mc_last; i++) {
		D_ASSERT(mc->mc_tls_cntrs[i]);
		size += mc->mc_tls_cntrs[i]->mtc_size;
		count += mc->mc_tls_cntrs[i]->mtc_count;
	}
	D_SPIN_UNLOCK(&dm_lock);
	if (size_p)
		*size_p = size;
	if (count_p)
		*count_p = count;

	return mc->mc_name;
}

void
dm_mem_dump_log(void)
{
	char	*name;
	int64_t	 size;
	int64_t	 count;
	int	 i;

	D_DEBUG(DB_DM, "Memory Consumption Status:\n");
	for (size = count = i = 0; i < M_TAG_MAX; i++) {
		int64_t	s, c;

		name = dm_mem_tag_query(i, &s, &c);
		D_ASSERT(name);

		size += s;
		count += c;
		D_DEBUG(DB_DM, "%-16s: size="DF_U64"(KB) count="DF_U64"\n", name, s >> 10, c);
	}
	D_DEBUG(DB_DM, "ALL : size="DF_U64"(KB) count="DF_U64"\n", size >> 10, count);
}

static struct dm_header *
dm_ptr2hdr(void *ptr)
{
	struct dm_header *hdr;

	hdr = ptr - sizeof(struct dm_header);
	D_ASSERTF(hdr->mh_magic == DM_MAGIC_HDR,
		  "Corrupted memory header(magic: %x/%x), allocated by %s:%d\n",
		  hdr->mh_magic, DM_MAGIC_HDR, hdr->mh_func, hdr->mh_line);
	return hdr;
}

static struct dm_tail *
dm_ptr2tail(void *ptr)
{
	struct dm_header *hdr = dm_ptr2hdr(ptr);
	struct dm_tail	 *tail;

	tail = (struct dm_tail *)(ptr + hdr->mh_size);
	D_ASSERTF(tail->mt_magic == DM_MAGIC_TAIL,
		  "Corrupted memory tail(magic: %x/%x), allocated by %s:%d\n",
		  tail->mt_magic, DM_MAGIC_TAIL, hdr->mh_func, hdr->mh_line);
	return tail;
}

static void *
dm_hdr2ptr(struct dm_header *hdr)
{
	return (void *)&hdr->mh_payload[0];
}

static void
dm_free(void *ptr)
{
	struct dm_tls_counter	*mtc;
	struct dm_header	*hdr;
	struct dm_tail		*tail;

	if (!ptr)
		return;

	hdr = dm_ptr2hdr(ptr);
	tail = dm_ptr2tail(ptr);
	/* NB: TLS counter might belong to a different xstream, so it's not 100% safe w/o lock,
	 * but it's for debugging and hurts nothing except returning inaccurate result.
	 */
	mtc = tail->mt_counter;
	D_ASSERT(mtc);

	dm_cntr_lock();
	mtc->mtc_size -= hdr->mh_size;
	mtc->mtc_count--;
	dm_cntr_unlock();

	/* this is the real allocated address */
	D_ASSERT(hdr->mh_addr);
	free(hdr->mh_addr);
}

static void *
dm_alloc(int tag, int alignment, size_t size, const char *func, int line)
{
	struct dm_tls_counter	*mtc;
	struct dm_header	*hdr;
	struct dm_tail		*tail;
	void			*buf;
	int			 hdr_size = sizeof(*hdr);

	D_ASSERTF(tag >= M_TAG_MIN && tag < M_TAG_MAX, "tag=%d, alignment=%d, size="DF_U64"\n",
		  tag, alignment, size);

	if (dm_tls_enabled) {
		mtc = &dm_tls_counters[tag];
		/* register the thread-local counter (lockless) */
		if (!mtc->mtc_registered)
			dm_counter_register(tag, mtc);
	} else {
		/* use the global counter, which requires spinlock */
		mtc = &dm_counters[tag].mc_cntr;
	}

	if (alignment == 0) {
		buf = malloc(hdr_size + size + sizeof(*tail));
		if (!buf)
			return NULL;

		hdr = buf;
		hdr->mh_alignment = 0;
	} else {
		int	nob = alignment;

		while (alignment < hdr_size)
			alignment += nob;

		buf = aligned_alloc(alignment, alignment + size + sizeof(*tail));
		if (!buf)
			return NULL;

		hdr = buf + alignment - hdr_size;
		hdr->mh_alignment = alignment;
	}
	hdr->mh_func	= func;
	hdr->mh_line	= line;
	hdr->mh_tag	= tag;
	hdr->mh_addr	= buf;
	hdr->mh_size	= size;
	hdr->mh_magic	= DM_MAGIC_HDR;

	tail = (struct dm_tail *)(dm_hdr2ptr(hdr) + size);
	tail->mt_magic	 = DM_MAGIC_TAIL;
	tail->mt_reserv	 = 0;
	tail->mt_counter = mtc;

	dm_cntr_lock();
	mtc->mtc_size += size;
	mtc->mtc_count++;
	dm_cntr_unlock();

	return dm_hdr2ptr(hdr);
}

void
d_free(void *ptr)
{
	dm_free(ptr);
}

void *
d_malloc(int tag, size_t size, const char *func, int line)
{
	return dm_alloc(tag, 0, size, func, line);
}

void *
d_calloc(int tag, size_t count, size_t eltsize, const char *func, int line)
{
	void   *buf;
	size_t  rsize = count * eltsize;

	buf = dm_alloc(tag, 0, rsize, func, line);
	memset(buf, 0, rsize);
	return buf;
}

void *
d_realloc(int tag, void *ptr, size_t size, const char *func, int line)
{
	struct dm_header	*hdr = NULL;
	void			*new_ptr;
	int			 alignment = 0;

	/* trick part is @ptr can be NULL, so we still require @tag as input */
	if (ptr) {
		hdr = dm_ptr2hdr(ptr);
		tag = hdr->mh_tag;
		alignment = hdr->mh_alignment;
	}

	new_ptr = dm_alloc(tag, alignment, size, func, line);
	if (!new_ptr)
		return NULL;

	if (ptr) {
		memcpy(new_ptr, ptr, min(size, hdr->mh_size));
		dm_free(ptr);
	}
	return new_ptr;
}

void *
d_aligned_alloc(int tag, size_t alignment, size_t size, const char *func, int line)
{
	return dm_alloc(tag, alignment, size, func, line);
}

char *
d_strndup(const char *s, size_t n)
{
	char	*str;

	str = dm_alloc(M_GURT, 0, n + 1, __func__, __LINE__);
	if (!str)
		return NULL;

	strncpy(str, s, n);
	str[n] = 0;
	return str;
}

int
d_asprintf(char **strp, const char *fmt, ...)
{
	char	*tmp1;
	char	*tmp2;
	va_list	ap;
	int	len;
	int	rc;

	va_start(ap, fmt);
	rc = vasprintf(&tmp1, fmt, ap);
	va_end(ap);

	if (rc < 0)
		return rc;

	len = strlen(tmp1) + 1;
	tmp2 = dm_alloc(M_GURT, 0, len, __func__, __LINE__);
	if (!tmp2) {
		free(tmp1);
		return -1;
	}
	strncpy(tmp2, tmp1, len - 1);
	tmp2[len] = 0;
	free(tmp1);
	*strp = tmp2;

	return rc;
}

char *
d_realpath(const char *path, char *resolved_path)
{
	char *tmp1;
	char *tmp2;

	if (!resolved_path) {
		tmp1 = d_calloc(M_GURT, strlen(path) + 1, 1, __func__, __LINE__);
		if (!tmp1)
			return NULL;
	} else {
		tmp1 = resolved_path;
		memset(tmp1, 0, strlen(path) + 1);
	}

	tmp2 = realpath(path, tmp1);
	if (!tmp2) {
		D_PRINT("Invalid output %s\n", tmp1);
		if (tmp1 != resolved_path)
			dm_free(tmp1);
		return NULL;
	}
	return tmp1;
}

#else /* DAOS_BUILD_RELEASE */

int
dm_init(void)
{
	return 0;
}

void
dm_fini(void)
{
}

void
dm_use_tls_counter(void)
{
}

char *
dm_mem_tag_query(int tag, int64_t *size_p, int64_t *count_p)
{
	return NULL;
}

void
dm_mem_dump_log(void)
{
}

void
d_free(void *ptr)
{
	free(ptr);
}

void *
d_calloc(int tag, size_t count, size_t eltsize, const char *func, int line)
{
	return calloc(count, eltsize);
}

void *
d_malloc(int tag, size_t size, const char *func, int line)
{
	return malloc(size);
}

void *
d_realloc(int tag, void *ptr, size_t size, const char *func, int line)
{
	return realloc(ptr, size);
}

void *
d_aligned_alloc(int tag, size_t alignment, size_t size, const char *func, int line)
{
	return aligned_alloc(alignment, size);
}

char *
d_strndup(const char *s, size_t n)
{
	return strndup(s, n);
}

int
d_asprintf(char **strp, const char *fmt, ...)
{
	va_list	ap;
	int	rc;

	va_start(ap, fmt);
	rc = vasprintf(strp, fmt, ap);
	va_end(ap);

	return rc;
}

char *
d_realpath(const char *path, char *resolved_path)
{
	return realpath(path, resolved_path);
}

#endif /* DAOS_BUILD_RELEASE */
