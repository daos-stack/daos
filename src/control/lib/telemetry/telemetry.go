//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// +build linux,amd64
//

package telemetry

/*
#cgo LDFLAGS: -lgurt

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"

static uint64_t
get_min_int(struct d_tm_stats_t *stats)
{
	return stats->dtm_min.min_int;
}

static double
get_min_float(struct d_tm_stats_t *stats)
{
	return stats->dtm_min.min_float;
}

static uint64_t
get_max_int(struct d_tm_stats_t *stats)
{
	return stats->dtm_max.max_int;
}

static double
get_max_float(struct d_tm_stats_t *stats)
{
	return stats->dtm_max.max_float;
}

static uint64_t
get_sum_int(struct d_tm_stats_t *stats)
{
	return stats->dtm_sum.sum_int;
}

static double
get_sum_float(struct d_tm_stats_t *stats)
{
	return stats->dtm_sum.sum_float;
}
*/
import "C"

import (
	"bytes"
	"context"
	"io"
	"os"
	"strings"
	"sync"
	"unsafe"

	"github.com/pkg/errors"
)

type MetricType int

const (
	MetricTypeUnknown   MetricType = 0
	MetricTypeCounter   MetricType = C.D_TM_COUNTER
	MetricTypeDuration  MetricType = C.D_TM_DURATION
	MetricTypeGauge     MetricType = C.D_TM_GAUGE
	MetricTypeSnapshot  MetricType = C.D_TM_TIMER_SNAPSHOT
	MetricTypeTimestamp MetricType = C.D_TM_TIMESTAMP
)

type (
	Metric interface {
		Path() string
		Name() string
		Type() MetricType
		ShortDesc() string
		LongDesc() string
		FloatValue() float64
		String() string
	}

	StatsMetric interface {
		Metric
		FloatMin() float64
		FloatMax() float64
		FloatSum() float64
		Mean() float64
		StdDev() float64
	}
)

type (
	handle struct {
		sync.RWMutex
		idx   uint32
		rank  *uint32
		shmem *C.uint64_t
		root  *C.struct_d_tm_node_t
	}

	metricBase struct {
		handle *handle
		node   *C.struct_d_tm_node_t

		path      string
		name      *string
		shortDesc *string
		longDesc  *string
	}

	statsMetric struct {
		metricBase
		stats C.struct_d_tm_stats_t
	}

	telemetryKey string
)

const (
	handleKey telemetryKey = "handle"
)

func getHandle(ctx context.Context) (*handle, error) {
	handle, ok := ctx.Value(handleKey).(*handle)
	if !ok {
		return nil, errors.New("no handle set on context")
	}
	return handle, nil
}

func findNode(hdl *handle, name string) (*C.struct_d_tm_node_t, error) {
	if hdl == nil {
		return nil, errors.New("nil handle")
	}

	node := C.d_tm_find_metric(hdl.shmem, C.CString(name))
	if node == nil {
		return nil, errors.Errorf("unable to find metric named %q", name)
	}

	return node, nil
}

func (mb *metricBase) Type() MetricType {
	return MetricTypeUnknown
}

func (mb *metricBase) Path() string {
	if mb == nil {
		return "<nil>"
	}
	return mb.path
}

func (mb *metricBase) Name() string {
	if mb == nil || mb.handle == nil || mb.node == nil {
		return "<nil>"
	}

	if mb.name == nil {
		name := C.GoString((*C.char)(C.d_tm_conv_ptr(mb.handle.shmem, unsafe.Pointer(mb.node.dtn_name))))
		mb.name = &name
	}

	return *mb.name
}

func (mb *metricBase) fillMetadata() {
	if mb == nil || mb.handle == nil || mb.handle.root == nil {
		return
	}

	var shortDesc *C.char
	var longDesc *C.char
	res := C.d_tm_get_metadata(&shortDesc, &longDesc, mb.handle.shmem, mb.node, C.CString(mb.Name()))
	if res == C.DER_SUCCESS {
		short := C.GoString(shortDesc)
		mb.shortDesc = &short
		long := C.GoString(longDesc)
		mb.longDesc = &long

		C.free(unsafe.Pointer(shortDesc))
		C.free(unsafe.Pointer(longDesc))
	} else {
		failed := "failed to retrieve metadata"
		mb.shortDesc = &failed
		mb.longDesc = &failed
	}
}

func (mb *metricBase) ShortDesc() string {
	if mb.shortDesc == nil {
		mb.fillMetadata()
	}

	return *mb.shortDesc
}

func (mb *metricBase) LongDesc() string {
	if mb.longDesc == nil {
		mb.fillMetadata()
	}

	return *mb.longDesc
}

func (mb *metricBase) String() string {
	r, w, err := os.Pipe()
	if err != nil {
		return err.Error()
	}
	defer r.Close()
	defer w.Close()

	f := C.fdopen(C.int(w.Fd()), C.CString("w"))
	if f == nil {
		return "fdopen() failed"
	}

	go func() {
		C.d_tm_print_node(mb.handle.shmem, mb.node, C.int(0), f)
		C.fclose(f)
	}()

	buf := make([]byte, 128)

	_, err = r.Read(buf)
	if err != nil && err != io.EOF {
		return err.Error()
	}

	return strings.TrimSpace(string(buf[:bytes.Index(buf, []byte{0})]))
}

func (sm *statsMetric) Mean() float64 {
	return float64(sm.stats.mean)
}

func (sm *statsMetric) StdDev() float64 {
	return float64(sm.stats.std_dev)
}

func getStatsMinInt(stats *C.struct_d_tm_stats_t) uint64 {
	return uint64(C.get_min_int(stats))
}

func getStatsMaxInt(stats *C.struct_d_tm_stats_t) uint64 {
	return uint64(C.get_max_int(stats))
}

func getStatsMinFloat(stats *C.struct_d_tm_stats_t) float64 {
	return float64(C.get_min_float(stats))
}

func getStatsMaxFloat(stats *C.struct_d_tm_stats_t) float64 {
	return float64(C.get_max_float(stats))
}

func getStatsSumInt(stats *C.struct_d_tm_stats_t) uint64 {
	return uint64(C.get_sum_int(stats))
}

func getStatsSumFloat(stats *C.struct_d_tm_stats_t) float64 {
	return float64(C.get_sum_float(stats))
}

func Init(parent context.Context, idx uint32) (context.Context, error) {
	shmemRoot := C.d_tm_get_shared_memory(C.int(idx))
	if shmemRoot == nil {
		return nil, errors.Errorf("no shared memory segment found for idx: %d", idx)
	}

	root := C.d_tm_get_root(shmemRoot)
	if root == nil {
		return nil, errors.Errorf("no root node found in shared memory segment for idx: %d", idx)
	}

	handle := &handle{
		idx:   idx,
		shmem: shmemRoot,
		root:  root,
	}

	return context.WithValue(parent, handleKey, handle), nil
}

func visit(hdl *handle, node *C.struct_d_tm_node_t, pathComps []string, out chan<- Metric) {
	var next *C.struct_d_tm_node_t

	if node == nil {
		return
	}
	path := strings.Join(pathComps, "/")
	name := C.GoString((*C.char)(C.d_tm_conv_ptr(hdl.shmem, unsafe.Pointer(node.dtn_name))))
	//fmt.Printf("visiting: %q/%q (%p)\n", path, name, node)

	switch node.dtn_type {
	case C.D_TM_DIRECTORY:
		/*if name != "" {
			pathComps = append(pathComps, name)
		}*/
		next = (*C.struct_d_tm_node_t)(C.d_tm_conv_ptr(hdl.shmem, unsafe.Pointer(node.dtn_child)))
		if next != nil {
			visit(hdl, next, append(pathComps, name), out)
		}
	case C.D_TM_GAUGE:
		out <- newGauge(hdl, path, &name, node)
	case C.D_TM_COUNTER:
		out <- newCounter(hdl, path, &name, node)
	default:
		//fmt.Printf("%s: metric type 0x%x not supported yet\n", name, node.dtn_type)
	}

	next = (*C.struct_d_tm_node_t)(C.d_tm_conv_ptr(hdl.shmem, unsafe.Pointer(node.dtn_sibling)))
	if next != nil && next != node {
		visit(hdl, next, pathComps, out)
	}
}

func CollectMetrics(ctx context.Context, dirname string, out chan<- Metric) error {
	hdl, err := getHandle(ctx)
	if err != nil {
		return err
	}

	node := hdl.root

	/*if dirname != "/" && dirname != "" {
		node = FindMetric(shmemRoot, dirname)
	}*/

	if node == nil {
		return errors.Errorf("Directory or metric:[%s] was not found", dirname)
	}

	var nl *C.struct_d_tm_nodeList_t

	filter := C.D_TM_ALL_NODES
	rc := C.d_tm_list(&nl, hdl.shmem, node, C.int(filter))

	if rc != C.DER_SUCCESS {
		return errors.Errorf("Unable to find entry for %s.  rc = %d\n", dirname, rc)
	}

	//num := C.d_tm_count_metrics(hdl.shmem, node, C.int(filter))
	//fmt.Printf("found %d metrics under %q\n", num, dirname)

	var pathComps []string
	if dirname != "" {
		pathComps = append(pathComps, dirname)
	}
	visit(hdl, nl.dtnl_node, pathComps, out)

	close(out)
	C.d_tm_list_free(nl)

	return nil
}

func GetRank(ctx context.Context) (uint32, error) {
	hdl, err := getHandle(ctx)
	if err != nil {
		return 0, err
	}

	hdl.Lock()
	defer hdl.Unlock()

	if hdl.rank == nil {
		g, err := GetGauge(ctx, "/rank")
		if err != nil {
			return 0, err
		}
		r := uint32(g.Value())
		hdl.rank = &r
	}

	return *hdl.rank, nil
}

func GetAPIVersion() int {
	version := C.d_tm_get_version()
	return int(version)
}
