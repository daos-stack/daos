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

/**
 * These globals are used for all data producers sharing the same process space
 */

/** Points to the root directory node */
struct d_tm_node_t	*d_tm_root;

/** Protects d_tm_add_metric operations */
pthread_mutex_t		d_tm_add_lock;

/** Points to the base address of the shared memory segment */
uint64_t		*d_tm_shmem_root;

/** Tracks amount of shared memory bytes available for allocation */
uint64_t		d_tm_shmem_free;

/** Points to the base address of the free shared memory */
uint8_t			*d_tm_shmem_idx;

/** Shared memory ID for the segment of shared memory created by the producer */
int			d_tm_shmid;

/** Enables metric read/write serialization */
bool			d_tm_serialization;

/** Enables shared memory retention on process exit */
bool			d_tm_retain_shmem;

/**
 * Returns a pointer to the root node for the given shared memory segment
 *
 * \param[in]	shmem	Shared memory segment
 *
 * \return		Pointer to the root node
 */
struct d_tm_node_t *
d_tm_get_root(uint64_t *shmem)
{
	if (shmem != NULL)
		return (struct d_tm_node_t *)(shmem + 1);
	else
		return NULL;
}

/**
 * Search for a \a parent's child with the given \a name.
 * Return a pointer to the child if found.
 *
 * \param[in]	shmem_root	Pointer to the shared memory segment
 * \param[in]	parent		The parent node
 * \param[in]	name		The name of the child to find
 *
 * \return			Pointer to the child node
 *				NULL if not found
 */
struct d_tm_node_t *
d_tm_find_child(uint64_t *shmem_root, struct d_tm_node_t *parent, char *name)
{
	struct d_tm_node_t	*child = NULL;
	char			*client_name;

	if (parent == NULL)
		return NULL;

	if (parent->dtn_child == NULL)
		return NULL;

	child = d_tm_conv_ptr(shmem_root, parent->dtn_child);

	client_name = NULL;
	if (child != NULL)
		client_name = d_tm_conv_ptr(shmem_root, child->dtn_name);

	while ((child != NULL) && (client_name != NULL) &&
	       strncmp(client_name, name, D_TM_MAX_NAME_LEN) != 0) {
		child = d_tm_conv_ptr(shmem_root, child->dtn_sibling);
		client_name = NULL;
		if (child == NULL)
			break;
		client_name = d_tm_conv_ptr(shmem_root, child->dtn_name);
	}

	return child;
}

/**
 * Allocate a \a newnode and initialize its \a name.
 *
 * \param[in,out]	newnode	A pointer for the new node
 * \param[in]		name	The name of the new node
 *
 * \return		DER_SUCCESS		Success
 *			-DER_NO_SHMEM		No shared memory available
 *			-DER_EXCEEDS_PATH_LEN	The full name length is
 *						too long
 *			-DER_INVAL		bad pointers given
 */
int
d_tm_alloc_node(struct d_tm_node_t **newnode, char *name)
{
	struct d_tm_node_t	*node = NULL;
	int			buff_len = 0;
	int			rc = DER_SUCCESS;

	if ((newnode == NULL) || (name == NULL)) {
		rc = -DER_INVAL;
		goto failure;
	}

	node = d_tm_shmalloc(sizeof(struct d_tm_node_t));
	if (node == NULL) {
		rc = -DER_NO_SHMEM;
		goto failure;
	}
	buff_len = strnlen(name, D_TM_MAX_NAME_LEN);
	if (buff_len == D_TM_MAX_NAME_LEN) {
		rc = DER_EXCEEDS_PATH_LEN;
		goto failure;
	}
	buff_len += 1; /* make room for the trailing null */
	node->dtn_name = d_tm_shmalloc(buff_len);
	if (node->dtn_name == NULL) {
		rc = -DER_NO_SHMEM;
		goto failure;
	}
	strncpy(node->dtn_name, name, buff_len);
	node->dtn_child = NULL;
	node->dtn_sibling = NULL;
	node->dtn_metric = NULL;
	node->dtn_type = D_TM_DIRECTORY;
	*newnode = node;
	return rc;

failure:
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
int
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
	rc = d_tm_alloc_node(&node, name);
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
	uint64_t	*base_addr = NULL;
	char		tmp[D_TM_MAX_NAME_LEN];
	int		rc = DER_SUCCESS;

	if ((d_tm_shmem_root != NULL) && (d_tm_root != NULL)) {
		D_INFO("d_tm_init already completed for id %d\n", id);
		return rc;
	}

	if ((flags & ~(D_TM_SERIALIZATION | D_TM_RETAIN_SHMEM)) != 0) {
		rc = -DER_INVAL;
		goto failure;
	}

	if (flags & D_TM_SERIALIZATION) {
		d_tm_serialization = true;
		D_INFO("Serialization enabled for id %d\n", id);
	}

	if (flags & D_TM_RETAIN_SHMEM) {
		d_tm_retain_shmem = true;
		D_INFO("Retaining shared memory for id %d\n", id);
	}

	d_tm_shmem_root = d_tm_allocate_shared_memory(id, mem_size);

	if (d_tm_shmem_root == NULL) {
		rc = -DER_SHMEM_PERMS;
		goto failure;
	}

	d_tm_shmem_idx = (uint8_t *)d_tm_shmem_root;
	d_tm_shmem_free = mem_size;
	D_DEBUG(DB_TRACE, "Shared memory allocation success!\n"
		"Memory size is %" PRIu64 " bytes at address 0x%" PRIx64
		"\n", mem_size, (uint64_t)d_tm_shmem_root);
	/**
	 * Store the base address of the shared memory as seen by the
	 * server in this first uint64_t sized slot.
	 * Used by the client to adjust pointers in the shared memory
	 * to its own address space.
	 */
	base_addr = d_tm_shmalloc(sizeof(uint64_t));
	if (base_addr == NULL) {
		rc = -DER_NO_SHMEM;
		goto failure;
	}
	*base_addr = (uint64_t)d_tm_shmem_root;

	snprintf(tmp, sizeof(tmp), "ID: %d", id);
	rc = d_tm_alloc_node(&d_tm_root, tmp);
	if (rc != DER_SUCCESS)
		goto failure;

	rc = D_MUTEX_INIT(&d_tm_add_lock, NULL);
	if (rc != 0) {
		D_ERROR("Mutex init failure: " DF_RC "\n", DP_RC(rc));
		goto failure;
	}

	D_INFO("Telemetry and metrics initialized for ID %u\n", id);

	return rc;

failure:
	D_ERROR("Failed to initialize telemetry and metrics for ID %u: "
		DF_RC "\n", id, DP_RC(rc));
	return rc;
}

/**
 * Releases resources claimed by init
 */
void d_tm_fini(void)
{
	int	rc = 0;

	if (d_tm_shmem_root == NULL)
		return;

	rc = shmdt(d_tm_shmem_root);
	if (rc < 0)
		D_ERROR("Unable to detach from shared memory segment.  "
			"shmdt failed, %s.\n", strerror(errno));

	if ((rc == 0) && !d_tm_retain_shmem) {
		rc = shmctl(d_tm_shmid, IPC_RMID, NULL);
		if (rc < 0)
			D_ERROR("Unable to remove shared memory segment.  "
				"shmctl failed, %s.\n", strerror(errno));
	}

	d_tm_serialization = false;
	d_tm_retain_shmem = false;
	d_tm_shmem_root = NULL;
	d_tm_root = NULL;
	d_tm_shmem_idx = NULL;
	d_tm_shmid = 0;
}

/**
 * Recursively free resources underneath the given node.
 *
 * \param[in]	shmem_root	Pointer to the shared memory segment
 * \param[in]	node		Pointer to the node containing the resources
 *				to free
 */
void
d_tm_free_node(uint64_t *shmem_root, struct d_tm_node_t *node)
{
	char	*name;
	int	rc = 0;

	if (node == NULL)
		return;

	if (!d_tm_serialization)
		return;

	if (node->dtn_type != D_TM_DIRECTORY) {
		rc = D_MUTEX_DESTROY(&node->dtn_lock);
		if (rc != 0) {
			name = d_tm_conv_ptr(shmem_root, node->dtn_name);
			D_ERROR("Failed to destroy mutex for node [%s]: "
				DF_RC "\n", name, DP_RC(rc));
			return;
		}
	}

	node = node->dtn_child;
	node = d_tm_conv_ptr(shmem_root, node);
	if (node == NULL)
		return;

	d_tm_free_node(shmem_root, node);
	node = node->dtn_sibling;
	node = d_tm_conv_ptr(shmem_root, node);
	while (node != NULL) {
		d_tm_free_node(shmem_root, node);
		node = node->dtn_sibling;
		node = d_tm_conv_ptr(shmem_root, node);
	}
}

/**
 * Prints the counter \a val with \a name to the \a stream provided
 *
 * \param[in]	val	Counter value
 * \param[in]	name	Counter name
 * \param[in]	stream	Output stream (stdout, stderr)
 */
void
d_tm_print_counter(uint64_t val, char *name, FILE *stream)
{
	if ((name == NULL) || (stream == NULL))
		return;

	fprintf(stream, "Counter: %s = %" PRIu64 "\n", name, val);
}

/**
 * Prints the timestamp \a clk with \a name to the \a stream provided
 *
 * \param[in]	clk	Timestamp value
 * \param[in]	name	Timestamp name
 * \param[in]	stream	Output stream (stdout, stderr)
 */
void
d_tm_print_timestamp(time_t *clk, char *name, FILE *stream)
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
	fprintf(stream, "Timestamp: %s: %s\n", name, temp);
}

/**
 * Prints the time snapshot \a tms with \a name to the \a stream provided
 *
 * \param[in]	tms	Timer value
 * \param[in]	name	Timer name
 * \param[in]	tm_type	Timer type
 * \param[in]	stream	Output stream (stdout, stderr)
 */
void
d_tm_print_timer_snapshot(struct timespec *tms, char *name, int tm_type,
			  FILE *stream)
{
	if ((tms == NULL) || (name == NULL) || (stream == NULL))
		return;

	switch (tm_type) {
	case D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_REALTIME:
		fprintf(stream, "Timer snapshot (realtime): %s = %lds, "
			"%ldns\n", name, tms->tv_sec, tms->tv_nsec);
		break;
	case D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_PROCESS_CPUTIME:
		fprintf(stream, "Timer snapshot (process): %s = %lds, "
			"%ldns\n", name, tms->tv_sec, tms->tv_nsec);
		break;
	case D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_THREAD_CPUTIME:
		fprintf(stream, "Timer snapshot (thread): %s = %lds, "
			"%ldns\n", name, tms->tv_sec, tms->tv_nsec);
		break;
	default:
		fprintf(stream, "Invalid timer snapshot type: 0x%x\n",
			tm_type & ~D_TM_TIMER_SNAPSHOT);
		break;
	}
}

/**
 * Prints the duration \a tms with \a stats and \a name to the \a stream
 * provided
 *
 * \param[in]	tms	Duration timer value
 * \param[in]	stats	Optional stats
 * \param[in]	name	Duration timer name
 * \param[in]	tm_type	Duration timer type
 * \param[in]	stream	Output stream (stdout, stderr)
 */
void
d_tm_print_duration(struct timespec *tms, struct d_tm_stats_t *stats,
		    char *name, int tm_type, FILE *stream)
{
	bool printStats;

	if ((tms == NULL) || (name == NULL) || (stream == NULL))
		return;

	printStats = (stats != NULL) && (stats->sample_size > 0);

	switch (tm_type) {
	case D_TM_DURATION | D_TM_CLOCK_REALTIME:
		fprintf(stream, "Duration (realtime): %s = %.9fs",
			name, tms->tv_sec + tms->tv_nsec / 1e9);
		break;
	case D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME:
		fprintf(stream, "Duration (process): %s = %.9fs",
			name, tms->tv_sec + tms->tv_nsec / 1e9);
		break;
	case D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME:
		fprintf(stream, "Duration (thread): %s = %.9fs",
			name, tms->tv_sec + tms->tv_nsec / 1e9);
		break;
	default:
		fprintf(stream, "Invalid timer duration type: 0x%x",
			tm_type & ~D_TM_DURATION);
		printStats = false;
		break;
	}

	if (printStats)
		D_TM_PRINT_STATS(stream, stats, float, lf);

	fprintf(stream, "\n");
}

/**
 * Prints the gauge \a val and \a stats with \a name to the \a stream provided
 *
 * \param[in]	tms	Timer value
 * \param[in]	stats	Optional statistics
 * \param[in]	name	Timer name
 * \param[in]	stream	Output stream (stdout, stderr)
 */
void
d_tm_print_gauge(uint64_t val, struct d_tm_stats_t *stats, char *name,
		 FILE *stream)
{
	if ((name == NULL) || (stream == NULL))
		return;

	fprintf(stream, "Gauge: %s = %" PRIu64, name, val);

	if ((stats != NULL) && (stats->sample_size > 0))
		D_TM_PRINT_STATS(stream, stats, int, lu);

	fprintf(stream, "\n");
}

/**
 * Prints a single \a node.
 * Used as a convenience function to demonstrate usage for the client
 *
 * \param[in]	shmem_root	Pointer to the shared memory segment
 * \param[in]	node		Pointer to a parent or child node
 * \param[in]	level		Indicates level of indentation when printing
 *				this \a node
 * \param[in]	stream		Direct output to this stream (stdout, stderr)
 */
void
d_tm_print_node(uint64_t *shmem_root, struct d_tm_node_t *node, int level,
		FILE *stream)
{
	struct d_tm_stats_t	stats = {0};
	struct timespec		tms;
	uint64_t		val;
	time_t			clk;
	char			*name = NULL;
	int			i = 0;
	int			rc;

	name = d_tm_conv_ptr(shmem_root, node->dtn_name);
	if (name == NULL)
		return;

	for (i = 0; i < level; i++)
		fprintf(stream, "%20s", " ");

	switch (node->dtn_type) {
	case D_TM_DIRECTORY:
		fprintf(stream, "%-20s\n", name);
		break;
	case D_TM_COUNTER:
		rc = d_tm_get_counter(&val, shmem_root, node, NULL);
		if (rc != DER_SUCCESS) {
			fprintf(stream, "Error on counter read: %d\n", rc);
			break;
		}
		d_tm_print_counter(val, name, stream);
		break;
	case D_TM_TIMESTAMP:
		rc = d_tm_get_timestamp(&clk, shmem_root, node, NULL);
		if (rc != DER_SUCCESS) {
			fprintf(stream, "Error on timestamp read: %d\n", rc);
			break;
		}
		d_tm_print_timestamp(&clk, name, stream);
		break;
	case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_REALTIME):
	case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_PROCESS_CPUTIME):
	case (D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_THREAD_CPUTIME):
		rc = d_tm_get_timer_snapshot(&tms, shmem_root, node, NULL);
		if (rc != DER_SUCCESS) {
			fprintf(stream, "Error on highres timer read: %d\n",
				rc);
			break;
		}
		d_tm_print_timer_snapshot(&tms, name, node->dtn_type, stream);
		break;
	case (D_TM_DURATION | D_TM_CLOCK_REALTIME):
	case (D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME):
	case (D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME):
		rc = d_tm_get_duration(&tms, &stats, shmem_root, node, NULL);
		if (rc != DER_SUCCESS) {
			fprintf(stream, "Error on duration read: %d\n", rc);
			break;
		}
		d_tm_print_duration(&tms, &stats, name, node->dtn_type,
				    stream);
		break;
	case D_TM_GAUGE:
		rc = d_tm_get_gauge(&val, &stats, shmem_root, node, NULL);
		if (rc != DER_SUCCESS) {
			fprintf(stream, "Error on gauge read: %d\n", rc);
			break;
		}
		d_tm_print_gauge(val, &stats, name, stream);
		break;
	default:
		fprintf(stream, "Item: %s has unknown type: 0x%x\n", name,
			node->dtn_type);
		break;
	}
}

/**
 * Recursively prints all nodes underneath the given \a node.
 * Used as a convenience function to demonstrate usage for the client
 *
 * \param[in]	shmem_root	Pointer to the shared memory segment
 * \param[in]	node		Pointer to a parent or child node
 * \param[in]	level		Indicates level of indentation when printing
 *				this \a node
 * \param[in]	stream		Direct output to this stream (stdout, stderr)
 */
void
d_tm_print_my_children(uint64_t *shmem_root, struct d_tm_node_t *node,
		       int level, FILE *stream)
{
	if ((node == NULL) || (stream == NULL))
		return;

	d_tm_print_node(shmem_root, node, level, stream);

	node = node->dtn_child;
	node = d_tm_conv_ptr(shmem_root, node);
	if (node == NULL)
		return;

	d_tm_print_my_children(shmem_root, node, level + 1, stream);
	node = node->dtn_sibling;
	node = d_tm_conv_ptr(shmem_root, node);
	while (node != NULL) {
		d_tm_print_my_children(shmem_root, node, level + 1, stream);
		node = node->dtn_sibling;
		node = d_tm_conv_ptr(shmem_root, node);
	}
}

/**
 * Recursively counts number of metrics at and underneath the given \a node.
 *
 * \param[in]	shmem_root	Pointer to the shared memory segment
 * \param[in]	node		Pointer to a parent or child node
 * \param[in]	d_tm_type	A bitmask of d_tm_metric_types that
 *				determines if an item should be counted
 *
 * \return			Number of metrics found
 */
uint64_t
d_tm_count_metrics(uint64_t *shmem_root, struct d_tm_node_t *node,
		   int d_tm_type)
{
	uint64_t	count = 0;

	if (node == NULL)
		return 0;

	if (d_tm_type & node->dtn_type)
		count++;

	node = node->dtn_child;
	node = d_tm_conv_ptr(shmem_root, node);
	if (node == NULL)
		return count;

	count += d_tm_count_metrics(shmem_root, node, d_tm_type);
	node = node->dtn_sibling;
	node = d_tm_conv_ptr(shmem_root, node);
	while (node != NULL) {
		count += d_tm_count_metrics(shmem_root, node, d_tm_type);
		node = node->dtn_sibling;
		node = d_tm_conv_ptr(shmem_root, node);
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
 * Compute gauge statistics: sample size, min, max, sum and sum of squares.
 * Standard deviation calculation is deferred until the metric is read.
 *
 * \param[in]	node		Pointer to a gauge node
 */
void
d_tm_compute_gauge_stats(struct d_tm_node_t *node)
{
	struct d_tm_stats_t	*dtm_stats;
	uint64_t		value = 0;

	dtm_stats = node->dtn_metric->dtm_stats;

	if (dtm_stats == NULL)
		return;

	value = node->dtn_metric->dtm_data.value;
	dtm_stats->sample_size++;
	dtm_stats->dtm_sum.sum_int += value;
	dtm_stats->sum_of_squares += value * value;

	if (value > dtm_stats->dtm_max.max_int)
		dtm_stats->dtm_max.max_int = value;

	if (value < dtm_stats->dtm_min.min_int)
		dtm_stats->dtm_min.min_int = value;
}

/**
 * Compute duration statistics: sample size, min, max, sum and sum of squares.
 * Standard deviation calculation is deferred until the metric is read.
 *
 * \param[in]	node		Pointer to a duration node
 */
void
d_tm_compute_duration_stats(struct d_tm_node_t *node)
{
	struct d_tm_stats_t	*dtm_stats;
	double			value = 0;

	dtm_stats = node->dtn_metric->dtm_stats;

	if (dtm_stats == NULL)
		return;

	value = node->dtn_metric->dtm_data.tms[0].tv_sec +
		(node->dtn_metric->dtm_data.tms[0].tv_nsec / 1E9);

	dtm_stats->sample_size++;
	dtm_stats->dtm_sum.sum_float += value;
	dtm_stats->sum_of_squares += value * value;

	if (value > dtm_stats->dtm_max.max_float)
		dtm_stats->dtm_max.max_float = value;

	if (value < dtm_stats->dtm_min.min_float)
		dtm_stats->dtm_min.min_float = value;
}


/**
 * Increment the given counter by the specified \a value
 *
 * The counter is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 *
 * \param[in,out]	metric	Pointer to the metric
 * \param[in]		value	Increments the counter by this \a value
 * \param[in]		fmt	Format specifier for the name and full path of
 *				the metric followed by optional args to
 *				populate the string, printf style.
 *				The format specifier and optional arguments
 *				are used only if the pointer to the metric
 *				is NULL.
 *
 * \return			DER_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a counter
 *				-DER_UNINIT		API not initialized
 *				-DER_INVAL		\a metric and \a item
 *							are NULL
 */
int
d_tm_increment_counter(struct d_tm_node_t **metric, uint64_t value,
		       const char *fmt, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = DER_SUCCESS;

	if (d_tm_shmem_root == NULL)
		return -DER_UNINIT;

	if ((metric != NULL) && (*metric != NULL)) {
		node = *metric;
	} else {
		va_list	args;
		int	ret;

		if (fmt == NULL) {
			rc = -DER_INVAL;
			goto failure;
		}

		va_start(args, fmt);
		ret = vsnprintf(path, sizeof(path), fmt, args);
		va_end(args);

		if (ret <= 0 || ret >= D_TM_MAX_NAME_LEN) {
			rc = -DER_EXCEEDS_PATH_LEN;
			goto failure;
		}

		node = d_tm_find_metric(d_tm_shmem_root, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, D_TM_COUNTER, "N/A", "N/A", path);
		if (rc != DER_SUCCESS) {
			D_ERROR("Failed to add and incremement counter [%s]: "
				DF_RC "\n", path, DP_RC(rc));
			goto failure;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->dtn_type != D_TM_COUNTER) {
		rc = -DER_OP_NOT_PERMITTED;
		D_ERROR("Failed to increment counter [%s] on item not a "
			"counter.  Operation mismatch: " DF_RC "\n",
			node->dtn_name, DP_RC(rc));
		goto failure;
	}

	if (node->dtn_protect)
		D_MUTEX_LOCK(&node->dtn_lock);
	node->dtn_metric->dtm_data.value += value;
	if (node->dtn_protect)
		D_MUTEX_UNLOCK(&node->dtn_lock);

	return rc;

failure:
	return rc;
}

/**
 * Record the current timestamp
 *
 * The timestamp is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 *
 * \param[in,out]	metric	Pointer to the metric
 * \param[in]		fmt	Format specifier for the name and full path of
 *				the metric followed by optional args to
 *				populate the string, printf style.
 *				The format specifier and optional arguments
 *				are used only if the pointer to the metric
 *				is NULL.
 *
 * \return			DER_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a timestamp
 *				-DER_UNINIT		API not initialized
 *				-DER_INVAL		\a metric and \a item
 *							are NULL
 */
int
d_tm_record_timestamp(struct d_tm_node_t **metric, const char *fmt, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = DER_SUCCESS;

	if (d_tm_shmem_root == NULL)
		return -DER_UNINIT;

	if ((metric != NULL) && (*metric != NULL)) {
		node = *metric;
	} else {
		va_list	args;
		int	ret;

		if (fmt == NULL) {
			rc = -DER_INVAL;
			goto failure;
		}

		va_start(args, fmt);
		ret = vsnprintf(path, sizeof(path), fmt, args);
		va_end(args);

		if (ret <= 0 || ret >= D_TM_MAX_NAME_LEN) {
			rc = -DER_EXCEEDS_PATH_LEN;
			goto failure;
		}

		node = d_tm_find_metric(d_tm_shmem_root, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, D_TM_TIMESTAMP, "N/A", "N/A", path);
		if (rc != DER_SUCCESS) {
			D_ERROR("Failed to add and record timestamp [%s]: "
				DF_RC "\n", path, DP_RC(rc));
			goto failure;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->dtn_type != D_TM_TIMESTAMP) {
		rc = -DER_OP_NOT_PERMITTED;
		D_ERROR("Failed to record timestamp [%s] on item not a "
			"timestamp.  Operation mismatch: " DF_RC "\n", path,
			DP_RC(rc));
	}

	if (node->dtn_protect)
		D_MUTEX_LOCK(&node->dtn_lock);
	node->dtn_metric->dtm_data.value = (uint64_t)time(NULL);
	if (node->dtn_protect)
		D_MUTEX_UNLOCK(&node->dtn_lock);

	return rc;

failure:
	return rc;
}

/**
 * Read and store a high resolution timer snapshot value
 *
 * The timer is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 *
 * \param[in,out]	metric	Pointer to the metric
 * \param[in]		clk_id	A D_TM_CLOCK_* that identifies the clock type
 * \param[in]		fmt	Format specifier for the name and full path of
 *				the metric followed by optional args to
 *				populate the string, printf style.
 *				The format specifier and optional arguments
 *				are used only if the pointer to the metric
 *				is NULL.
 *
 * \return			DER_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a high resolution
 *							timer
 *				-DER_UNINIT		API not initialized
 *				-DER_INVAL		\a metric and \a item
 *							are NULL
 *							Invalid \a clk_id
 */
int
d_tm_take_timer_snapshot(struct d_tm_node_t **metric, int clk_id,
			 const char *fmt, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = DER_SUCCESS;

	if (d_tm_shmem_root == NULL)
		return -DER_UNINIT;

	if ((metric != NULL) && (*metric != NULL)) {
		node = *metric;
	} else {
		va_list	args;
		int	ret;

		if (fmt == NULL) {
			rc = -DER_INVAL;
			goto failure;
		}

		va_start(args, fmt);
		ret = vsnprintf(path, sizeof(path), fmt, args);
		va_end(args);

		if (ret <= 0 || ret >= D_TM_MAX_NAME_LEN) {
			rc = -DER_EXCEEDS_PATH_LEN;
			goto failure;
		}

		node = d_tm_find_metric(d_tm_shmem_root, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		if (!((clk_id == D_TM_CLOCK_REALTIME) ||
		      (clk_id == D_TM_CLOCK_PROCESS_CPUTIME) ||
		      (clk_id == D_TM_CLOCK_THREAD_CPUTIME))) {
			rc = -DER_INVAL;
			D_ERROR("Invalid clk_id for [%s] "
				"Failed to add metric: " DF_RC "\n", path,
				DP_RC(rc));
			goto failure;
		}
		rc = d_tm_add_metric(&node, D_TM_TIMER_SNAPSHOT | clk_id,
				     "N/A", "N/A", path);
		if (rc != DER_SUCCESS) {
			D_ERROR("Failed to add and record high resolution timer"
				" [%s]: " DF_RC "\n", path, DP_RC(rc));
			goto failure;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (!(node->dtn_type & D_TM_TIMER_SNAPSHOT)) {
		rc = -DER_OP_NOT_PERMITTED;
		D_ERROR("Failed to record high resolution timer [%s] on item "
			"not a high resolution timer.  Operation mismatch: "
			DF_RC "\n", path, DP_RC(rc));
		goto failure;
	}

	if (node->dtn_protect)
		D_MUTEX_LOCK(&node->dtn_lock);
	clock_gettime(d_tm_clock_id(node->dtn_type & ~D_TM_TIMER_SNAPSHOT),
		      &node->dtn_metric->dtm_data.tms[0]);
	if (node->dtn_protect)
		D_MUTEX_UNLOCK(&node->dtn_lock);

	return rc;

failure:
	return rc;
}

/**
 * Record the start of a time interval (paired with d_tm_mark_duration_end())
 *
 * The duration is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 *
 * \param[in,out]	metric	Pointer to the metric
 * \param[in]		clk_id	A D_TM_CLOCK_* that identifies the clock type
 * \param[in]		fmt	Format specifier for the name and full path of
 *				the metric followed by optional args to
 *				populate the string, printf style.
 *				The format specifier and optional arguments
 *				are used only if the pointer to the metric
 *				is NULL.
 *
 * \return			DER_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a duration
 *				-DER_UNINIT		API not initialized
 *				-DER_INVAL		\a metric and \a item
 *							are NULL
 *							Invalid \a clk_id
 */
int
d_tm_mark_duration_start(struct d_tm_node_t **metric, int clk_id,
			 const char *fmt, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = DER_SUCCESS;

	if (d_tm_shmem_root == NULL)
		return -DER_UNINIT;

	if ((metric != NULL) && (*metric != NULL)) {
		node = *metric;
	} else {
		va_list	args;
		int	ret;

		if (fmt == NULL) {
			rc = -DER_INVAL;
			goto failure;
		}

		va_start(args, fmt);
		ret = vsnprintf(path, sizeof(path), fmt, args);
		va_end(args);

		if (ret <= 0 || ret >= D_TM_MAX_NAME_LEN) {
			rc = -DER_EXCEEDS_PATH_LEN;
			goto failure;
		}

		node = d_tm_find_metric(d_tm_shmem_root, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		if (!((clk_id == D_TM_CLOCK_REALTIME) ||
		      (clk_id == D_TM_CLOCK_PROCESS_CPUTIME) ||
		      (clk_id == D_TM_CLOCK_THREAD_CPUTIME))) {
			rc = -DER_INVAL;
			D_ERROR("Invalid clk_id for [%s] "
				"Failed to add metric: " DF_RC "\n", path,
				DP_RC(rc));
			goto failure;
		}
		rc = d_tm_add_metric(&node, D_TM_DURATION | clk_id,
				     "N/A", "N/A", path);
		if (rc != DER_SUCCESS) {
			D_ERROR("Failed to add and mark duration start [%s]: "
				DF_RC "\n", path, DP_RC(rc));
			goto failure;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (!(node->dtn_type & D_TM_DURATION)) {
		rc = -DER_OP_NOT_PERMITTED;
		D_ERROR("Failed to mark duration start [%s] on item "
			"not a duration.  Operation mismatch: " DF_RC "\n",
			path, DP_RC(rc));
		goto failure;
	}

	if (node->dtn_protect)
		D_MUTEX_LOCK(&node->dtn_lock);
	clock_gettime(d_tm_clock_id(node->dtn_type & ~D_TM_DURATION),
		      &node->dtn_metric->dtm_data.tms[1]);
	if (node->dtn_protect)
		D_MUTEX_UNLOCK(&node->dtn_lock);

	return rc;

failure:
	return rc;
}

/**
 * Mark the end of the time interval started by d_tm_mark_duration_start()
 * Calculates the total interval and stores the result as the value of this
 * metric.
 *
 * The duration is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  Because this function must be
 * paired with d_tm_mark_duration_start(), the metric is not created if it
 * does not already exist.
 *
 * \param[in]		metric	Pointer to the metric
 * \param[in]		err	If non-zero, aborts the interval calculation
 *				so that this interval is not added to the stats.
 * \param[in]		fmt	Format specifier for the name and full path of
 *				the metric followed by optional args to
 *				populate the string, printf style.
 *				The format specifier and optional arguments
 *				are used only if the pointer to the metric
 *				is NULL.
 *
 * \return			DER_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a duration
 *				-DER_DURATION_MISMATCH	This function was called
 *							without first calling
 *							d_tm_mark_duration_start
 *				-DER_UNINIT		API not initialized
 *				-DER_INVAL		\a metric and \a item
 *							are NULL
 */
int
d_tm_mark_duration_end(struct d_tm_node_t **metric, int err,
		       const char *fmt, ...)
{
	struct d_tm_node_t	*node = NULL;
	struct timespec		end;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = DER_SUCCESS;

	if (d_tm_shmem_root == NULL)
		return -DER_UNINIT;

	if (err != DER_SUCCESS)
		return rc;

	if ((metric != NULL) && (*metric != NULL)) {
		node = *metric;
	} else {
		va_list	args;
		int	ret;

		if (fmt == NULL) {
			rc = -DER_INVAL;
			goto failure;
		}

		va_start(args, fmt);
		ret = vsnprintf(path, sizeof(path), fmt, args);
		va_end(args);

		if (ret <= 0 || ret >= D_TM_MAX_NAME_LEN) {
			rc = -DER_EXCEEDS_PATH_LEN;
			goto failure;
		}

		node = d_tm_find_metric(d_tm_shmem_root, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = -DER_DURATION_MISMATCH;
		D_ERROR("Failed to mark duration end [%s].  "
			"No existing metric found: " DF_RC "\n", path,
			DP_RC(rc));
		goto failure;
	}

	if (!(node->dtn_type & D_TM_DURATION)) {
		rc = -DER_OP_NOT_PERMITTED;
		D_ERROR("Failed to mark duration end [%s] on item "
			"not a duration.  Operation mismatch: " DF_RC "\n",
			path, DP_RC(rc));
		goto failure;
	}

	if (node->dtn_protect)
		D_MUTEX_LOCK(&node->dtn_lock);
	clock_gettime(d_tm_clock_id(node->dtn_type & ~D_TM_DURATION), &end);
	node->dtn_metric->dtm_data.tms[0] = d_timediff(
					node->dtn_metric->dtm_data.tms[1], end);
	d_tm_compute_duration_stats(node);
	if (node->dtn_protect)
		D_MUTEX_UNLOCK(&node->dtn_lock);

	return rc;

failure:
	return rc;
}

/**
 * Set an arbitrary \a value for the gauge.
 *
 * The gauge is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 *
 * \param[in,out]	metric	Pointer to the metric
 * \param[in]		value	Set the gauge to this value
 * \param[in]		fmt	Format specifier for the name and full path of
 *				the metric followed by optional args to
 *				populate the string, printf style.
 *				The format specifier and optional arguments
 *				are used only if the pointer to the metric
 *				is NULL.
 *
 * \return			DER_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a gauge
 *				-DER_UNINIT		API not initialized
 *				-DER_INVAL		\a metric and \a item
 *							are NULL
 */
int
d_tm_set_gauge(struct d_tm_node_t **metric, uint64_t value,
	       const char *fmt, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = DER_SUCCESS;

	if (d_tm_shmem_root == NULL)
		return -DER_UNINIT;

	if ((metric != NULL) && (*metric != NULL)) {
		node = *metric;
	} else {
		va_list	args;
		int	ret;

		if (fmt == NULL) {
			rc = -DER_INVAL;
			goto failure;
		}

		va_start(args, fmt);
		ret = vsnprintf(path, sizeof(path), fmt, args);
		va_end(args);

		if (ret <= 0 || ret >= D_TM_MAX_NAME_LEN) {
			rc = -DER_EXCEEDS_PATH_LEN;
			goto failure;
		}

		node = d_tm_find_metric(d_tm_shmem_root, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, D_TM_GAUGE, "N/A", "N/A", path);
		if (rc != DER_SUCCESS) {
			D_ERROR("Failed to add and set gauge [%s]: "
				DF_RC "\n", path, DP_RC(rc));
			goto failure;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->dtn_type != D_TM_GAUGE) {
		rc = -DER_OP_NOT_PERMITTED;
		D_ERROR("Failed to set gauge [%s] on item "
			"not a gauge.  Operation mismatch: " DF_RC "\n",
			path, DP_RC(rc));
		goto failure;
	}

	if (node->dtn_protect)
		D_MUTEX_LOCK(&node->dtn_lock);
	node->dtn_metric->dtm_data.value = value;
	d_tm_compute_gauge_stats(node);
	if (node->dtn_protect)
		D_MUTEX_UNLOCK(&node->dtn_lock);

	return rc;

failure:
	return rc;
}

/**
 * Increments the gauge by the \a value provided
 *
 * The gauge is specified either by an initialized pointer or by a fully
 * qualified \a item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 *
 * \param[in,out]	metric	Pointer to the metric
 * \param[in]		value	Increment the gauge by this value
 * \param[in]		fmt	Format specifier for the name and full path of
 *				the metric followed by optional args to
 *				populate the string, printf style.
 *				The format specifier and optional arguments
 *				are used only if the pointer to the metric
 *				is NULL.
 *
 * \return			DER_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a gauge
 *				-DER_UNINIT		API not initialized
 *				-DER_INVAL		\a metric and \a item
 *							are NULL
 */
int
d_tm_increment_gauge(struct d_tm_node_t **metric, uint64_t value,
		     const char *fmt, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = DER_SUCCESS;

	if (d_tm_shmem_root == NULL)
		return -DER_UNINIT;

	if ((metric != NULL) && (*metric != NULL)) {
		node = *metric;
	} else {
		va_list	args;
		int	ret;

		if (fmt == NULL) {
			rc = -DER_INVAL;
			goto failure;
		}

		va_start(args, fmt);
		ret = vsnprintf(path, sizeof(path), fmt, args);
		va_end(args);

		if (ret <= 0 || ret >= D_TM_MAX_NAME_LEN) {
			rc = -DER_EXCEEDS_PATH_LEN;
			goto failure;
		}

		node = d_tm_find_metric(d_tm_shmem_root, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, D_TM_GAUGE, "N/A", "N/A", path);
		if (rc != DER_SUCCESS) {
			D_ERROR("Failed to add and incremement gauge [%s]: "
				DF_RC "\n", path, DP_RC(rc));
			goto failure;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->dtn_type != D_TM_GAUGE) {
		rc = -DER_OP_NOT_PERMITTED;
		D_ERROR("Failed to increment gauge [%s] on item "
			"not a gauge.  Operation mismatch: " DF_RC "\n",
			path, DP_RC(rc));
		goto failure;
	}

	if (node->dtn_protect)
		D_MUTEX_LOCK(&node->dtn_lock);
	node->dtn_metric->dtm_data.value += value;
	d_tm_compute_gauge_stats(node);
	if (node->dtn_protect)
		D_MUTEX_UNLOCK(&node->dtn_lock);

	return rc;

failure:
	return rc;
}

/**
 * Decrements the gauge by the \a value provided
 *
 * The gauge is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 *
 * \param[in,out]	metric	Pointer to the metric
 * \param[in]		value	Decrement the gauge by this value
 * \param[in]		fmt	Format specifier for the name and full path of
 *				the metric followed by optional args to
 *				populate the string, printf style.
 *				The format specifier and optional arguments
 *				are used only if the pointer to the metric
 *				is NULL.
 *
 * \return			DER_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a gauge
 *				-DER_UNINIT		API not initialized
 *				-DER_INVAL		\a metric and \a item
 *							are NULL
 */
int
d_tm_decrement_gauge(struct d_tm_node_t **metric, uint64_t value,
		     const char *fmt, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = DER_SUCCESS;

	if (d_tm_shmem_root == NULL)
		return -DER_UNINIT;

	if ((metric != NULL) && (*metric != NULL)) {
		node = *metric;
	} else {
		va_list	args;
		int	ret;

		if (fmt == NULL) {
			rc = -DER_INVAL;
			goto failure;
		}

		va_start(args, fmt);
		ret = vsnprintf(path, sizeof(path), fmt, args);
		va_end(args);

		if (ret <= 0 || ret >= D_TM_MAX_NAME_LEN) {
			rc = -DER_EXCEEDS_PATH_LEN;
			goto failure;
		}

		node = d_tm_find_metric(d_tm_shmem_root, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, D_TM_GAUGE, "N/A", "N/A", path);
		if (rc != DER_SUCCESS) {
			D_ERROR("Failed to add and decrement gauge [%s]: "
				DF_RC "\n", path, DP_RC(rc));
			goto failure;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->dtn_type != D_TM_GAUGE) {
		rc = -DER_OP_NOT_PERMITTED;
		D_ERROR("Failed to decrement gauge [%s] on item "
			"not a gauge.  Operation mismatch: " DF_RC "\n",
			path, DP_RC(rc));
		goto failure;
	}

	if (node->dtn_protect)
		D_MUTEX_LOCK(&node->dtn_lock);
	node->dtn_metric->dtm_data.value -= value;
	d_tm_compute_gauge_stats(node);
	if (node->dtn_protect)
		D_MUTEX_UNLOCK(&node->dtn_lock);

	return rc;

failure:
	return rc;
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
 * Finds the node pointing to the given metric described by path name provided
 *
 * \param[in]	shmem_root	Pointer to the shared memory segment
 * \param[in]	path		The full name of the metric to find
 *
 * \return			A pointer to the metric node
 */
struct d_tm_node_t *
d_tm_find_metric(uint64_t *shmem_root, char *path)
{
	struct d_tm_node_t	*parent_node;
	struct d_tm_node_t	*node = NULL;
	char			str[D_TM_MAX_NAME_LEN];
	char			*token;
	char			*rest = str;

	if ((shmem_root == NULL) || (path == NULL))
		return NULL;

	parent_node = d_tm_get_root(shmem_root);

	if (parent_node == NULL)
		return NULL;

	snprintf(str, sizeof(str), "%s", path);
	token = strtok_r(rest, "/", &rest);
	while (token != NULL) {
		node = d_tm_find_child(shmem_root, parent_node, token);
		if (node == NULL)
			return NULL;
		parent_node =  node;
		token = strtok_r(rest, "/", &rest);
	}
	return node;
}

/**
 * Adds a new metric at the specified path, with the given \a metric_type.
 * An optional short description and long description may be added at this time.
 * This function may be called by the developer to initialize a metric at init
 * time in order to avoid the overhead of creating the metric at a more
 * critical time.
 *
 * \param[out]	node		Points to the new metric if supplied
 * \param[in]	metric_type	One of the corresponding d_tm_metric_types
 * \param[in]	sh_desc		A short description of the metric containing
 *				D_TM_MAX_SHORT_LEN - 1 characters maximum
 * \param[in]	lng_desc	A long description of the metric containing
 *				D_TM_MAX_LONG_LEN - 1 characters maximum
 * \param[in]	fmt		Format specifier for the name and full path of
 *				the new metric followed by optional args to
 *				populate the string, printf style.
 * \return			DER_SUCCESS		Success
 *				-DER_NO_SHMEM		Out of shared memory
 *				-DER_NOMEM		Out of global heap
 *				-DER_EXCEEDS_PATH_LEN	node name exceeds
 *							path len
 *				-DER_INVAL		node is invalid
 *				-DER_ADD_METRIC_FAILED	Operation failed
 *				-DER_UNINIT		API not initialized
 */
int d_tm_add_metric(struct d_tm_node_t **node, int metric_type, char *sh_desc,
		    char *lng_desc, const char *fmt, ...)
{
	pthread_mutexattr_t	mattr;
	struct d_tm_node_t	*parent_node;
	struct d_tm_node_t	*temp;
	char			path[D_TM_MAX_NAME_LEN] = {};
	char			*token;
	char			*rest;
	int			buff_len;
	int			rc;
	int			ret;
	va_list			args;

	if (d_tm_shmem_root == NULL)
		return -DER_UNINIT;

	if (node == NULL)
		return -DER_INVAL;

	if (fmt == NULL)
		return -DER_INVAL;

	rc = D_MUTEX_LOCK(&d_tm_add_lock);
	if (rc != 0) {
		D_ERROR("Failed to get mutex: " DF_RC "\n", DP_RC(rc));
		goto failure;
	}

	va_start(args, fmt);
	ret = vsnprintf(path, sizeof(path), fmt, args);
	va_end(args);

	if (ret <= 0 || ret >= D_TM_MAX_NAME_LEN) {
		rc = -DER_EXCEEDS_PATH_LEN;
		goto failure;
	}

	/**
	 * The item could exist due to a race condition where the
	 * unprotected d_tm_find_metric() does not find the metric,
	 * which leads to this d_tm_add_metric() call.
	 * If the metric is found, it's not an error.  Just return.
	 */
	*node = d_tm_find_metric(d_tm_shmem_root, path);
	if (*node != NULL) {
		D_MUTEX_UNLOCK(&d_tm_add_lock);
		return DER_SUCCESS;
	}

	rest = path;
	parent_node = d_tm_get_root(d_tm_shmem_root);
	token = strtok_r(rest, "/", &rest);
	while (token != NULL) {
		temp = d_tm_find_child(d_tm_shmem_root, parent_node, token);
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

	temp->dtn_type = metric_type;
	temp->dtn_metric = d_tm_shmalloc(sizeof(struct d_tm_metric_t));
	if (temp->dtn_metric == NULL) {
		rc = -DER_NO_SHMEM;
		goto failure;
	}

	temp->dtn_metric->dtm_stats = NULL;
	if (metric_type == D_TM_GAUGE) {
		temp->dtn_metric->dtm_stats =
				     d_tm_shmalloc(sizeof(struct d_tm_stats_t));
		if (temp->dtn_metric->dtm_stats == NULL) {
			rc = -DER_NO_SHMEM;
			goto failure;
		}
		temp->dtn_metric->dtm_stats->dtm_min.min_int = UINT64_MAX;
	}

	if (metric_type & D_TM_DURATION) {
		temp->dtn_metric->dtm_stats =
				     d_tm_shmalloc(sizeof(struct d_tm_stats_t));
		if (temp->dtn_metric->dtm_stats == NULL) {
			rc = -DER_NO_SHMEM;
			goto failure;
		}
		temp->dtn_metric->dtm_stats->dtm_min.min_float = DBL_MAX;
	}

	buff_len = strnlen(sh_desc, D_TM_MAX_SHORT_LEN);
	if (buff_len == D_TM_MAX_SHORT_LEN) {
		rc = -DER_EXCEEDS_PATH_LEN;
		goto failure;
	}
	buff_len += 1; /** make room for the trailing null */
	temp->dtn_metric->dtm_sh_desc = d_tm_shmalloc(buff_len);
	if (temp->dtn_metric->dtm_sh_desc == NULL) {
		rc = -DER_NO_SHMEM;
		goto failure;
	}
	strncpy(temp->dtn_metric->dtm_sh_desc, sh_desc, buff_len);

	buff_len = strnlen(lng_desc, D_TM_MAX_LONG_LEN);
	if (buff_len == D_TM_MAX_LONG_LEN) {
		rc = -DER_EXCEEDS_PATH_LEN;
		goto failure;
	}
	buff_len += 1; /** make room for the trailing null */
	temp->dtn_metric->dtm_lng_desc = d_tm_shmalloc(buff_len);
	if (temp->dtn_metric->dtm_lng_desc == NULL) {
		rc = -DER_NO_SHMEM;
		goto failure;
	}
	strncpy(temp->dtn_metric->dtm_lng_desc, lng_desc, buff_len);

	temp->dtn_protect = false;
	if (d_tm_serialization && (temp->dtn_type != D_TM_DIRECTORY)) {
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
	D_MUTEX_UNLOCK(&d_tm_add_lock);
	return DER_SUCCESS;

failure:
	D_MUTEX_UNLOCK(&d_tm_add_lock);
	D_ERROR("Failed to add metric [%s]: " DF_RC "\n", path, DP_RC(rc));
	return rc;
}

/**
 * Client function to read the specified counter.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.
 *
 * \param[in,out]	val		The value of the counter is stored here
 * \param[in]		shmem_root	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		DER_SUCCESS		Success
 *			-DER_INVAL		Bad \a value pointer
 *			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a counter
 */
int
d_tm_get_counter(uint64_t *val, uint64_t *shmem_root, struct d_tm_node_t *node,
		 char *metric)
{
	struct d_tm_metric_t	*metric_data = NULL;

	if (val == NULL)
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(shmem_root, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->dtn_type != D_TM_COUNTER)
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(shmem_root, node->dtn_metric);
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
 * Client function to read the specified timestamp.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.
 *
 * \param[in,out]	val		The value of the timestamp is stored
 *					here
 * \param[in]		shmem_root	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		DER_SUCCESS		Success
 *			-DER_INVAL		Bad \a val pointer
 *			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a timestamp
 */
int
d_tm_get_timestamp(time_t *val, uint64_t *shmem_root, struct d_tm_node_t *node,
		   char *metric)
{
	struct d_tm_metric_t	*metric_data = NULL;

	if (val == NULL)
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(shmem_root, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->dtn_type != D_TM_TIMESTAMP)
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(shmem_root, node->dtn_metric);
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
 * Client function to read the specified high resolution timer.  If the node is
 * provided, that pointer is used for the read.  Otherwise, a lookup by the
 * metric name is performed.
 *
 * \param[in,out]	tms		The value of the timer is stored here
 * \param[in]		shmem_root	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		DER_SUCCESS		Success
 *			-DER_INVAL		Bad \a tms pointer
 *			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a high resolution
 *						timer
 */
int
d_tm_get_timer_snapshot(struct timespec *tms, uint64_t *shmem_root,
			struct d_tm_node_t *node, char *metric)
{
	struct d_tm_metric_t	*metric_data = NULL;

	if (tms == NULL)
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(shmem_root, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (!(node->dtn_type & D_TM_TIMER_SNAPSHOT))
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(shmem_root, node->dtn_metric);
	if (metric_data != NULL) {
		if (node->dtn_protect)
			D_MUTEX_LOCK(&node->dtn_lock);
		tms->tv_sec = metric_data->dtm_data.tms[0].tv_sec;
		tms->tv_nsec = metric_data->dtm_data.tms[0].tv_nsec;
		if (node->dtn_protect)
			D_MUTEX_UNLOCK(&node->dtn_lock);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return DER_SUCCESS;
}

/**
 * Client function to read the specified duration.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.  A pointer for the \a tms is required.  A pointer for \a stats
 * is optional.
 *
 * The computation of mean and standard deviation are completed upon this
 * read operation.
 *
 * \param[in,out]	tms		The value of the duration is stored here
 * \param[in,out]	stats		The statistics are stored here
 * \param[in]		shmem_root	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		DER_SUCCESS		Success
 *			-DER_INVAL		Bad \a tms pointer
 *			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a duration
 */
int
d_tm_get_duration(struct timespec *tms, struct d_tm_stats_t *stats,
		  uint64_t *shmem_root, struct d_tm_node_t *node, char *metric)
{
	struct d_tm_metric_t	*metric_data = NULL;
	struct d_tm_stats_t	*dtm_stats = NULL;

	if (tms == NULL)
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(shmem_root, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (!(node->dtn_type & D_TM_DURATION))
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(shmem_root, node->dtn_metric);
	if (metric_data != NULL) {
		dtm_stats = d_tm_conv_ptr(shmem_root, metric_data->dtm_stats);
		if (node->dtn_protect)
			D_MUTEX_LOCK(&node->dtn_lock);
		tms->tv_sec = metric_data->dtm_data.tms[0].tv_sec;
		tms->tv_nsec = metric_data->dtm_data.tms[0].tv_nsec;
		if ((stats != NULL) && (dtm_stats != NULL)) {
			stats->dtm_min.min_float = dtm_stats->dtm_min.min_float;
			stats->dtm_max.max_float = dtm_stats->dtm_max.max_float;
			stats->dtm_sum.sum_float = dtm_stats->dtm_sum.sum_float;
			if (dtm_stats->sample_size > 0)
				stats->mean = dtm_stats->dtm_sum.sum_float /
					      dtm_stats->sample_size;
			stats->std_dev = d_tm_compute_standard_dev(
						      dtm_stats->sum_of_squares,
						      dtm_stats->sample_size,
						      stats->mean);
			stats->sum_of_squares = dtm_stats->sum_of_squares;
			stats->sample_size = dtm_stats->sample_size;
		}
		if (node->dtn_protect)
			D_MUTEX_UNLOCK(&node->dtn_lock);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return DER_SUCCESS;
}

/**
 * Client function to read the specified gauge.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.  A pointer for the \a val is required.  A pointer for \a stats
 * is optional.
 *
 * The computation of mean and standard deviation are completed upon this
 * read operation.
 *
 * \param[in,out]	val		The value of the gauge is stored here
 * \param[in,out]	stats		The statistics are stored here
 * \param[in]		shmem_root	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		DER_SUCCESS		Success
 *			-DER_INVAL		Bad \a val pointer
 *			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a gauge
 */

int
d_tm_get_gauge(uint64_t *val, struct d_tm_stats_t *stats, uint64_t *shmem_root,
	       struct d_tm_node_t *node, char *metric)
{
	struct d_tm_metric_t	*metric_data = NULL;
	struct d_tm_stats_t	*dtm_stats = NULL;
	double			sum = 0;

	if (val == NULL)
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(shmem_root, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->dtn_type != D_TM_GAUGE)
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(shmem_root, node->dtn_metric);
	if (metric_data != NULL) {
		dtm_stats = d_tm_conv_ptr(shmem_root, metric_data->dtm_stats);
		if (node->dtn_protect)
			D_MUTEX_LOCK(&node->dtn_lock);
		*val = metric_data->dtm_data.value;
		if ((stats != NULL) && (dtm_stats != NULL)) {
			stats->dtm_min.min_int = dtm_stats->dtm_min.min_int;
			stats->dtm_max.max_int = dtm_stats->dtm_max.max_int;
			stats->dtm_sum.sum_int = dtm_stats->dtm_sum.sum_int;
			if (dtm_stats->sample_size > 0) {
				sum = (double)dtm_stats->dtm_sum.sum_int;
				stats->mean = sum / dtm_stats->sample_size;
			}
			stats->std_dev = d_tm_compute_standard_dev(
						      dtm_stats->sum_of_squares,
						      dtm_stats->sample_size,
						      stats->mean);
			stats->sum_of_squares = dtm_stats->sum_of_squares;
			stats->sample_size = dtm_stats->sample_size;
		}
		if (node->dtn_protect)
			D_MUTEX_UNLOCK(&node->dtn_lock);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return DER_SUCCESS;
}

/**
 * Client function to read the metadata for the specified metric.  If the node
 * is provided, that pointer is used for the read.  Otherwise, a lookup by the
 * metric name is performed.  Memory is allocated for the \a sh_desc and
 * \a lng_desc and should be freed by the caller.
 *
 * \param[in,out]	sh_desc		Memory is allocated and the short
 *					description is copied here
 * \param[in,out]	lng_desc	Memory is allocated and the long
 *					description is copied here
 * \param[in]		shmem_root	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		DER_SUCCESS		Success
 *			-DER_INVAL		Bad \a sh_desc or \a lng_desc
 *						pointer
 *			-DER_METRIC_NOT_FOUND	Metric node not found
 *			-DER_OP_NOT_PERMITTED	Node is not a metric
 */
int d_tm_get_metadata(char **sh_desc, char **lng_desc, uint64_t *shmem_root,
		      struct d_tm_node_t *node, char *metric)
{
	struct d_tm_metric_t	*metric_data = NULL;
	char			*sh_desc_str;
	char			*lng_desc_str;

	if ((sh_desc == NULL) && (lng_desc == NULL))
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(shmem_root, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(shmem_root, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->dtn_type == D_TM_DIRECTORY)
		return -DER_OP_NOT_PERMITTED;

	metric_data = d_tm_conv_ptr(shmem_root, node->dtn_metric);
	if (metric_data != NULL) {
		if (node->dtn_protect)
			D_MUTEX_LOCK(&node->dtn_lock);
		sh_desc_str = d_tm_conv_ptr(shmem_root,
					    metric_data->dtm_sh_desc);
		if ((sh_desc != NULL) && (sh_desc_str != NULL))
			D_STRNDUP(*sh_desc, sh_desc_str ? sh_desc_str :
				  "N/A", D_TM_MAX_SHORT_LEN);
		lng_desc_str = d_tm_conv_ptr(shmem_root,
					     metric_data->dtm_lng_desc);
		if ((lng_desc != NULL) && (lng_desc_str != NULL))
			D_STRNDUP(*lng_desc, lng_desc_str ? lng_desc_str :
				  "N/A", D_TM_MAX_LONG_LEN);
		if (node->dtn_protect)
			D_MUTEX_UNLOCK(&node->dtn_lock);
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
 * \param[in,out]	head		Pointer to a nodelist
 * \param[in]		shmem_root	Pointer to the shared memory segment
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
d_tm_list(struct d_tm_nodeList_t **head, uint64_t *shmem_root,
	  struct d_tm_node_t *node, int d_tm_type)
{
	int	rc = DER_SUCCESS;

	if ((head == NULL) || (node == NULL)) {
		rc = -DER_INVAL;
		goto failure;
	}

	if (d_tm_type & node->dtn_type) {
		rc = d_tm_add_node(node, head);
		if (rc != DER_SUCCESS)
			goto failure;
	}

	node = node->dtn_child;
	if (node == NULL)
		goto success;

	node = d_tm_conv_ptr(shmem_root, node);
	if (node == NULL) {
		rc = -DER_INVAL;
		goto failure;
	}

	rc = d_tm_list(head, shmem_root, node, d_tm_type);
	if (rc != DER_SUCCESS)
		goto failure;

	node = node->dtn_sibling;
	if (node == NULL)
		return rc;

	node = d_tm_conv_ptr(shmem_root, node);
	while (node != NULL) {
		rc = d_tm_list(head, shmem_root, node, d_tm_type);
		if (rc != DER_SUCCESS)
			goto failure;
		node = node->dtn_sibling;
		node = d_tm_conv_ptr(shmem_root, node);
	}
success:
	return rc;

failure:
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
d_tm_add_node(struct d_tm_node_t *src, struct d_tm_nodeList_t **nodelist)
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
uint64_t *
d_tm_allocate_shared_memory(int srv_idx, size_t mem_size)
{
	uint64_t	*addr;
	key_t		key;

	key = d_tm_get_key(srv_idx);
	d_tm_shmid = shmget(key, mem_size, IPC_CREAT | 0660);
	if (d_tm_shmid < 0) {
		D_ERROR("Unable to allocate shared memory.  shmget failed, "
			"%s\n", strerror(errno));
		return NULL;
	}

	addr = shmat(d_tm_shmid, NULL, 0);
	if (addr == (void *)-1) {
		D_ERROR("Unable to allocate shared memory.  shmat failed, "
			"%s\n", strerror(errno));
		return NULL;
	}
	return addr;
}

/**
 * Client side function that retrieves a pointer to the shared memory segment
 * for this server instance.
 *
 * \param[in]	srv_idx		A unique value that identifies the producer
 *				process that the client seeks to read data from
 * \return			Address of the shared memory region
 *				NULL if failure
 */
uint64_t *
d_tm_get_shared_memory(int srv_idx)
{
	uint64_t	*addr;
	key_t		key;
	int		shmid;

	key = d_tm_get_key(srv_idx);
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
	return addr;
}

/**
 * Allocates memory from within the shared memory pool with 16-bit alignment
 * Clears the allocated buffer.
 *
 * param[in]	length	Size in bytes of the region within the shared memory
 *			pool to allocate
 *
 * \return		Address of the allocated memory
 *			NULL if there was no more memory available
 */
void *
d_tm_shmalloc(int length)
{
	if (length % sizeof(uint16_t) != 0) {
		length += sizeof(uint16_t);
		length &= ~(sizeof(uint16_t) - 1);
	}

	if (d_tm_shmem_idx) {
		if ((d_tm_shmem_free - length) > 0) {
			d_tm_shmem_free -= length;
			d_tm_shmem_idx += length;
			D_DEBUG(DB_TRACE,
				"Allocated %d bytes.  Now %" PRIu64 " remain\n",
				length, d_tm_shmem_free);
			memset((void *)(d_tm_shmem_idx - length), 0, length);
			return d_tm_shmem_idx - length;
		}
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
d_tm_validate_shmem_ptr(uint64_t *shmem_root, void *ptr)
{
	if (((uint64_t)ptr < (uint64_t)shmem_root) ||
	    ((uint64_t)ptr >= (uint64_t)shmem_root + D_TM_SHARED_MEMORY_SIZE)) {
		D_DEBUG(DB_TRACE,
			"shmem ptr 0x%" PRIx64 " was outside the shmem range "
			"0x%" PRIx64 " to 0x%" PRIx64, (uint64_t)ptr,
			(uint64_t)shmem_root, (uint64_t)shmem_root +
			D_TM_SHARED_MEMORY_SIZE);
		return false;
	}
	return true;
}

/**
 * Convert the virtual address of the pointer in shared memory from a server
 * address to a client side virtual address.
 *
 * \param[in]	shmem_root	Pointer to the shared memory segment
 * \param[in]	ptr		The pointer to convert
 *
 * \return			A pointer to the item in the clients address
 *				space
 *				NULL if the pointer is invalid
 */
void *
d_tm_conv_ptr(uint64_t *shmem_root, void *ptr)
{
	void	*temp;

	if ((ptr == NULL) || (shmem_root == NULL))
		return NULL;

	temp = (void *)((uint64_t)shmem_root + ((uint64_t)ptr) -
			*(uint64_t *)shmem_root);

	if (d_tm_validate_shmem_ptr(shmem_root, temp))
		return temp;
	return NULL;
}
