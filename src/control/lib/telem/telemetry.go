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

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"

typedef struct d_tm_node_t *d_tm_node_p;
typedef struct d_tm_nodeList_t d_tm_nodeList;
typedef struct d_tm_metric_t *d_tm_metric_p;
typedef struct timespec tspec;
*/
import "C"

import (
	"unsafe"

	"github.com/pkg/errors"
)

type Counter struct {
	name  string
	value uint64
}

type Gauge struct {
	name  string
	value uint64
}

type Timestamp struct {
	name string
	ts   string
	clk  C.time_t
}

type Duration struct {
	name string
	sec  uint64
	nsec uint64
}

type Snapshot struct {
	name string
	sec  uint64
	nsec uint64
}

func InitTelemetry(rank int) (*C.uint64_t, C.d_tm_node_p, error) {

	shmemRoot := C.d_tm_get_shared_memory(C.int(rank))
	if shmemRoot == nil {
		return nil, nil, errors.Errorf("no shared memory segment found for rank: %d", rank)
	}

	root := C.d_tm_get_root(shmemRoot)
	if root == nil {
		return nil, nil, errors.Errorf("no root node found in shared memory segment for rank: %d", rank)
	}

	return shmemRoot, root, nil
}

func FindMetric(shmemRoot *C.uint64_t, name string) C.d_tm_node_p {
	return C.d_tm_find_metric(shmemRoot, C.CString(name))
}

func GetCounter(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (uint64, error) {
	var val C.uint64_t
	res := C.d_tm_get_counter(&val, shmemRoot, node, C.CString(name))
	if res == C.D_TM_SUCCESS {
		return uint64(val), nil
	}
	return 0, errors.Errorf("error %d", int(res))
}

func GetTimestamp(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (string, C.time_t, error) {
	var clk C.time_t
	res := C.d_tm_get_timestamp(&clk, shmemRoot, node, C.CString(name))
	if res == C.D_TM_SUCCESS {
		return C.GoString(C.ctime(&clk))[:24], clk, nil
	}
	return "", 0, errors.Errorf("error %d", int(res))
}

func GetTimerSnapshot(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (uint64, uint64, error) {
	var tms C.tspec
	res := C.d_tm_get_timer_snapshot(&tms, shmemRoot, node, C.CString(name))
	if res == C.D_TM_SUCCESS {
		return uint64(tms.tv_sec), uint64(tms.tv_nsec), nil
	}
	return 0, 0, errors.Errorf("error %d", int(res))
}

func GetDuration(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (uint64, uint64, error) {
	var tms C.tspec
	res := C.d_tm_get_duration(&tms, shmemRoot, node, C.CString(name))
	if res == C.D_TM_SUCCESS {
		return uint64(tms.tv_sec), uint64(tms.tv_nsec), nil
	}
	return 0, 0, errors.Errorf("error %d", int(res))
}

func GetGauge(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (uint64, error) {
	var val C.uint64_t
	res := C.d_tm_get_gauge(&val, shmemRoot, node, C.CString(name))
	if res == C.D_TM_SUCCESS {
		return uint64(val), nil
	}
	return 0, errors.Errorf("error %d", int(res))
}

func GetMetadata(shmemRoot *C.uint64_t, node C.d_tm_node_p, name string) (string, string, error) {
	var shortDesc *C.char
	var longDesc *C.char
	res := C.d_tm_get_metadata(&shortDesc, &longDesc, shmemRoot, node, C.CString(name))
	if res == C.D_TM_SUCCESS {
		short := C.GoString(shortDesc)
		long := C.GoString(longDesc)
		C.free(unsafe.Pointer(shortDesc))
		C.free(unsafe.Pointer(longDesc))
		return short, long, nil
	}
	return "", "", errors.Errorf("error %d", int(res))
}

func GetNodeName(shmemRoot *C.uint64_t, node C.d_tm_node_p) string {
	nodeName := (*C.char)(C.d_tm_conv_ptr(shmemRoot, unsafe.Pointer(node.dtn_name)))
	return C.GoString(nodeName)
}

func CountMetrics(shmemRoot *C.uint64_t, node C.d_tm_node_p, filter int) uint64 {
	num := C.d_tm_count_metrics(shmemRoot, node, C.int(filter))
	return uint64(num)
}

func PrintMyChildren(shmemRoot *C.uint64_t, node C.d_tm_node_p, level int, stream *C.FILE) {
	C.d_tm_print_my_children(shmemRoot, node, C.int(level), stream)
}

func PrintNode(shmemRoot *C.uint64_t, node C.d_tm_node_p, level int, stream *C.FILE) {
	C.d_tm_print_node(shmemRoot, node, C.int(level), stream)
}

func PrintCounter(val uint64, name string, stream *C.FILE) {
	C.d_tm_print_counter(C.uint64_t(val), C.CString(name), stream)
}

func PrintTimestamp(clk *C.time_t, name string, stream *C.FILE) {
	C.d_tm_print_timestamp(clk, C.CString(name), stream)
}

func PrintTimerSnapshot(sec uint64, nsec uint64, name string, tm_type C.int, stream *C.FILE) {
	var tms C.tspec

	tms.tv_sec = C.time_t(sec)
	tms.tv_nsec = C.long(nsec)
	C.d_tm_print_timer_snapshot(&tms, C.CString(name), tm_type, stream)
}

func PrintDuration(sec uint64, nsec uint64, name string, tm_type C.int, stream *C.FILE) {
	var tms C.tspec

	tms.tv_sec = C.time_t(sec)
	tms.tv_nsec = C.long(nsec)
	C.d_tm_print_duration(&tms, C.CString(name), tm_type, stream)
}

func PrintGauge(val uint64, name string, stream *C.FILE) {
	C.d_tm_print_gauge(C.uint64_t(val), C.CString(name), stream)
}

func ListFree(nodeList *C.d_tm_nodeList) {
	C.d_tm_list_free(nodeList)
}

func GetAPIVersion() int {
	version := C.d_tm_get_version()
	return int(version)
}
