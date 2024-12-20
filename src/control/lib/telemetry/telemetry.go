//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build linux && (amd64 || arm64)
// +build linux
// +build amd64 arm64

//

package telemetry

/*
#cgo LDFLAGS: -lgurt

#include <daos/metrics.h>
#include <gurt/telemetry_common.h>
#include <gurt/telemetry_consumer.h>
#include <gurt/telemetry_producer.h>

static int
rm_ephemeral_dir(const char *path)
{
	return d_tm_del_ephemeral_dir(path);
}

static int
add_ephemeral_dir(struct d_tm_node_t **node, size_t size_bytes, char *path)
{
	return d_tm_add_ephemeral_dir(node, size_bytes, path);
}

static int
attach_segment_path(key_t key, char *path)
{
	return d_tm_attach_path_segment(key, path);
}
*/
import "C"

import (
	"bytes"
	"context"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
	"unsafe"

	"github.com/pkg/errors"
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
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

	ClientJobRootID         = C.DC_TM_JOB_ROOT_ID
	ClientJobMax            = 1024
	ClientMetricsEnabledEnv = C.DAOS_CLIENT_METRICS_ENABLE
	ClientMetricsRetainEnv  = C.DAOS_CLIENT_METRICS_RETAIN

	BadUintVal  = ^uint64(0)
	BadFloatVal = float64(BadUintVal)
	BadIntVal   = int64(BadUintVal >> 1)
	BadDuration = time.Duration(BadIntVal)

	PathSep = filepath.Separator

	maxFetchRetries = 1
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
		Min() uint64
		Max() uint64
		Sum() uint64
		Mean() float64
		StdDev() float64
		SumSquares() float64
		SampleSize() uint64
	}
)

type (
	handle struct {
		sync.RWMutex
		id   uint32
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

func (mt MetricType) String() string {
	strFmt := func(name string) string {
		numStr := strconv.Itoa(int(mt))
		return name + " (" + numStr + ")"
	}

	switch mt {
	case MetricTypeDirectory:
		return strFmt("directory")
	case MetricTypeCounter:
		return strFmt("counter")
	case MetricTypeTimestamp:
		return strFmt("timestamp")
	case MetricTypeSnapshot:
		return strFmt("snapshot")
	case MetricTypeDuration:
		return strFmt("duration")
	case MetricTypeGauge:
		return strFmt("gauge")
	case MetricTypeStatsGauge:
		return strFmt("gauge (stats)")
	case MetricTypeLink:
		return strFmt("link")
	default:
		return strFmt("unknown")
	}
}

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

	return mb.Path() + string(PathSep) + mb.Name()
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

func (mb *metricBase) fetchValWithRetry(fetchFn func() C.int) C.int {
	var rc C.int
	for i := 0; i < maxFetchRetries; i++ {
		if rc = fetchFn(); rc == C.DER_SUCCESS {
			return rc
		}
	}
	return rc
}

func (sm *statsMetric) Min() uint64 {
	return uint64(sm.stats.dtm_min)
}

func (sm *statsMetric) Max() uint64 {
	return uint64(sm.stats.dtm_max)
}

func (sm *statsMetric) Sum() uint64 {
	return uint64(sm.stats.dtm_sum)
}

func (sm *statsMetric) Mean() float64 {
	return float64(sm.stats.mean)
}

func (sm *statsMetric) StdDev() float64 {
	return float64(sm.stats.std_dev)
}

func (sm *statsMetric) SumSquares() float64 {
	return float64(sm.stats.sum_of_squares)
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

func initClientRoot(parent context.Context, shmID uint32) (context.Context, error) {
	if parent == nil {
		return nil, errors.New("nil parent context")
	}

	shmSize := C.ulong(ClientJobMax * C.D_TM_METRIC_SIZE)

	rc := C.d_tm_init(C.int(shmID), shmSize, C.D_TM_OPEN_OR_CREATE)
	if rc != 0 {
		return nil, errors.Errorf("failed to init client root: %s", daos.Status(rc))
	}

	return Init(parent, shmID)
}

func InitClientRoot(ctx context.Context) (context.Context, error) {
	return initClientRoot(ctx, ClientJobRootID)
}

// Init initializes the DAOS telemetry consumer library.
func Init(parent context.Context, id uint32) (context.Context, error) {
	if parent == nil {
		return nil, errors.New("nil parent context")
	}

	tmCtx := C.d_tm_open(C.int(id))
	if tmCtx == nil {
		return nil, errors.Errorf("no shared memory segment found for key: %d", id)
	}

	root := C.d_tm_get_root(tmCtx)
	if root == nil {
		return nil, errors.Errorf("no root node found in shared memory segment for key: %d", id)
	}

	handle := &handle{
		id:   id,
		ctx:  tmCtx,
		root: root,
	}

	newCtx := context.WithValue(parent, handleKey, handle)
	go collectGarbageLoop(newCtx, time.NewTicker(60*time.Second))

	return newCtx, nil
}

// Fini releases resources claimed by Init().
func Fini() {
	C.d_tm_fini()
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

func addEphemeralDir(path string, shmSize uint64) error {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))
	if rc := C.add_ephemeral_dir(nil, C.ulong(shmSize), cPath); rc != 0 {
		return daos.Status(rc)
	}

	return nil
}

// SetupClientRoot performs the necessary actions to get the client telemetry
// segment linked into the agent-managed tree.
func SetupClientRoot(ctx context.Context, jobid string, pid, shm_key int) error {
	log := logging.FromContext(ctx)

	if _, err := getHandle(ctx); err != nil {
		return errors.Wrap(daos.NotInit, "client telemetry library not initialized")
	}

	if err := addEphemeralDir(jobid, ClientJobMax*C.D_TM_METRIC_SIZE); err != nil {
		if err != daos.Exists {
			return errors.Wrapf(err, "failed to add client job path %q", jobid)
		}
	}

	pidPath := filepath.Join(jobid, string(PathSep), strconv.Itoa(pid))
	cPidPath := C.CString(pidPath)
	defer C.free(unsafe.Pointer(cPidPath))
	if rc := C.attach_segment_path(C.key_t(shm_key), cPidPath); rc != 0 {
		return errors.Wrapf(daos.Status(rc), "failed to attach client segment 0x%x at %q", shm_key, pidPath)
	}

	log.Tracef("attached client segment @ %q (key: 0x%x)", pidPath, shm_key)
	return nil
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
	for i >= 0 && id[i] != PathSep {
		i--
	}

	name := id[i+1:]
	// Trim trailing separator.
	if id[i] == PathSep {
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

type procNodeFn func(hdl *handle, id string, node *C.struct_d_tm_node_t)

func visit(hdl *handle, node *C.struct_d_tm_node_t, pathComps string, procLinks bool, procNode procNodeFn) {
	var next *C.struct_d_tm_node_t

	if node == nil || procNode == nil {
		return
	}
	name := C.GoString(C.d_tm_get_name(hdl.ctx, node))
	id := pathComps + string(PathSep) + name
	if len(pathComps) == 0 {
		id = name
	}

	switch node.dtn_type {
	case C.D_TM_DIRECTORY:
		next = C.d_tm_get_child(hdl.ctx, node)
		if next != nil {
			visit(hdl, next, id, procLinks, procNode)
		}
	case C.D_TM_LINK:
		next = C.d_tm_follow_link(hdl.ctx, node)
		if next != nil {
			if procLinks {
				// Use next to get the linked shm key
				procNode(hdl, id, next)
			}

			// link leads to a directory with the same name
			visit(hdl, next, pathComps, procLinks, procNode)
		}
	default:
		procNode(hdl, id, node)
	}

	next = C.d_tm_get_sibling(hdl.ctx, node)
	if next != nil && next != node {
		visit(hdl, next, pathComps, procLinks, procNode)
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

	procNode := func(hdl *handle, id string, node *C.struct_d_tm_node_t) {
		m := s.Add(hdl, id, node.dtn_type, node)
		if m != nil {
			out <- m
		}
	}

	visit(hdl, hdl.root, "", false, procNode)

	return nil
}

type pruneMap map[string]struct{}

func (pm pruneMap) add(path string) {
	pm[path] = struct{}{}
}

func (pm pruneMap) removeParents(path string) {
	for parent := range pm {
		if strings.HasPrefix(path, parent) {
			delete(pm, parent)
		}
	}
}

func (pm pruneMap) toPrune() []string {
	var paths []string
	for path := range pm {
		paths = append(paths, path)
	}
	sort.Sort(sort.Reverse(sort.StringSlice(paths)))
	return paths
}

// PruneUnusedSegments removes shared memory segments associated with
// unused ephemeral subdirectories.
func PruneUnusedSegments(ctx context.Context, maxSegAge time.Duration) error {
	log := logging.FromContext(ctx)

	hdl, err := getHandle(ctx)
	if err != nil {
		return err
	}
	hdl.Lock()
	defer hdl.Unlock()

	if !hdl.isValid() {
		return errors.New("invalid handle")
	}

	pruneCandidates := make(pruneMap)
	procNode := func(hdl *handle, id string, node *C.struct_d_tm_node_t) {
		if node == nil || node.dtn_type != C.D_TM_DIRECTORY {
			return
		}

		path := id
		comps := strings.SplitN(path, string(PathSep), 2)
		if strings.HasPrefix(comps[0], "ID:") && len(comps) > 1 {
			path = comps[1]
		}

		st, err := shmStatKey(node.dtn_shmem_key)
		if err != nil {
			log.Errorf("failed to shmStat(%s): %s", path, err)
			return
		}

		log.Tracef("path:%s shmid:%d spid:%d cpid:%d lpid:%d age:%s",
			path, st.id, os.Getpid(), st.Cpid(), st.Lpid(), time.Since(st.Ctime()))

		// If the creator process was someone other than us, and it's still
		// around, don't mess with the segment.
		if _, err := common.GetProcName(st.Cpid()); err == nil && st.Cpid() != unix.Getpid() {
			pruneCandidates.removeParents(path)
			return
		}

		if time.Since(st.Ctime()) <= maxSegAge {
			pruneCandidates.removeParents(path)
			return
		}

		log.Tracef("adding %s to prune candidates list", path)
		pruneCandidates.add(path)
	}

	visit(hdl, hdl.root, "", true, procNode)

	for _, path := range pruneCandidates.toPrune() {
		log.Tracef("pruning %s", path)
		if err := removeLink(hdl, path); err != nil {
			log.Errorf("failed to prune %s: %s", path, err)
		}
	}

	return nil
}

func removeLink(hdl *handle, path string) error {
	_, err := findNode(hdl, path)
	if err != nil {
		return err
	}

	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))
	rc := C.rm_ephemeral_dir(cPath)
	if rc != 0 {
		return errors.Wrapf(daos.Status(rc), "failed to remove link %q", path)
	}

	if _, err := findNode(hdl, path); err == nil {
		return errors.Errorf("failed to remove %s", path)
	}

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
