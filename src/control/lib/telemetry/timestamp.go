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

type Timestamp struct {
	metricBase
}

func (t *Timestamp) Type() MetricType {
	return MetricTypeTimestamp
}

func (t *Timestamp) Value() time.Time {
	zero := time.Time{}
	if t.handle == nil || t.node == nil {
		return zero
	}
	var clk C.time_t
	res := C.d_tm_get_timestamp(t.handle.ctx, &clk, t.node)
	if res == C.DER_SUCCESS {
		return time.Unix(int64(clk), 0)
	}
	return zero
}

// FloatValue converts the timestamp to time in seconds since the UNIX epoch.
func (t *Timestamp) FloatValue() float64 {
	return float64(t.Value().Unix())
}

func newTimestamp(hdl *handle, path string, name *string, node *C.struct_d_tm_node_t) *Timestamp {
	return &Timestamp{
		metricBase: metricBase{
			handle: hdl,
			path:   path,
			name:   name,
			node:   node,
		},
	}
}

func GetTimestamp(ctx context.Context, name string) (*Timestamp, error) {
	hdl, err := getHandle(ctx)
	if err != nil {
		return nil, err
	}

	node, err := findNode(hdl, name)
	if err != nil {
		return nil, err
	}

	if node.dtn_type != C.D_TM_TIMESTAMP {
		return nil, fmt.Errorf("metric %q is not a timestamp", name)
	}
	n, p := splitFullName(name)
	return newTimestamp(hdl, p, &n, node), nil
}
