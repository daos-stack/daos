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

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"
*/
import "C"

import (
	"context"
	"fmt"
)

var _ Metric = (*Gauge)(nil)
var _ StatsMetric = (*StatsGauge)(nil)

// Gauge is a metric that consists of a single value that may increase or decrease.
type Gauge struct {
	metricBase
}

// Type returns the type of gauge.
func (g *Gauge) Type() MetricType {
	return MetricTypeGauge
}

// FloatValue returns the value as a float.
func (g *Gauge) FloatValue() float64 {
	return float64(g.Value())
}

// Value returns the value as an unsigned integer.
func (g *Gauge) Value() uint64 {
	gaugeVal := BadUintVal
	if g.handle == nil || g.node == nil {
		return gaugeVal
	}

	fetch := func() C.int {
		var val C.uint64_t
		res := C.d_tm_get_gauge(g.handle.ctx, &val, nil, g.node)
		if res == C.DER_SUCCESS {
			gaugeVal = uint64(val)
		}
		return res
	}
	g.fetchValWithRetry(fetch)

	return gaugeVal
}

func newGauge(hdl *handle, path string, name *string, node *C.struct_d_tm_node_t) *Gauge {
	return &Gauge{
		metricBase: metricBase{
			handle: hdl,
			path:   path,
			name:   name,
			node:   node,
		},
	}
}

// GetGauge finds the gauge with the requested name in the telemetry tree.
func GetGauge(ctx context.Context, name string) (*Gauge, error) {
	hdl, err := getHandle(ctx)
	if err != nil {
		return nil, err
	}

	node, err := findNode(hdl, name)
	if err != nil {
		return nil, err
	}

	if node.dtn_type != C.D_TM_GAUGE {
		return nil, fmt.Errorf("metric %q is not a gauge", name)
	}

	n, p := splitFullName(name)
	return newGauge(hdl, p, &n, node), nil
}

// StatsGauge is a gauge with statistics gathered.
type StatsGauge struct {
	statsMetric
	hist *Histogram // optional histogram data
}

// Type returns the type of the gauge with stats.
func (g *StatsGauge) Type() MetricType {
	return MetricTypeStatsGauge
}

// FloatValue returns the gauge value as a float.
func (g *StatsGauge) FloatValue() float64 {
	return float64(g.Value())
}

// Value returns the gauge value as an unsigned integer.
func (g *StatsGauge) Value() uint64 {
	gaugeVal := BadUintVal
	if g.handle == nil || g.node == nil {
		return gaugeVal
	}

	fetch := func() C.int {
		var val C.uint64_t
		res := C.d_tm_get_gauge(g.handle.ctx, &val, &g.stats, g.node)
		if res == C.DER_SUCCESS {
			gaugeVal = uint64(val)
		}
		return res
	}
	g.fetchValWithRetry(fetch)

	return gaugeVal
}

func newStatsGauge(hdl *handle, path string, name *string, node *C.struct_d_tm_node_t) *StatsGauge {
	g := &StatsGauge{
		statsMetric: statsMetric{
			metricBase: metricBase{
				handle: hdl,
				path:   path,
				name:   name,
				node:   node,
			},
		},
	}
	g.hist = newHistogram(&g.statsMetric)

	// Load up the stats
	_ = g.Value()

	return g
}

// GetStatsGauge finds the gauge with statistics with the given name in the telemetry tree.
func GetStatsGauge(ctx context.Context, name string) (*StatsGauge, error) {
	hdl, err := getHandle(ctx)
	if err != nil {
		return nil, err
	}

	node, err := findNode(hdl, name)
	if err != nil {
		return nil, err
	}

	if node.dtn_type != C.D_TM_STATS_GAUGE {
		return nil, fmt.Errorf("metric %q is not a gauge with stats", name)
	}

	n, p := splitFullName(name)
	return newStatsGauge(hdl, p, &n, node), nil
}
