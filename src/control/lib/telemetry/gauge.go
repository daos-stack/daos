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

type Gauge struct {
	statsMetric
}

func (g *Gauge) Type() MetricType {
	return MetricTypeGauge
}

func (g *Gauge) FloatValue() float64 {
	return float64(g.Value())
}

func (g *Gauge) Value() uint64 {
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

func newGauge(hdl *handle, path string, name *string, node *C.struct_d_tm_node_t) *Gauge {
	g := &Gauge{
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
