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
*/
import "C"

import (
	"context"
	"fmt"
)

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
	if g.handle == nil || g.node == nil {
		return BadUintVal
	}

	var val C.uint64_t

	res := C.d_tm_get_gauge(g.handle.ctx, &val, nil, g.node)
	if res == C.DER_SUCCESS {
		return uint64(val)
	}

	return BadUintVal
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

	return newGauge(hdl, name, &name, node), nil
}

// GaugeStats is a gauge with statistics gathered.
type GaugeStats struct {
	statsMetric
}

// Type returns the type of the gauge with stats.
func (g *GaugeStats) Type() MetricType {
	return MetricTypeGaugeStats
}

// FloatValue returns the gauge value as a float.
func (g *GaugeStats) FloatValue() float64 {
	return float64(g.Value())
}

// Value returns the gauge value as an unsigned integer.
func (g *GaugeStats) Value() uint64 {
	if g.handle == nil || g.node == nil {
		return BadUintVal
	}

	var val C.uint64_t

	res := C.d_tm_get_gauge(g.handle.ctx, &val, &g.stats, g.node)
	if res == C.DER_SUCCESS {
		return uint64(val)
	}

	return BadUintVal
}

func newGaugeStats(hdl *handle, path string, name *string, node *C.struct_d_tm_node_t) *GaugeStats {
	g := &GaugeStats{
		statsMetric: statsMetric{
			metricBase: metricBase{
				handle: hdl,
				path:   path,
				name:   name,
				node:   node,
			},
		},
	}

	// Load up the stats
	_ = g.Value()
	return g
}

// GetGaugeStats finds the gauge with statistics with the given name in the telemetry tree.
func GetGaugeStats(ctx context.Context, name string) (*GaugeStats, error) {
	hdl, err := getHandle(ctx)
	if err != nil {
		return nil, err
	}

	node, err := findNode(hdl, name)
	if err != nil {
		return nil, err
	}

	if node.dtn_type != C.D_TM_GAUGE_STATS {
		return nil, fmt.Errorf("metric %q is not a gauge with stats", name)
	}

	return newGaugeStats(hdl, name, &name, node), nil
}
