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

type Snapshot struct {
	metricBase
}

func (s *Snapshot) Type() MetricType {
	return MetricTypeSnapshot
}

func (s *Snapshot) Value() time.Time {
	if s.handle == nil || s.node == nil {
		return time.Time{}
	}

	var tms C.struct_timespec

	res := C.d_tm_get_timer_snapshot(s.handle.ctx, &tms, s.node)
	if res == C.DER_SUCCESS {
		return time.Unix(int64(tms.tv_sec), int64(tms.tv_nsec))
	}

	return time.Time{}
}

func (s *Snapshot) FloatValue() float64 {
	return float64(s.Value().UnixNano())
}

func newSnapshot(hdl *handle, path string, name *string, node *C.struct_d_tm_node_t) *Snapshot {
	return &Snapshot{
		metricBase: metricBase{
			handle: hdl,
			path:   path,
			name:   name,
			node:   node,
		},
	}
}

func GetSnapshot(ctx context.Context, name string) (*Snapshot, error) {
	hdl, err := getHandle(ctx)
	if err != nil {
		return nil, err
	}

	node, err := findNode(hdl, name)
	if err != nil {
		return nil, err
	}

	if (node.dtn_type & C.D_TM_TIMER_SNAPSHOT) == 0 {
		return nil, fmt.Errorf("metric %q is not a timer snapshot", name)
	}
	n, p := splitFullName(name)
	return newSnapshot(hdl, p, &n, node), nil
}
