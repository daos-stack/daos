/**
 * (C) Copyright 2020-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

/*
 * telemetry: TELEMETRY common logic
 */
#define D_LOGFAC	DD_FAC(telem)

#include <math.h>
#include <float.h>
#include <gurt/common.h>
#include <sys/shm.h>
#include "gurt/telemetry_common.h"
#include "gurt/telemetry_producer.h"
#include "gurt/telemetry_consumer.h"

/** Header of a shared memory region */
struct d_tm_shmem_hdr {
	uint64_t		sh_base_addr;	/** address of this struct */
	uint32_t		sh_id;		/** shmid */
	uint8_t			sh_reserved[4];	/** for alignment */
	uint64_t		sh_bytes_total;	/** total size of region */
	uint64_t		sh_bytes_free;	/** free bytes in this region */
	void			*sh_free_addr;	/** start of free space */
	struct d_tm_node_t	*sh_root;	/** root of metric tree */
};

struct d_tm_context {
	struct d_tm_shmem_hdr *shmem_root; /** primary shared memory region */
};

/**
 * Internal tracking data for shared memory for this process.
 */
static struct d_tm_shmem {
	struct d_tm_context	*dts_ctx; /** context for the producer */
	struct d_tm_node_t	*dts_root; /** root node of shmem */
	pthread_mutex_t		dts_add_lock; /** for synchronized access */
	bool			dts_sync_access; /** whether to sync access */
	bool			dts_retain; /** retain shmem region on exit */
} tm_shmem;

/* Internal helper functions */
static struct d_tm_shmem_hdr *allocate_shared_memory(int srv_idx,
						     size_t mem_size);
static void *d_tm_shmalloc(struct d_tm_shmem_hdr *region, int length);
static bool d_tm_validate_shmem_ptr(struct d_tm_shmem_hdr *shmem_root,
				    void *ptr);
static int d_tm_alloc_node(struct d_tm_shmem_hdr *shmem,
			   struct d_tm_node_t **newnode, char *name);
static struct d_tm_node_t *d_tm_find_child(struct d_tm_context *ctx,
					   struct d_tm_node_t *parent,
					   char *name);
static int d_tm_add_child(struct d_tm_node_t **newnode,
			  struct d_tm_node_t *parent, char *name);

/**
 * Returns a pointer to the root node for the given shared memory segment
 *
 * \param[in]	ctx	Client context
 *
 * \return		Pointer to the root node
 */
struct d_tm_node_t *
d_tm_get_root(struct d_tm_context *ctx)
{
	if (ctx != NULL && ctx->shmem_root != NULL)
		return d_tm_conv_ptr(ctx, ctx->shmem_root->sh_root);

	return NULL;
}

static int
d_tm_lock_shmem(void)
{
	return D_MUTEX_LOCK(&tm_shmem.dts_add_lock);
}

static int
d_tm_unlock_shmem(void)
{
	return D_MUTEX_UNLOCK(&tm_shmem.dts_add_lock);
}

/**
 * Search for a \a parent's child with the given \a name.
 * Return a pointer to the child if found.
 *
 * \param[in]	ctx	Telemetry context
 * \param[in]	parent	The parent node
 * \param[in]	name	The name of the child to find
 *
 * \return		Pointer to the child node
 *			NULL if not found
 */
static struct d_tm_node_t *
d_tm_find_child(struct d_tm_context *ctx, struct d_tm_node_t *parent,
		char *name)
{
	struct d_tm_node_t	*child = NULL;
	char			*client_name;

	if (parent == NULL)
		return NULL;

	if (parent->dtn_child == NULL)
		return NULL;

	child = d_tm_conv_ptr(ctx, parent->dtn_child);

	client_name = NULL;
	if (child != NULL)
		client_name = d_tm_conv_ptr(ctx, child->dtn_name);

	while ((child != NULL) && (client_name != NULL) &&
	       strncmp(client_name, name, D_TM_MAX_NAME_LEN) != 0) {
		child = d_tm_conv_ptr(ctx, child->dtn_sibling);
		client_name = NULL;
		if (child == NULL)
			break;
		client_name = d_tm_conv_ptr(ctx, child->dtn_name);
	}

	return child;
}

/**
 * Allocate a \a newnode and initialize its \a name.
 *
 * \param[in]	shmem	Shared memory region
 * \param[out]	newnode	A pointer for the new node
 * \param[in]	name	The name of the new node
 *
 * \return	DER_SUCCESS		Success
 *		-DER_NO_SHMEM		No shared memory available
 *		-DER_EXCEEDS_PATH_LEN	The full name length is
 *					too long
 *		-DER_INVAL		bad pointers given
 */
static int
d_tm_alloc_node(struct d_tm_shmem_hdr *shmem, struct d_tm_node_t **newnode,
		char *name)
{
	struct d_tm_node_t	*node = NULL;
	int			buff_len = 0;
	int			rc = DER_SUCCESS;

	if (shmem == NULL || newnode == NULL || name == NULL) {
		rc = -DER_INVAL;
		goto out;
	}

	node = d_tm_shmalloc(shmem, sizeof(struct d_tm_node_t));
	if (node == NULL) {
		rc = -DER_NO_SHMEM;
		goto out;
	}
	buff_len = strnlen(name, D_TM_MAX_NAME_LEN);
	if (buff_len == D_TM_MAX_NAME_LEN) {
		rc = DER_EXCEEDS_PATH_LEN;
		goto out;
	}
	buff_len += 1; /* make room for the trailing null */
	node->dtn_name = d_tm_shmalloc(shmem, buff_len);
	if (node->dtn_name == NULL) {
		rc = -DER_NO_SHMEM;
		goto out;
	}
	strncpy(node->dtn_name, name, buff_len);
	node->dtn_region = shmem;
	node->dtn_child = NULL;
	node->dtn_sibling = NULL;
	node->dtn_metric = NULL;
	node->dtn_type = D_TM_DIRECTORY;
	*newnode = node;

out:
	return rc;
}

/**
 * Add a child node the \a parent node in shared memory.
 * A child will either be a first child, or a sibling of an existing child.
 * \param[in,out]	newnode	A pointer for the new node
 * \param[in]		parent	The parent node for this new child
 * \param[in]		name	The name of the new node
 *
 * \return		DER_SUCCESS		Success
 *			-DER_NO_SHMEM		No shared memory available
 *			-DER_EXCEEDS_PATH_LEN	The full name length is
 *						too long
 *			-DER_INVAL		Bad pointers given
 */
static int
d_tm_add_child(struct d_tm_node_t **newnode, struct d_tm_node_t *parent,
	       char *name)
{
	struct d_tm_node_t	*child = NULL;
	struct d_tm_node_t	*sibling = NULL;
	struct d_tm_node_t	*node = NULL;
	int			rc = DER_SUCCESS;

	if ((newnode == NULL) || (parent == NULL) || (name == NULL)) {
		rc = -DER_INVAL;
		goto failure;
	}

	child = parent->dtn_child;
	sibling = parent->dtn_child;
	/** Same region as parent */
	rc = d_tm_alloc_node(parent->dtn_region, &node, name);
	if (rc != DER_SUCCESS)
		goto failure;

	*newnode = node;

	/**
	 * If there are no children, add the first child to this
	 * parent
	 */
	if (child == NULL) {
		parent->dtn_child = node;
		return rc;
	}

	/**
	 * Find the youngest child of this parent by traversing the siblings
	 * of the first child
	 */
	child = child->dtn_sibling;
	while (child != NULL) {
		sibling = child; /** youngest known child */
		child = child->dtn_sibling;
	}
	sibling->dtn_sibling = node;
	return rc;

failure:
	D_ERROR("Failed to add metric [%s]: " DF_RC "\n", name, DP_RC(rc));
	return rc;
}

static int
alloc_ctx(struct d_tm_context **ctx, struct d_tm_shmem_hdr *shmem)
{
	struct d_tm_context *new_ctx;

	D_ASSERT(ctx != NULL);
	D_ASSERT(shmem != NULL);

	D_ALLOC_PTR(new_ctx);
	if (new_ctx == NULL)
		return -DER_NOMEM;

	new_ctx->shmem_root = shmem;

	*ctx = new_ctx;
	return 0;
}

/**
 * Initialize an instance of the telemetry and metrics API for the producer
 * process.
 *
 * \param[in]	id		Identifies the producer process amongst others
 *				on the same machine.
 * \param[in]	mem_size	Size in bytes of the shared memory segment that
 *				is allocated.
 * \param[in]	flags		Optional flags to control initialization.
 *				Use D_TM_SERIALIZATION to enable read/write
 *				synchronization of individual nodes.
 *				Use D_TM_RETAIN_SHMEM to retain the shared
 *				memory segment created for these metrics after
 *				this process exits.
 *
 * \return		DER_SUCCESS		Success
 *			-DER_NO_SHMEM		Out of shared memory
 *			-DER_EXCEEDS_PATH_LEN	Root node name exceeds path len
 *			-DER_INVAL		Invalid \a flag(s)
 */
int
d_tm_init(int id, uint64_t mem_size, int flags)
{
	struct d_tm_shmem_hdr	*new_shmem;
	char			tmp[D_TM_MAX_NAME_LEN];
	int			rc = DER_SUCCESS;

	memset(&tm_shmem, 0, sizeof(tm_shmem));

	if ((flags & ~(D_TM_SERIALIZATION | D_TM_RETAIN_SHMEM)) != 0) {
		D_ERROR("Invalid flags\n");
		rc = -DER_INVAL;
		goto failure;
	}

	if (flags & D_TM_SERIALIZATION) {
		tm_shmem.dts_sync_access = true;
		D_INFO("Serialization enabled for id %d\n", id);
	}

	if (flags & D_TM_RETAIN_SHMEM) {
		tm_shmem.dts_retain = true;
		D_INFO("Retaining shared memory for id %d\n", id);
	}

	new_shmem = allocate_shared_memory(id, mem_size);
	if (new_shmem == NULL) {
		rc = -DER_SHMEM_PERMS;
		goto failure;
	}

	rc = alloc_ctx(&tm_shmem.dts_ctx, new_shmem);
	if (rc != 0)
		goto failure;

	D_DEBUG(DB_TRACE, "Shared memory allocation success!\n"
		"Memory size is %" PRIu64 " bytes at address 0x%" PRIx64
		"\n", mem_size, new_shmem->sh_base_addr);

	snprintf(tmp, sizeof(tmp), "ID: %d", id);
	rc = d_tm_alloc_node(new_shmem, &tm_shmem.dts_root, tmp);
	if (rc != DER_SUCCESS)
		goto failure;

	new_shmem->sh_root = tm_shmem.dts_root;

	rc = D_MUTEX_INIT(&tm_shmem.dts_add_lock, NULL);
	if (rc != 0) {
		D_ERROR("Mutex init failure: " DF_RC "\n", DP_RC(rc));
		goto failure;
	}

	D_INFO("Telemetry and metrics initialized for ID %u\n", id);

	return rc;

failure:
	D_ERROR("Failed to initialize telemetry and metrics for ID %u: "
		DF_RC "\n", id, DP_RC(rc));
	d_tm_close(&tm_shmem.dts_ctx);
	return rc;
}

/**
 * Releases resources claimed by init
 */
void
d_tm_fini(void)
{
	int		rc;
	bool		needs_cleanup = false;
	uint32_t	shmid = 0;

	if (tm_shmem.dts_ctx == NULL)
		goto out;

	if (tm_shmem.dts_ctx->shmem_root != NULL) {
		shmid = tm_shmem.dts_ctx->shmem_root->sh_id;
		if (!tm_shmem.dts_retain)
			needs_cleanup = true;
	}

	d_tm_close(&tm_shmem.dts_ctx);

	if (needs_cleanup) {
		rc = shmctl(shmid, IPC_RMID, NULL);
		if (rc < 0)
			D_ERROR("Unable to remove shared memory segment.  "
				"shmctl failed, %s.\n", strerror(errno));
	}
out:
	memset(&tm_shmem, 0, sizeof(tm_shmem));
}

/**
 * Prints the counter \a val with \a name to the \a stream provided
 *
 * \param[in]	val		Counter value
 * \param[in]	name		Counter name
 * \param[in]	format		Output format.
 *				Choose D_TM_STANDARD for standard output.
 *				Choose D_TM_CSV for comma separated values.
 * \param[in]	units		The units expressed as a string
 * \param[in]	opt_fields	A bitmask.  Set to D_TM_INCLUDE_TYPE to display
 *				metric type.
 * \param[in]	stream		Output stream (stdout, stderr)
 */
void
d_tm_print_counter(uint64_t val, char *name, int format, char *units,
		   int opt_fields, FILE *stream)
{
	if ((stream == NULL) || (name == NULL))
		return;

	if (format == D_TM_CSV) {
		fprintf(stream, "%s", name);
		if (opt_fields & D_TM_INCLUDE_TYPE)
			fprintf(stream, ",counter");
		fprintf(stream, ",%lu", val);
	} else {
		if (opt_fields & D_TM_INCLUDE_TYPE)
			fprintf(stream, "type: counter, ");
		fprintf(stream, "%s: %" PRIu64, name, val);
		if (units != NULL)
			fprintf(stream, " %s", units);
	}
}

/**
 * Prints the timestamp \a clk with \a name to the \a stream provided
 *
 * \param[in]	clk		Timestamp value
 * \param[in]	name		Timestamp name
 * \param[in]	format		Output format.
 *				Choose D_TM_STANDARD for standard output.
 *				Choose D_TM_CSV for comma separated values.
 * \param[in]	opt_fields	A bitmask.  Set to D_TM_INCLUDE_TYPE to display
 *				metric type.
 * \param[in]	stream		Output stream (stdout, stderr)
 */
void
d_tm_print_timestamp(time_t *clk, char *name, int format, int opt_fields,
		     FILE *stream)
{
	char	time_buff[D_TM_TIME_BUFF_LEN];
	char	*temp;

	if ((clk == NULL) || (name == NULL) || (stream == NULL))
		return;

	temp = ctime_r(clk, time_buff);
	if (temp == NULL) {
		fprintf(stream, "Error on timestamp read: ctime() "
			"failure\n");
		return;
	}

	/**
	 * ctime_r result is always D_TM_TIME_BUFF_LEN in length
	 * Remove the trailing newline character
	 */
	temp[D_TM_TIME_BUFF_LEN - 2] = 0;

	if (format == D_TM_CSV) {
		fprintf(stream, "%s", name);
		if (opt_fields & D_TM_INCLUDE_TYPE)
			fprintf(stream, ",timestamp");
		fprintf(stream, ",%s", temp);
	} else {
		if (opt_fields & D_TM_INCLUDE_TYPE)
			fprintf(stream, "type: timestamp, ");
		fprintf(stream, "%s: %s", name, temp);
	}
}

/**
 * Prints the time snapshot \a tms with \a name to the \a stream provided
 *
 * \param[in]	tms		Timer value
 * \param[in]	name		Timer name
 * \param[in]	tm_type		Timer type
 * \param[in]	format		Output format.
 *				Choose D_TM_STANDARD for standard output.
 *				Choose D_TM_CSV for comma separated values.
 * \param[in]	opt_fields	A bitmask.  Set to D_TM_INCLUDE_TYPE to display
 *				metric type.
 * \param[in]	stream		Output stream (stdout, stderr)
 */
void
d_tm_print_timer_snapshot(struct timespec *tms, char *name, int tm_type,
			  int format, int opt_fields, FILE *stream)
{
	uint64_t	us;

	if ((tms == NULL) || (name == NULL) || (stream == NULL))
		return;

	us = tms->tv_sec * 1000000 + tms->tv_nsec / 1000;

	switch (tm_type) {
	case D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_REALTIME:
		if (format == D_TM_CSV) {
			fprintf(stream, "%s", name);
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, ",snapshot realtime");
			fprintf(stream, ",%lu", us);
		} else {
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, "type: snapshot realtime, ");
			fprintf(stream, "%s: %lu us", name, us);
		}
		break;
	case D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_PROCESS_CPUTIME:
		if (format == D_TM_CSV) {
			fprintf(stream, "%s", name);
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, ",snapshot process");
			fprintf(stream, ",%lu", us);
		} else {
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, "type: snapshot process, ");
			fprintf(stream, "%s: %lu us", name, us);
		}
		break;
	case D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_THREAD_CPUTIME:
		if (format == D_TM_CSV) {
			fprintf(stream, "%s", name);
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, ",snapshot thread");
			fprintf(stream, ",%lu", us);
		} else {
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, "type: snapshot thread, ");
			fprintf(stream, "%s: %lu us", name, us);
		}
		break;
	default:
		fprintf(stream, "Invalid timer snapshot type: 0x%x",
			tm_type & ~D_TM_TIMER_SNAPSHOT);
		break;
	}
}

/**
 * Prints the duration \a tms with \a stats and \a name to the \a stream
 * provided
 *
 * \param[in]	tms		Duration timer value
 * \param[in]	stats		Optional stats
 * \param[in]	name		Duration timer name
 * \param[in]	tm_type		Duration timer type
 * \param[in]	format		Output format.
 *				Choose D_TM_STANDARD for standard output.
 *				Choose D_TM_CSV for comma separated values.
 * \param[in]	opt_fields	A bitmask.  Set to D_TM_INCLUDE_TYPE to display
 *				metric type.
 * \param[in]	stream		Output stream (stdout, stderr)
 */
void
d_tm_print_duration(struct timespec *tms, struct d_tm_stats_t *stats,
		    char *name, int tm_type, int format, int opt_fields,
		    FILE *stream)
{
	uint64_t	us;
	bool		printStats;

	if ((tms == NULL) || (name == NULL) || (stream == NULL))
		return;

	printStats = (stats != NULL) && (stats->sample_size > 0);
	us = (tms->tv_sec * 1000000) + (tms->tv_nsec / 1000);

	switch (tm_type) {
	case D_TM_DURATION | D_TM_CLOCK_REALTIME:
		if (format == D_TM_CSV) {
			fprintf(stream, "%s", name);
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, ",duration realtime");
			fprintf(stream, ",%lu", us);
		} else {
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, "type: duration realtime, ");
			fprintf(stream, "%s: %lu us", name, us);
		}
		break;
	case D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME:
		if (format == D_TM_CSV) {
			fprintf(stream, "%s", name);
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, ",duration process");
			fprintf(stream, ",%lu", us);
		} else {
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, "type: duration process, ");
			fprintf(stream, "%s: %lu us", name, us);
		}
		break;
	case D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME:
		if (format == D_TM_CSV) {
			fprintf(stream, "%s", name);
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, ",duration thread");
			fprintf(stream, ",%lu", us);
		} else {
			if (opt_fields & D_TM_INCLUDE_TYPE)
				fprintf(stream, "type: duration thread, ");
			fprintf(stream, "%s: %lu us", name, us);
		}
		break;
	default:
		fprintf(stream, "Invalid timer duration type: 0x%x",
			tm_type & ~D_TM_DURATION);
		printStats = false;
		break;
	}

	if (printStats)
		d_tm_print_stats(stream, stats, format);
}

/**
 * Prints the gauge \a val and \a stats with \a name to the \a stream provided
 *
 * \param[in]	tms		Timer value
 * \param[in]	stats		Optional statistics
 * \param[in]	name		Timer name
 * \param[in]	format		Output format.
 *				Choose D_TM_STANDARD for standard output.
 *				Choose D_TM_CSV for comma separated values.
 * \param[in]	units		The units expressed as a string
 * \param[in]	opt_fields	A bitmask.  Set to D_TM_INCLUDE_TYPE to display
 *				metric type.
 * \param[in]	stream		Output stream (stdout, stderr)
 */
void
d_tm_print_gauge(uint64_t val, struct d_tm_stats_t *stats, char *name,
		 int format, char *units, int opt_fields, FILE *stream)
{
	if ((name == NULL) || (stream == NULL))
		return;

	if (format == D_TM_CSV) {
		fprintf(stream, "%s", name);
		if (opt_fields & D_TM_INCLUDE_TYPE)
			fprintf(stream, ",gauge");
		fprintf(stream, ",%lu", val);
	} else {
		if (opt_fields & D_TM_INCLUDE_TYPE)
			fprintf(stream, "type: gauge, ");
		fprintf(stream, "%s: %lu", name, val);
		if (units != NULL)
			fprintf(stream, " %s", units);
	}

	if ((stats != NULL) && (stats->sample_size > 0))
		d_tm_print_stats(stream, stats, format);
}

/**
 * Client function to print the metadata strings \a desc and \a units
 * to the \a stream provided
 *
 * \param[in]	desc		Pointer to the description string
 * \param[in]	units		Pointer to the units string
 * \param[in]	format		Output format.
 *				Choose D_TM_STANDARD for standard output.
 *				Choose D_TM_CSV for comma separated values.
 * \param[in]	stream		Output stream (stdout, stderr)
 */
void
d_tm_print_metadata(char *desc, char *units, int format, FILE *stream)
{
	if (format == D_TM_CSV) {
		if (desc != NULL)
			fprintf(stream, ",%s", desc);
		else if (units != NULL)
			fprintf(stream, ",");

		if (units != NULL)
			fprintf(stream, ",%s", units);
	} else {
		if (desc != NULL)
			fprintf(stream, ", desc: %s", desc);

		if (units != NULL)
			fprintf(stream, ", units: %s", units);
	}
}

/**
 * Prints a single \a node.
 * Used as a convenience function to demonstrate usage for the client
 *
 * \param[in]	ctx		Client context
 * \param[in]	node		Pointer to a parent or child node
 * \param[in]	level		Indicates level of indentation when printing
 *				this \a node
 * \param[in]	path		The full path of the node.
 *				This path is not stored with the node itself.
 *				This string is passed in so that it may be
 *				printed for this and children nodes.
 * \param[in]	format		Output format.
 *				Choose D_TM_STANDARD for standard output.
 *				Choose D_TM_CSV for comma separated values.
 * \param[in]	opt_fields	A bitmask.  Set D_TM_INCLUDE_* as desired for
 *				the optional output fields.
 * \param[in]	stream		Direct output to this stream (stdout, stderr)
 */
void
d_tm_print_node(struct d_tm_context *ctx, struct d_tm_node_t *node, int level,
		char *path, int format, int opt_fields, FILE *stream)
{
	struct d_tm_stats_t	stats = {0};
	struct timespec		tms;
	uint64_t		val;
	time_t			clk;
	char			time_buff[D_TM_TIME_BUFF_LEN];
	char			*timestamp;
	char			*name = NULL;
	char			*desc = NULL;
	char			*units = NULL;
	bool			stats_printed = false;
	bool			show_timestamp = false;
	bool			show_meta = false;
	int			i = 0;
	int			rc;

	if (node == NULL)
		return;

	name = d_tm_conv_ptr(ctx, node->dtn_name);
	if (name == NULL)
		return;

	show_meta = opt_fields & D_TM_INCLUDE_METADATA;
	show_timestamp = opt_fields & D_TM_INCLUDE_TIMESTAMP;

	if (show_timestamp) {
		clk = time(NULL);
		timestamp = ctime_r(&clk, time_buff);
		timestamp[D_TM_TIME_BUFF_LEN - 2] = 0;
	}

	if (format == D_TM_CSV) {
		if (show_timestamp)
			fprintf(stream, "%s,", timestamp);
		if (path != NULL)
			fprintf(stream, "%s/", path);
	} else {
		for (i = 0; i < level; i++)
			fprintf(stream, "%20s", " ");
		if ((show_timestamp) && (node->dtn_type != D_TM_DIRECTORY))
			fprintf(stream, "%s, ", timestamp);
	}

	d_tm_get_metadata(ctx, &desc, &units, node);

	switch (node->dtn_type) {
	case D_TM_DIRECTORY:
		/**
		 * A tree is printed for standard output where the directory
		 * names are printed in a hierarchy.  In CSV format, the full
		 * path names are printed in each line of output.
		 */
		if (format == D_TM_STANDARD)
			fprintf(stream, "%-20s\n", name);
		break;
	case D_TM_COUNTER:
		rc = d_tm_get_counter(ctx, &val, node);
		if (rc != DER_SUCCESS) {
			fprintf(stream, "Error on counter read: %d\n", rc);
			break;
		}
		d_tm_print_counter(val, name, format, units, opt_fields,
				   stream);
		break;
	case D_TM_TIMESTAMP:
		rc = d_tm_get_timestamp(ctx, &clk, node);
		if (rc != DER_SUCCESS) {
			fprintf(stream, "Error on timestamp read: %d\n", rc);
			break;
		}
		d_tm_print_timestamp(&clk, name, format, opt_fields, stream);
		break;
	case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_REALTIME):
	case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_PROCESS_CPUTIME):
	case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_THREAD_CPUTIME):
		rc = d_tm_get_timer_snapshot(ctx, &tms, node);
		if (rc != DER_SUCCESS) {
			fprintf(stream, "Error on highres timer read: %d\n",
				rc);
			break;
		}
		d_tm_print_timer_snapshot(&tms, name, node->dtn_type, format,
					  opt_fields, stream);
		break;
	case (D_TM_DURATION | D_TM_CLOCK_REALTIME):
	case (D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME):
	case (D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME):
		rc = d_tm_get_duration(ctx, &tms, &stats, node);
		if (rc != DER_SUCCESS) {
			fprintf(stream, "Error on duration read: %d\n", rc);
			break;
		}
		d_tm_print_duration(&tms, &stats, name, node->dtn_type, format,
				    opt_fields, stream);
		if (stats.sample_size > 0)
			stats_printed = true;
		break;
	case D_TM_GAUGE:
		rc = d_tm_get_gauge(ctx, &val, &stats, node);
		if (rc != DER_SUCCESS) {
			fprintf(stream, "Error on gauge read: %d\n", rc);
			break;
		}
		d_tm_print_gauge(val, &stats, name, format, units, opt_fields,
				 stream);
		if (stats.sample_size > 0)
			stats_printed = true;
		break;
	default:
		fprintf(stream, "Item: %s has unknown type: 0x%x\n", name,
			node->dtn_type);
		break;
	}

	if (node->dtn_type == D_TM_DIRECTORY)
		show_meta = false;

	if (show_meta) {
		if (format == D_TM_CSV) {
			/** print placeholders for the missing stats */
			if (!stats_printed &&
			    ((desc != NULL) || (units != NULL)))
				fprintf(stream, ",,,,,");
		}

		d_tm_print_metadata(desc, units, format, stream);
	}
	D_FREE_PTR(desc);
	D_FREE_PTR(units);

	if (node->dtn_type != D_TM_DIRECTORY)
		fprintf(stream, "\n");
}

/**
 * Prints the \a stats to the \a stream
 *
 * \param[in]	stream	Identifies the output stream
 * \param[in]	stats	Pointer to the node statistics
 * \param[in]	format	Output format.
 *			Choose D_TM_STANDARD for standard output.
 *			Choose D_TM_CSV for comma separated values.
 */
void
d_tm_print_stats(FILE *stream, struct d_tm_stats_t *stats, int format)
{
	if (format == D_TM_CSV) {
		fprintf(stream, ",%lu,%lu,%lf,%lu",
			stats->dtm_min, stats->dtm_max, stats->mean,
			stats->sample_size);
		if (stats->sample_size > 2)
			fprintf(stream, ",%lf", stats->std_dev);
		else
			fprintf(stream, ",");
		return;
	}

	fprintf(stream, ", min: %lu, max: %lu, mean: %lf, sample size: %lu",
		stats->dtm_min, stats->dtm_max, stats->mean,
		stats->sample_size);
	if (stats->sample_size > 2)
		fprintf(stream, ", std dev: %lf", stats->std_dev);
}

/**
 * Recursively prints all nodes underneath the given \a node.
 * Used as a convenience function to demonstrate usage for the client
 *
 * \param[in]	ctx		Client context
 * \param[in]	node		Pointer to a parent or child node
 * \param[in]	level		Indicates level of indentation when printing
 *				this \a node
 * \param[in]	filter		A bitmask of d_tm_metric_types that filters the
 *				results.
 * \param[in]	path		Path to this metric (for printing)
 * \param[in]	format		Output format.
 *				Choose D_TM_STANDARD for standard output.
 *				Choose D_TM_CSV for comma separated values.
 * \param[in]	opt_fields	A bitmask.  Set D_TM_INCLUDE_* as desired for
 *				the optional output fields.
 * \param[in]	show_timestamp	Set to true to print the timestamp the metric
 *				was read by the consumer.
 * \param[in]	stream		Direct output to this stream (stdout, stderr)
 */
void
d_tm_print_my_children(struct d_tm_context *ctx, struct d_tm_node_t *node,
		       int level, int filter, char *path, int format,
		       int opt_fields, FILE *stream)
{
	char	*fullpath = NULL;
	char	*node_name = NULL;
	char	*parent_name = NULL;

	if ((node == NULL) || (stream == NULL))
		return;

	if (node->dtn_type & filter)
		d_tm_print_node(ctx, node, level, path, format,
				opt_fields, stream);
	parent_name = d_tm_conv_ptr(ctx, node->dtn_name);
	node = node->dtn_child;
	node = d_tm_conv_ptr(ctx, node);
	if (node == NULL)
		return;

	while (node != NULL) {
		node_name = d_tm_conv_ptr(ctx, node->dtn_name);
		if (node_name == NULL)
			break;

		if ((path == NULL) ||
		    (strncmp(path, "/", D_TM_MAX_NAME_LEN) == 0))
			D_ASPRINTF(fullpath, "%s", parent_name);
		else
			D_ASPRINTF(fullpath, "%s/%s", path, parent_name);

		d_tm_print_my_children(ctx, node, level + 1, filter,
				       fullpath, format, opt_fields, stream);
		D_FREE_PTR(fullpath);
		node = node->dtn_sibling;
		node = d_tm_conv_ptr(ctx, node);
	}
}

/**
 * Prints the header for CSV output
 *
 * \param[in]	opt_fields	A bitmask.  Set D_TM_INCLUDE_* as desired for
 *				the optional output fields.
 * \param[in]	stream		Direct output to this stream (stdout, stderr)
 */
void
d_tm_print_field_descriptors(int opt_fields, FILE *stream)
{
	if (opt_fields & D_TM_INCLUDE_TIMESTAMP)
		fprintf(stream, "timestamp,");

	fprintf(stream, "name,");

	if (opt_fields & D_TM_INCLUDE_TYPE)
		fprintf(stream, "type,");

	fprintf(stream, "value,min,max,mean,sample_size,std_dev");

	if (opt_fields & D_TM_INCLUDE_METADATA)
		fprintf(stream, ",description,units");

	fprintf(stream, "\n");
}

/**
 * Recursively counts number of metrics at and underneath the given \a node.
 *
 * \param[in]	ctx		Telemetry context
 * \param[in]	node		Pointer to a parent or child node
 * \param[in]	d_tm_type	A bitmask of d_tm_metric_types that
 *				determines if an item should be counted
 *
 * \return			Number of metrics found
 */
uint64_t
d_tm_count_metrics(struct d_tm_context *ctx, struct d_tm_node_t *node,
		   int d_tm_type)
{
	uint64_t count = 0;

	if (node == NULL)
		return 0;

	if (d_tm_type & node->dtn_type)
		count++;

	node = node->dtn_child;
	node = d_tm_conv_ptr(ctx, node);

	while (node != NULL) {
		count += d_tm_count_metrics(ctx, node, d_tm_type);
		node = node->dtn_sibling;
		node = d_tm_conv_ptr(ctx, node);
	}
	return count;
}

/**
 * Compute standard deviation
 *
 * \param[in]	sum_of_squares	Precomputed sum of squares
 * \param[in]	sample_size	Number of elements in the data set
 * \param[in]	mean		Mean of all elements
 *
 * \return			computed standard deviation
 */
double
d_tm_compute_standard_dev(double sum_of_squares, uint64_t sample_size,
			  double mean)
{
	if (sample_size < 2)
		return 0;

	return sqrtl((sum_of_squares - (sample_size * mean * mean)) /
		     (sample_size - 1));
}

/**
 * Compute statistics: sample size, min, max, sum and sum of squares.
 * Standard deviation calculation is deferred until the metric is read.
 *
 * \param[in]	node		Pointer to a node with stats
 * \param[in]	value		The new sample value
 */
void
d_tm_compute_stats(struct d_tm_node_t *node, uint64_t value)
{
	struct d_tm_stats_t	*dtm_stats;

	dtm_stats = node->dtn_metric->dtm_stats;

	if (dtm_stats == NULL)
		return;

	dtm_stats->sample_size++;
	dtm_stats->dtm_sum += value;
	dtm_stats->sum_of_squares += value * value;

	if (value > dtm_stats->dtm_max)
		dtm_stats->dtm_max = value;

	if (value < dtm_stats->dtm_min)
		dtm_stats->dtm_min = value;
}

/**
 * Computes the histogram for this metric by finding the bucket that corresponds
 * to the \a value given, and increments the counter for that bucket.
 *
 * \param[in]	node		Pointer to a duration or gauge node
 * \param[in]	value		The value that is sorted into a bucket
 */
void
d_tm_compute_histogram(struct d_tm_node_t *node, uint64_t value)
{
	struct d_tm_histogram_t	*dtm_histogram;
	struct d_tm_node_t	*bucket;
	int			i;

	if (!node || !node->dtn_metric || !node->dtn_metric->dtm_histogram)
		return;

	dtm_histogram = node->dtn_metric->dtm_histogram;

	for (i = 0; i < dtm_histogram->dth_num_buckets; i++) {
		if (value <= dtm_histogram->dth_buckets[i].dtb_max) {
			bucket = dtm_histogram->dth_buckets[i].dtb_bucket;
			d_tm_inc_counter(bucket, 1);
			break;
		}
	}
}

static void
d_tm_node_lock(struct d_tm_node_t *node) {
	if (unlikely(node->dtn_protect))
		D_MUTEX_LOCK(&node->dtn_lock);
}

static void
d_tm_node_unlock(struct d_tm_node_t *node) {
	if (unlikely(node->dtn_protect))
		D_MUTEX_UNLOCK(&node->dtn_lock);
}

/**
 * Set the given counter to the specified \a value
 *
 * \param[in]	metric	Pointer to the metric
 * \param[in]	value	Sets the counter to this \a value
 */
void
d_tm_set_counter(struct d_tm_node_t *metric, uint64_t value)
{
	if (unlikely(metric == NULL))
		return;

	if (unlikely(metric->dtn_type != D_TM_COUNTER)) {
		D_ERROR("Failed to set counter [%s] on item not a "
			"counter.\n", metric->dtn_name);
		return;
	}

	d_tm_node_lock(metric);
	metric->dtn_metric->dtm_data.value = value;
	d_tm_node_unlock(metric);
}

/**
 * Increment the given counter by the specified \a value
 *
 * \param[in]	metric	Pointer to the metric
 * \param[in]	value	Increments the counter by this \a value
 */
void
d_tm_inc_counter(struct d_tm_node_t *metric, uint64_t value)
{

	if (unlikely(metric == NULL))
		return;

	if (unlikely(metric->dtn_type != D_TM_COUNTER)) {
		D_ERROR("Failed to set counter [%s] on item not a "
			"counter.\n", metric->dtn_name);
		return;
	}

	d_tm_node_lock(metric);
	metric->dtn_metric->dtm_data.value += value;
	d_tm_node_unlock(metric);
}

/**
 * Record the current timestamp
 *
 * \param[in]	metric	Pointer to the metric
 */
void
d_tm_record_timestamp(struct d_tm_node_t *metric)
{
	if (metric == NULL)
		return;

	if (metric->dtn_type != D_TM_TIMESTAMP) {
		D_ERROR("Failed to record timestamp [%s] on item not a "
			"timestamp.  Operation mismatch: " DF_RC "\n",
			metric->dtn_name, DP_RC(-DER_OP_NOT_PERMITTED));
		return;
	}

	d_tm_node_lock(metric);
	metric->dtn_metric->dtm_data.value = (uint64_t)time(NULL);
	d_tm_node_unlock(metric);
}

/**
 * Read and store a high resolution timer snapshot value
 *
 * \param[in]	metric	Pointer to the metric
 * \param[in]	clk_id	A D_TM_CLOCK_* that identifies the clock type
 */
void
d_tm_take_timer_snapshot(struct d_tm_node_t *metric, int clk_id)
{
	if (metric == NULL)
		return;

	if (!(metric->dtn_type & D_TM_TIMER_SNAPSHOT)) {
		D_ERROR("Failed to record high resolution timer [%s] on item "
			"not a high resolution timer.  Operation mismatch: "
			DF_RC "\n", metric->dtn_name,
			DP_RC(-DER_OP_NOT_PERMITTED));
		return;
	}

	d_tm_node_lock(metric);
	clock_gettime(d_tm_clock_id(metric->dtn_type & ~D_TM_TIMER_SNAPSHOT),
		      &metric->dtn_metric->dtm_data.tms[0]);
	d_tm_node_unlock(metric);
}

/**
 * Record the start of a time interval (paired with d_tm_mark_duration_end())
 *
 * \param[in]	metric	Pointer to the metric
 * \param[in]	clk_id	A D_TM_CLOCK_* that identifies the clock type
 */
void
d_tm_mark_duration_start(struct d_tm_node_t *metric, int clk_id)
{
	if (metric == NULL)
		return;

	if (!(metric->dtn_type & D_TM_DURATION)) {
		D_ERROR("Failed to mark duration start [%s] on item "
			"not a duration.  Operation mismatch: " DF_RC "\n",
			metric->dtn_name, DP_RC(-DER_OP_NOT_PERMITTED));
		return;
	}

	d_tm_node_lock(metric);
	clock_gettime(d_tm_clock_id(metric->dtn_type & ~D_TM_DURATION),
		      &metric->dtn_metric->dtm_data.tms[1]);
	d_tm_node_unlock(metric);
}

/**
 * Mark the end of the time interval started by d_tm_mark_duration_start()
 * Calculates the total interval and stores the result as the value of this
 * metric.
 *
 * \param[in]		metric	Pointer to the metric
 */
void
d_tm_mark_duration_end(struct d_tm_node_t *metric)
{
	struct timespec	end;
	struct timespec	*tms;
	uint64_t	us;

	if (metric == NULL)
		return;

	if (!(metric->dtn_type & D_TM_DURATION)) {
		D_ERROR("Failed to mark duration end [%s] on item "
			"not a duration.  Operation mismatch: " DF_RC "\n",
			metric->dtn_name, DP_RC(-DER_OP_NOT_PERMITTED));
		return;
	}

	d_tm_node_lock(metric);
	clock_gettime(d_tm_clock_id(metric->dtn_type & ~D_TM_DURATION), &end);
	metric->dtn_metric->dtm_data.tms[0] =
		d_timediff(metric->dtn_metric->dtm_data.tms[1], end);
	tms = metric->dtn_metric->dtm_data.tms;
	us = (tms->tv_sec * 1000000) + (tms->tv_nsec / 1000);
	d_tm_compute_stats(metric, us);
	d_tm_compute_histogram(metric, us);
	d_tm_node_unlock(metric);
}

/**
 * Set an arbitrary \a value for the gauge.
 *
 * \param[in,out]	metric	Pointer to the metric
 * \param[in]		value	Set the gauge to this value
 */
void
d_tm_set_gauge(struct d_tm_node_t *metric, uint64_t value)
{
	if (metric == NULL)
		return;

	if (metric->dtn_type != D_TM_GAUGE) {
		D_ERROR("Failed to set gauge [%s] on item "
			"not a gauge.  Operation mismatch: " DF_RC "\n",
			metric->dtn_name, DP_RC(-DER_OP_NOT_PERMITTED));
		return;
	}

	d_tm_node_lock(metric);
	metric->dtn_metric->dtm_data.value = value;
	d_tm_compute_stats(metric, metric->dtn_metric->dtm_data.value);
	d_tm_compute_histogram(metric, value);
	d_tm_node_unlock(metric);
}

/**
 * Increments the gauge by the \a value provided
 *
 * \param[in,out]	metric	Pointer to the metric
 * \param[in]		value	Increment the gauge by this value
 */
void
d_tm_inc_gauge(struct d_tm_node_t *metric, uint64_t value)
{
	if (metric == NULL)
		return;

	if (metric->dtn_type != D_TM_GAUGE) {
		D_ERROR("Failed to increment gauge [%s] on item "
			"not a gauge.  Operation mismatch: " DF_RC "\n",
			metric->dtn_name, DP_RC(-DER_OP_NOT_PERMITTED));
		return;
	}

	d_tm_node_lock(metric);
	metric->dtn_metric->dtm_data.value += value;
	d_tm_compute_stats(metric, metric->dtn_metric->dtm_data.value);
	d_tm_compute_histogram(metric, value);
	d_tm_node_unlock(metric);
}

/**
 * Decrements the gauge by the \a value provided
 *
 * \param[in,out]	metric	Pointer to the metric
 * \param[in]		value	Decrement the gauge by this value
 */
void
d_tm_dec_gauge(struct d_tm_node_t *metric, uint64_t value)
{
	if (metric == NULL)
		return;

	if (metric->dtn_type != D_TM_GAUGE) {
		D_ERROR("Failed to decrement gauge [%s] on item "
			"not a gauge.  Operation mismatch: " DF_RC "\n",
			metric->dtn_name, DP_RC(-DER_OP_NOT_PERMITTED));
		return;
	}

	d_tm_node_lock(metric);
	metric->dtn_metric->dtm_data.value -= value;
	d_tm_compute_stats(metric, metric->dtn_metric->dtm_data.value);
	d_tm_compute_histogram(metric, value);
	d_tm_node_unlock(metric);
}

/**
 * Convert a D_TM_CLOCK_* type into a clockid_t
 *
 * \param[in]	clk_id	One of the D_TM_CLOCK_* types
 *
 * \return		The matching clockid_t
 */
int
d_tm_clock_id(int clk_id) {
	switch (clk_id) {
	case D_TM_CLOCK_REALTIME:
		return CLOCK_REALTIME;
	case D_TM_CLOCK_PROCESS_CPUTIME:
		return CLOCK_PROCESS_CPUTIME_ID;
	case D_TM_CLOCK_THREAD_CPUTIME:
		return CLOCK_THREAD_CPUTIME_ID;
	default:
		return CLOCK_REALTIME;
	}
	return CLOCK_REALTIME;
}

/**
 * Convert a D_TM_CLOCK_* type into a string
 *
 * \param[in]	clk_id	One of the D_TM_CLOCK_* types
 *
 * \return		The matching string
 */
char *
d_tm_clock_string(int clk_id) {
	switch (clk_id) {
	case D_TM_CLOCK_REALTIME:
		return D_TM_CLOCK_REALTIME_STR;
	case D_TM_CLOCK_PROCESS_CPUTIME:
		return D_TM_CLOCK_PROCESS_CPUTIME_STR;
	case D_TM_CLOCK_THREAD_CPUTIME:
		return D_TM_CLOCK_THREAD_CPUTIME_STR;
	default:
		break;
	}
	return D_TM_CLOCK_REALTIME_STR;
}

/**
 * Finds the node pointing to the given metric described by path name provided
 *
 * \param[in]	ctx	Telemetry context
 * \param[in]	path	The full name of the metric to find
 *
 * \return		A pointer to the metric node
 */
struct d_tm_node_t *
d_tm_find_metric(struct d_tm_context *ctx, char *path)
{
	struct d_tm_node_t	*parent_node;
	struct d_tm_node_t	*node = NULL;
	char			str[D_TM_MAX_NAME_LEN];
	char			*token;
	char			*rest = str;

	if ((ctx == NULL) || (path == NULL))
		return NULL;

	parent_node = d_tm_get_root(ctx);

	if (parent_node == NULL)
		return NULL;

	snprintf(str, sizeof(str), "%s", path);
	token = strtok_r(rest, "/", &rest);
	while (token != NULL) {
		node = d_tm_find_child(ctx, parent_node, token);
		if (node == NULL)
			return NULL;
		parent_node =  node;
		token = strtok_r(rest, "/", &rest);
	}
	return node;
}

/**
 * Adds a new metric at the specified path, with the given \a metric_type.
 * An optional description and unit name may be added at this time.
 * This function may be called by the developer to initialize a metric at init
 * time in order to avoid the overhead of creating the metric at a more
 * critical time.
 *
 * \param[out]	node		Points to the new metric if supplied
 * \param[in]	metric_type	One of the corresponding d_tm_metric_types
 * \param[in]	desc		A description of the metric containing
 *				D_TM_MAX_DESC_LEN - 1 characters maximum
 * \param[in]	units		A string defining the units of the metric
 *				containing D_TM_UNIT_LEN - 1 characters maximum
 * \param[in]	fmt		Format specifier for the name and full path of
 *				the new metric followed by optional args to
 *				populate the string, printf style.
 * \return			DER_SUCCESS		Success
 *				-DER_NO_SHMEM		Out of shared memory
 *				-DER_NOMEM		Out of global heap
 *				-DER_EXCEEDS_PATH_LEN	node name exceeds
 *							path len or \a units
 *							exceeds length
 *				-DER_INVAL		node is invalid or
 *							invalid units were
 *							specified for the metric
 *							type
 *				-DER_ADD_METRIC_FAILED	Operation failed
 *				-DER_UNINIT		API not initialized
 */
int d_tm_add_metric(struct d_tm_node_t **node, int metric_type, char *desc,
		    char *units, const char *fmt, ...)
{
	pthread_mutexattr_t	mattr;
	struct d_tm_node_t	*parent_node;
	struct d_tm_node_t	*temp = NULL;
	struct d_tm_shmem_hdr	*shmem;
	char			path[D_TM_MAX_NAME_LEN] = {};
	char			*token;
	char			*rest;
	char			*unit_string;
	int			buff_len;
	int			rc;
	int			ret;
	va_list			args;

	if (tm_shmem.dts_ctx == NULL || tm_shmem.dts_ctx->shmem_root == NULL)
		return -DER_UNINIT;

	if (node == NULL)
		return -DER_INVAL;

	if (fmt == NULL)
		return -DER_INVAL;

	rc = d_tm_lock_shmem();
	if (rc != 0) {
		D_ERROR("Failed to get mutex: " DF_RC "\n", DP_RC(rc));
		goto failure;
	}

	va_start(args, fmt);
	ret = vsnprintf(path, sizeof(path), fmt, args);
	va_end(args);

	if (ret <= 0 || ret >= D_TM_MAX_NAME_LEN) {
		D_ERROR("Path too long (max=%d)\n", D_TM_MAX_NAME_LEN);
		rc = -DER_EXCEEDS_PATH_LEN;
		goto failure;
	}

	/**
	 * The item could exist due to a race condition where the
	 * unprotected d_tm_find_metric() does not find the metric,
	 * which leads to this d_tm_add_metric() call.
	 * If the metric is found, it's not an error.  Just return.
	 */
	*node = d_tm_find_metric(tm_shmem.dts_ctx, path);
	if (*node != NULL) {
		d_tm_unlock_shmem();
		return DER_SUCCESS;
	}

	rest = path;
	parent_node = d_tm_get_root(tm_shmem.dts_ctx);
	token = strtok_r(rest, "/", &rest);
	while (token != NULL) {
		temp = d_tm_find_child(tm_shmem.dts_ctx,
				       parent_node,
				       token);
		if (temp == NULL) {
			rc = d_tm_add_child(&temp, parent_node, token);
			if (rc != DER_SUCCESS)
				goto failure;
			temp->dtn_type = D_TM_DIRECTORY;
		}
		parent_node = temp;
		token = strtok_r(rest, "/", &rest);
	}

	if (temp == NULL) {
		rc = -DER_ADD_METRIC_FAILED;
		goto failure;
	}

	shmem = temp->dtn_region;

	temp->dtn_type = metric_type;
	temp->dtn_metric = d_tm_shmalloc(shmem, sizeof(struct d_tm_metric_t));
	if (temp->dtn_metric == NULL) {
		rc = -DER_NO_SHMEM;
		goto failure;
	}

	temp->dtn_metric->dtm_stats = NULL;
	if ((metric_type == D_TM_GAUGE) || (metric_type & D_TM_DURATION)) {
		temp->dtn_metric->dtm_stats =
				     d_tm_shmalloc(shmem,
						   sizeof(struct d_tm_stats_t));
		if (temp->dtn_metric->dtm_stats == NULL) {
			rc = -DER_NO_SHMEM;
			goto failure;
		}
		temp->dtn_metric->dtm_stats->dtm_min = UINT64_MAX;
	}

	buff_len = 0;
	if (desc != NULL)
		buff_len = strnlen(desc, D_TM_MAX_DESC_LEN);
	if (buff_len == D_TM_MAX_DESC_LEN) {
		D_ERROR("Desc string too long (max=%d)\n", D_TM_MAX_DESC_LEN);
		rc = -DER_OVERFLOW;
		goto failure;
	}

	if (buff_len > 0) {
		buff_len += 1; /** make room for the trailing null */
		temp->dtn_metric->dtm_desc = d_tm_shmalloc(shmem, buff_len);
		if (temp->dtn_metric->dtm_desc == NULL) {
			rc = -DER_NO_SHMEM;
			goto failure;
		}
		strncpy(temp->dtn_metric->dtm_desc, desc, buff_len);
	} else {
		temp->dtn_metric->dtm_desc = NULL;
	}

	unit_string = units;

	switch (metric_type & D_TM_ALL_NODES) {
	case D_TM_TIMESTAMP:
		/** Prohibit units for timestamp */
		unit_string = NULL;
		break;
	case D_TM_TIMER_SNAPSHOT:
		/** Always use D_TM_MICROSECOND for timer snapshot */
		unit_string = D_TM_MICROSECOND;
		break;
	case D_TM_DURATION:
		/** Always use D_TM_MICROSECOND for duration */
		unit_string = D_TM_MICROSECOND;
		break;
	default:
		break;
	}

	buff_len = 0;
	if (unit_string != NULL)
		buff_len = strnlen(unit_string, D_TM_MAX_UNIT_LEN);

	if (buff_len == D_TM_MAX_UNIT_LEN) {
		D_ERROR("Units string too long (max=%d)\n", D_TM_MAX_UNIT_LEN);
		rc = -DER_OVERFLOW;
		goto failure;
	}

	if (buff_len > 0) {
		buff_len += 1; /** make room for the trailing null */
		temp->dtn_metric->dtm_units = d_tm_shmalloc(shmem, buff_len);
		if (temp->dtn_metric->dtm_units == NULL) {
			rc = -DER_NO_SHMEM;
			goto failure;
		}
		strncpy(temp->dtn_metric->dtm_units, unit_string, buff_len);
	} else {
		temp->dtn_metric->dtm_units = NULL;
	}

	temp->dtn_protect = false;
	if (tm_shmem.dts_sync_access &&
	    (temp->dtn_type != D_TM_DIRECTORY)) {
		rc = pthread_mutexattr_init(&mattr);
		if (rc != 0) {
			D_ERROR("pthread_mutexattr_init failed: " DF_RC "\n",
				DP_RC(rc));
			goto failure;
		}

		rc = pthread_mutexattr_setpshared(&mattr,
						  PTHREAD_PROCESS_SHARED);
		if (rc != 0) {
			D_ERROR("pthread_mutexattr_setpshared failed: "
				DF_RC "\n", DP_RC(rc));
			goto failure;
		}

		rc = D_MUTEX_INIT(&temp->dtn_lock, &mattr);
		if (rc != 0) {
			D_ERROR("Mutex init failed: " DF_RC "\n", DP_RC(rc));
			goto failure;
		}

		pthread_mutexattr_destroy(&mattr);
		temp->dtn_protect = true;
	}
	*node = temp;

	D_DEBUG(DB_TRACE, "successfully added item: [%s]\n", path);
	d_tm_unlock_shmem();
	return DER_SUCCESS;

failure:
	d_tm_unlock_shmem();
	D_ERROR("Failed to add metric [%s]: " DF_RC "\n", path, DP_RC(rc));
	return rc;
}

/**
 * Creates histogram counters for the given node.  It calculates the
 * extents of each bucket and creates counters at the path specified that
 * correspond to each bucket required.  The name of each counter created is
 * given by the bucket number.  The bucket number and range of each bucket
 * is stored as metadata for each counter.
 *
 * \param[in]	node		Pointer to a node with a metric of type duration
 *				or gauge.
 * \param[in]	path		Path name of the metric specified.
 *				by \a node.  Can be an arbitrary location.
 *				However, specifying the full path allows this
 *				function to create counters underneath the given
 *				node.
 * \param[in]	num_buckets	Specifies the number of buckets the histogram
 *				should have.  Must be > 1.
 * \param[in]	initial_width	The number of elements in the first bucket
 *				Must be > 0.
 * \param[in]	multiplier	Increases the width of bucket N (for N > 0)
 *				by this factor over the width of bucket (N-1).
 *				A multiplier of 1 creates equal size buckets.
 *				A multiplier of 2 creates buckets that are
 *				twice the size of the previous bucket.
 *				Must be > 0.
 *
 * \return			DER_SUCCESS		Success
 *				-DER_INVAL		node, path, num_buckets,
 *							initial_width or
 *							multiplier is invalid.
 *				-DER_OP_NOT_PERMITTED	Node was not a gauge
 *							or duration.
 *				-DER_NO_SHMEM		Out of shared memory
 *				-DER_NOMEM		Out of heap
 */
int
d_tm_init_histogram(struct d_tm_node_t *node, char *path, int num_buckets,
		    int initial_width, int multiplier)
{
	struct d_tm_metric_t	*metric;
	struct d_tm_histogram_t	*histogram;
	struct d_tm_bucket_t	*dth_buckets;
	struct d_tm_shmem_hdr	*shmem;
	uint64_t		min = 0;
	uint64_t		max = 0;
	uint64_t		prev_width = 0;
	int			rc = DER_SUCCESS;
	int			i;
	char			*meta_data;
	char			*fullpath;

	if (node == NULL)
		return -DER_INVAL;

	if (path == NULL)
		return -DER_INVAL;

	if (num_buckets < 2)
		return -DER_INVAL;

	if (initial_width < 1)
		return -DER_INVAL;

	if (multiplier < 1)
		return -DER_INVAL;

	if (!((node->dtn_type == D_TM_GAUGE) ||
	      (node->dtn_type & D_TM_DURATION)))
		return -DER_OP_NOT_PERMITTED;

	rc = d_tm_lock_shmem();
	if (rc != 0) {
		D_ERROR("Failed to get mutex: " DF_RC "\n", DP_RC(rc));
		goto failure;
	}

	shmem = node->dtn_region;
	metric = node->dtn_metric;
	histogram = d_tm_shmalloc(shmem, sizeof(struct d_tm_histogram_t));

	if (histogram == NULL) {
		d_tm_unlock_shmem();
		rc = -DER_NO_SHMEM;
		goto failure;
	}

	histogram->dth_buckets = d_tm_shmalloc(shmem, num_buckets *
					       sizeof(struct d_tm_bucket_t));
	if (histogram->dth_buckets == NULL) {
		d_tm_unlock_shmem();
		rc = -DER_NO_SHMEM;
		goto failure;
	}
	histogram->dth_num_buckets = num_buckets;
	histogram->dth_initial_width = initial_width;
	histogram->dth_value_multiplier = multiplier;

	metric->dtm_histogram = histogram;

	d_tm_unlock_shmem();

	dth_buckets = metric->dtm_histogram->dth_buckets;

	min = 0;
	max = initial_width - 1;
	prev_width = initial_width;
	for (i = 0; i < num_buckets; i++) {
		D_ASPRINTF(meta_data, "histogram bucket %d [%lu .. %lu]",
			   i, min, max);
		if (meta_data == NULL) {
			rc = -DER_NOMEM;
			goto failure;
		}

		D_ASPRINTF(fullpath, "%s/bucket %d", path, i);
		if (fullpath == NULL) {
			rc = -DER_NOMEM;
			goto failure;
		}

		dth_buckets[i].dtb_min = min;
		dth_buckets[i].dtb_max = max;

		rc = d_tm_add_metric(&dth_buckets[i].dtb_bucket, D_TM_COUNTER,
				     meta_data, "elements", fullpath);
		D_FREE(fullpath);
		D_FREE(meta_data);
		if (rc)
			goto failure;

		min = max + 1;

		if (i == (num_buckets - 2)) {
			max = UINT64_MAX;
		} else if (multiplier == 1) {
			max += initial_width;
		} else {
			max = min + (prev_width * multiplier) - 1;
		}
		prev_width = (max - min) + 1;
	}

	D_DEBUG(DB_TRACE, "Successfully added histogram for: [%s]\n", path);
	return DER_SUCCESS;

failure:

	D_ERROR("Failed to histogram for [%s]: " DF_RC "\n", path, DP_RC(rc));
	return rc;
}

/**
 * Retrieves the histogram creation data for the given node, which includes
 * the number of buckets, initial width and multiplier used to create the
 * given histogram.
 *
 * \param[in]	ctx		Client context
 * \param[out]	histogram	Pointer to a d_tm_histogram_t used to
 *				store the results.
 * \param[in]	node		Pointer to the metric node with a
 *				histogram.
 *
 * \return	DER_SUCCESS		Success
 *		-DER_INVAL		node or histogram is
 *					invalid.
 *		-DER_METRIC_NOT_FOUND	The metric node, the
 *					metric data or histogram
 *					was not found.
 *		-DER_OP_NOT_PERMITTED	Node was not a gauge
 *					or duration with
 *					an associated histogram.
 */
int
d_tm_get_num_buckets(struct d_tm_context *ctx,
		     struct d_tm_histogram_t *histogram,
		     struct d_tm_node_t *node)
{
	struct d_tm_histogram_t	*dtm_histogram = NULL;
	struct d_tm_metric_t	*metric_data = NULL;
	struct d_tm_shmem_hdr	*shmem_root;

	if (ctx == NULL || histogram == NULL || node == NULL)
		return -DER_INVAL;

	shmem_root = ctx->shmem_root;

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (!((node->dtn_type == D_TM_GAUGE) ||
	      (node->dtn_type & D_TM_DURATION)))
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(ctx, node->dtn_metric);
	if (metric_data == NULL)
		return -DER_METRIC_NOT_FOUND;

	dtm_histogram = d_tm_conv_ptr(ctx, metric_data->dtm_histogram);
	if (dtm_histogram == NULL)
		return -DER_METRIC_NOT_FOUND;

	histogram->dth_num_buckets = dtm_histogram->dth_num_buckets;
	histogram->dth_initial_width = dtm_histogram->dth_initial_width;
	histogram->dth_value_multiplier = dtm_histogram->dth_value_multiplier;

	return DER_SUCCESS;
}

/**
 * Retrieves the range of the given bucket for the node with a histogram.
 *
 * \param[in]	ctx		Client context
 * \param[out]	bucket		Pointer to a d_tm_bucket_t used to
 *				store the results.
 * \param[in]	bucket_id	Identifies which bucket (0 .. n-1)
 * \param[in]	node		Pointer to the metric node with a
 *				histogram.
 *
 * \return	DER_SUCCESS		Success
 *		-DER_INVAL		node, bucket, or bucket
 *					ID is invalid.
 *		-DER_METRIC_NOT_FOUND	The metric node, the
 *					metric data, histogram
 *					or bucket data was
 *					not found.
 *		-DER_OP_NOT_PERMITTED	Node was not a gauge
 *					or duration with
 *					an associated histogram.
 */
int
d_tm_get_bucket_range(struct d_tm_context *ctx, struct d_tm_bucket_t *bucket,
		      int bucket_id,
		      struct d_tm_node_t *node)
{
	struct d_tm_histogram_t	*dtm_histogram = NULL;
	struct d_tm_bucket_t	*dth_bucket = NULL;
	struct d_tm_metric_t	*metric_data = NULL;
	struct d_tm_shmem_hdr	*shmem_root;

	if (ctx == NULL || node == NULL || bucket == NULL)
		return -DER_INVAL;

	if (bucket_id < 0)
		return -DER_INVAL;

	shmem_root = ctx->shmem_root;

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (!((node->dtn_type == D_TM_GAUGE) ||
	      (node->dtn_type & D_TM_DURATION)))
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(ctx, node->dtn_metric);
	if (metric_data == NULL)
		return -DER_METRIC_NOT_FOUND;

	dtm_histogram = d_tm_conv_ptr(ctx, metric_data->dtm_histogram);
	if (dtm_histogram == NULL)
		return -DER_METRIC_NOT_FOUND;

	if (bucket_id >= dtm_histogram->dth_num_buckets)
		return -DER_INVAL;

	dth_bucket = d_tm_conv_ptr(ctx, dtm_histogram->dth_buckets);
	if (dth_bucket == NULL)
		return -DER_METRIC_NOT_FOUND;

	bucket->dtb_min = dth_bucket[bucket_id].dtb_min;
	bucket->dtb_max = dth_bucket[bucket_id].dtb_max;
	bucket->dtb_bucket = d_tm_conv_ptr(ctx,
					   dth_bucket[bucket_id].dtb_bucket);
	return DER_SUCCESS;
}

/**
 * Client function to read the specified counter.
 *
 * \param[in]	ctx	Client context
 * \param[out]	val	The value of the counter is stored here
 * \param[in]	node	Pointer to the stored metric node
 *
 * \return	DER_SUCCESS		Success
 *		-DER_INVAL		Invalid input
 *		-DER_METRIC_NOT_FOUND	Metric not found
 *		-DER_OP_NOT_PERMITTED	Metric was not a counter
 */
int
d_tm_get_counter(struct d_tm_context *ctx, uint64_t *val,
		 struct d_tm_node_t *node)
{
	struct d_tm_metric_t	*metric_data = NULL;
	struct d_tm_shmem_hdr	*shmem_root;

	if (ctx == NULL || val == NULL || node == NULL)
		return -DER_INVAL;

	shmem_root = ctx->shmem_root;

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->dtn_type != D_TM_COUNTER)
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(ctx, node->dtn_metric);
	if (metric_data != NULL) {
		if (node->dtn_protect)
			D_MUTEX_LOCK(&node->dtn_lock);
		*val = metric_data->dtm_data.value;
		if (node->dtn_protect)
			D_MUTEX_UNLOCK(&node->dtn_lock);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return DER_SUCCESS;
}

/**
 * Client function to read the specified timestamp.
 *
 * \param[in]	ctx	Client context
 * \param[out]	val	The value of the timestamp is stored here
 * \param[in]	node	Pointer to the stored metric node
 *
 * \return		DER_SUCCESS		Success
 *			-DER_INVAL		Invalid input
 *			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a timestamp
 */
int
d_tm_get_timestamp(struct d_tm_context *ctx, time_t *val,
		   struct d_tm_node_t *node)
{
	struct d_tm_metric_t	*metric_data = NULL;
	struct d_tm_shmem_hdr	*shmem_root;

	if (ctx == NULL || val == NULL || node == NULL)
		return -DER_INVAL;

	shmem_root = ctx->shmem_root;

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->dtn_type != D_TM_TIMESTAMP)
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(ctx, node->dtn_metric);
	if (metric_data != NULL) {
		d_tm_node_lock(node);
		*val = metric_data->dtm_data.value;
		d_tm_node_unlock(node);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return DER_SUCCESS;
}

/**
 * Client function to read the specified high resolution timer.
 *
 * \param[in]	ctx	Client context
 * \param[out]	tms	The value of the timer is stored here
 * \param[in]	node	Pointer to the stored metric node
 *
 * \return	DER_SUCCESS		Success
 *		-DER_INVAL		Invalid input
 *		-DER_METRIC_NOT_FOUND	Metric not found
 *		-DER_OP_NOT_PERMITTED	Metric was not a high resolution
 *					timer
 */
int
d_tm_get_timer_snapshot(struct d_tm_context *ctx, struct timespec *tms,
			struct d_tm_node_t *node)
{
	struct d_tm_metric_t	*metric_data = NULL;
	struct d_tm_shmem_hdr	*shmem_root;

	if (ctx == NULL || tms == NULL || node == NULL)
		return -DER_INVAL;

	shmem_root = ctx->shmem_root;

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (!(node->dtn_type & D_TM_TIMER_SNAPSHOT))
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(ctx, node->dtn_metric);
	if (metric_data != NULL) {
		d_tm_node_lock(node);
		tms->tv_sec = metric_data->dtm_data.tms[0].tv_sec;
		tms->tv_nsec = metric_data->dtm_data.tms[0].tv_nsec;
		d_tm_node_unlock(node);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return DER_SUCCESS;
}

/**
 * Client function to read the specified duration.  A pointer for \a stats
 * is optional.
 *
 * The computation of mean and standard deviation are completed upon this
 * read operation.
 *
 * \param[in]	ctx	Client context
 * \param[out]	tms	The value of the duration is stored here
 * \param[out]	stats	The statistics are stored here
 * \param[in]	node	Pointer to the stored metric node
 *
 * \return	DER_SUCCESS		Success
 *		-DER_INVAL		Invalid input
 *		-DER_METRIC_NOT_FOUND	Metric not found
 *		-DER_OP_NOT_PERMITTED	Metric was not a duration
 */
int
d_tm_get_duration(struct d_tm_context *ctx, struct timespec *tms,
		  struct d_tm_stats_t *stats,
		  struct d_tm_node_t *node)
{
	struct d_tm_metric_t	*metric_data = NULL;
	struct d_tm_stats_t	*dtm_stats = NULL;
	double			sum = 0;
	struct d_tm_shmem_hdr	*shmem_root;

	if (ctx == NULL || tms == NULL || node == NULL)
		return -DER_INVAL;

	shmem_root = ctx->shmem_root;

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (!(node->dtn_type & D_TM_DURATION))
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(ctx, node->dtn_metric);
	if (metric_data != NULL) {
		dtm_stats = d_tm_conv_ptr(ctx, metric_data->dtm_stats);
		d_tm_node_lock(node);
		tms->tv_sec = metric_data->dtm_data.tms[0].tv_sec;
		tms->tv_nsec = metric_data->dtm_data.tms[0].tv_nsec;
		if ((stats != NULL) && (dtm_stats != NULL)) {
			stats->dtm_min = dtm_stats->dtm_min;
			stats->dtm_max = dtm_stats->dtm_max;
			stats->dtm_sum = dtm_stats->dtm_sum;
			if (dtm_stats->sample_size > 0) {
				sum = (double)dtm_stats->dtm_sum;
				stats->mean = sum / dtm_stats->sample_size;
			}
			stats->std_dev = d_tm_compute_standard_dev(
						      dtm_stats->sum_of_squares,
						      dtm_stats->sample_size,
						      stats->mean);
			stats->sum_of_squares = dtm_stats->sum_of_squares;
			stats->sample_size = dtm_stats->sample_size;
		}
		d_tm_node_unlock(node);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return DER_SUCCESS;
}

/**
 * Client function to read the specified gauge.  A pointer for \a stats
 * is optional.
 *
 * The computation of mean and standard deviation are completed upon this
 * read operation.
 *
 * \param[in]	ctx	Client context
 * \param[out]	val	The value of the gauge is stored here
 * \param[out]	stats	The statistics are stored here
 * \param[in]	node	Pointer to the stored metric node
 *
 * \return	DER_SUCCESS		Success
 *		-DER_INVAL		Invalid input
 *		-DER_METRIC_NOT_FOUND	Metric not found
 *		-DER_OP_NOT_PERMITTED	Metric was not a gauge
 */

int
d_tm_get_gauge(struct d_tm_context *ctx, uint64_t *val,
	       struct d_tm_stats_t *stats, struct d_tm_node_t *node)
{
	struct d_tm_metric_t	*metric_data = NULL;
	struct d_tm_stats_t	*dtm_stats = NULL;
	double			sum = 0;
	struct d_tm_shmem_hdr	*shmem_root;

	if (ctx == NULL || val == NULL || node == NULL)
		return -DER_INVAL;

	shmem_root = ctx->shmem_root;

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->dtn_type != D_TM_GAUGE)
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(ctx, node->dtn_metric);
	if (metric_data != NULL) {
		dtm_stats = d_tm_conv_ptr(ctx, metric_data->dtm_stats);
		d_tm_node_lock(node);
		*val = metric_data->dtm_data.value;
		if ((stats != NULL) && (dtm_stats != NULL)) {
			stats->dtm_min = dtm_stats->dtm_min;
			stats->dtm_max = dtm_stats->dtm_max;
			stats->dtm_sum = dtm_stats->dtm_sum;
			if (dtm_stats->sample_size > 0) {
				sum = (double)dtm_stats->dtm_sum;
				stats->mean = sum / dtm_stats->sample_size;
			}
			stats->std_dev = d_tm_compute_standard_dev(
						      dtm_stats->sum_of_squares,
						      dtm_stats->sample_size,
						      stats->mean);
			stats->sum_of_squares = dtm_stats->sum_of_squares;
			stats->sample_size = dtm_stats->sample_size;
		}
		d_tm_node_unlock(node);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return DER_SUCCESS;
}

/**
 * Client function to read the metadata for the specified metric.
 * Memory is allocated for the \a desc and \a units and should be freed by the
 * caller.
 *
 * \param[in]	ctx	Client context
 * \param[out]	desc	Memory is allocated and the
 *			description is copied here
 * \param[out]	units	Memory is allocated and the unit
 *			description is copied here
 * \param[in]	node	Pointer to the stored metric node
 *
 * \return	DER_SUCCESS		Success
 *		-DER_INVAL		Invalid input
 *		-DER_METRIC_NOT_FOUND	Metric node not found
 *		-DER_OP_NOT_PERMITTED	Node is not a metric
 */
int d_tm_get_metadata(struct d_tm_context *ctx, char **desc, char **units,
		      struct d_tm_node_t *node)
{
	struct d_tm_metric_t	*metric_data = NULL;
	char			*desc_str;
	char			*units_str;
	struct d_tm_shmem_hdr	*shmem_root;

	if (ctx == NULL || node == NULL)
		return -DER_INVAL;

	if ((desc == NULL) && (units == NULL))
		return -DER_INVAL;

	if (desc != NULL)
		*desc = NULL;

	if (units != NULL)
		*units = NULL;

	shmem_root = ctx->shmem_root;

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->dtn_type == D_TM_DIRECTORY)
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(ctx, node->dtn_metric);
	if (metric_data != NULL) {
		d_tm_node_lock(node);
		desc_str = d_tm_conv_ptr(ctx, metric_data->dtm_desc);
		if ((desc != NULL) && (desc_str != NULL))
			D_STRNDUP(*desc, desc_str, D_TM_MAX_DESC_LEN);
		units_str = d_tm_conv_ptr(ctx, metric_data->dtm_units);
		if ((units != NULL) && (units_str != NULL))
			D_STRNDUP(*units, units_str, D_TM_MAX_UNIT_LEN);
		d_tm_node_unlock(node);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return DER_SUCCESS;
}

/**
 * Returns the API version
 *
 * \return	D_TM_VERSION	The API version the caller is using
 */
int
d_tm_get_version(void)
{
	/*
	 * TODO store the D_TM_VERSION the producer used
	 * so that the consumer can retrieve that and compare.
	 */
	return D_TM_VERSION;
}

/**
 * Perform a recursive directory listing from the given \a node for the items
 * described by the \a d_tm_type bitmask.  A result is added to the list if it
 * matches one of the metric types specified by that filter mask. The mask may
 * be a combination of d_tm_metric_types.  Creates a linked list that points to
 * each node found that matches the search criteria.  Adds elements to an
 * existing node list if head is already initialized. The client should free the
 * memory with d_tm_list_free().
 *
 * \param[in]		ctx		Telemetry context
 * \param[in,out]	head		Pointer to a nodelist
 * \param[in]		node		The recursive directory listing starts
 *					from this node.
 * \param[in]		d_tm_type	A bitmask of d_tm_metric_types that
 *					filters the results.
 *
 * \return		DER_SUCCESS		Success
 *			-DER_NOMEM		Out of global heap
 *			-DER_INVAL		Invalid pointer for \a head or
 *						\a node
 */
int
d_tm_list(struct d_tm_context *ctx, struct d_tm_nodeList_t **head,
	  struct d_tm_node_t *node, int d_tm_type)
{
	int rc = DER_SUCCESS;

	if ((head == NULL) || (node == NULL)) {
		rc = -DER_INVAL;
		goto out;
	}

	if (d_tm_type & node->dtn_type) {
		rc = d_tm_list_add_node(node, head);
		if (rc != DER_SUCCESS)
			goto out;
	}

	node = node->dtn_child;
	if (node == NULL)
		goto out;

	node = d_tm_conv_ptr(ctx, node);
	if (node == NULL) {
		rc = -DER_INVAL;
		goto out;
	}

	rc = d_tm_list(ctx, head, node, d_tm_type);
	if (rc != DER_SUCCESS)
		goto out;

	node = node->dtn_sibling;
	if (node == NULL)
		return rc;

	node = d_tm_conv_ptr(ctx, node);
	while (node != NULL) {
		rc = d_tm_list(ctx, head, node, d_tm_type);
		if (rc != DER_SUCCESS)
			goto out;
		node = node->dtn_sibling;
		node = d_tm_conv_ptr(ctx, node);
	}

out:
	return rc;
}

/**
 * Frees the memory allocated for the given \a nodeList
 * that was allocated by d_tm_list()
 *
 * \param[in]	nodeList	The nodeList to free
 */
void
d_tm_list_free(struct d_tm_nodeList_t *nodeList)
{
	struct d_tm_nodeList_t	*head = NULL;

	while (nodeList) {
		head = nodeList->dtnl_next;
		D_FREE_PTR(nodeList);
		nodeList = head;
	}
}

/**
 * Adds a node to an existing nodeList, or creates it if the list is empty.
 *
 * \param[in]		src		The src node to add
 * \param[in,out]	nodelist	The nodelist to add \a src to
 *
 * \return		DER_SUCCESS	Success
 *			-DER_NOMEM	Out of global heap
 *			-DER_INVAL	Invalid pointer for \a head or
 *					\a node
 */
int
d_tm_list_add_node(struct d_tm_node_t *src, struct d_tm_nodeList_t **nodelist)
{
	struct d_tm_nodeList_t	*list = NULL;

	if (nodelist == NULL)
		return -DER_INVAL;

	if (*nodelist == NULL) {
		D_ALLOC_PTR(*nodelist);
		if (*nodelist) {
			(*nodelist)->dtnl_node = src;
			(*nodelist)->dtnl_next = NULL;
			return DER_SUCCESS;
		}
		return -DER_NOMEM;
	}

	list = *nodelist;

	/** advance to the last node in the list */
	while (list->dtnl_next)
		list = list->dtnl_next;

	D_ALLOC_PTR(list->dtnl_next);
	if (list->dtnl_next) {
		list = list->dtnl_next;
		list->dtnl_node = src;
		list->dtnl_next = NULL;
		return DER_SUCCESS;
	}
	return -DER_NOMEM;
}

/** create a unique key for this instance */
static key_t
d_tm_get_key(int srv_idx)
{
	return D_TM_SHARED_MEMORY_KEY + srv_idx;
}

/**
 * Server side function that allocates the shared memory segment for this
 * server instance
 *
 * \param[in]	srv_idx		A unique value that identifies the producer
 *				process
 * \param[in]	mem_size	Size in bytes of the shared memory region
 *
 * \return			Address of the shared memory region
 *				NULL if failure
 */
static struct d_tm_shmem_hdr *
allocate_shared_memory(int srv_idx, size_t mem_size)
{
	void			*addr;
	key_t			key;
	int			shmid;
	struct d_tm_shmem_hdr	*header;

	key = d_tm_get_key(srv_idx);
	shmid = shmget(key, mem_size, IPC_CREAT | 0660);
	if (shmid < 0) {
		D_ERROR("Unable to allocate shared memory.  shmget failed, "
			"%s\n", strerror(errno));
		return NULL;
	}

	addr = shmat(shmid, NULL, 0);
	if (addr == (void *)-1) {
		D_ERROR("Unable to allocate shared memory.  shmat failed, "
			"%s\n", strerror(errno));
		return NULL;
	}

	header = (struct d_tm_shmem_hdr *)addr;
	/**
	 * Store the base address of the shared memory as seen by the
	 * server.
	 * Used by the client to adjust pointers in the shared memory
	 * to its own address space.
	 */
	header->sh_base_addr = (uint64_t)addr;
	header->sh_id = (uint32_t)shmid;
	header->sh_bytes_total = mem_size;
	header->sh_bytes_free = mem_size - sizeof(struct d_tm_shmem_hdr);
	header->sh_free_addr = addr + sizeof(struct d_tm_shmem_hdr);
	return header;
}

/**
 * Opens the given telemetry memory region for reading. Returns a context for
 * this session that must be closed by the caller.
 *
 * \param[in]	id	A unique value that identifies the telemetry region
 *
 * \return		New context, or NULL if failure
 */
struct d_tm_context *
d_tm_open(int id)
{
	struct d_tm_context	*new_ctx;
	struct d_tm_shmem_hdr	*addr;
	key_t			key;
	int			shmid;
	int			rc;

	key = d_tm_get_key(id);
	shmid = shmget(key, 0, 0);
	if (shmid < 0) {
		D_ERROR("Unable to access shared memory.  shmget failed, "
			"%s\n", strerror(errno));
		return NULL;
	}

	addr = shmat(shmid, NULL, 0);
	if (addr == (void *)-1) {
		D_ERROR("Unable to access shared memory.  shmat failed, "
			"%s\n", strerror(errno));
		return NULL;
	}

	rc = alloc_ctx(&new_ctx, addr);
	if (rc != 0)
		return NULL;

	return new_ctx;
}

/**
 * Detaches from a telemetry memory region and frees the context.
 *
 * \param[in]	ctx	Context to be freed
 */
void
d_tm_close(struct d_tm_context **ctx)
{
	int			 rc;
	struct d_tm_shmem_hdr	*shmem_root;

	if (ctx == NULL || *ctx == NULL)
		return;

	shmem_root = (*ctx)->shmem_root;

	if (shmem_root == NULL)
		goto out;

	rc = shmdt((void *)shmem_root);
	if (rc < 0)
		D_ERROR("Unable to detach from shared memory segment.  "
			"shmdt failed, %s.\n", strerror(errno));
out:
	D_FREE(*ctx);
}

/**
 * Allocates memory from within the shared memory pool with 64-bit alignment
 * Clears the allocated buffer.
 *
 * param[in]	shmem	The shmem pool in which to alloc
 * param[in]	length	Size in bytes of the region within the shared memory
 *			pool to allocate
 *
 * \return		Address of the allocated memory
 *			NULL if there was no more memory available
 */
static void *
d_tm_shmalloc(struct d_tm_shmem_hdr *shmem, int length)
{
	if (shmem == NULL || length == 0)
		return NULL;

	if (length % sizeof(uint64_t) != 0) {
		length += sizeof(uint64_t);
		length &= ~(sizeof(uint64_t) - 1);
	}

	if (shmem->sh_bytes_free > 0 && length <= shmem->sh_bytes_free) {
		void *new_mem = shmem->sh_free_addr;

		shmem->sh_bytes_free -= length;
		shmem->sh_free_addr += length;
		D_DEBUG(DB_TRACE,
			"Allocated %d bytes.  Now %" PRIu64 " remain\n",
			length, shmem->sh_bytes_free);
		memset(new_mem, 0, length);
		return new_mem;
	}
	D_CRIT("Shared memory allocation failure!\n");
	return NULL;
}

/**
 * Validates that the pointer resides within the address space
 * of the client's shared memory region.
 *
 * \param[in]	shmem_root	Pointer to the shared memory segment
 * \param[in]	ptr		The pointer to validate
 *
 * \return	true		The pointer is valid
 *		false		The the pointer is invalid
 */
bool
d_tm_validate_shmem_ptr(struct d_tm_shmem_hdr *shmem_root, void *ptr)
{
	uint64_t shmem_max_addr;

	shmem_max_addr = (uint64_t)shmem_root + shmem_root->sh_bytes_total;
	if (((uint64_t)ptr < (uint64_t)shmem_root) ||
	    ((uint64_t)ptr >= shmem_max_addr)) {
		D_DEBUG(DB_TRACE,
			"shmem ptr 0x%" PRIx64 " was outside the shmem range "
			"0x%" PRIx64 " to 0x%" PRIx64 "\n", (uint64_t)ptr,
			(uint64_t)shmem_root, shmem_max_addr);
		return false;
	}
	return true;
}

/**
 * Convert the virtual address of the pointer in shared memory from a server
 * address to a client side virtual address.
 *
 * \param[in]	ctx	Telemetry context
 * \param[in]	ptr	The pointer to convert
 *
 * \return		A pointer to the item in the clients address
 *			space
 *			NULL if the pointer is invalid
 */
void *
d_tm_conv_ptr(struct d_tm_context *ctx, void *ptr)
{
	void			*temp;
	struct d_tm_shmem_hdr	*shmem_root;

	if (ptr == NULL || ctx == NULL || ctx->shmem_root == NULL)
		return NULL;

	shmem_root = ctx->shmem_root;

	temp = (void *)((uint64_t)shmem_root + (uint64_t)ptr -
			shmem_root->sh_base_addr);

	if (d_tm_validate_shmem_ptr(shmem_root, temp))
		return temp;
	return NULL;
}
