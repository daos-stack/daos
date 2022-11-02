//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package raft

import (
	"encoding/json"
	"time"

	"google.golang.org/protobuf/encoding/protojson"
	"google.golang.org/protobuf/proto"
)

const (
	DefaultRequestRetentionPeriod = 1 * time.Hour
)

type (
	RequestEntry struct {
		Created         time.Time
		Completed       time.Time
		CompletionError error
		RequestLeader   string
		Request         json.RawMessage
		Response        json.RawMessage
	}

	RequestMap map[uint64]*RequestEntry

	RequestDatabase struct {
		RetentionPeriod time.Duration
		Requests        RequestMap
	}
)

func newRequestDatabase(cfg *DatabaseConfig) *RequestDatabase {
	rPeriod := cfg.RequestRetentionPeriod
	if rPeriod == 0 {
		rPeriod = DefaultRequestRetentionPeriod
	}
	return &RequestDatabase{
		RetentionPeriod: rPeriod,
		Requests:        make(RequestMap),
	}
}

func (e *RequestEntry) UnmarshalResponse(msg proto.Message) error {
	return protojson.Unmarshal(e.Response, msg)
}
