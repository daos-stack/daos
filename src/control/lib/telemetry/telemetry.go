//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build linux && amd64
// +build linux,amd64

//

package telemetry

/*
#cgo LDFLAGS: -lgurt

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"
*/
import "C"

import (
	"bytes"
	"context"
	"io"
	"os"
	"strings"
	"sync"
	"time"
	"unsafe"

	"github.com/pkg/errors"
)

type MetricType int

const (
	MetricTypeUnknown    MetricType = 0
	MetricTypeCounter    MetricType = C.D_TM_COUNTER
	MetricTypeDuration   MetricType = C.D_TM_DURATION
	MetricTypeGauge      MetricType = C.D_TM_GAUGE
	MetricTypeStatsGauge MetricType = C.D_TM_STATS_GAUGE
	MetricTypeSnapshot   MetricType = C.D_TM_TIMER_SNAPSHOT
	MetricTypeTimestamp  MetricType = C.D_TM_TIMESTAMP
	MetricTypeDirectory  MetricType = C.D_TM_DIRECTORY
	MetricTypeLink       MetricType = C.D_TM_LINK

	BadUintVal  = ^uint64(0)
	BadFloatVal = float64(BadUintVal)
	BadIntVal   = int64(BadUintVal >> 1)
	BadDuration = time.Duration(BadIntVal)

	pathSep = '/'
)

type (
	Metric interface {
		Path() string
		Name() string
		FullPath() string
		Type() MetricType
		Desc() string
		Units() string
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
		SampleSize() uint64
	}
)

type (
	handle struct {
		sync.RWMutex
		idx  uint32
		rank *uint32
		ctx  *C.struct_d_tm_context
		root *C.struct_d_tm_node_t
	}

	metricBase struct {
		handle *handle
		node   *C.struct_d_tm_node_t

		path  string
		name  *string
		desc  *string
		units *string
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

func (h *handle) isValid() bool {
	return h != nil && h.ctx != nil && h.root != nil
}

func getHandle(ctx context.Context) (*handle, error) {
	handle, ok := ctx.Value(handleKey).(*handle)
	if !ok || handle == nil {
		return nil, errors.New("no handle set on context")
	}
	return handle, nil
}

func findNode(hdl *handle, name string) (*C.struct_d_tm_node_t, error) {
	if hdl == nil {
		return nil, errors.New("nil handle")
	}

	node := C.d_tm_find_metric(hdl.ctx, C.CString(name))
	if node == nil {
		return nil, errors.Errorf("unable to find metric named %q", name)
	}

	return node, nil
}

func splitFullName(fullName string) (string, string) {
	tokens := strings.Split(fullName, "/")
	if len(tokens) == 1 {
		return "", tokens[0]
	}

	name := tokens[len(tokens)-1]
	path := strings.Join(tokens[:len(tokens)-1], "/")
	return name, path
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
		name := C.GoString(C.d_tm_get_name(mb.handle.ctx, mb.node))
		mb.name = &name
	}

	return *mb.name
}

func (mb *metricBase) FullPath() string {
	if mb == nil || mb.handle == nil || mb.node == nil {
		return "<nil>"
	}

	return mb.Path() + string(pathSep) + mb.Name()
}

func (mb *metricBase) fillMetadata() {
	if mb == nil || mb.handle == nil || mb.handle.root == nil {
		return
	}

	var desc *C.char
	var units *C.char
	res := C.d_tm_get_metadata(mb.handle.ctx, &desc, &units, mb.node)
	if res == C.DER_SUCCESS {
		descStr := C.GoString(desc)
		mb.desc = &descStr
		unitsStr := C.GoString(units)
		mb.units = &unitsStr

		C.free(unsafe.Pointer(desc))
		C.free(unsafe.Pointer(units))
	} else {
		failed := "failed to retrieve metadata"
		mb.desc = &failed
		mb.units = &failed
	}
}

func (mb *metricBase) Desc() string {
	if mb.desc == nil {
		mb.fillMetadata()
	}

	return *mb.desc
}

func (mb *metricBase) Units() string {
	if mb.units == nil {
		mb.fillMetadata()
	}

	return *mb.units
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
		C.d_tm_print_node(mb.handle.ctx, mb.node, C.int(0), C.CString(""), C.D_TM_STANDARD, C.int(0), f)
		C.fclose(f)
	}()

	buf := make([]byte, 128)

	_, err = r.Read(buf)
	if err != nil && err != io.EOF {
		return err.Error()
	}

	return strings.TrimSpace(string(buf[:bytes.Index(buf, []byte{0})]))
}

func (sm *statsMetric) FloatMin() float64 {
	return float64(sm.stats.dtm_min)
}

func (sm *statsMetric) FloatMax() float64 {
	return float64(sm.stats.dtm_max)
}

func (sm *statsMetric) FloatSum() float64 {
	return float64(sm.stats.dtm_sum)
}

func (sm *statsMetric) Mean() float64 {
	return float64(sm.stats.mean)
}

func (sm *statsMetric) StdDev() float64 {
	return float64(sm.stats.std_dev)
}

func (sm *statsMetric) SampleSize() uint64 {
	return uint64(sm.stats.sample_size)
}

func collectGarbageLoop(ctx context.Context, ticker *time.Ticker) {
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			hdl, err := getHandle(ctx)
			if err != nil {
				return // can't do anything with this
			}
			hdl.Lock()
			if !hdl.isValid() {
				// Handle won't become valid again on this ctx
				hdl.Unlock()
				return
			}
			C.d_tm_gc_ctx(hdl.ctx)
			hdl.Unlock()
		}
	}
}

// Init initializes the telemetry bindings
func Init(parent context.Context, idx uint32) (context.Context, error) {
	if parent == nil {
		return nil, errors.New("nil parent context")
	}

	tmCtx := C.d_tm_open(C.int(idx))
	if tmCtx == nil {
		return nil, errors.Errorf("no shared memory segment found for idx: %d", idx)
	}

	root := C.d_tm_get_root(tmCtx)
	if root == nil {
		return nil, errors.Errorf("no root node found in shared memory segment for idx: %d", idx)
	}

	handle := &handle{
		idx:  idx,
		ctx:  tmCtx,
		root: root,
	}

	newCtx := context.WithValue(parent, handleKey, handle)
	go collectGarbageLoop(newCtx, time.NewTicker(60*time.Second))

	return newCtx, nil
}

// Detach detaches from the telemetry handle
func Detach(ctx context.Context) {
	if hdl, err := getHandle(ctx); err == nil {
		hdl.Lock()
		C.d_tm_close(&hdl.ctx)
		hdl.root = nil
		hdl.Unlock()
	}
}

type Schema struct {
	mu      sync.RWMutex
	metrics map[string]Metric
	seen    map[string]struct{}
}

func (s *Schema) setSeen(id string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.seen[id] = struct{}{}
}

func (s *Schema) Prune() {
	s.mu.Lock()
	defer s.mu.Unlock()
	for k := range s.metrics {
		if _, found := s.seen[k]; !found {
			delete(s.metrics, k)
		}
	}
	s.seen = make(map[string]struct{}) // reset for the next time
}

func splitId(id string) (string, string) {
	i := len(id) - 1
	for i >= 0 && id[i] != pathSep {
		i--
	}

	name := id[i+1:]
	// Trim trailing separator.
	if id[i] == pathSep {
		i--
	}

	return id[:i+1], name
}

func (s *Schema) Add(hdl *handle, id string, typ C.int, node *C.struct_d_tm_node_t) Metric {
	s.setSeen(id)
	s.mu.RLock()
	if m, found := s.metrics[id]; found {
		s.mu.RUnlock()
		return m
	}
	s.mu.RUnlock()

	var m Metric
	path, name := splitId(id)
	switch {
	case typ == C.D_TM_GAUGE:
		m = newGauge(hdl, path, &name, node)
	case typ == C.D_TM_STATS_GAUGE:
		m = newStatsGauge(hdl, path, &name, node)
	case typ == C.D_TM_COUNTER:
		m = newCounter(hdl, path, &name, node)
	case typ == C.D_TM_TIMESTAMP:
		m = newTimestamp(hdl, path, &name, node)
	case (typ & C.D_TM_TIMER_SNAPSHOT) != 0:
		m = newSnapshot(hdl, path, &name, node)
	case (typ & C.D_TM_DURATION) != 0:
		m = newDuration(hdl, path, &name, node)
	default:
		return nil
	}
	s.mu.Lock()
	s.metrics[id] = m
	s.mu.Unlock()

	return m
}

func NewSchema() *Schema {
	return &Schema{
		metrics: make(map[string]Metric),
		seen:    make(map[string]struct{}),
	}

}

func visit(hdl *handle, s *Schema, node *C.struct_d_tm_node_t, pathComps []string, out chan<- Metric) {
	var next *C.struct_d_tm_node_t

	if node == nil {
		return
	}
	name := C.GoString(C.d_tm_get_name(hdl.ctx, node))
	id := strings.Join(append(pathComps, name), string(pathSep))

	cType := node.dtn_type

	switch {
	case cType == C.D_TM_DIRECTORY:
		next = C.d_tm_get_child(hdl.ctx, node)
		if next != nil {
			visit(hdl, s, next, append(pathComps, name), out)
		}
	case cType == C.D_TM_LINK:
		next = C.d_tm_follow_link(hdl.ctx, node)
		if next != nil {
			// link leads to a directory with the same name
			visit(hdl, s, next, pathComps, out)
		}
	default:
		m := s.Add(hdl, id, cType, node)
		if m != nil {
			out <- m
		}
	}

	next = C.d_tm_get_sibling(hdl.ctx, node)
	if next != nil && next != node {
		visit(hdl, s, next, pathComps, out)
	}
}

func CollectMetrics(ctx context.Context, s *Schema, out chan<- Metric) error {
	defer close(out)

	hdl, err := getHandle(ctx)
	if err != nil {
		return err
	}
	hdl.Lock()
	defer hdl.Unlock()

	if !hdl.isValid() {
		return errors.New("invalid handle")
	}

	node := hdl.root
	var pathComps []string
	visit(hdl, s, node, pathComps, out)

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
