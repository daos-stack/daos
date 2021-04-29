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
	"time"
)

type Duration struct {
	statsMetric
}

func (d *Duration) Value() time.Duration {
	if d.handle == nil || d.node == nil {
		return BadDuration
	}

	var tms C.struct_timespec

	res := C.d_tm_get_duration(d.handle.ctx, &tms, &d.stats, d.node)
	if res == C.DER_SUCCESS {
		return time.Duration(tms.tv_sec)*time.Second + time.Duration(tms.tv_nsec)
	}

	return BadDuration
}

func newDuration(hdl *handle, path string, name *string, node *C.struct_d_tm_node_t) *Duration {
	return &Duration{
		statsMetric: statsMetric{
			metricBase: metricBase{
				handle: hdl,
				path:   path,
				name:   name,
				node:   node,
			},
		},
	}
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

	return newDuration(hdl, "", &name, node), nil
}
