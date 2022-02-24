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
	"context"
	"fmt"
)

type Counter struct {
	metricBase
}

func (c *Counter) Type() MetricType {
	return MetricTypeCounter
}

func (c *Counter) FloatValue() float64 {
	return float64(c.Value())
}

func (c *Counter) Value() uint64 {
	if c.handle == nil || c.node == nil {
		return BadUintVal
	}

	var val C.uint64_t

	res := C.d_tm_get_counter(c.handle.ctx, &val, c.node)
	if res == C.DER_SUCCESS {
		return uint64(val)
	}

	return BadUintVal
}

func newCounter(hdl *handle, path string, name *string, node *C.struct_d_tm_node_t) *Counter {
	return &Counter{
		metricBase: metricBase{
			handle: hdl,
			node:   node,
			path:   path,
			name:   name,
		},
	}
}

func GetCounter(ctx context.Context, name string) (*Counter, error) {
	hdl, err := getHandle(ctx)
	if err != nil {
		return nil, err
	}

	node, err := findNode(hdl, name)
	if err != nil {
		return nil, err
	}

	if node.dtn_type != C.D_TM_COUNTER {
		return nil, fmt.Errorf("metric %q is not a counter", name)
	}

	n, p := splitFullName(name)
	return newCounter(hdl, p, &n, node), nil
}
