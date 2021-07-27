//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// +build linux,amd64
//

package telemetry

import (
	"context"
	"fmt"
	"regexp"
	"sync"
	"testing"
	"time"

	"github.com/daos-stack/daos/src/control/common"
)

/*
#cgo LDFLAGS: -lgurt

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"
#include "gurt/telemetry_producer.h"

// The following helpers are necessary because cgo can't call
// variadic functions.

static int
add_metric(struct d_tm_node_t **node, int metric_type, char *sh_desc,
           char *lng_desc, const char *str)
{
	return d_tm_add_metric(node, metric_type, sh_desc, lng_desc, str);
}
*/
import "C"

var nextID int
var nextIDMutex sync.Mutex

const (
	telemetryIDBase = 100
	PromexpIDBase   = 200
)

// NextTestID gets the next available ID for a shmem segment. This helps avoid
// conflicts amongst tests running concurrently.
// Different packages should use different bases.
func NextTestID(base ...int) int {
	nextIDMutex.Lock()
	defer nextIDMutex.Unlock()

	idBase := telemetryIDBase
	if len(base) > 0 {
		idBase = base[0]
	}
	id := nextID
	nextID++
	return idBase + id
}

type (
	TestMetric struct {
		Name   string
		path   string
		desc   string
		units  string
		min    float64
		max    float64
		Cur    float64 // value - may be exact or approximate
		sum    float64
		mean   float64
		stddev float64
		str    string // string of regex to compare String() against
		node   *C.struct_d_tm_node_t
	}
	TestMetricsMap map[MetricType]*TestMetric
)

func (tm *TestMetric) FullPath() string {
	fullName := tm.path
	if fullName != "" {
		fullName += "/"
	}
	fullName += tm.Name
	return fullName
}

func InitTestMetricsProducer(t *testing.T, id int, size uint64) {
	t.Helper()

	rc := C.d_tm_init(C.int(id), C.ulong(size), C.D_TM_SERVER_PROCESS)
	if rc != 0 {
		t.Fatalf("failed to init telemetry: %d", rc)
	}
}

func AddTestMetrics(t *testing.T, testMetrics TestMetricsMap) {
	t.Helper()

	for mt, tm := range testMetrics {
		fullName := tm.FullPath()
		switch mt {
		case MetricTypeGauge:
			rc := C.add_metric(&tm.node, C.D_TM_GAUGE, C.CString(tm.desc), C.CString(tm.units), C.CString(fullName))
			if rc != 0 {
				t.Fatalf("failed to add %s: %d", fullName, rc)
			}
			C.d_tm_set_gauge(tm.node, C.uint64_t(tm.Cur))
		case MetricTypeStatsGauge:
			rc := C.add_metric(&tm.node, C.D_TM_STATS_GAUGE, C.CString(tm.desc), C.CString(tm.units), C.CString(fullName))
			if rc != 0 {
				t.Fatalf("failed to add %s: %d", tm.Name, rc)
			}
			for _, val := range []float64{tm.min, tm.max, tm.Cur} {
				C.d_tm_set_gauge(tm.node, C.uint64_t(val))
			}
		case MetricTypeCounter:
			rc := C.add_metric(&tm.node, C.D_TM_COUNTER, C.CString(tm.desc), C.CString(tm.units), C.CString(fullName))
			if rc != 0 {
				t.Fatalf("failed to add %s: %d", fullName, rc)
			}
			C.d_tm_inc_counter(tm.node, C.ulong(tm.Cur))
		case MetricTypeDuration:
			rc := C.add_metric(&tm.node, C.D_TM_DURATION|C.D_TM_CLOCK_REALTIME, C.CString(tm.desc), C.CString(tm.units), C.CString(fullName))
			if rc != 0 {
				t.Fatalf("failed to add %s: %d", fullName, rc)
			}
			C.d_tm_mark_duration_start(tm.node, C.D_TM_CLOCK_REALTIME)
			time.Sleep(time.Duration(tm.Cur))
			C.d_tm_mark_duration_end(tm.node)
		case MetricTypeTimestamp:
			rc := C.add_metric(&tm.node, C.D_TM_TIMESTAMP, C.CString(tm.desc), C.CString(tm.units), C.CString(fullName))
			if rc != 0 {
				t.Fatalf("failed to add %s: %d", fullName, rc)
			}
			C.d_tm_record_timestamp(tm.node)
		case MetricTypeSnapshot:
			rc := C.add_metric(&tm.node, C.D_TM_TIMER_SNAPSHOT|C.D_TM_CLOCK_REALTIME, C.CString(tm.desc), C.CString(tm.units), C.CString(fullName))
			if rc != 0 {
				t.Fatalf("failed to add %s: %d", fullName, rc)
			}
			C.d_tm_take_timer_snapshot(tm.node, C.D_TM_CLOCK_REALTIME)
		default:
			t.Fatalf("metric type %d not supported", mt)
		}
	}
}

func setupTestMetrics(t *testing.T) (context.Context, TestMetricsMap) {
	t.Helper()

	id := NextTestID()
	InitTestMetricsProducer(t, id, 2048)

	ctx, err := Init(context.Background(), uint32(id))
	if err != nil {
		t.Fatal(err)
	}

	testMetrics := TestMetricsMap{
		MetricTypeGauge: {
			Name:  "test_gauge",
			path:  "test",
			desc:  "some gauge",
			units: "rpc/s",
			Cur:   42,
			str:   "test_gauge: 42 rpc/s",
		},
		MetricTypeStatsGauge: {
			Name:   "test_gauge_stats",
			path:   "test",
			desc:   "some gauge with stats",
			units:  "rpc/s",
			min:    1,
			Cur:    42,
			max:    64,
			sum:    107,
			mean:   35.666666666666664,
			stddev: 31.973947728319903,
			str:    `test_gauge_stats: 42 rpc/s \p{Ps}min: 1, max: 64, avg: 36, stddev: 32, samples: 3\p{Pe}`,
		},
		MetricTypeCounter: {
			Name:  "test_counter",
			path:  "test",
			desc:  "some counter",
			units: "KB",
			Cur:   1,
			str:   "test_counter: 1 KB",
		},
		MetricTypeDuration: {
			Name:  "test_duration",
			path:  "test",
			desc:  "some duration",
			units: "us", // TODO: fix at library level, should be ns
			Cur:   float64(10 * time.Millisecond),
			str:   `test_duration: [0-9]+ us \p{Ps}min: [0-9]+, max: [0-9]+, avg: [0-9]+, samples: [0-9]+\p{Pe}`,
		},
		MetricTypeTimestamp: {
			Name: "test_timestamp",
			path: "test",
			desc: "some timestamp",
			Cur:  float64(time.Now().Unix()),
			str:  `test_timestamp: [[:alpha:]]{3,} [[:alpha:]]{3,} +[0-9]{1,2} [0-9]{2}:[0-9]{2}:[0-9]{2} [0-9]{4}`,
		},
		MetricTypeSnapshot: {
			Name:  "test_snapshot",
			path:  "test",
			desc:  "some snapshot",
			units: "us", // TODO: fix at library level, should be ns
			Cur:   float64(time.Now().UnixNano()),
			str:   `test_snapshot: [0-9]+ us`,
		},
	}

	AddTestMetrics(t, testMetrics)

	return ctx, testMetrics
}

func cleanupTestMetrics(ctx context.Context, t *testing.T) {
	Detach(ctx)
	CleanupTestMetricsProducer(t)
}

func CleanupTestMetricsProducer(t *testing.T) {
	C.d_tm_fini()
}

func testMetricBasics(t *testing.T, tm *TestMetric, m Metric) {
	t.Helper()

	common.AssertEqual(t, tm.Name, m.Name(), "Name() failed")
	common.AssertEqual(t, tm.path, m.Path(), "Path() failed")
	common.AssertEqual(t, tm.FullPath(), m.FullPath(), "FullPath() failed")
	common.AssertEqual(t, tm.desc, m.Desc(), "Desc() failed")
	common.AssertEqual(t, tm.units, m.Units(), "Units() failed")

	strRE, err := regexp.Compile(tm.str)
	if err != nil {
		t.Fatalf("invalid regex %q", tm.str)
	}
	common.AssertTrue(t, strRE.Match([]byte(m.String())), fmt.Sprintf("String() failed: %q", m.String()))
}
