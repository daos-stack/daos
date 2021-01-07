//
// (C) Copyright 2020-2021 Intel Corporation.
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

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"

typedef struct d_tm_node_t *d_tm_node_p;
typedef struct d_tm_nodeList_t d_tm_nodeList;
typedef struct d_tm_metric_t *d_tm_metric_p;
typedef struct timespec tspec;
*/
import "C"
import (
	"context"
	"fmt"
	"strings"
	"time"
	"unsafe"

	"github.com/pkg/errors"
)

func ConvertNodeList(shmemRoot *C.uint64_t, nodeList *C.d_tm_nodeList) ([]string, []string, error) {
	var directory []string
	var metrics []string

	if nodeList != nil {
		for ; nodeList != nil; nodeList = nodeList.dtnl_next {
			name := (*C.char)(C.d_tm_conv_ptr(shmemRoot, unsafe.Pointer(nodeList.dtnl_node.dtn_name)))
			if name == nil {
				continue
			}
			if nodeList.dtnl_node.dtn_type == C.D_TM_DIRECTORY {
				directory = append(directory, C.GoString(name))
			} else {
				metrics = append(metrics, C.GoString(name))
			}
		}
	}
	return directory, metrics, nil
}

func GetDirListing(shmemRoot *C.uint64_t, nl **C.d_tm_nodeList, dirname string, filter int) error {
	node := C.d_tm_get_root(shmemRoot)

	if dirname != "/" && dirname != "" {
		node = FindMetric(shmemRoot, dirname)
	}

	if node == nil {
		return errors.Errorf("Directory or metric:[%s] was not found", dirname)
	}

	rc := C.d_tm_list(nl, shmemRoot, node, C.int(filter))
	if rc != C.D_TM_SUCCESS {
		return errors.Errorf("Unable to find entry for %s.  rc = %d\n", dirname, rc)
	}

	return nil
}

func UpdateCounters(shmemRoot *C.uint64_t, counterMap map[C.d_tm_node_p]Counter) error {
	for node, _ := range counterMap {
		nodeName := GetNodeName(shmemRoot, node)
		val, err := GetCounter(shmemRoot, node, "")
		if err != nil {
			continue
		}
		counterMap[node] = Counter{
			name:  nodeName,
			value: val,
		}
	}
	return nil
}

func UpdateGauges(shmemRoot *C.uint64_t, gaugeMap map[C.d_tm_node_p]Gauge) error {
	for node, _ := range gaugeMap {
		nodeName := GetNodeName(shmemRoot, node)
		val, err := GetGauge(shmemRoot, node, "")
		if err != nil {
			continue
		}
		gaugeMap[node] = Gauge{
			name:  nodeName,
			value: val,
		}
	}
	return nil
}

func UpdateSnapshots(shmemRoot *C.uint64_t, snapshotMap map[C.d_tm_node_p]Snapshot) error {
	for node, _ := range snapshotMap {
		nodeName := GetNodeName(shmemRoot, node)
		sec, nsec, err := GetTimerSnapshot(shmemRoot, node, "")
		if err != nil {
			continue
		}
		snapshotMap[node] = Snapshot{
			name: nodeName,
			sec:  sec,
			nsec: nsec,
		}
	}
	return nil
}

func InitMaps(ctx context.Context, shmemRoot *C.uint64_t, nl *C.d_tm_nodeList, counterMap map[C.d_tm_node_p]Counter, gaugeMap map[C.d_tm_node_p]Gauge, timestampMap map[C.d_tm_node_p]*Timestamp, durationMap map[C.d_tm_node_p]*Duration, snapshotMap map[C.d_tm_node_p]Snapshot) error {
	if nl == nil {
		return errors.Errorf("nodelist is uninitialized\n")
	}

	for ; nl != nil; nl = nl.dtnl_next {
		nodeName := GetNodeName(shmemRoot, nl.dtnl_node)
		switch nl.dtnl_node.dtn_type {
		case C.D_TM_DIRECTORY:
		case C.D_TM_COUNTER:
			val, err := GetCounter(shmemRoot, nl.dtnl_node, "")
			if err != nil {
				continue
			}
			counterMap[nl.dtnl_node] = Counter{
				name:  nodeName,
				value: val,
			}
		case C.D_TM_TIMESTAMP:
			ts, err := GetTimestamp(ctx, nodeName)
			if err != nil {
				continue
			}
			timestampMap[nl.dtnl_node] = ts
		case C.D_TM_TIMER_SNAPSHOT | C.D_TM_CLOCK_REALTIME:
			fallthrough
		case C.D_TM_TIMER_SNAPSHOT | C.D_TM_CLOCK_PROCESS_CPUTIME:
			fallthrough
		case C.D_TM_TIMER_SNAPSHOT | C.D_TM_CLOCK_THREAD_CPUTIME:
			sec, nsec, err := GetTimerSnapshot(shmemRoot, nl.dtnl_node, "")
			if err != nil {
				continue
			}
			snapshotMap[nl.dtnl_node] = Snapshot{
				name: nodeName,
				sec:  sec,
				nsec: nsec,
			}
		case C.D_TM_DURATION | C.D_TM_CLOCK_REALTIME:
			fallthrough
		case C.D_TM_DURATION | C.D_TM_CLOCK_PROCESS_CPUTIME:
			fallthrough
		case C.D_TM_DURATION | C.D_TM_CLOCK_THREAD_CPUTIME:
			d, err := GetDuration(ctx, nodeName)
			if err != nil {
				continue
			}
			durationMap[nl.dtnl_node] = d
		case C.D_TM_GAUGE:
			val, err := GetGauge(shmemRoot, nl.dtnl_node, "")
			if err != nil {
				continue
			}
			gaugeMap[nl.dtnl_node] = Gauge{
				name:  nodeName,
				value: val,
			}
		default:
			return errors.Errorf("Unknown type: %s : %d\n", nodeName, nl.dtnl_node.dtn_type)
		}
	}
	return nil
}

func PrintNodeList(shmemRoot *C.uint64_t, nl *C.d_tm_nodeList, dirname string, level int) error {
	if nl == nil {
		return errors.Errorf("nodelist is uninitialized\n")
	}

	if dirname == "" {
		dirname = "/"
	}
	filter := C.D_TM_DIRECTORY | C.D_TM_COUNTER | C.D_TM_TIMESTAMP | C.D_TM_TIMER_SNAPSHOT | C.D_TM_DURATION | C.D_TM_GAUGE
	numMetrics := CountMetrics(shmemRoot, nl.dtnl_node, filter)

	i := 0
	fmt.Printf("There are %d metrics under: %s\n", numMetrics, dirname)

	for ; nl != nil; nl = nl.dtnl_next {
		if nl.dtnl_node.dtn_type != C.D_TM_DIRECTORY {
			fmt.Printf("%d) ", i)
			PrintNode(shmemRoot, nl.dtnl_node, level, C.stdout)
			i++
		}
		short, long, err := GetMetadata(shmemRoot, nl.dtnl_node, "")
		if err == nil && (short != "N/A" || long != "N/A") {
			fmt.Printf("\tMetadata %s, %s\n", short, long)
		}
	}
	return nil
}

func ReadMetrics(ctx context.Context, shmemRoot *C.uint64_t, counterMap map[C.d_tm_node_p]Counter, gaugeMap map[C.d_tm_node_p]Gauge, snapshotMap map[C.d_tm_node_p]Snapshot) {
	err := UpdateCounters(shmemRoot, counterMap)
	if err != nil {
		fmt.Printf("Error %v", err)
	}

	err = UpdateGauges(shmemRoot, gaugeMap)
	if err != nil {
		fmt.Printf("Error %v", err)
	}

	err = UpdateSnapshots(shmemRoot, snapshotMap)
	if err != nil {
		fmt.Printf("Error %v", err)
	}
}

// Display a telemetry tree starting from the given directory for the given rank.
// If no directory is specified, the tree starts at the root.  The data is only
// printed.  It is not stored anywhere for later use.
func ShowDirectoryTree(rank int, dirname string, iterations int) error {
	shmemRoot, root, err := InitTelemetry(rank)
	if err != nil {
		fmt.Printf("Failed to init telemetry for rank: %d\n", rank)
		return err
	}

	fmt.Printf("DAOS Telemetry & Metrics API version: %d\n", GetAPIVersion())

	node := root
	if dirname != "/" && dirname != "" {
		node = FindMetric(shmemRoot, dirname)
		if node == nil {
			return errors.Errorf("Could not find an entry for: %s", dirname)
		}
	}

	filter := C.D_TM_COUNTER | C.D_TM_TIMESTAMP | C.D_TM_TIMER_SNAPSHOT | C.D_TM_DURATION | C.D_TM_GAUGE
	if iterations <= 1 {
		iterations = 1
	}

	for i := 0; i < iterations; i++ {
		numMetrics := CountMetrics(shmemRoot, node, filter)
		fmt.Printf("There are %d metrics found in the tree.\n", numMetrics)
		fmt.Printf("\nIteration %d/%d\n", i+1, iterations)
		PrintMyChildren(shmemRoot, node, 0, C.stdout)
		time.Sleep(time.Second)
	}
	return nil
}

// This example shows one method for exploring the telemetry tree to build maps
// that facilitate reading the associated metric's data.  In this example, the
// metrics are read in a loop 'iterations' times, and each entry is printed. A
// 1 second delay is inserted between each iteration
func ListMetrics(rank int, dirname string, iterations int, filterString string) error {
	var nl *C.d_tm_nodeList

	shmemRoot, _, err := InitTelemetry(rank)
	if err != nil {
		fmt.Printf("Failed to init telemetry for rank: %d\n", rank)
		return err
	}
	ctx, err := Init(uint32(rank))
	if err != nil {
		fmt.Printf("Failed to init telemetry for rank: %d\n", rank)
		return err
	}

	fmt.Printf("DAOS Telemetry & Metrics API version: %d\n", GetAPIVersion())

	filter := 0
	if strings.Contains(filterString, "c") {
		filter |= C.D_TM_COUNTER
	}

	if strings.Contains(filterString, "d") {
		filter |= C.D_TM_DURATION
	}

	if strings.Contains(filterString, "g") {
		filter |= C.D_TM_GAUGE
	}

	if strings.Contains(filterString, "s") {
		filter |= C.D_TM_TIMER_SNAPSHOT
	}

	if strings.Contains(filterString, "t") {
		filter |= C.D_TM_TIMESTAMP
	}

	err = GetDirListing(shmemRoot, &nl, dirname, filter)
	if err != nil {
		return err
	}

	counterMap := make(map[C.d_tm_node_p]Counter)
	gaugeMap := make(map[C.d_tm_node_p]Gauge)
	timestampMap := make(map[C.d_tm_node_p]*Timestamp)
	durationMap := make(map[C.d_tm_node_p]*Duration)
	snapshotMap := make(map[C.d_tm_node_p]Snapshot)

	// Initialize maps for each data type with the metrics found in the node list
	err = InitMaps(ctx, shmemRoot, nl, counterMap, gaugeMap, timestampMap, durationMap, snapshotMap)
	if err != nil {
		return err
	}
	ListFree(nl)

	fmt.Printf("There are %d counters, %d gauges, %d timestamps, %d durations and %d snapshots\n",
		len(counterMap), len(gaugeMap), len(timestampMap), len(durationMap), len(snapshotMap))

	if iterations <= 1 {
		iterations = 1
	}

	for i := 0; i < iterations; i++ {

		// Iterate through all the nodes stored in all the maps, to read the metrics associated
		// with each node.  The data is stored in associated map entry.
		ReadMetrics(ctx, shmemRoot, counterMap, gaugeMap, snapshotMap)
		fmt.Printf("\nIteration %d/%d\n", i+1, iterations)

		// For the demonstration, print out each of the metrics
		for _, counter := range counterMap {
			fmt.Printf("\tCounter: %s = %d\n", counter.name, counter.value)
		}
		for _, gauge := range gaugeMap {
			fmt.Printf("\tGauge: %s = %d\n", gauge.name, gauge.value)
		}
		for _, timestamp := range timestampMap {
			fmt.Printf("\tTimestamp: %s: %s\n", timestamp.Name, timestamp.Value())
		}
		for node, duration := range durationMap {
			var name string
			switch node.dtn_type {
			case C.D_TM_DURATION | C.D_TM_CLOCK_REALTIME:
				name = "Duration (realtime)"
			case C.D_TM_DURATION | C.D_TM_CLOCK_PROCESS_CPUTIME:
				name = "Duration (process)"
			case C.D_TM_DURATION | C.D_TM_CLOCK_THREAD_CPUTIME:
				name = "Duration (thread)"
			default:
				name = "Duration (Unknown!)"
			}
			fmt.Printf("\t%s: %s = %s\n", name, duration.Name, duration.Value())
		}
		for node, snapshot := range snapshotMap {
			var name string
			switch node.dtn_type {
			case C.D_TM_TIMER_SNAPSHOT | C.D_TM_CLOCK_REALTIME:
				name = "Timer snapshot (realtime)"
			case C.D_TM_TIMER_SNAPSHOT | C.D_TM_CLOCK_PROCESS_CPUTIME:
				name = "Timer snapshot (process)"
			case C.D_TM_TIMER_SNAPSHOT | C.D_TM_CLOCK_THREAD_CPUTIME:
				name = "Timer snapshot (thread)"
			default:
				name = "Timer snapshot (Unknown!)"
			}
			fmt.Printf("\t%s: %s = %ds, %dns\n", name, snapshot.name, snapshot.sec, snapshot.nsec)
		}

		time.Sleep(time.Second)
	}
	return nil
}
