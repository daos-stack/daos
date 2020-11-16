/**
 * (C) Copyright 2020 Intel Corporation.
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
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

/*
 * telemetry: TELEMETRY common logic
 */
#define D_LOGFAC	DD_FAC(telem)

#include <gurt/common.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <semaphore.h>
#include <errno.h>
#include <stdbool.h>
#include <gurt/telemetry.h>

/**
 * These are server side global variables that apply to the entire server
 * side process
 */
uint64_t 		*shmemRoot;
uint8_t 		*shmemIdx;
uint64_t 		shmemFree;
pthread_mutex_t 	addlock;
struct d_tm_node_t 	*root;

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
 * \param[in]	cshmemRoot	Pointer to the shared memory segment
 * \param[in]	parent		The parent node
 * \param[in]	name		The name of the child to find
 *
 * \return			Pointer to the child node
 * 				NULL if not found
 */
struct d_tm_node_t *
d_tm_find_child(uint64_t *cshmemRoot, struct d_tm_node_t *parent, char *name)
{
	struct d_tm_node_t	*child = NULL;
	char			*clientName;

	if (parent == NULL)
		return NULL;

	if (parent->child == NULL)
		return NULL;

	child = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot, parent->child);

	clientName = NULL;
	if (child != NULL)
		clientName = (char *)d_tm_conv_ptr(cshmemRoot, child->name);

	while ((child != NULL) &&
	       strncmp(clientName, name, D_TM_MAX_NAME_LEN) != 0) {
		child = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot,
							    child->sibling);
		clientName = NULL;
		if (child == NULL)
			break;
		clientName = (char *)d_tm_conv_ptr(cshmemRoot, child->name);
	}

	return child;
}

/**
 * Add a child node \a newnode to the tree in shared memory.
 * A child will either be a first child, or a sibling of an existing child.
 * \param[in]	newnode	The child node to be added
 * \param[in]	parent	The parent node for this new child
 * \param[in]	name	The name of the new node
 *
 * \return		D_TM_SUCCESS		Success
 * 			-DER_NO_SHMEM		No shared memory available
 * 			-DER_EXCEEDS_PATH_LEN	The full name length is
 * 						too long
 */
int
d_tm_add_child(struct d_tm_node_t **newnode, struct d_tm_node_t *parent,
	       char *name)
{
	struct d_tm_node_t	*child = parent->child;
	struct d_tm_node_t	*sibling = parent->child;
	int			rc = D_TM_SUCCESS;
	int			buffLen = 0;

	/* If there are no children, add the first child to this parent */
	if (child == NULL) {
		*newnode = (struct d_tm_node_t *)d_tm_shmalloc(
						    sizeof(struct d_tm_node_t));
		if (*newnode == NULL)
			D_GOTO(failure, rc = -DER_NO_SHMEM);
		buffLen = strnlen(name, D_TM_MAX_NAME_LEN);
		if (buffLen == D_TM_MAX_NAME_LEN)
			D_GOTO(failure, rc = -DER_EXCEEDS_PATH_LEN);
		buffLen += 1; /* make room for the trailing null */
		(*newnode)->name = (char *)d_tm_shmalloc(buffLen *
							 sizeof(char));
		if ((*newnode)->name == NULL)
			D_GOTO(failure, rc = -DER_NO_SHMEM);
		strncpy((*newnode)->name, name, buffLen);
		(*newnode)->child = NULL;
		(*newnode)->sibling = NULL;
		(*newnode)->metric = NULL;
		parent->child = *newnode;
		return D_TM_SUCCESS;
	}

	/*
	 * Find the youngest child of this parent by traversing the siblings
	 * of the first child
	 */
	child = child->sibling;
	while (child != NULL) {
		sibling = child; /** youngest known child */
		child = child->sibling;
	}

	/* Add the new node to the sibling list */
	*newnode = (struct d_tm_node_t *)d_tm_shmalloc(
						    sizeof(struct d_tm_node_t));
	if (*newnode != NULL) {
		buffLen = strnlen(name, D_TM_MAX_NAME_LEN);
		if (buffLen == D_TM_MAX_NAME_LEN)
			D_GOTO(failure, rc = -DER_EXCEEDS_PATH_LEN);
		buffLen += 1; /* make room for the trailing null */
		(*newnode)->name = (char *)d_tm_shmalloc(buffLen *
							 sizeof(char));
		if ((*newnode)->name == NULL)
			D_GOTO(failure, rc = -DER_NO_SHMEM);
		strncpy((*newnode)->name, name, buffLen);
		(*newnode)->child = NULL;
		(*newnode)->sibling = NULL;
		(*newnode)->metric = NULL;
		sibling->sibling = *newnode;
		return D_TM_SUCCESS;
	}
failure:
	D_ERROR("Failed to add child node [%s]: rc = %d", name, rc);
	return rc;
}

/**
 * Initialize an instance of the telemetry and metrics API for the producer
 * process.
 *
 * \param[in]	rank	Identifies the server process amongst others on the same
 * 			machine
 * \param[in]	memSize Size in bytes of the shared memory segment that is
 * 			allocated
 *
 * \return		D_TM_SUCCESS		Success
 *			-DER_NO_SHMEM		Out of shared memory
 *			-DER_EXCEEDS_PATH_LEN	Root node name exceeds path len
 */
int
d_tm_init(int rank, uint64_t memSize)
{
	uint64_t	*base_addr = NULL;
	char		tmp[D_TM_MAX_NAME_LEN];
	int		rc = D_TM_SUCCESS;
	int		buffLen = 0;

	if ((shmemRoot != NULL) && (root != NULL)) {
		D_INFO("d_tm_init already completed for rank %d\n", rank);
		return rc;
	}

	shmemRoot = (uint64_t *)d_tm_allocate_shared_memory(rank, memSize);
	if (shmemRoot != NULL) {
		shmemIdx = (uint8_t *)shmemRoot;
		shmemFree = memSize;
		D_DEBUG(DB_TRACE, "Shared memory allocation success!\n"
		       "Memory size is %"PRIu64" bytes at address 0x%"PRIx64
		       "\n", memSize, (uint64_t)shmemRoot);
		/*
		 * Store the base address of the shared memory as seen by the
		 * server in this first uint64_t sized slot.
		 * Used by the client to adjust pointers in the shared memory
		 * to its own address space.
		 */
		base_addr = (uint64_t *)d_tm_shmalloc(sizeof(uint64_t));
		D_ASSERT(base_addr != NULL);
		*base_addr = (uint64_t)shmemRoot;
	} else {
		D_GOTO(failure, rc = -DER_NO_SHMEM);
	}

	root = (struct d_tm_node_t *)d_tm_shmalloc(sizeof(struct d_tm_node_t));
	D_ASSERT(root != NULL);

	sprintf(tmp, "rank %d", rank);
	buffLen = strnlen(tmp, D_TM_MAX_NAME_LEN);
	if (buffLen == D_TM_MAX_NAME_LEN)
		D_GOTO(failure, rc = -DER_EXCEEDS_PATH_LEN);
	buffLen += 1; /* make room for the trailing null */

	root->name = (char *)d_tm_shmalloc(buffLen * sizeof(char));
	D_ASSERT(root->name != NULL);
	strncpy(root->name, tmp, buffLen);

	root->child = NULL;
	root->sibling = NULL;
	root->metric = NULL;
	root->d_tm_type = D_TM_DIRECTORY;

	/*
	 * This mutex protects the d_tm_add_metric operations
	 */
	rc = D_MUTEX_INIT(&addlock, NULL);
	if (rc != 0) {
		D_ERROR("Mutex init failure: %d.", rc);
		D_GOTO(failure, rc);
	}

	D_INFO("Telemetry and Metrics initialized for rank: %u\n", rank);
	return rc;

failure:
	D_ERROR("Failed to initialize telemetry and metrics API: %d", rc);
	return rc;
}

/**
 * Releases resources claimed by init
 * Currently, only detaches from the shared memory to keep the memory and the
 * shared mutex objects available for the client to read data even when the
 * producer has gone offline.
 */
void d_tm_fini(void)
{
	if (shmemRoot != NULL) {
		/*
		 * If we decide to free the mutex objects on shutdown for the
		 * nodes that have them, call:
		 * d_tm_free_node(shmemRoot, root);
		 *
		 * Destroying the mutex objects on shutdown makes them
		 * unavailable to a client process that may want to read the
		 * data.  To remedy that, can implement a reference count that
		 * monitors the number of users of the shared memory segment
		 * and only frees the resources if it's the last one using it.
		 */
		shmdt(shmemRoot);
	}
}

/**
 * Recursively free resources underneath the given node.
 *
 * \param[in]	cshmemRoot	Pointer to the shared memory segment
 * \param[in]	node		Pointer to the node containing the resources
 * 				to free
 */
void
d_tm_free_node(uint64_t *cshmemRoot, struct d_tm_node_t *node)
{
	char	*name;

	if (node == NULL)
		return;

	if (node->d_tm_type != D_TM_DIRECTORY) {
		if (D_MUTEX_DESTROY(&node->lock) != 0) {
			name = (char *)d_tm_conv_ptr(cshmemRoot, node->name);
			D_ERROR("Failed to destroy mutex for node: %s", name);
		}
	}

	node = node->child;
	node = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot, node);
	if (node == NULL)
		return;

	d_tm_free_node(cshmemRoot, node);
	node = node->sibling;
	node = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot, node);
	while (node != NULL) {
		d_tm_free_node(cshmemRoot, node);
		node = node->sibling;
		node = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot, node);
	}
}

/**
 * Recursively prints all nodes underneath the given \a node.
 * Used as a convenience function to demonstrate usage for the client
 *
 * \param[in]	cshmemRoot	Pointer to the shared memory segment
 * \param[in]	node		Pointer to a parent or child node
 * \param[in]	level		Indicates level of indentation when printing
 *				this \a node
 */
void
d_tm_print_my_children(uint64_t *cshmemRoot, struct d_tm_node_t *node,
		       int level)
{
	struct timespec	tms;
	uint64_t	val;
	time_t		clk;
	char		*convertedNamePtr = NULL;
	char		tmp[D_TM_TIME_BUFF_LEN];
	int		i = 0;
	int		len = 0;
	int		rc;

	if (node == NULL)
		return;

	convertedNamePtr = (char *)d_tm_conv_ptr(cshmemRoot, node->name);
	if (convertedNamePtr != NULL) {
		for (i = 0; i < level; i++)
			printf("%20s", " ");

		switch (node->d_tm_type) {
		case D_TM_DIRECTORY:
			printf("%-20s\n", convertedNamePtr);
			break;
		case D_TM_COUNTER:
			rc = d_tm_get_counter(&val, cshmemRoot, node, NULL);
			if (rc == D_TM_SUCCESS)
				printf("COUNTER: %s %" PRIu64 "\n",
				       convertedNamePtr, val);
			else
				printf("Error on counter read: %d\n", rc);
			break;
		case D_TM_TIMESTAMP:
			rc = d_tm_get_timestamp(&clk, cshmemRoot, node, NULL);
			if (rc == D_TM_SUCCESS) {
				strncpy(tmp, ctime(&clk), sizeof(tmp) - 1);
				len = strnlen(tmp, D_TM_TIME_BUFF_LEN - 1);
				if (len > 0) {
					if (tmp[len - 1] == '\n') {
						tmp[len - 1] = 0;
					}
				}
				printf("TIMESTAMP %s: %s\n",
				       convertedNamePtr, tmp);
			} else {
				printf("Error on timestamp read: %d\n",	rc);
			}
			break;
		case D_TM_HIGH_RES_TIMER:
			rc = d_tm_get_highres_timer(&tms, cshmemRoot,
						    node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("HIGH RES TIMER %s:%lds, %ldns\n",
				       convertedNamePtr, tms.tv_sec,
				       tms.tv_nsec);
			} else {
				printf("Error on highres timer read: %d\n", rc);
			}
			break;
		case (D_TM_DURATION | D_TM_CLOCK_REALTIME):
			rc = d_tm_get_duration(&tms, cshmemRoot, node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("REALTIME DURATION %s: %.9fs\n",
				       convertedNamePtr, tms.tv_sec +
				       tms.tv_nsec / 1e9);
			} else {
				printf("Error on duration read: %d\n", rc);
			}
			break;
		case (D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME):
			rc = d_tm_get_duration(&tms, cshmemRoot, node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("PROC CPU DURATION %s: %.9fs\n",
				       convertedNamePtr, tms.tv_sec +
				       tms.tv_nsec / 1e9);
			} else {
				printf("Error on duration read: %d\n", rc);
			}
			break;
		case (D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME):
			rc = d_tm_get_duration(&tms, cshmemRoot, node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("THRD CPU DURATION %s: %.9fs\n",
				       convertedNamePtr, tms.tv_sec +
				       tms.tv_nsec / 1e9);
			} else {
				printf("Error on duration read: %d\n", rc);
			}
			break;
		case D_TM_GAUGE:
			rc = d_tm_get_gauge(&val, cshmemRoot, node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("GAUGE %s: %" PRIu64 "\n",
				       convertedNamePtr, val);
			} else {
				printf("Error on gauge read: %d\n", rc);
			}
			break;
		default:
			printf("Unknown type: %d\n", node->d_tm_type);
			break;
		}
	}
	node = node->child;
	node = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot, node);
	if (node == NULL)
		return;

	d_tm_print_my_children(cshmemRoot, node, level + 1);
	node = node->sibling;
	node = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot, node);
	while (node != NULL) {
		d_tm_print_my_children(cshmemRoot, node, level + 1);
		node = node->sibling;
		node = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot, node);
	}
}

/**
 * Recursively counts number of metrics at and underneath the given \a node.
 *
 * \param[in]	cshmemRoot	Pointer to the shared memory segment
 * \param[in]	node		Pointer to a parent or child node
 *
 * \return			Number of metrics found
 */
uint64_t
d_tm_count_metrics(uint64_t *cshmemRoot, struct d_tm_node_t *node)
{
	uint64_t	count = 0;

	if (node == NULL)
		return 0;

	if (node->d_tm_type != D_TM_DIRECTORY)
		count++;

	node = node->child;
	node = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot, node);
	if (node == NULL)
		return count;

	count += d_tm_count_metrics(cshmemRoot, node);
	node = node->sibling;
	node = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot, node);
	while (node != NULL) {
		count += d_tm_count_metrics(cshmemRoot, node);
		node = node->sibling;
		node = (struct d_tm_node_t *)d_tm_conv_ptr(cshmemRoot, node);
	}
	return count;
}

/**
 * Increment the given counter
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
 * \param[in]		item	Full path name to this \a item
 * 				If supplied, the path name is specified by the
 * 				\a item and any strings following it.
 *
 * \return			D_TM_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a counter
 */
int
d_tm_increment_counter(struct d_tm_node_t **metric, char *item, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = D_TM_SUCCESS;

	if ((metric != NULL) && (*metric != NULL))
		node = *metric;
	else {
		va_list	args;
		char	*str;

		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str != NULL) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN - 1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, path, D_TM_COUNTER, "N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to incremement counter [%s]  "
				"Failed to add metric", path);
			return rc;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->d_tm_type == D_TM_COUNTER) {
		D_MUTEX_LOCK(&node->lock);
		node->metric->data.value++;
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		D_ERROR("Failed to increment counter [%s] on item not a "
			"counter.  Operation mismatch.", node->name);
		return -DER_OP_NOT_PERMITTED;
	}
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
 * \param[in]		item	Full path name to this \a item
 * 				If supplied, the path name is specified by the
 * 				\a item and any strings following it.
 *
 * \return			D_TM_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a timestamp
 */
int
d_tm_record_timestamp(struct d_tm_node_t **metric, char *item, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = D_TM_SUCCESS;

	if ((metric != NULL) && (*metric != NULL))
		node = *metric;
	else {
		va_list	args;
		char	*str;

		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str != NULL) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN - 1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, path, D_TM_TIMESTAMP, "N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to record timestamp [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->d_tm_type == D_TM_TIMESTAMP) {
		D_MUTEX_LOCK(&node->lock);
		node->metric->data.value = (uint64_t)time(NULL);
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		D_ERROR("Failed to record timestamp [%s] on item not a "
			"timestamp.  Operation mismatch.", path);
		return -DER_OP_NOT_PERMITTED;
	}
	return rc;
}

/**
 * Read and store a high resolution timer value
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
 * \param[in]		item	Full path name to this \a item
 * 				If supplied, the path name is specified by the
 * 				\a item and any strings following it.
 *
 * \return			D_TM_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a high resolution
 *							timer
 */
int
d_tm_record_high_res_timer(struct d_tm_node_t **metric, char *item, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = D_TM_SUCCESS;

	if ((metric != NULL) && (*metric != NULL))
		node = *metric;
	else {
		va_list	args;
		char	*str;

		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str != NULL) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN - 1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, path, D_TM_HIGH_RES_TIMER,
				     "N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to record high resolution timer [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->d_tm_type == D_TM_HIGH_RES_TIMER) {
		D_MUTEX_LOCK(&node->lock);
		clock_gettime(CLOCK_REALTIME, &node->metric->data.tms[0]);
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		D_ERROR("Failed to record high resolution timer [%s] on item "
			"not a high resolution timer.  Operation mismatch.",
			path);
		return -DER_OP_NOT_PERMITTED;
	}
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
 * \param[in]		item	Full path name to this \a item
 * 				If supplied, the path name is specified by the
 * 				\a item and any strings following it.
 *
 * \return			D_TM_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a duration
 *				-DER_INVAL		clk_id was invalid
 */
int
d_tm_mark_duration_start(struct d_tm_node_t **metric, int clk_id,
			 char *item, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = D_TM_SUCCESS;

	if ((metric != NULL) && (*metric != NULL))
		node = *metric;
	else {
		va_list	args;
		char	*str;

		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str != NULL) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN - 1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		if (!((clk_id == D_TM_CLOCK_REALTIME) ||
		      (clk_id == D_TM_CLOCK_PROCESS_CPUTIME) ||
		      (clk_id == D_TM_CLOCK_THREAD_CPUTIME))) {
			rc = -DER_INVAL;
			D_ERROR("Invalid clk_id for [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		rc = d_tm_add_metric(&node, path, D_TM_DURATION | clk_id,
				     "N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to mark duration start [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->d_tm_type & D_TM_DURATION) {
		D_MUTEX_LOCK(&node->lock);
		clock_gettime(d_tm_clock_id(node->d_tm_type & ~D_TM_DURATION),
			      &node->metric->data.tms[1]);
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		D_ERROR("Failed to mark duration start [%s] on item "
			"not a duration.  Operation mismatch.",
			path);
		return -DER_OP_NOT_PERMITTED;
	}
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
 * \param[in]		item	Full path name to this \a item
 * 				If supplied, the path name is specified by the
 * 				\a item and any strings following it.
 *
 * \return			D_TM_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a duration
 *				-DER_DURATION_MISMATCH	This function was called
 *							without first calling
 *							d_tm_mark_duration_start
 */
int
d_tm_mark_duration_end(struct d_tm_node_t **metric, char *item, ...)
{
	struct d_tm_node_t	*node = NULL;
	struct timespec		end;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = D_TM_SUCCESS;

	if ((metric != NULL) && (*metric != NULL))
		node = *metric;
	else {
		va_list	args;
		char	*str;

		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str != NULL) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN - 1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		D_ERROR("Failed to mark duration end [%s]  "
			"No existing metric found", path);
		return -DER_DURATION_MISMATCH;
	}

	if (node->d_tm_type & D_TM_DURATION) {
		D_MUTEX_LOCK(&node->lock);
		clock_gettime(d_tm_clock_id(node->d_tm_type & ~D_TM_DURATION),
			      &end);
		node->metric->data.tms[0] = d_timediff(
						node->metric->data.tms[1], end);
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		D_ERROR("Failed to mark duration end [%s] on item "
			"not a duration.  Operation mismatch.",
			path);
		return -DER_OP_NOT_PERMITTED;
	}
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
 * \param[in]		item	Full path name to this \a item
 * 				If supplied, the path name is specified by the
 * 				\a item and any strings following it.
 *
 * \return			D_TM_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a gauge
 */
int
d_tm_set_gauge(struct d_tm_node_t **metric, uint64_t value, char *item, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = D_TM_SUCCESS;

	if ((metric != NULL) && (*metric != NULL))
		node = *metric;
	else {
		va_list	args;
		char	*str;

		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str != NULL) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN - 1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, path, D_TM_GAUGE, "N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to set gauge [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->d_tm_type == D_TM_GAUGE) {
		D_MUTEX_LOCK(&node->lock);
		node->metric->data.value = value;
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		D_ERROR("Failed to set gauge [%s] on item "
			"not a gauge.  Operation mismatch.",
			path);
		return -DER_OP_NOT_PERMITTED;
	}
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
 * \param[in]		item	Full path name to this \a item
 * 				If supplied, the path name is specified by the
 * 				\a item and any strings following it.
 *
 * \return			D_TM_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a gauge
 */
int
d_tm_increment_gauge(struct d_tm_node_t **metric, uint64_t value,
		     char *item, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = D_TM_SUCCESS;

	if ((metric != NULL) && (*metric != NULL))
		node = *metric;
	else {
		va_list	args;
		char	*str;

		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str != NULL) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN - 1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, path, D_TM_GAUGE, "N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to incremement gauge [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->d_tm_type == D_TM_GAUGE) {
		D_MUTEX_LOCK(&node->lock);
		node->metric->data.value += value;
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		D_ERROR("Failed to increment gauge [%s] on item "
			"not a gauge.  Operation mismatch.",
			path);
		return -DER_OP_NOT_PERMITTED;
	}
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
 * \param[in]		item	Full path name to this \a item
 * 				If supplied, the path name is specified by the
 * 				\a item and any strings following it.
 *
 * \return			D_TM_SUCCESS		Success
 *				-DER_EXCEEDS_PATH_LEN	The full name length is
 *							too long
 *				-DER_OP_NOT_PERMITTED	Operation not permitted
 *							because the \a item is
 *							not a gauge
 */
int
d_tm_decrement_gauge(struct d_tm_node_t **metric, uint64_t value,
		     char *item, ...)
{
	struct d_tm_node_t	*node = NULL;
	char			path[D_TM_MAX_NAME_LEN] = {};
	int			rc = D_TM_SUCCESS;

	if ((metric != NULL) && (*metric != NULL))
		node = *metric;
	else {
		va_list	args;
		char	*str;

		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str != NULL) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN - 1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric != NULL)
			*metric = node;
	}

	if (node == NULL) {
		rc = d_tm_add_metric(&node, path, D_TM_GAUGE, "N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to decrement gauge [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric != NULL)
			*metric = node;
	}

	if (node->d_tm_type == D_TM_GAUGE) {
		D_MUTEX_LOCK(&node->lock);
		node->metric->data.value -= value;
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		D_ERROR("Failed to decrement gauge [%s] on item "
			"not a gauge.  Operation mismatch.",
			path);
		return -DER_OP_NOT_PERMITTED;
	}
	return rc;
}

/**
 * Finds the node pointing to the given metric described by path name provided
 *
 * \param[in]	cshmemRoot	Pointer to the shared memory segment
 * \param[in]	path		The full name of the metric to find
 *
 * \return			A pointer to the metric node
 */
struct d_tm_node_t *
d_tm_find_metric(uint64_t *cshmemRoot, char *path)
{
	struct d_tm_node_t	*parentNode = d_tm_get_root(cshmemRoot);
	struct d_tm_node_t	*node = NULL;
	char			str[D_TM_MAX_NAME_LEN];
	char			*token;
	char			*rest = str;

	sprintf(str, "%s", path);
	token = strtok_r(rest, "/", &rest);
	while (token != NULL) {
		node = d_tm_find_child(cshmemRoot, parentNode, token);
		if (node == NULL)
			return NULL;
		parentNode =  node;
		token = strtok_r(rest, "/", &rest);
	}
	return node;
}

/**
 * Adds a new metric at the specified path, with the given \a metricType.
 * An optional short description and long description may be added at this time.
 * This function may be called by the developer to initialize a metric at init
 * time in order to avoid the overhead of creating the metric at a more
 * critical time.
 *
 * \param[out]	node		Points to the new metric if supplied
 * \param[in]	metric		Full path name of the new metric to create
 * \param[in]	metricType	One of the corresponding d_tm_metric_types
 * \param[in]	shortDesc	A short description of the metric
 * \param[in]	longDesc	A long description of the metric
 *
 * \return			D_TM_SUCCESS		Success
 *				-DER_NO_SHMEM		Out of shared memory
 *				-DER_NOMEM		Out of global heap
 *				-DER_EXCEEDS_PATH_LEN	Node name exceeds
 *							path len
 *				-DER_INVAL		node is invalid
 *				-DER_ADD_METRIC_FAILED	Operation failed
 */
int
d_tm_add_metric(struct d_tm_node_t **node, char *metric, int metricType,
		char *shortDesc, char *longDesc)
{
	pthread_mutexattr_t	mattr;
	struct d_tm_node_t	*parentNode;
	char			*str = NULL;
	char			*token;
	char			*rest;
	int			buffLen;
	int 			rc;

	if (node == NULL)
		return -DER_INVAL;

	rc = D_MUTEX_LOCK(&addlock);
	if (rc != 0) {
		D_ERROR("Failed to get mutex rc = %d", rc);
		D_GOTO(failure, rc);
	}

	/* The item could exist due to a race condition where the
	* unprotected d_tm_find_metric() does not find the metric,
	* which leads to this d_tm_add_metric() call.
	* If the metric is found, it's not an error.  Just return.
	*/
	*node = d_tm_find_metric(shmemRoot, metric);
	if (*node != NULL) {
		D_MUTEX_UNLOCK(&addlock);
		return D_TM_SUCCESS;
	}

	D_STRNDUP(str, metric, D_TM_MAX_NAME_LEN);
	if (str == NULL)
		D_GOTO(failure, rc = -DER_NOMEM);

	if (strnlen(metric, D_TM_MAX_NAME_LEN) == D_TM_MAX_NAME_LEN)
		D_GOTO(failure, rc = -DER_EXCEEDS_PATH_LEN);

	rest = str;
	parentNode = d_tm_get_root(shmemRoot);
	token = strtok_r(rest, "/", &rest);
	while (token != NULL) {
		*node = d_tm_find_child(shmemRoot, parentNode, token);
		if (*node == NULL) {
			rc = d_tm_add_child(&(*node), parentNode, token);
			if ((rc == D_TM_SUCCESS) && (*node != NULL)) {
				parentNode = *node;
				(*node)->d_tm_type = D_TM_DIRECTORY;
			} else {
				D_GOTO(failure, rc);
			}
		} else {
			parentNode = *node;
		}
		token = strtok_r(rest, "/", &rest);
	}

	if (*node == NULL)
		D_GOTO(failure, rc = -DER_ADD_METRIC_FAILED);

	(*node)->d_tm_type = metricType;
	(*node)->metric = (struct d_tm_metric_t *)d_tm_shmalloc(
						  sizeof(struct d_tm_metric_t));
	if ((*node)->metric == NULL)
		D_GOTO(failure, rc = -DER_NO_SHMEM);

	/*
	 * initialize the data for this metric
	 * clearing data.tms[0] and data.tms[1] clears data.value
	 * because of the union
	 */
	(*node)->metric->data.tms[0].tv_sec = 0;
	(*node)->metric->data.tms[0].tv_nsec = 0;
	(*node)->metric->data.tms[1].tv_sec = 0;
	(*node)->metric->data.tms[1].tv_nsec = 0;

	buffLen = strnlen(shortDesc, D_TM_MAX_SHORT_LEN);
	if (buffLen == D_TM_MAX_SHORT_LEN)
		D_GOTO(failure, rc = -DER_EXCEEDS_PATH_LEN);
	buffLen += 1; /* make room for the trailing null */
	(*node)->metric->shortDesc = (char *)d_tm_shmalloc(buffLen *
							   sizeof(char));
	if ((*node)->metric->shortDesc == NULL)
		D_GOTO(failure, rc = -DER_NO_SHMEM);
	strncpy((*node)->metric->shortDesc, shortDesc, buffLen);

	buffLen = strnlen(shortDesc, D_TM_MAX_LONG_LEN);
	if (buffLen == D_TM_MAX_LONG_LEN)
		D_GOTO(failure, rc = -DER_EXCEEDS_PATH_LEN);
	buffLen += 1; /* make room for the trailing null */
	(*node)->metric->longDesc = (char *)d_tm_shmalloc(buffLen *
							  sizeof(char));
	if ((*node)->metric->longDesc == NULL)
		D_GOTO(failure, rc = -DER_NO_SHMEM);
	strncpy((*node)->metric->longDesc, longDesc, buffLen);

	rc = pthread_mutexattr_init(&mattr);
	if (rc != 0) {
		D_ERROR("pthread_mutexattr_init failed: rc = %d", rc);
		D_GOTO(failure, rc);
	}

	rc = pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
	if (rc != 0) {
		D_ERROR("pthread_mutexattr_setpshared failed: rc = %d", rc);
		D_GOTO(failure, rc);
	}

	rc = D_MUTEX_INIT(&(*node)->lock, &mattr);
	if (rc != 0) {
		D_ERROR("Mutex init failed: rc = %d", rc);
		D_GOTO(failure, rc);
	}
	pthread_mutexattr_destroy(&mattr);

	D_DEBUG(DB_TRACE, "successfully added item: [%s]\n", metric);

	free(str);
	D_MUTEX_UNLOCK(&addlock);
	return D_TM_SUCCESS;

failure:
	free(str);
	D_MUTEX_UNLOCK(&addlock);
	D_ERROR("Failed to add child node for [%s], add child error: %d",
		metric, rc);
	return rc;
}

/**
 * Client function to read the specified counter.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.  Access to the data is guarded by the use of the shared
 * mutex for this specific node.
 *
 * \param[in,out]	val		The value of the counter is stored here
 * \param[in]		cshmemRoot	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		D_TM_SUCCESS		Success
 * 			-DER_INVAL		Bad pointer
 * 			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a counter
 */
int
d_tm_get_counter(uint64_t *val, uint64_t *cshmemRoot, struct d_tm_node_t *node,
		 char *metric)
{
	struct d_tm_metric_t	*cMetric = NULL;

	if (val == NULL)
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->d_tm_type != D_TM_COUNTER)
		return -DER_OP_NOT_PERMITTED;

	cMetric = (struct d_tm_metric_t *)d_tm_conv_ptr(cshmemRoot,
							node->metric);
	if (cMetric != NULL) {
		D_MUTEX_LOCK(&node->lock);
		*val = cMetric->data.value;
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return D_TM_SUCCESS;
}

/*
 * Client function to read the specified timestamp.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.  Access to the data is guarded by the use of the shared
 * mutex for this specific node.
 *
 * \param[in,out]	val		The value of the timestamp is stored
 * 					here
 * \param[in]		cshmemRoot	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		D_TM_SUCCESS		Success
 * 			-DER_INVAL		Bad pointer
 * 			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a timestamp
 */
int
d_tm_get_timestamp(time_t *val, uint64_t *cshmemRoot, struct d_tm_node_t *node,
		   char *metric)
{
	struct d_tm_metric_t	*cMetric = NULL;

	if (val == NULL)
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->d_tm_type != D_TM_TIMESTAMP)
		return -DER_OP_NOT_PERMITTED;

	cMetric = (struct d_tm_metric_t *)d_tm_conv_ptr(cshmemRoot,
							node->metric);
	if (cMetric != NULL) {
		D_MUTEX_LOCK(&node->lock);
		*val = cMetric->data.value;
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return D_TM_SUCCESS;
}

/**
 * Client function to read the specified high resolution timer.  If the node is
 * provided, that pointer is used for the read.  Otherwise, a lookup by the
 * metric name is performed.  Access to the data is guarded by the use of the
 * mutex semaphore for this specific node.
 *
 * \param[in,out]	tms		The value of the timer is stored here
 * \param[in]		cshmemRoot	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		D_TM_SUCCESS		Success
 * 			-DER_INVAL		Bad pointer
 * 			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a high resolution
 *						timer
 */
int
d_tm_get_highres_timer(struct timespec *tms, uint64_t *cshmemRoot,
		       struct d_tm_node_t *node, char *metric)
{
	struct d_tm_metric_t	*cMetric = NULL;

	if (tms == NULL)
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->d_tm_type != D_TM_HIGH_RES_TIMER)
		return -DER_OP_NOT_PERMITTED;

	cMetric = (struct d_tm_metric_t *)d_tm_conv_ptr(cshmemRoot,
							node->metric);
	if (cMetric != NULL) {
		D_MUTEX_LOCK(&node->lock);
		tms->tv_sec = cMetric->data.tms[0].tv_sec;
		tms->tv_nsec = cMetric->data.tms[0].tv_nsec;
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return D_TM_SUCCESS;
}

/**
 * Client function to read the specified duration.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.  Access to the data is guarded by the use of the shared
 * mutex for this specific node.
 *
 * \param[in,out]	val		The value of the duration is stored here
 * \param[in]		cshmemRoot	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		D_TM_SUCCESS		Success
 * 			-DER_INVAL		Bad pointer
 * 			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a duration
 */
int
d_tm_get_duration(struct timespec *tms, uint64_t *cshmemRoot,
		  struct d_tm_node_t *node, char *metric)
{
	struct d_tm_metric_t	*cMetric = NULL;

	if (tms == NULL)
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (!(node->d_tm_type & D_TM_DURATION))
		return -DER_OP_NOT_PERMITTED;

	cMetric = (struct d_tm_metric_t *)d_tm_conv_ptr(cshmemRoot,
							node->metric);
	if (cMetric != NULL) {
		D_MUTEX_LOCK(&node->lock);
		tms->tv_sec = cMetric->data.tms[0].tv_sec;
		tms->tv_nsec = cMetric->data.tms[0].tv_nsec;
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return D_TM_SUCCESS;
}

/**
 * Client function to read the specified gauge.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.  Access to the data is guarded by the use of the shared
 * mutex for this specific node.
 *
 * \param[in,out]	val		The value of the gauge is stored here
 * \param[in]		cshmemRoot	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		D_TM_SUCCESS		Success
 * 			-DER_INVAL		Bad pointer
 * 			-DER_METRIC_NOT_FOUND	Metric not found
 *			-DER_OP_NOT_PERMITTED	Metric was not a gauge
 */
int
d_tm_get_gauge(uint64_t *val, uint64_t *cshmemRoot, struct d_tm_node_t *node,
	       char *metric)
{
	struct d_tm_metric_t	*cMetric = NULL;

	if (val == NULL)
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->d_tm_type != D_TM_GAUGE)
		return -DER_OP_NOT_PERMITTED;

	cMetric = (struct d_tm_metric_t *)d_tm_conv_ptr(cshmemRoot,
							node->metric);
	if (cMetric != NULL) {
		D_MUTEX_LOCK(&node->lock);
		*val = cMetric->data.value;
		D_MUTEX_UNLOCK(&node->lock);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return D_TM_SUCCESS;
}

/**
 * Client function to read the metadata for the specified metric.  If the node
 * is provided, that pointer is used for the read.  Otherwise, a lookup by the
 * metric name is performed.  Access to the data is guarded by the use of the
 * shared mutex for this specific node.  Memory is allocated for the
 * \a shortDesc and \a longDesc and should be freed by the caller.
 *
 * \param[in,out]	shortDesc	Memory is allocated and the short
 * 					description is copied here
 * \param[in,out]	longDesc	Memory is allocated and the long
 * 					description is copied here
 * \param[in]		cshmemRoot	Pointer to the shared memory segment
 * \param[in]		node		Pointer to the stored metric node
 * \param[in]		metric		Full path name to the stored metric
 *
 * \return		D_TM_SUCCESS		Success
 * 			-DER_INVAL		Bad pointer
 * 			-DER_METRIC_NOT_FOUND	Metric node not found
 *			-DER_OP_NOT_PERMITTED	Node is not a metric
 */
int d_tm_get_metadata(char **shortDesc, char **longDesc, uint64_t *cshmemRoot,
		      struct d_tm_node_t *node, char *metric)
{
	struct d_tm_metric_t	*cMetric = NULL;
	char			*shortDescStr;
	char			*longDescStr;

	if ((shortDesc == NULL) && (longDesc == NULL))
		return -DER_INVAL;

	if (node == NULL) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (node == NULL)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->d_tm_type == D_TM_DIRECTORY)
		return -DER_OP_NOT_PERMITTED;

	cMetric = (struct d_tm_metric_t *)d_tm_conv_ptr(cshmemRoot,
							node->metric);
	if (cMetric != NULL) {
		D_MUTEX_LOCK(&node->lock);

		shortDescStr = (char *)d_tm_conv_ptr(cshmemRoot,
						     cMetric->shortDesc);
		if ((shortDesc != NULL) && (shortDescStr != NULL))
			D_STRNDUP(*shortDesc, shortDescStr ? shortDescStr :
				  "N/A", D_TM_MAX_SHORT_LEN);

		longDescStr = (char *)d_tm_conv_ptr(cshmemRoot,
						    cMetric->longDesc);
		if ((longDesc != NULL) && (longDescStr != NULL))
			D_STRNDUP(*longDesc, longDescStr ? longDescStr : "N/A",
				  D_TM_MAX_LONG_LEN);

		D_MUTEX_UNLOCK(&node->lock);
	} else {
		return -DER_METRIC_NOT_FOUND;
	}
	return D_TM_SUCCESS;
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
 * Perform a directory listing at the path provided for the items described by
 * the \a d_tm_type bitmask.  A result is added to the list it if matches
 * one of the metric types specified by that filter mask. The mask may
 * be a combination of d_tm_metric_types.  The search is performed only on the
 * direct children specified by the path.  Returns a linked list that points to
 * each node found that matches the search criteria.  Adds elements to an
 * existing node list if head is already initialized. The client should free the
 * memory with d_tm_list_free().
 *
 * \param[in,out]	head		Pointer to a nodelist
 * \param[in]		cshmemRoot	Pointer to the shared memory segment
 * \param[in]		path		Full path name to the directory or
 *					metric to list
 * \param[in]		d_tm_type	A bitmask of d_tm_metric_types that
 *					filters the results.
 *
 * \return		D_TM_SUCCESS		Success
 *			-DER_NOMEM		Out of global heap
 *			-DER_EXCEEDS_PATH_LEN	The full name length is
 * 						too long
 *			-DER_METRIC_NOT_FOUND	No items were found at the path
 *						provided
 */
int
d_tm_list(struct d_tm_nodeList_t **head, uint64_t *cshmemRoot, char *path,
	  int d_tm_type)
{
	struct d_tm_nodeList_t	*nodelist = NULL;
	struct d_tm_node_t	*node = NULL;
	struct d_tm_node_t	*parentNode = NULL;
	char			*str = NULL;
	char			*token;
	char			*rest;
	int			rc = D_TM_SUCCESS;
	bool			searchSiblings = false;

	D_STRNDUP(str, path, D_TM_MAX_NAME_LEN);

	if (str == NULL) {
		D_ERROR("Failed to allocate memory for path");
		D_GOTO(failure, rc = -DER_NOMEM);
	}

	if (strnlen(str, D_TM_MAX_NAME_LEN) == D_TM_MAX_NAME_LEN) {
		D_ERROR("Path exceeds maximum length");
		D_GOTO(failure, rc = -DER_EXCEEDS_PATH_LEN);
	}
	rest = str;

	parentNode = d_tm_get_root(cshmemRoot);
	node = parentNode;
	if (parentNode != NULL) {
		token = strtok_r(rest, "/", &rest);
		while (token != NULL) {
			node = d_tm_find_child(cshmemRoot, parentNode, token);
			if (node == NULL)
				D_GOTO(failure, rc = -DER_METRIC_NOT_FOUND);
			parentNode = node;
			token = strtok_r(rest, "/", &rest);
		}

		if (node == NULL)
			node = parentNode;

		if (node->d_tm_type == D_TM_DIRECTORY) {
			searchSiblings = true;
			node = (struct d_tm_node_t *)
				d_tm_conv_ptr(cshmemRoot, node->child);
		}

		nodelist = *head;

		while (node != NULL) {
			if (d_tm_type & node->d_tm_type) {
				nodelist = d_tm_add_node(node, nodelist);
				if (*head == NULL)
					*head = nodelist;
			}
			if (searchSiblings)
				node = (struct d_tm_node_t *)
					d_tm_conv_ptr(cshmemRoot,
						      node->sibling);
			else
				node = NULL;
		}
	}
failure:
	free(str);
	return rc;
}

/**
 * Returns the number of metrics found at the \a path provided for the items
 * matching an element of the \a d_tm_type bitmask.  A result is counted if it
 * matches one of the metric types specified by that filter mask. The mask may
 * be a combination of d_tm_metric_types.  The count is performed only on the
 * direct children specified by the path.
 *
 * \param[in]		cshmemRoot	Pointer to the shared memory segment
 * \param[in]		path		Full path name to the directory or
 *					metric to count
 * \param[in]		d_tm_type	A bitmask of d_tm_metric_types that
 *					determines if an item should be counted
 *
 * \return		D_TM_SUCCESS		Success
 *			-DER_NOMEM		Out of global heap
 *			-DER_EXCEEDS_PATH_LEN	The full name length is
 * 						too long
 *			-DER_METRIC_NOT_FOUND	No items were found at the path
 *						provided
 */
uint64_t
d_tm_get_num_objects(uint64_t *cshmemRoot, char *path, int d_tm_type)
{
	struct d_tm_node_t	*parentNode = NULL;
	struct d_tm_node_t	*node = NULL;
	uint64_t		count = 0;
	char			str[D_TM_MAX_NAME_LEN];
	char			*token;
	char			*rest = str;

	sprintf(str, "%s", path);

	parentNode = d_tm_get_root(cshmemRoot);
	node = parentNode;
	if (parentNode != NULL) {
		token = strtok_r(rest, "/", &rest);
		while (token != NULL) {
			node = d_tm_find_child(cshmemRoot, parentNode, token);
			if (node == NULL)
				/** no node was found matching the token */
				return count;
			parentNode = node;
			token = strtok_r(rest, "/", &rest);
		}
		if (node == NULL)
			node = parentNode;

		if (node->d_tm_type == D_TM_DIRECTORY) {
			node = (struct d_tm_node_t *)
				d_tm_conv_ptr(cshmemRoot, node->child);
			while (node != NULL) {
				if (d_tm_type & node->d_tm_type)
					count++;
				node = (struct d_tm_node_t *)
					d_tm_conv_ptr(cshmemRoot,
						      node->sibling);
			}
		} else {
			if (d_tm_type & node->d_tm_type)
				count++;
		}
	}
	return count;
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
		head = nodeList->next;
		free(nodeList);
		nodeList = head;
	}
}

/**
 * Adds a node to an existing nodeList, or creates it if the list is empty.
 *
 * \param[in]	src		The src node to add
 * \param[in]	nodelist	The nodelist to add \a src to
 *
 * \return			Pointer to the new entry that was added.
 *				Subsequent calls can pass this back here
 *				as \a nodelist so that the new node can be added
 *				without traversing the list
 */
struct d_tm_nodeList_t *
d_tm_add_node(struct d_tm_node_t *src, struct d_tm_nodeList_t *nodelist)
{
	struct d_tm_nodeList_t	*list = NULL;

	if (nodelist == NULL) {
		nodelist = (struct d_tm_nodeList_t *)malloc(
						sizeof(struct d_tm_nodeList_t));
		if (nodelist) {
			nodelist->node = src;
			nodelist->next = NULL;
			return nodelist;
		}
		return NULL;
	}

	list = nodelist;

	/** advance to the last node in the list */
	while (list->next)
		list = list->next;

	list->next = (struct d_tm_nodeList_t *)malloc(
						sizeof(struct d_tm_nodeList_t));
	if (list->next) {
		list = list->next;
		list->node = src;
		list->next = NULL;
		return list;
	}
	return NULL;
}

/**
 * Server side function that allocates the shared memory segment for this rank.
 *
 * \param[in]	rank		A unique value that identifies the producer
 * 				process
 * \param[in]	mem_size	Size in bytes of the shared memory region
 *
 * \return			Address of the shared memory region
 * 				NULL if failure
 */
uint8_t *
d_tm_allocate_shared_memory(int rank, size_t mem_size)
{
	key_t	key;
	int	shmid;

	/** create a unique key for this rank */
	key = D_TM_SHARED_MEMORY_KEY + rank;
	shmid = shmget(key, mem_size, IPC_CREAT | 0666);
	if (shmid < 0)
		return NULL;

	return (uint8_t *)shmat(shmid, NULL, 0);
}

/**
 * Client side function that retrieves a pointer to the shared memory segment
 * for this rank.
 *
 * \param[in]	rank		A unique value that identifies the producer
 * 				process that the client seeks to read data from
 * \return			Address of the shared memory region
 * 				NULL if failure
 */
uint64_t *
d_tm_get_shared_memory(int rank)
{
	key_t	key;
	int	shmid;

	/** create a unique key for this rank */
	key = D_TM_SHARED_MEMORY_KEY + rank;
	shmid = shmget(key, 0, 0666);
	if (shmid < 0)
		return NULL;

	return (uint64_t *)shmat(shmid, NULL, 0);
}

/**
 * Allocates memory from within the shared memory pool with 16-bit alignment
 *
 * param[in]	length	Size in bytes of the region within the shared memory
 *			pool to allocate
 *
 * \return		Address of the allocated memory
 * 			NULL if there was no more memory available
 */
void *
d_tm_shmalloc(int length)
{
	if (length % sizeof(uint16_t) != 0) {
		length += sizeof(uint16_t);
		length &= ~(sizeof(uint16_t) - 1);
	}

	if (shmemIdx) {
		if ((shmemFree - length) > 0) {
			shmemFree -= length;
			shmemIdx += length;
			D_DEBUG(DB_TRACE,
				"Allocated %d bytes.  Now %" PRIu64 " remain\n",
				length, shmemFree);
			return shmemIdx - length;
		}
	}
	D_CRIT("Shared memory allocation failure!\n");
	return NULL;
}

/**
 * Validates that the pointer resides within the address space
 * of the client's shared memory region.
 *
 * \param[in]	cshmemRoot	Pointer to the shared memory segment
 * \param[in]	ptr		The pointer to validate
 *
 * \return	true		The pointer is valid
 *		false		The the pointer is invalid
 */
bool
d_tm_validate_shmem_ptr(uint64_t *cshmemRoot, void *ptr)
{
	if (((uint64_t)ptr < (uint64_t)cshmemRoot) ||
	    ((uint64_t)ptr >= (uint64_t)cshmemRoot + D_TM_SHARED_MEMORY_SIZE)) {
		D_DEBUG(DB_TRACE,
			"shmem ptr 0x%"PRIx64" was outside the shmem range "
			"0x%"PRIx64" to 0x%"PRIx64, (uint64_t)ptr,
			(uint64_t)cshmemRoot, (uint64_t)cshmemRoot +
			D_TM_SHARED_MEMORY_SIZE);
		return false;
	}
	return true;
}

/**
 * Convert the virtual address of the pointer in shared memory from a server
 * address to a client side virtual address.
 *
 * \param[in]	cshmemRoot	Pointer to the shared memory segment
 * \param[in]	ptr		The pointer to convert
 *
 * \return			A pointer to the item in the clients address
 * 				space
 * 				NULL if the pointer is invalid
 */
void *
d_tm_conv_ptr(uint64_t *cshmemRoot, void *ptr)
{
	void	*temp;

	if ((ptr == NULL) || (cshmemRoot == NULL))
		return NULL;

	temp = (void *)((uint64_t)cshmemRoot + ((uint64_t)ptr) -
			*(uint64_t *)cshmemRoot);

	if (d_tm_validate_shmem_ptr(cshmemRoot, temp))
		return temp;
	return NULL;
}