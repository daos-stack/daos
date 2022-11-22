//
// (C) Copyright 2021-2022 Intel Corporation.
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
	"time"
)

type Duration struct {
	statsMetric
}

func (d *Duration) Type() MetricType {
	return MetricTypeDuration
}

func (d *Duration) Value() time.Duration {
	if d.handle == nil || d.node == nil {
		return BadDuration
	}

	var tms C.struct_timespec

	res := C.d_tm_get_duration(d.handle.ctx, &tms, &d.stats, d.node)
	if res == C.DER_SUCCESS {
		return time.Duration(tms.tv_sec)*time.Second + time.Duration(tms.tv_nsec)*time.Nanosecond
	}

	return BadDuration
}

func (d *Duration) FloatValue() float64 {
	return float64(d.Value())
}

func newDuration(hdl *handle, path string, name *string, node *C.struct_d_tm_node_t) *Duration {
	d := &Duration{
		statsMetric: statsMetric{
			metricBase: metricBase{
				handle: hdl,
				path:   path,
				name:   name,
				node:   node,
			},
		},
	}

	// Load up statistics
	_ = d.Value()

	return d
}

func GetDuration(ctx context.Context, name string) (*Duration, error) {
	hdl, err := getHandle(ctx)
	if err != nil {
		return nil, err
	}

	node, err := findNode(hdl, name)
	if err != nil {
		return nil, err
	}

	if (node.dtn_type & C.D_TM_DURATION) == 0 {
		return nil, fmt.Errorf("metric %q is not a duration", name)
	}

	n, p := splitFullName(name)
	return newDuration(hdl, p, &n, node), nil
}
