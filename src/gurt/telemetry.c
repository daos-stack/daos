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

/*
 * These are server side global variables that apply to the entire server
 * side process
 */
d_tm_node_t * root = NULL;
uint64_t * shmemRoot = NULL;
uint8_t * shmemIdx = NULL;
uint64_t shmemFree = 0;
pthread_mutex_t addlock;

/*
 * Returns a pointer to the root node for the given shared memory segment
 */
d_tm_node_t *
d_tm_get_root(uint64_t *shmem)
{
	return (d_tm_node_t *) (shmem + 1);
}

/*
 * Search for a parent's child with the given name.
 * Return a pointer to the child if found.
 */
d_tm_node_t *
d_tm_find_child(uint64_t *cshmemRoot, d_tm_node_t *parent, char *name)
{
	d_tm_node_t *child = NULL;
	char *clientName;

	if (!parent)
		return NULL;

	if (!parent->child)
		return NULL;

	child = d_tm_convert_node_ptr(cshmemRoot, parent->child);

	clientName = NULL;
	if (child)
		clientName = d_tm_convert_char_ptr(cshmemRoot, child->name);

	while (child && strncmp(clientName, name, D_TM_MAX_NAME_LEN) != 0) {
		child = d_tm_convert_node_ptr(cshmemRoot, child->sibling);
		clientName = NULL;
		if (child)
			clientName = d_tm_convert_char_ptr(cshmemRoot,
							   child->name);
	}

	return child;
}

/*
 * Add a child node to a the tree in shared memory.
 * A child will either be a first child, or a sibling of an existing child.
 */
int
d_tm_add_child(d_tm_node_t **newnode, d_tm_node_t *parent, char *name)
{
	d_tm_node_t *child = parent->child;
	d_tm_node_t *sibling = parent->child;
	int rc = D_TM_SUCCESS;

	/* If there are no children, add the first child to this parent */
	if (!child) {
		*newnode = (d_tm_node_t *)d_tm_shmalloc(sizeof(d_tm_node_t));
		if (!*newnode) {
			rc = -DER_NO_SHMEM;
			D_GOTO(failure, rc);
		}
		int buffLen = strnlen(name, D_TM_MAX_NAME_LEN);
		if (buffLen == D_TM_MAX_NAME_LEN) {
			rc = -DER_EXCEEDS_PATH_LEN;
			D_GOTO(failure, rc);
		}
		buffLen += 1; /* make room for the trailing null */
		(*newnode)->name = (char *)d_tm_shmalloc(buffLen *
							 sizeof(char));
		if (!(*newnode)->name) {
			rc = -DER_NO_SHMEM;
			D_GOTO(failure, rc);
		}
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
	while (child) {
		sibling = child; /** youngest known child */
		child = child->sibling;
	}

	/* Add the new node to the sibling list */
	*newnode = (d_tm_node_t *)d_tm_shmalloc(sizeof(d_tm_node_t));
	if (*newnode) {
		int buffLen = strnlen(name, D_TM_MAX_NAME_LEN);
		if (buffLen == D_TM_MAX_NAME_LEN) {
			rc = -DER_EXCEEDS_PATH_LEN;
			D_GOTO(failure, rc);
		}
		buffLen += 1; /* make room for the trailing null */
		(*newnode)->name = (char *)d_tm_shmalloc(buffLen *
							 sizeof(char));
		if (!(*newnode)->name) {
			rc = -DER_NO_SHMEM;
			D_GOTO(failure, rc);
		}
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

/*
 * Initialize an instance of the telemetry and metrics API for the producer
 * process.
 */
int
d_tm_init(int rank, uint64_t memSize)
{
	char tmp[D_TM_MAX_NAME_LEN];
	uint64_t *base_addr = NULL;
	int rc = 0;
	int buffLen = 0;

	if (shmemRoot && root) {
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
		base_addr = (uint64_t *) d_tm_shmalloc(sizeof(uint64_t));
		/*
		 * Just allocated the pool, and this memory is dedicated
		 * for this process, so a d_tm_shmalloc failure cannot
		 * happen here.  Just checking for a null pointer for
		 * good hygiene.
		 */
		if (!base_addr) {
			rc = -DER_NO_SHMEM;
			D_ERROR("Out of shared memory: %d", rc);
			D_GOTO(failure, rc);
		}
		*base_addr = (uint64_t)shmemRoot;
	} else {
		rc = -DER_NO_SHMEM;
		D_ERROR("Could not allocate shared memory: %d", rc);
		D_GOTO(failure, rc);
	}

	root = (d_tm_node_t *) d_tm_shmalloc(sizeof(d_tm_node_t));

	/*
	 * Just allocated the pool, and this memory is dedicated
	 * for this process, so a d_tm_shmalloc failure cannot
	 * happen here.  Just checking for a null pointer for
	 * good hygiene.
	 */
	if (!root){
		rc = -DER_NO_SHMEM;
		D_ERROR("Out of shared memory: %d", rc);
		D_GOTO(failure, rc);
	}

	sprintf(tmp, "rank %d", rank);
	buffLen = strnlen(tmp, D_TM_MAX_NAME_LEN);
	if (buffLen == D_TM_MAX_NAME_LEN) {
		rc = -DER_EXCEEDS_PATH_LEN;
		D_ERROR("Path name exceeds length: %d", rc);
		D_GOTO(failure, rc);
	}
	buffLen += 1; /* make room for the trailing null */

	root->name = (char *)d_tm_shmalloc(buffLen * sizeof(char));
	if (!root->name){
		rc = -DER_NO_SHMEM;
		D_ERROR("Out of shared memory: %d", rc);
		D_GOTO(failure, rc);
	}
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

/*
 * Releases resources claimed by init
 * Currently, only detaches from the shared memory to keep the memory and the
 * shared mutex objects available for the client to read data even when the
 * producer has gone offline.
 */
void d_tm_fini(void)
{
	if (shmemRoot != NULL) {
		D_INFO("There are %"PRIu64" metrics in the tree\n",
			d_tm_count_metrics(shmemRoot, root));
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
	return;
}

/*
 * Recursively free resources underneath the given node.
 */
void
d_tm_free_node(uint64_t *cshmemRoot, d_tm_node_t *node)
{
	char *name;
	if (!node)
		return;

	if (node->d_tm_type != D_TM_DIRECTORY) {
		if (D_MUTEX_DESTROY(&node->lock) != 0) {
			name = d_tm_convert_char_ptr(cshmemRoot, node->name);
			D_ERROR("Failed to destroy mutex for node: %s", name);
		}
	}

	node = node->child;
	node = (d_tm_node_t *) d_tm_convert_node_ptr(cshmemRoot, node);
	if (node) {
		d_tm_free_node(cshmemRoot, node);
		node = node->sibling;
		node = (d_tm_node_t *) d_tm_convert_node_ptr(cshmemRoot, node);
		while(node) {
			d_tm_free_node(cshmemRoot, node);
			node = node->sibling;
			node = (d_tm_node_t *) d_tm_convert_node_ptr(cshmemRoot,
								     node);
		}
	}
	return;
}

/*
 * Recursively prints all nodes underneath the given node.
 * Used as a convenience function to demonstrate usage for the client
 */
void
d_tm_print_my_children(uint64_t *cshmemRoot, d_tm_node_t *node, int level)
{
	char *convertedNamePtr = NULL;
	int i = 0;
	time_t clk;
	uint64_t val;
	struct timespec tms;
	int rc;

	if (!node)
		return;

	convertedNamePtr = d_tm_convert_char_ptr(cshmemRoot, node->name);

	if (convertedNamePtr) {
		for (i = 0; i < level; i++)
			printf("%20s", " ");

		switch (node->d_tm_type) {
			char tmp[D_TM_TIME_BUFF_LEN];
			int len = 0;

			case D_TM_DIRECTORY:
				printf("%-20s\n", convertedNamePtr);
				break;
			case D_TM_COUNTER:
				rc = d_tm_get_counter(&val, cshmemRoot, node,
							 NULL);
				if (rc == D_TM_SUCCESS)
					printf("COUNTER: %s %" PRIu64 "\n",
						convertedNamePtr, val);
				else
					printf("Error on counter read: %d\n",
						rc);
				break;
			case D_TM_TIMESTAMP:
				rc = d_tm_get_timestamp(&clk, cshmemRoot,
							   node, NULL);
				if (rc == D_TM_SUCCESS) {
					strncpy(tmp, ctime(&clk), sizeof(tmp));
					len = strnlen(tmp,
						      D_TM_TIME_BUFF_LEN - 1);
					if (len) {
						if (tmp[len-1] == '\n') {
							tmp[len-1] = 0;
						}
					}
					printf("TIMESTAMP %s: %s\n",
						convertedNamePtr, tmp);
				} else
					printf("Error on timestamp read: %d\n",
						rc);
				break;
			case D_TM_HIGH_RES_TIMER:
				rc = d_tm_get_highres_timer(&tms, cshmemRoot,
								node, NULL);
				if (rc == D_TM_SUCCESS) {
					printf("HIGH RES TIMER %s:%lds, "
						"%ldns\n", convertedNamePtr,
						tms.tv_sec, tms.tv_nsec);
				} else
					printf("Error on highres timer read:"
						" %d\n", rc);
				break;
			case (D_TM_DURATION | D_TM_CLOCK_REALTIME):
				rc = d_tm_get_duration(&tms, cshmemRoot,
							  node, NULL);
				if (rc == D_TM_SUCCESS) {
					printf("REALTIME DURATION %s: %.9fs\n",
						convertedNamePtr, tms.tv_sec +
						tms.tv_nsec/1e9);
				} else
					printf("Error on duration read: %d\n",
						rc);
				break;
			case (D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME):
				rc = d_tm_get_duration(&tms, cshmemRoot,
							  node, NULL);
				if (rc == D_TM_SUCCESS) {
					printf("PROC CPU DURATION %s: %.9fs\n",
						convertedNamePtr, tms.tv_sec +
						tms.tv_nsec/1e9);
				} else
					printf("Error on duration read: %d\n",
						rc);
				break;
			case (D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME):
				rc = d_tm_get_duration(&tms, cshmemRoot,
							  node, NULL);
				if (rc == D_TM_SUCCESS) {
					printf("THRD CPU DURATION %s: %.9fs\n",
						convertedNamePtr, tms.tv_sec +
						tms.tv_nsec/1e9);
				} else
					printf("Error on duration read: %d\n",
						rc);
				break;
			case D_TM_GAUGE:
				rc = d_tm_get_gauge(&val, cshmemRoot, node,
							NULL);
				if (rc == D_TM_SUCCESS) {
					printf("GAUGE %s: %" PRIu64 "\n",
						convertedNamePtr, val);
				} else
					printf("Error on gauge read: %d\n", rc);
				break;
			default:
				printf("Unknown type: %d\n", node->d_tm_type);
				break;
		}
	}
	node = node->child;
	node = (d_tm_node_t *) d_tm_convert_node_ptr(cshmemRoot, node);

	if (node) {
		d_tm_print_my_children(cshmemRoot, node, level+1);
		node = node->sibling;
		node = (d_tm_node_t *) d_tm_convert_node_ptr(cshmemRoot, node);
		while(node) {
			d_tm_print_my_children(cshmemRoot, node, level+1);
			node = node->sibling;
			node = (d_tm_node_t *) d_tm_convert_node_ptr(cshmemRoot,
								   node);
		}
	}
}

/*
 * Recursively counts number of metrics underneath the given node.
 */
uint64_t
d_tm_count_metrics(uint64_t *cshmemRoot, d_tm_node_t *node)
{
	uint64_t count = 0;

	if (!node)
		return 0;

	if (node->d_tm_type != D_TM_DIRECTORY)
		count++;

	node = node->child;
	node = (d_tm_node_t *) d_tm_convert_node_ptr(cshmemRoot, node);
	if (node) {
		count += d_tm_count_metrics(cshmemRoot, node);
		node = node->sibling;
		node = (d_tm_node_t *) d_tm_convert_node_ptr(cshmemRoot, node);
		while(node) {
			count += d_tm_count_metrics(cshmemRoot, node);
			node = node->sibling;
			node = (d_tm_node_t *) d_tm_convert_node_ptr(cshmemRoot,
								     node);
		}
	}
	return count;
}

/*
 * Increment the given counter
 *
 * The counter is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 */
int
d_tm_increment_counter(d_tm_node_t **metric, char *item, ...)
{
	d_tm_node_t *node = NULL;
	char path[D_TM_MAX_NAME_LEN] = {};
	char *str;
	int rc = D_TM_SUCCESS;

	if (metric && *metric)
		node = *metric;
	else {
		va_list args;
		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN-1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric)
			*metric = node;
	}

	if (!node) {
		rc = d_tm_add_metric(&node, path, D_TM_COUNTER,
					"N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to incremement counter [%s]  "
				"Failed to add metric", path);
			return rc;
		}
		if (metric)
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


/*
 * Record the current timestamp
 *
 * The timestamp is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 */
int
d_tm_record_timestamp(d_tm_node_t **metric, char *item, ...)
{
	d_tm_node_t *node = NULL;
	char path[D_TM_MAX_NAME_LEN] = {};
	char *str;
	int rc = D_TM_SUCCESS;

	if (metric && *metric)
		node = *metric;
	else {
		va_list args;
		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN-1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric)
			*metric = node;
	}

	if (!node) {
		rc = d_tm_add_metric(&node, path, D_TM_TIMESTAMP,
					"N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to record timestamp [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric)
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

/*
 * Read and store a high resolution timer value
 *
 * The timer is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 */
int
d_tm_record_high_res_timer(d_tm_node_t **metric, char *item, ...)
{
	d_tm_node_t *node = NULL;
	char path[D_TM_MAX_NAME_LEN] = {};
	char *str;
	int rc = D_TM_SUCCESS;

	if (metric && *metric)
		node = *metric;
	else {
		va_list args;
		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN-1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric)
			*metric = node;
	}

	if (!node) {
		rc = d_tm_add_metric(&node, path, D_TM_HIGH_RES_TIMER,
					"N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to record high resolution timer [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric)
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

int
d_tm_clock_id(int clk_id) {
	switch (clk_id) {
		case D_TM_CLOCK_REALTIME:
			return CLOCK_REALTIME;
			break;
		case D_TM_CLOCK_PROCESS_CPUTIME:
			return CLOCK_PROCESS_CPUTIME_ID;
			break;
		case D_TM_CLOCK_THREAD_CPUTIME:
			return CLOCK_THREAD_CPUTIME_ID;
			break;
		default:
			return CLOCK_REALTIME;
			break;
	}
	return CLOCK_REALTIME;
}

/*
 * Record the start of a time interval (paired with d_tm_mark_duration_end())
 *
 * The duration is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 */
int
d_tm_mark_duration_start(d_tm_node_t **metric, int clk_id, char *item, ...)
{
	d_tm_node_t *node = NULL;
	char path[D_TM_MAX_NAME_LEN] = {};
	char *str;
	int rc = D_TM_SUCCESS;

	if (metric && *metric)
		node = *metric;
	else {
		va_list args;
		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN-1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric)
			*metric = node;
	}

	if (!node) {
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
		if (metric)
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

/*
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
 */
int
d_tm_mark_duration_end(d_tm_node_t **metric, char *item, ...)
{
	struct timespec end;
	d_tm_node_t *node = NULL;
	char path[D_TM_MAX_NAME_LEN] = {};
	char *str;
	int rc = D_TM_SUCCESS;

	if (metric && *metric)
		node = *metric;
	else {
		va_list args;
		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN-1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric)
			*metric = node;
	}

	if (!node) {
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

/*
 * Set an arbitrary value for the gauge.
 *
 * The gauge is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 */
int
d_tm_set_gauge(d_tm_node_t **metric, uint64_t value, char *item, ...)
{
	d_tm_node_t *node = NULL;
	char path[D_TM_MAX_NAME_LEN] = {};
	char *str;
	int rc = D_TM_SUCCESS;

	if (metric && *metric)
		node = *metric;
	else {
		va_list args;
		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN-1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric)
			*metric = node;
	}

	if (!node) {
		rc = d_tm_add_metric(&node, path, D_TM_GAUGE,
					"N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to set gauge [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric)
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

/*
 * Increments the gauge by the value provided
 *
 * The gauge is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 */
int
d_tm_increment_gauge(d_tm_node_t **metric, uint64_t value, char *item, ...)
{
	d_tm_node_t *node = NULL;
	char path[D_TM_MAX_NAME_LEN] = {};
	char *str;
	int rc = D_TM_SUCCESS;

	if (metric && *metric)
		node = *metric;
	else {
		va_list args;
		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN-1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric)
			*metric = node;
	}

	if (!node) {
		rc = d_tm_add_metric(&node, path, D_TM_GAUGE,
					"N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to incremement gauge [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric)
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

/*
 * Decrements the gauge by the value provided
 *
 * The gauge is specified either by an initialized pointer or by a fully
 * qualified item name.  If an initialized pointer is provided, the metric is
 * accessed directly.  Otherwise, a lookup is performed on the path name
 * provided in order to find the specified item.  If the item cannot be found,
 * it is created.  If the item is created, the callers pointer is initialized
 * for this item.  The pointer is used for direct access on subsequent calls for
 * faster access.
 */
int
d_tm_decrement_gauge(d_tm_node_t **metric, uint64_t value, char *item, ...)
{
	d_tm_node_t *node = NULL;
	char path[D_TM_MAX_NAME_LEN] = {};
	char *str;
	int rc = D_TM_SUCCESS;

	if (metric && *metric)
		node = *metric;
	else {
		va_list args;
		va_start(args, item);
		sprintf(path, "%s", item);
		str = va_arg(args, char *);
		while (str) {
			/*
			 * verify that adding the next str token + '/'
			 * will fit into the path string buffer
			 * If it fits, append it to the path
			 */
			if ((strnlen(path, D_TM_MAX_NAME_LEN) +
			     strnlen(str, D_TM_MAX_NAME_LEN) + 1) <
			     D_TM_MAX_NAME_LEN) {
				strncat(path, "/", 1);
				strncat(path, str, D_TM_MAX_NAME_LEN-1);
				str = va_arg(args, char *);
			} else {
				D_ERROR("Failed to find metric");
				return -DER_EXCEEDS_PATH_LEN;
			}
		}
		va_end(args);
		node = d_tm_find_metric(shmemRoot, path);
		if (metric)
			*metric = node;
	}

	if (!node) {
		rc = d_tm_add_metric(&node, path, D_TM_GAUGE,
					"N/A", "N/A");
		if (rc != D_TM_SUCCESS) {
			D_ERROR("Failed to decrement gauge [%s]  "
				"Failed to add metric.", path);
			return rc;
		}
		if (metric)
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

/*
 * Finds the node pointing to the given metric described by path name provided
 */
d_tm_node_t *
d_tm_find_metric(uint64_t *cshmemRoot, char *path)
{
	char str[256];
	char *token;
	char *rest = str;
	d_tm_node_t *node = NULL;
	d_tm_node_t *parentNode = d_tm_get_root(cshmemRoot);

	sprintf(str, "%s", path);
	while ((token = strtok_r(rest, "/", &rest))) {
		node = d_tm_find_child(cshmemRoot, parentNode, token);
		if (!node)
			return NULL;
		else
			parentNode =  node;
	}
	return node;
}

/*
 * Adds a new metric at the specified path, with the given metricType.
 * An optional short description and long description may be added at this time.
 * This function may be called by the developer to initialize a metric at init
 * time in order to avoid the overhead of creating the metric at a more
 * critical time.
 */
int
d_tm_add_metric(d_tm_node_t **node, char *metric, int metricType,
		char *shortDesc, char *longDesc)
{
	char *str;
	char *token;
	char *rest;
	int buffLen;
	d_tm_node_t *parentNode;
	int rc;
 	pthread_mutexattr_t mattr;

	if (!node)
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
	if (*node) {
		D_MUTEX_UNLOCK(&addlock);
		return D_TM_SUCCESS;
	}

	D_STRNDUP(str, metric, D_TM_MAX_NAME_LEN);
	if (!str) {
		rc = -DER_NOMEM;
		D_GOTO(failure, rc);
	}

	if (strnlen(metric, D_TM_MAX_NAME_LEN) == D_TM_MAX_NAME_LEN) {
		rc = -DER_EXCEEDS_PATH_LEN;
		D_GOTO(failure, rc);
	}

	rest = str;
	parentNode = d_tm_get_root(shmemRoot);
	while ((token = strtok_r(rest, "/", &rest))) {
		*node = d_tm_find_child(shmemRoot, parentNode,
					token);
		if (!*node) {
			rc = d_tm_add_child(&(*node), parentNode, token);
			if ((rc == D_TM_SUCCESS) && (*node != NULL)) {
				parentNode = *node;
				(*node)->d_tm_type = D_TM_DIRECTORY;
			} else
				D_GOTO(failure, rc);
		} else
			parentNode = *node;
	}

	if (!*node) {
		rc = -DER_ADD_METRIC_FAILED;
		D_GOTO(failure, rc);
	}

	(*node)->d_tm_type = metricType;
	(*node)->metric = (d_tm_metric_t *)d_tm_shmalloc(sizeof(d_tm_metric_t));
	if (!(*node)->metric) {
		rc = -DER_NO_SHMEM;
		D_GOTO(failure, rc);
	}

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
	if (buffLen == D_TM_MAX_SHORT_LEN) {
		rc = -DER_EXCEEDS_PATH_LEN;
		D_GOTO(failure, rc);
	}
	buffLen += 1; /* make room for the trailing null */
	(*node)->metric->shortDesc = (char *)d_tm_shmalloc(buffLen *
							   sizeof(char));
	if (!(*node)->metric->shortDesc) {
		rc = -DER_NO_SHMEM;
		D_GOTO(failure, rc);
	}
	strncpy((*node)->metric->shortDesc, shortDesc, buffLen);

	buffLen = strnlen(shortDesc, D_TM_MAX_LONG_LEN);
	if (buffLen == D_TM_MAX_LONG_LEN) {
		rc = -DER_EXCEEDS_PATH_LEN;
		D_GOTO(failure, rc);
	}
	buffLen += 1; /* make room for the trailing null */
	(*node)->metric->longDesc = (char *)d_tm_shmalloc(buffLen *
							  sizeof(char));
	if (!(*node)->metric->longDesc) {
		rc = -DER_NO_SHMEM;
		D_GOTO(failure, rc);
	}
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

/*
 * Client function to read the specified counter.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.  Access to the data is guarded by the use of the shared
 * semaphore for this specific node.  If the semaphore cannot be taken, the read
 * proceeds to avoid unlimited blocking.
 */
int
d_tm_get_counter(uint64_t *val, uint64_t *cshmemRoot, d_tm_node_t *node,
		    char *metric)
{
	d_tm_metric_t *cMetric = NULL;
	int rc = D_TM_SUCCESS;

	if (!val)
		return -DER_INVAL;

	if (!node) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (!node)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->d_tm_type != D_TM_COUNTER)
		return -DER_OP_NOT_PERMITTED;

	cMetric = d_tm_convert_metric_ptr(cshmemRoot, node->metric);
	if (cMetric) {
		D_MUTEX_LOCK(&node->lock);
		*val = cMetric->data.value;
		D_MUTEX_UNLOCK(&node->lock);
	} else
		rc = -DER_METRIC_NOT_FOUND;
	return rc;
}

/*
 * Client function to read the specified timestamp.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.  Access to the data is guarded by the use of the shared
 * semaphore for this specific node.  If the semaphore cannot be taken, the read
 * proceeds to avoid unlimited blocking.
 */
int
d_tm_get_timestamp(time_t *val, uint64_t *cshmemRoot, d_tm_node_t *node,
		      char *metric)
{
	d_tm_metric_t *cMetric = NULL;
	int rc = D_TM_SUCCESS;

	if (!val)
		return -DER_INVAL;

	if (!node) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (!node)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->d_tm_type != D_TM_TIMESTAMP)
		return -DER_OP_NOT_PERMITTED;

	cMetric = d_tm_convert_metric_ptr(cshmemRoot, node->metric);
	if (cMetric) {
		D_MUTEX_LOCK(&node->lock);
		*val = cMetric->data.value;
		D_MUTEX_UNLOCK(&node->lock);
	} else
		rc = -DER_METRIC_NOT_FOUND;
	return rc;
}

/*
 * Client function to read the specified high resolution timer.  If the node is
 * provided, that pointer is used for the read.  Otherwise, a lookup by the
 * metric name is performed.  Access to the data is guarded by the use of the
 * shared semaphore for this specific node.  If the semaphore cannot be taken,
 * the read proceeds to avoid unlimited blocking.
 */
int
d_tm_get_highres_timer(struct timespec *tms, uint64_t *cshmemRoot,
			  d_tm_node_t *node, char *metric)
{
	d_tm_metric_t *cMetric = NULL;
	int rc = D_TM_SUCCESS;

	if (!tms)
		return -DER_INVAL;

	if (!node) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (!node)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->d_tm_type != D_TM_HIGH_RES_TIMER)
		return -DER_OP_NOT_PERMITTED;

	cMetric = d_tm_convert_metric_ptr(cshmemRoot, node->metric);
	if (cMetric) {
		D_MUTEX_LOCK(&node->lock);
		tms->tv_sec = cMetric->data.tms[0].tv_sec;
		tms->tv_nsec = cMetric->data.tms[0].tv_nsec;
		D_MUTEX_UNLOCK(&node->lock);
	} else
		rc = -DER_METRIC_NOT_FOUND;
	return rc;
}

/*
 * Client function to read the specified duration.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.  Access to the data is guarded by the use of the shared
 * semaphore for this specific node.  If the semaphore cannot be taken, the read
 * proceeds to avoid unlimited blocking.
 */
int
d_tm_get_duration(struct timespec *tms, uint64_t *cshmemRoot, d_tm_node_t *node,
		     char *metric)
{
	d_tm_metric_t *cMetric = NULL;
	int rc = D_TM_SUCCESS;

	if (!tms)
		return -DER_INVAL;

	if (!node) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (!node)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (!(node->d_tm_type & D_TM_DURATION))
		return -DER_OP_NOT_PERMITTED;

	cMetric = d_tm_convert_metric_ptr(cshmemRoot, node->metric);
	if (cMetric) {
		D_MUTEX_LOCK(&node->lock);
		tms->tv_sec = cMetric->data.tms[0].tv_sec;
		tms->tv_nsec = cMetric->data.tms[0].tv_nsec;
		D_MUTEX_UNLOCK(&node->lock);
	} else
		rc = -DER_METRIC_NOT_FOUND;
	return rc;
}

/*
 * Client function to read the specified gauge.  If the node is provided,
 * that pointer is used for the read.  Otherwise, a lookup by the metric name
 * is performed.  Access to the data is guarded by the use of the shared
 * semaphore for this specific node.  If the semaphore cannot be taken, the read
 * proceeds to avoid unlimited blocking.
 */
int
d_tm_get_gauge(uint64_t *val, uint64_t *cshmemRoot, d_tm_node_t *node,
		  char *metric)
{
	d_tm_metric_t *cMetric = NULL;
	int rc = D_TM_SUCCESS;

	if (!val)
		return -DER_INVAL;

	if (!node) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (!node)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->d_tm_type != D_TM_GAUGE)
		return -DER_OP_NOT_PERMITTED;


	cMetric = d_tm_convert_metric_ptr(cshmemRoot, node->metric);
	if (cMetric) {
		D_MUTEX_LOCK(&node->lock);
		*val = cMetric->data.value;
		D_MUTEX_UNLOCK(&node->lock);
	} else
		rc = -DER_METRIC_NOT_FOUND;
	return rc;
}

int d_tm_get_metadata(char **shortDesc, char **longDesc, uint64_t *cshmemRoot,
	d_tm_node_t *node, char *metric)
{
	d_tm_metric_t *cMetric = NULL;
	int rc = D_TM_SUCCESS;
	char *shortDescStr;
	char *longDescStr;

	if (!shortDesc || !longDesc)
		return -DER_INVAL;

	if (!node) {
		node = d_tm_find_metric(cshmemRoot, metric);
		if (!node)
			return -DER_METRIC_NOT_FOUND;
	}

	if (!d_tm_validate_shmem_ptr(cshmemRoot, (void *)node))
		return -DER_METRIC_NOT_FOUND;

	if (node->d_tm_type == D_TM_DIRECTORY)
		return -DER_OP_NOT_PERMITTED;


	cMetric = d_tm_convert_metric_ptr(cshmemRoot, node->metric);
	if (cMetric) {
		D_MUTEX_LOCK(&node->lock);
		shortDescStr = d_tm_convert_char_ptr(cshmemRoot,
						     cMetric->shortDesc);
		if (shortDescStr)
			D_STRNDUP(*shortDesc, shortDescStr?shortDescStr:"N/A",
				D_TM_MAX_SHORT_LEN);
		longDescStr = d_tm_convert_char_ptr(cshmemRoot,
						    cMetric->longDesc);
		if (longDescStr)
			D_STRNDUP(*longDesc, longDescStr?longDescStr:"N/A",
				  D_TM_MAX_LONG_LEN);
		D_MUTEX_UNLOCK(&node->lock);
	} else
		rc = -DER_METRIC_NOT_FOUND;
	return rc;
}

/*
 * Returns the API version
 */
int
d_tm_get_version(void)
{
	return D_TM_VERSION;
}

/*
 * Perform a directory listing at the path provided for the items described by
 * the d_tm_type bit mask.  The mask may be a combination of
 * d_tm_metric_types.  The search is performed only on the direct children
 * specified by the path.  Returns a linked list that points to each node found
 * that matches the search criteria.
 *
 * The client should free the memory with d_tm_list_free().
 */

int
d_tm_list(d_tm_nodeList_t **head, uint64_t *cshmemRoot, char *path,
	  int d_tm_type)
{
	d_tm_node_t *node = NULL;
	d_tm_node_t *parentNode = NULL;
	d_tm_nodeList_t *nodelist = NULL;
	char *str;
	char *token;
	char *rest;
	int rc = D_TM_SUCCESS;

	D_STRNDUP(str, path, D_TM_MAX_NAME_LEN);

	if (!str) {
		D_ERROR("Failed to allocate memory for path");
		rc = -DER_NOMEM;
		D_GOTO(failure, rc);
	}

	if (strnlen(str, D_TM_MAX_NAME_LEN) == D_TM_MAX_NAME_LEN) {
		D_ERROR("Path exceeds maximum length");
		rc = -DER_EXCEEDS_PATH_LEN;
		D_GOTO(failure, rc);
	}
	rest = str;

	parentNode = d_tm_get_root(cshmemRoot);
	node = parentNode;
	if (parentNode) {
		while ((token = strtok_r(rest, "/", &rest))) {
			node = d_tm_find_child(cshmemRoot, parentNode,
						  token);
			if (!node) {
				/** no node was found matching the token */
				rc = -DER_METRIC_NOT_FOUND;
				D_GOTO(failure, rc);
			}
			else
				parentNode = node;
		}
		if (!node)
			node = parentNode;

		if (node->d_tm_type == D_TM_DIRECTORY) {
			node = d_tm_convert_node_ptr(cshmemRoot,
							node->child);
			while (node) {
				if (d_tm_type & node->d_tm_type) {
					nodelist = d_tm_add_node(node,
								    nodelist);
					if (!nodelist) {
						rc = -DER_NOMEM;
						D_GOTO(failure, rc);
					}
					if (!*head)
						*head = nodelist;
				}
				node = d_tm_convert_node_ptr(cshmemRoot,
								node->sibling);
			}
		} else {
			if (d_tm_type & node->d_tm_type) {
				nodelist = d_tm_add_node(node, nodelist);
				if (!nodelist) {
					rc = -DER_NOMEM;
					D_GOTO(failure, rc);
				}
				if (!*head)
					*head = nodelist;
			}
		}
	}
failure:
	free(str);
	return rc;
}

uint64_t
d_tm_get_num_objects(uint64_t *cshmemRoot, char *path, int d_tm_type)
{
	uint64_t count = 0;
	d_tm_node_t *node = NULL;
	d_tm_node_t *parentNode = NULL;
	char str[256];
	char *token;
	char *rest = str;

	sprintf(str, "%s", path);

	parentNode = d_tm_get_root(cshmemRoot);
	node = parentNode;
	if (parentNode) {
		while ((token = strtok_r(rest, "/", &rest))) {
			node = d_tm_find_child(cshmemRoot, parentNode,
						  token);
			if (!node)
				/** no node was found matching the token */
				return count;
			else
				parentNode = node;
		}
		if (!node)
			node = parentNode;

		if (node->d_tm_type == D_TM_DIRECTORY) {
			node = d_tm_convert_node_ptr(cshmemRoot,
							node->child);
			while (node) {
				if (d_tm_type & node->d_tm_type)
					count++;
				node = d_tm_convert_node_ptr(cshmemRoot,
								node->sibling);
			}
		} else {
			if (d_tm_type & node->d_tm_type)
				count++;
		}
	}
	return count;
}


/*
 * Frees the memory allocated for the given nodeList
 * that was allocated by d_tm_list()
 */
void
d_tm_list_free(d_tm_nodeList_t *nodeList)
{
	d_tm_nodeList_t *head = NULL;

	while(nodeList) {
		head = nodeList->next;
		free(nodeList);
		nodeList = head;
	}
}

/*
 * Adds a node to an existing nodeList, or creates it if the list is empty.
 */
d_tm_nodeList_t *
d_tm_add_node(d_tm_node_t *src, d_tm_nodeList_t *nodelist)
{
	d_tm_nodeList_t *list = NULL;

	if (!nodelist) {
		nodelist = (d_tm_nodeList_t *)malloc(sizeof(d_tm_nodeList_t));
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

	list->next = (d_tm_nodeList_t *)malloc(sizeof(d_tm_nodeList_t));
	if (list->next) {
		list = list->next;
		list->node = src;
		list->next = NULL;
		return list;
	}
	return NULL;
}

/*
 * Server side function that allocates the shared memory segment for this rank.
 */
uint8_t *
d_tm_allocate_shared_memory(int rank, size_t mem_size)
{
	int shmid;
	key_t key;

	/** create a unique key for this rank */
	key = D_TM_SHARED_MEMORY_KEY << rank;
	shmid = shmget(key, mem_size, IPC_CREAT | 0666);
	if (shmid < 0)
		return NULL;

	return (uint8_t *)shmat(shmid, NULL, 0);
}

/*
 * Client side function that retrieves a pointer to the shared memory segment
 * for this rank.
 */
uint8_t *
d_tm_get_shared_memory(int rank)
{
	int shmid;
	key_t key;

	/** create a unique key for this rank */
	key = D_TM_SHARED_MEMORY_KEY << rank;
	shmid = shmget(key, 0, 0666);
	if (shmid < 0)
		return NULL;

	return (uint8_t *) shmat(shmid, NULL, 0);
}

/*
 * Allocates memory from within the shared memory pool with 16-bit alignment
 */
void *
d_tm_shmalloc(int length)
{
	if (length % sizeof(uint16_t) != 0) {
 		length += sizeof(uint16_t);
 		length &= ~(sizeof(uint16_t)-1);
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

/*
 * Validates that the pointer resides within the address space
 * of the client's shared memory region.
 */
bool d_tm_validate_shmem_ptr(uint64_t *cshmemRoot, void *ptr)
{
	if (((uint64_t)ptr < (uint64_t)cshmemRoot) ||
	   ((uint64_t)ptr >= (uint64_t)cshmemRoot+D_TM_SHARED_MEMORY_SIZE)) {
		D_DEBUG(DB_TRACE,
			"shmem ptr 0x%"PRIx64" was outside the shmem range "
			"0x%"PRIx64" to 0x%"PRIx64, (uint64_t)ptr,
			(uint64_t)cshmemRoot, (uint64_t)cshmemRoot +
			D_TM_SHARED_MEMORY_SIZE);
		return false;
	}
	return true;
}


/*
 * Convert the virtual address of the pointer in shared memory from a server
 * address to a client side virtual address.
 */
d_tm_node_t *
d_tm_convert_node_ptr(uint64_t *cshmemRoot, void *ptr)
{
	d_tm_node_t *temp;

	if (!ptr || !cshmemRoot)
		return NULL;

	temp = (d_tm_node_t *) ((uint64_t)cshmemRoot + ((uint64_t)ptr) -
			   *(uint64_t *)cshmemRoot);

	if (d_tm_validate_shmem_ptr(cshmemRoot, temp))
		return temp;
	return NULL;
}

/*
 * Convert the virtual address of the pointer in shared memory from a server
 * address to a client side virtual address.
 */
d_tm_metric_t *
d_tm_convert_metric_ptr(uint64_t *cshmemRoot, void *ptr)
{
	d_tm_metric_t *temp;

	if (!ptr || !cshmemRoot)
		return NULL;

	temp = (d_tm_metric_t *) ((uint64_t)cshmemRoot + ((uint64_t)ptr) -
				     *(uint64_t *)cshmemRoot);

	if (d_tm_validate_shmem_ptr(cshmemRoot, temp))
		return temp;
	return NULL;
}

/*
 * Convert the virtual address of the pointer in shared memory from a server
 * address to a client side virtual address.
 */
char *
d_tm_convert_char_ptr(uint64_t *cshmemRoot, void *ptr)
{
	char *temp;

	if (!ptr || !cshmemRoot)
		return NULL;

	temp = (char *) ((uint64_t)cshmemRoot + ((uint64_t)ptr) -
			 *(uint64_t *)cshmemRoot);

	if (d_tm_validate_shmem_ptr(cshmemRoot, temp))
		return temp;
	return NULL;
}