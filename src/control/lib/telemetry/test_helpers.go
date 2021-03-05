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
	"testing"
)

/*
#cgo LDFLAGS: -lgurt

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"
#include "gurt/telemetry_producer.h"

// The following helpers are necessary because cgo can't call
// variadic functions.

static int
set_gauge(struct d_tm_node_t **metric, uint64_t value, char *item)
{
	return d_tm_set_gauge(metric, value, item);
}

static int
incr_counter(struct d_tm_node_t **metric, char *item)
{
	return d_tm_increment_counter(metric, 1, item);
}

static int
add_metric(struct d_tm_node_t **node, int metric_type, char *sh_desc,
           char *lng_desc, const char *str)
{
	return d_tm_add_metric(node, metric_type, sh_desc, lng_desc, str);
}
*/
import "C"

type (
	cMetric struct {
		name   string
		short  string
		long   string
		min    float64
		max    float64
		cur    float64
		sum    float64
		mean   float64
		stddev float64
		str    string
		node   *C.struct_d_tm_node_t
	}
	testMetric struct {
		c cMetric
		g Metric
	}
	testMetricsMap map[MetricType]testMetric
)

func setupTestMetrics(t *testing.T) (context.Context, testMetricsMap) {
	rc := C.d_tm_init(42, 8192, 0)
	if rc != 0 {
		t.Fatalf("failed to init telemetry: %d", rc)
	}

	ctx, err := Init(context.Background(), 42)
	if err != nil {
		t.Fatal(err)
	}

	testMetrics := testMetricsMap{
		MetricTypeGauge: {
			c: cMetric{
				name:   "test_gauge",
				short:  "short gauge",
				long:   "long gauge",
				min:    1,
				cur:    42,
				max:    64,
				sum:    107,
				mean:   35.666666666666664,
				stddev: 31.973947728319903,
				str:    "Gauge: test_gauge = 42 min: 1 max: 64 mean: 35.666667 size: 3 std dev: 31.973948",
			},
		},
		MetricTypeCounter: {
			c: cMetric{
				name:  "test_counter",
				short: "short counter",
				long:  "long counter",
				cur:   1,
				str:   "Counter: test_counter = 1",
			},
		},
	}

	for mt, mv := range testMetrics {
		switch mt {
		case MetricTypeGauge:
			rc = C.add_metric(&mv.c.node, C.D_TM_GAUGE, C.CString(mv.c.short), C.CString(mv.c.long), C.CString(mv.c.name))
			if rc != 0 {
				t.Fatalf("failed to add %s: %d", mv.c.name, rc)
			}
			for _, val := range []float64{mv.c.min, mv.c.max, mv.c.cur} {
				rc = C.set_gauge(&mv.c.node, C.uint64_t(val), nil)
				if rc != 0 {
					t.Fatalf("failed to set %s: %d", mv.c.name, rc)
				}
			}
		case MetricTypeCounter:
			rc = C.add_metric(&mv.c.node, C.D_TM_COUNTER, C.CString(mv.c.short), C.CString(mv.c.long), C.CString(mv.c.name))
			if rc != 0 {
				t.Fatalf("failed to add %s: %d", mv.c.name, rc)
			}
			rc = C.incr_counter(&mv.c.node, nil)
			if rc != 0 {
				t.Fatalf("failed to set %s: %d", mv.c.name, rc)
			}
		default:
			t.Fatalf("metric type %d not supported", mt)
		}
	}

	return ctx, testMetrics
}

func cleanupTestMetrics(t *testing.T) {
	C.d_tm_fini()
}
