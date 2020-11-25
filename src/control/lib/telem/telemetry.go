//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//
// +build linux,amd64
//

package telemetry

/*
#cgo LDFLAGS: -lgurt
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <time.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdbool.h>
#include "gurt/common.h"
#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"

typedef struct d_tm_node_t *d_tm_node_p;
typedef struct d_tm_nodeList_t d_tm_nodeList;
typedef struct d_tm_metric_t *d_tm_metric_p;
typedef struct timespec tspec;

char *
d_tm_conv_char_ptr(uint64_t *cshmemRoot, void *ptr)
{
	return (char *)d_tm_conv_ptr(cshmemRoot, ptr);
}

void
readMetrics(uint64_t *shmemRoot, struct d_tm_node_t *root, char *dirname,
		 int iteration)
{
	uint64_t val;
	struct d_tm_nodeList_t *nodelist = NULL;
	struct d_tm_nodeList_t *head = NULL;
	char *shortDesc;
	char *longDesc;
	char *name;
	time_t clk;
	char tmp[64];
	int len = 0;
	int rc;
	struct timespec tms;

	printf("----------------------------------------\n");

	printf("iteration: %d - %s/\n", iteration, dirname);
	rc = d_tm_list(&nodelist, shmemRoot, dirname,
		       D_TM_DIRECTORY | D_TM_COUNTER | D_TM_TIMESTAMP |
		       D_TM_TIMER_SNAPSHOT | D_TM_DURATION |
		       D_TM_GAUGE);

	if (rc == D_TM_SUCCESS)
		head = nodelist;

	printf("There are %"PRIu64" objects in the unfiltered list\n",
	       d_tm_get_num_objects(shmemRoot, dirname,
				    D_TM_DIRECTORY | D_TM_COUNTER |
				    D_TM_TIMESTAMP |
				    D_TM_TIMER_SNAPSHOT |
				    D_TM_DURATION | D_TM_GAUGE));

	printf("There are %"PRIu64" objects in the filtered list\n",
	       d_tm_get_num_objects(shmemRoot, dirname,
				    D_TM_COUNTER | D_TM_TIMESTAMP));

	printf("There are %"PRIu64" metrics in the tree\n",
	       d_tm_count_metrics(shmemRoot, root));

	while (nodelist) {
		name = (char *)d_tm_conv_ptr(shmemRoot,
					     nodelist->dtnl_node->dtn_name);
		if (name == NULL)
			break;
		switch (nodelist->dtnl_node->dtn_type) {
		case D_TM_DIRECTORY:
			printf("\tDIRECTORY: %s has %"PRIu64
			       " metrics underneath it\n",
			       name ? name : "Unavailable",
			d_tm_count_metrics(shmemRoot, nodelist->dtnl_node));
			break;
		case D_TM_COUNTER:
			rc = d_tm_get_counter(&val, shmemRoot,
					      nodelist->dtnl_node, NULL);
			if (rc == D_TM_SUCCESS) {
				d_tm_get_metadata(&shortDesc, &longDesc,
						  shmemRoot,
						  nodelist->dtnl_node, NULL);
				printf("\tCOUNTER: %s %" PRIu64
				       " With metadata: %s and %s\n",
				       name ? name : "Unavailable", val,
				       shortDesc, longDesc);
				free(shortDesc);
				free(longDesc);
			} else
				printf("Error on counter read: %d\n", rc);
			break;
		case D_TM_TIMESTAMP:
			rc = d_tm_get_timestamp(&clk, shmemRoot,
						nodelist->dtnl_node, NULL);
			if (rc == D_TM_SUCCESS) {
				strncpy(tmp, ctime(&clk), sizeof(tmp) - 1);
				len = strlen(tmp);
				if (len) {
					if (tmp[len - 1] == '\n') {
						tmp[len - 1] = 0;
					}
					printf("\tTIMESTAMP %s: %s\n",
					name ? name : "Unavailable", tmp);
				}
			} else
				printf("Error on timestamp read: %d\n", rc);
			break;
		case D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_REALTIME:
			rc = d_tm_get_timer_snapshot(&tms, shmemRoot,
						     nodelist->dtnl_node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tREALTIME HIGH RES TIMER %s: %lds, "
				       "%ldns\n", name ? name :
				       "Unavailable", tms.tv_sec,
				       tms.tv_nsec);
			} else
				printf("Error on highres timer read: %d\n", rc);
			break;
		case D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_PROCESS_CPUTIME:
			rc = d_tm_get_timer_snapshot(&tms, shmemRoot,
						     nodelist->dtnl_node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tPROCESS HIGH RES TIMER %s: %lds, "
				       "%ldns\n", name ? name :
				       "Unavailable", tms.tv_sec,
				       tms.tv_nsec);
			} else
				printf("Error on highres timer read: %d\n", rc);
			break;
		case D_TM_TIMER_SNAPSHOT | D_TM_CLOCK_THREAD_CPUTIME:
			rc = d_tm_get_timer_snapshot(&tms, shmemRoot,
						    nodelist->dtnl_node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tTHREAD HIGH RES TIMER %s: %lds, "
				       "%ldns\n", name ? name :
				       "Unavailable", tms.tv_sec,
				       tms.tv_nsec);
			} else
				printf("Error on highres timer read: %d\n", rc);
			break;
		case D_TM_DURATION | D_TM_CLOCK_REALTIME:
			rc = d_tm_get_duration(&tms, shmemRoot,
					       nodelist->dtnl_node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tD_TM_CLOCK_REALTIME DURATION"
				       " %s: %.9fs\n", name ? name :
				       "Unavailable", tms.tv_sec +
				       tms.tv_nsec / 1e9);
			} else
				printf("Error on duration read: %d\n", rc);
			break;
		case D_TM_DURATION | D_TM_CLOCK_PROCESS_CPUTIME:
			rc = d_tm_get_duration(&tms, shmemRoot,
					       nodelist->dtnl_node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tD_TM_CLOCK_PROCESS_CPUTIME "
				       "DURATION %s: %.9fs\n",
				       name ? name : "Unavailable",
				       tms.tv_sec + tms.tv_nsec / 1e9);
			} else
				printf("Error on duration read: %d\n", rc);
			break;
		case D_TM_DURATION | D_TM_CLOCK_THREAD_CPUTIME:
			rc = d_tm_get_duration(&tms, shmemRoot,
					       nodelist->dtnl_node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tD_TM_CLOCK_THREAD_CPUTIME "
				       "DURATION %s: %.9fs\n",
				       name ? name : "Unavailable",
				       tms.tv_sec + tms.tv_nsec / 1e9);
			} else
				printf("Error on duration read: %d\n", rc);
			break;
		case D_TM_DURATION:
			rc = d_tm_get_duration(&tms, shmemRoot,
					       nodelist->dtnl_node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tDEFAULT REALTIME DURATION %s:"
				       " %.9fs\n", name ? name :
				       "Unavailable", tms.tv_sec +
				       tms.tv_nsec / 1e9);
			} else
				printf("Error on duration read: %d\n", rc);
			break;
		case D_TM_GAUGE:
			rc = d_tm_get_gauge(&val, shmemRoot,
					    nodelist->dtnl_node, NULL);
			if (rc == D_TM_SUCCESS) {
				printf("\tGAUGE: %s %" PRIu64 "\n",
				       name ? name :
				       "Unavailable", val);
			} else
				printf("Error on gauge read: %d\n", rc);
			break;
		default:
			printf("\tUNKNOWN!: %s Type: %d\n",
			       name ? name : "Unavailable",
			       nodelist->dtnl_node->dtn_type);
			break;
		}
		nodelist = nodelist->dtnl_next;
	}
	d_tm_list_free(head);
}

*/
import "C"


import (
//	"context"
	"fmt"
//	"io/ioutil"
//	"net"
//	"strconv"
//	"strings"
//	"sync"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

type logger interface {
	Debug(string)
	Debugf(string, ...interface{})
}

var log logger = logging.NewStdoutLogger("telemetry")

// SetLogger sets the package-level logger
func SetLogger(l logger) {
	log = l
}

func InitTelemetry(rank int) (*C.uint64_t, C.d_tm_node_p, error) {

	shmemRoot := C.d_tm_get_shared_memory(C.int(rank))
	if (shmemRoot == nil) {
		return nil, nil, errors.Errorf("no shared memory segment found for rank: %d", rank)
	}

	root := C.d_tm_get_root(shmemRoot)
	if (root == nil) {
		return nil, nil, errors.Errorf("no root node found in shared memory segment for rank: %d", rank)
	}

	return shmemRoot, root, nil
}

func BuildNodeList(shmemRoot *C.uint64_t, list *C.d_tm_nodeList, dirname string, filter int) (*C.d_tm_nodeList, error) {

	if (&list == nil) {
		fmt.Printf("Bad list ptr ... \n")
		return nil, errors.Errorf("error: bad list ptr")
	}

	rc := C.d_tm_list(&list, shmemRoot, C.CString(dirname), C.int(filter))
	if (rc == C.D_TM_SUCCESS) {
		return list, nil
	}
	return nil, errors.Errorf("error: %d", rc)
}

func convertNodeList(shmemRoot *C.uint64_t, nodeList *C.d_tm_nodeList) ([]string, []string, error) {
	var directory []string
	var metrics []string

	if (nodeList != nil) {
		for ; nodeList != nil; nodeList = nodeList.dtnl_next {
			name := C.d_tm_conv_char_ptr(shmemRoot, unsafe.Pointer(nodeList.dtnl_node.dtn_name))
			if (name == nil) {
				continue
			}
			if (nodeList.dtnl_node.dtn_type == C.D_TM_DIRECTORY) {
				directory = append(directory, C.GoString(name))
			} else {
				metrics = append(metrics, C.GoString(name))
			}
		}
	}
	return directory, metrics, nil
}


func DiscoveryTree(shmemRoot *C.uint64_t, nodeList *C.d_tm_nodeList, dirname string, filter int) (*C.d_tm_nodeList, error) {

	fmt.Printf("Discovering tree for: %s\n", dirname)

	nodeList, err := BuildNodeList(shmemRoot, nodeList, dirname, filter)
	if (err != nil) {
		return nodeList, err
	}

	head := nodeList

	directories, metrics, err := convertNodeList(shmemRoot, nodeList)

	fmt.Printf("The directories are:")
	for _, d := range directories {
		fmt.Printf("\t%s", d)
	}
	fmt.Printf("\n")

	fmt.Printf("The metrics are:")
	for _, m := range metrics {
		fmt.Printf("\t%s", m)
	}
	fmt.Printf("\n")

	for _, d := range directories {
		var subdir string
		if dirname == "" {
			subdir = d
		} else {
			subdir = dirname + "/" + d
		}
		nodeList, err = DiscoveryTree(shmemRoot, nodeList, subdir, filter)
	}

	return head, nil
}

func FindMetric(shmemRoot *C.uint64_t, name string) (C.d_tm_node_p) {
	return C.d_tm_find_metric(shmemRoot, C.CString(name))
}

func GetCounter(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (uint64, error) {
	var val C.uint64_t
	res := C.d_tm_get_counter(&val, shmemRoot, node, C.CString(name))
	if (res == C.D_TM_SUCCESS) {
		return uint64(val), nil
	}
	return 0, errors.Errorf("error %d", int(res))
}

func GetTimestamp(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (string, error) {
	var clk C.time_t;
	res := C.d_tm_get_timestamp(&clk, shmemRoot, node, C.CString(name))
	if (res == C.D_TM_SUCCESS) {
		return C.GoString(C.ctime(&clk))[:24], nil
	}
	return "", errors.Errorf("error %d", int(res))
}

func GetTimerSnapshot(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (uint64, uint64, error) {
	var tms C.tspec
	res := C.d_tm_get_timer_snapshot(&tms, shmemRoot, node, C.CString(name))
	if (res == C.D_TM_SUCCESS) {
		return uint64(tms.tv_sec), uint64(tms.tv_nsec), nil
	}
	return 0, 0, errors.Errorf("error %d", int(res))
}

func GetDuration(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (uint64, uint64, error) {
	var tms C.tspec
	res := C.d_tm_get_duration(&tms, shmemRoot, node, C.CString(name))
	if (res == C.D_TM_SUCCESS) {
		return uint64(tms.tv_sec), uint64(tms.tv_nsec), nil
	}
	return 0, 0, errors.Errorf("error %d", int(res))
}

func GetGauge(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (uint64, error) {
	var val C.uint64_t
	res := C.d_tm_get_gauge(&val, shmemRoot, node, C.CString(name))
	if (res == C.D_TM_SUCCESS) {
		return uint64(val), nil
	}
	return 0, errors.Errorf("error %d", int(res))
}

func Test(rank int, name string) error {
	var nl *C.d_tm_nodeList
//	var tnl *C.d_tm_nodeList

	//shmemRoot, root, err := InitTelemetry(rank)
	shmemRoot, _, err := InitTelemetry(rank)
	if (err != nil) {
		fmt.Printf("Failed to init telemetry for rank: %d\n", rank)
		return err;
	}

	node := FindMetric(shmemRoot, name)
	if (node == nil) {
		fmt.Printf("metric:[%s] was not found\n", name)
		return errors.Errorf("metric:[%s] was not found", name)
	}
/*
	switch node.dtn_type {
	case C.D_TM_COUNTER:
		val, err := GetCounter(shmemRoot, node, "")
		if (err != nil) {
			break
		}
		fmt.Printf("Counter: [%s] Value: %d\n", name, val)
	case C.D_TM_TIMESTAMP:
		ts, err := GetTimestamp(shmemRoot, node, "")
		if (err != nil) {
			break
		}
		fmt.Printf("Timestamp: [%s]:%s\n", name, ts)
	case C.D_TM_TIMER_SNAPSHOT:
		sec, ns, err := GetTimerSnapshot(shmemRoot, node, "")
		if (err != nil) {
			break
		}
		fmt.Printf("High resolution timer: [%s]: %d(sec) %d(ns) \n", name, sec, ns)
	case C.D_TM_DURATION | C.D_TM_CLOCK_REALTIME:
		sec, ns, err := GetDuration(shmemRoot, node, "")
		if (err != nil) {
			break
		}
		fmt.Printf("Duration w/clock realtime: [%s]: %.9fs\n", name, float64(sec) + float64(ns) / float64(1e9))
	case C.D_TM_DURATION | C.D_TM_CLOCK_PROCESS_CPUTIME:
		sec, ns, err := GetDuration(shmemRoot, node, "")
		if (err != nil) {
			break
		}
		fmt.Printf("Duration w/clock process CPU time: [%s]: %.9fs\n", name, float64(sec) + float64(ns) / float64(1e9))
	case C.D_TM_DURATION | C.D_TM_CLOCK_THREAD_CPUTIME:
		sec, ns, err := GetDuration(shmemRoot, node, "")
		if (err != nil) {
			break
		}
		fmt.Printf("Duration w/clock thread CPU time: [%s]: %.9fs\n", name, float64(sec) + float64(ns) / float64(1e9))
	case C.D_TM_GAUGE:
		val, err := GetGauge(shmemRoot, node, "")
		if (err != nil) {
			break
		}
		fmt.Printf("Gauge: [%s] Value: %d\n", name, val)
	case C.D_TM_DIRECTORY:
		fmt.Printf("Directory: [%s]\n", name)
	default:
	}

	filter := C.D_TM_DIRECTORY | C.D_TM_COUNTER | C.D_TM_TIMESTAMP | C.D_TM_TIMER_SNAPSHOT | C.D_TM_DURATION | C.D_TM_GAUGE
	dirname := "src/gurt/examples/telem_producer_example.c/main/"

	nodeList, err := BuildNodeList(shmemRoot, nl, dirname, filter)
	if (err != nil) {
		return err
	}

	dirname = "src/gurt/examples/telem_producer_example.c/test_function1/"
	nodeList, err = BuildNodeList(shmemRoot, nodeList, dirname, filter)
	if (err != nil) {
		return err
	}

	dirname = "src/gurt/examples/telem_producer_example.c/timer_snapshot/snapshot 3"
	nodeList, err = BuildNodeList(shmemRoot, nodeList, dirname, filter)
	if (err != nil) {
		return err
	}


	dirname = "src/gurt/examples/telem_producer_example.c/test_function2/"
	nodeList, err = BuildNodeList(shmemRoot, nodeList, dirname, filter)
	if (err != nil) {
		return err
	}

	dirname = "src/gurt/examples/telem_producer_example.c/timer_snapshot/"
	nodeList, err = BuildNodeList(shmemRoot, nodeList, dirname, filter)
	if (err != nil) {
		return err
	}

	dirname = "src/gurt/examples/telem_producer_example.c/timer_snapshot/snapshot 2"
	nodeList, err = BuildNodeList(shmemRoot, nodeList, dirname, filter)
	if (err != nil) {
		return err
	}

	dirname = "src/gurt/examples/telem_producer_example.c/timer_snapshot/snapshot 4"
	nodeList, err = BuildNodeList(shmemRoot, nodeList, dirname, filter)
	if (err != nil) {
		return err
	}

	if (nodeList != nil) {
		head := nodeList
		for ; nodeList != nil; nodeList = nodeList.dtnl_next {
			name := C.d_tm_conv_char_ptr(shmemRoot, unsafe.Pointer(nodeList.dtnl_node.dtn_name))
			if (name != nil) {
				fmt.Printf("Name is: %s\n", C.GoString(name))
			}
		}
		C.d_tm_list_free(head)
	}

	dirname = "src/gurt/examples/telem_producer_example.c/timer_snapshot"
	C.readMetrics(shmemRoot, root, C.CString(dirname), 0)

	dirname = "src/gurt/examples/telem_producer_example.c/main"
	C.readMetrics(shmemRoot, root, C.CString(dirname), 0)
*/
	filter := C.D_TM_DIRECTORY | C.D_TM_COUNTER | C.D_TM_TIMESTAMP | C.D_TM_TIMER_SNAPSHOT | C.D_TM_DURATION | C.D_TM_GAUGE
/*
	dirname = ""
	nodeList, err = BuildNodeList(shmemRoot, nl, dirname, filter)
	if (err != nil) {
		fmt.Printf("Error after build node list: %v\n", err)
		return err
	}
	if (nodeList != nil) {
		head := nodeList
		for ; nodeList != nil; nodeList = nodeList.dtnl_next {
			name := C.d_tm_conv_char_ptr(shmemRoot, unsafe.Pointer(nodeList.dtnl_node.dtn_name))
			if (name != nil) {
				fmt.Printf("Name (list 1) is: %s\n", C.GoString(name))
			}
		}
		C.d_tm_list_free(head)
	}
*/
	nl = nil
	//nl, err = DiscoveryTree(shmemRoot, nl, "src/gurt/examples/telem_producer_example.c", filter)
//	nl, err = DiscoveryTree(shmemRoot, nl, "src/gurt/examples/telem_producer_example.c", filter)
	nl, err = DiscoveryTree(shmemRoot, nl, "", filter)
/*
	if (nl2 != nil) {
		head := nl2
		for ; nl2 != nil; nl2 = nl2.dtnl_next {
			name := C.d_tm_conv_char_ptr(shmemRoot, unsafe.Pointer(nl2.dtnl_node.dtn_name))
			if (name != nil) {
				fmt.Printf("Final directory tree list has name: %s\n", C.GoString(name))
			}
		}
		C.d_tm_list_free(head)
	}
*/
	if (nl != nil) {
		head := nl
		for ; nl != nil; nl = nl.dtnl_next {
			name := C.d_tm_conv_char_ptr(shmemRoot, unsafe.Pointer(nl.dtnl_node.dtn_name))
			if (name != nil) {
				fmt.Printf("Final directory tree list has name: %s\n", C.GoString(name))
			}
		}
		C.d_tm_list_free(head)
	} else {
		fmt.Printf("nl is null .. doh!\n")
	}
	return nil
}
