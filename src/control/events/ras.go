//
// (C) Copyright 2020-2021 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package events

/*
#include "daos_srv/ras.h"
*/
import "C"

import (
	"encoding/json"
	"fmt"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
)

// RASExtendedInfo provides extended information for an event.
type RASExtendedInfo interface {
	isExtendedInfo()
}

// NewFromProto creates an Event from given protobuf RASEvent message.
func NewFromProto(pbEvt *sharedpb.RASEvent) (*RASEvent, error) {
	evt := new(RASEvent)

	return evt, evt.FromProto(pbEvt)
}

// RASID identifies a given RAS event.
type RASID uint32

// RASID constant definitions matching those used when creating events either in
// the control or data (iosrv) planes.
const (
	RASRankDown       RASID = C.RAS_RANK_DOWN
	RASRankNoResponse RASID = C.RAS_RANK_NO_RESPONSE
	RASPoolRepsUpdate RASID = C.RAS_POOL_REPS_UPDATE
	RASSystemStop     RASID = C.RAS_SYSTEM_STOP
	RASSystemStart    RASID = C.RAS_SYSTEM_START
)

func (id RASID) String() string {
	return C.GoString(C.ras_event2str(C.ras_event_t(id)))
}

// Uint32 returns uint32 representation of event ID.
func (id RASID) Uint32() uint32 {
	return uint32(id)
}

// RASTypeID identifies the type of a given RAS event.
type RASTypeID uint32

// RASTypeID constant definitions.
const (
	RASTypeAny         RASTypeID = C.RAS_TYPE_ANY
	RASTypeStateChange RASTypeID = C.RAS_TYPE_STATE_CHANGE
	RASTypeInfoOnly    RASTypeID = C.RAS_TYPE_INFO
)

func (typ RASTypeID) String() string {
	return C.GoString(C.ras_type2str(C.ras_type_t(typ)))
}

// Uint32 returns uint32 representation of event type.
func (typ RASTypeID) Uint32() uint32 {
	return uint32(typ)
}

// RASSeverityID identifies the severity of a given RAS event.
type RASSeverityID uint32

// RASSeverityID constant definitions.
const (
	RASSeverityFatal RASSeverityID = C.RAS_SEV_FATAL
	RASSeverityWarn  RASSeverityID = C.RAS_SEV_WARN
	RASSeverityError RASSeverityID = C.RAS_SEV_ERROR
	RASSeverityInfo  RASSeverityID = C.RAS_SEV_INFO
)

func (sev RASSeverityID) String() string {
	return C.GoString(C.ras_sev2str(C.ras_sev_t(sev)))
}

// Uint32 returns uint32 representation of event severity.
func (sev RASSeverityID) Uint32() uint32 {
	return uint32(sev)
}

// RASEvent describes details of a specific RAS event.
type RASEvent struct {
	ID           RASID           `json:"id"`
	Timestamp    string          `json:"timestamp"`
	Type         RASTypeID       `json:"type"`
	Severity     RASSeverityID   `json:"severity"`
	Msg          string          `json:"msg"`
	Hostname     string          `json:"hostname"`
	Rank         uint32          `json:"rank"`
	HWID         string          `json:"hw_id"`
	ProcID       uint64          `json:"proc_id"`
	ThreadID     uint64          `json:"thread_id"`
	JobID        string          `json:"job_id"`
	PoolUUID     string          `json:"pool_uuid"`
	ContUUID     string          `json:"cont_uuid"`
	ObjID        string          `json:"obj_id"`
	CtlOp        string          `json:"ctl_op"`
	ExtendedInfo RASExtendedInfo `json:"extended_info"`

	forwarded bool
}

// IsForwarded returns true if event has been forwarded between hosts.
func (evt *RASEvent) IsForwarded() bool {
	return evt.forwarded
}

// WithIsForwarded sets the forwarded state of this event.
func (evt *RASEvent) WithIsForwarded(isForwarded bool) *RASEvent {
	evt.forwarded = isForwarded

	return evt
}

// MarshalJSON marshals RASEvent to JSON.
func (evt *RASEvent) MarshalJSON() ([]byte, error) {
	type toJSON RASEvent
	return json.Marshal(&struct {
		ID       uint32 `json:"id"`
		Severity uint32 `json:"severity"`
		Type     uint32 `json:"type"`
		*toJSON
	}{
		ID:       evt.ID.Uint32(),
		Type:     evt.Type.Uint32(),
		Severity: evt.Severity.Uint32(),
		toJSON:   (*toJSON)(evt),
	})
}

// UnmarshalJSON unmarshals RASEvent from JSON.
func (evt *RASEvent) UnmarshalJSON(data []byte) error {
	type fromJSON RASEvent
	from := &struct {
		ID       uint32 `json:"id"`
		Type     uint32 `json:"type"`
		Severity uint32 `json:"severity"`
		*fromJSON
	}{
		fromJSON: (*fromJSON)(evt),
	}

	evt.ID = RASID(from.ID)
	evt.Type = RASTypeID(from.Type)
	evt.Severity = RASSeverityID(from.Severity)

	return nil
}

// ToProto returns a protobuf representation of the native event.
func (evt *RASEvent) ToProto() (*sharedpb.RASEvent, error) {
	pbEvt := new(sharedpb.RASEvent)
	if err := convert.Types(evt, pbEvt); err != nil {
		return nil, errors.Wrapf(err, "converting %T->%T", evt, pbEvt)
	}

	if common.InterfaceIsNil(evt.ExtendedInfo) {
		return pbEvt, nil
	}

	var err error
	switch ei := evt.ExtendedInfo.(type) {
	case *RankStateInfo:
		pbEvt.ExtendedInfo, err = RankStateInfoToProto(ei)
	case *PoolSvcInfo:
		pbEvt.ExtendedInfo, err = PoolSvcInfoToProto(ei)
	case *StrInfo:
		pbEvt.ExtendedInfo, err = StrInfoToProto(ei)
	}

	return pbEvt, err
}

// FromProto initializes a native event from a provided protobuf event.
func (evt *RASEvent) FromProto(pbEvt *sharedpb.RASEvent) (err error) {
	*evt = RASEvent{
		ID:        RASID(pbEvt.Id),
		Timestamp: pbEvt.Timestamp,
		Type:      RASTypeID(pbEvt.Type),
		Severity:  RASSeverityID(pbEvt.Severity),
		Msg:       pbEvt.Msg,
		Hostname:  pbEvt.Hostname,
		Rank:      pbEvt.Rank,
		HWID:      pbEvt.HwId,
		ProcID:    pbEvt.ProcId,
		ThreadID:  pbEvt.ThreadId,
		JobID:     pbEvt.JobId,
		PoolUUID:  pbEvt.PoolUuid,
		ContUUID:  pbEvt.ContUuid,
		ObjID:     pbEvt.ObjId,
		CtlOp:     pbEvt.CtlOp,
	}

	switch ei := pbEvt.GetExtendedInfo().(type) {
	case *sharedpb.RASEvent_RankStateInfo:
		evt.ExtendedInfo, err = RankStateInfoFromProto(ei)
	case *sharedpb.RASEvent_PoolSvcInfo:
		evt.ExtendedInfo, err = PoolSvcInfoFromProto(ei)
	case *sharedpb.RASEvent_StrInfo:
		evt.ExtendedInfo, err = StrInfoFromProto(ei)
	default:
		err = errors.New("unknown extended info type")
	}

	return
}

// PrintRAS generates a string representation of the event consistent with
// what is logged in the data plane.
func (evt *RASEvent) PrintRAS() string {
	var b strings.Builder

	/* Log mandatory RAS fields. */
	fmt.Fprintf(&b, "&&& RAS EVENT id: [%s]", evt.ID)
	if evt.Timestamp != "" {
		fmt.Fprintf(&b, " ts: [%s]", evt.Timestamp)
	}
	if evt.Hostname != "" {
		fmt.Fprintf(&b, " host: [%s]", evt.Hostname)
	}
	fmt.Fprintf(&b, " type: [%s] sev: [%s]", evt.Type, evt.Severity)
	if evt.Msg != "" {
		fmt.Fprintf(&b, " msg: [%s]", evt.Msg)
	}
	fmt.Fprintf(&b, " pid: [%d]", evt.ProcID)
	fmt.Fprintf(&b, " tid: [%d]", evt.ThreadID)

	/* Log optional RAS fields. */
	if evt.HWID != "" {
		fmt.Fprintf(&b, " hwid: [%s]", evt.HWID)
	}
	if evt.Rank != C.CRT_NO_RANK {
		fmt.Fprintf(&b, " rank: [%d]", evt.Rank)
	}
	if evt.JobID != "" {
		fmt.Fprintf(&b, " jobid: [%s]", evt.JobID)
	}
	if evt.PoolUUID != "" {
		fmt.Fprintf(&b, " pool: [%s]", evt.PoolUUID)
	}
	if evt.ContUUID != "" {
		fmt.Fprintf(&b, " container: [%s]", evt.ContUUID)
	}
	if evt.ObjID != "" {
		fmt.Fprintf(&b, " objid: [%s]", evt.ObjID)
	}
	if evt.CtlOp != "" {
		fmt.Fprintf(&b, " ctlop: [%s]", evt.CtlOp)
	}

	/* Log data blob if event info is non-specific */
	if ei := evt.GetStrInfo(); ei != nil {
		fmt.Fprintf(&b, " data: [%s]", *ei)
	}

	return b.String()
}

// HandleClusterEvent extracts event field from protobuf request message and
// converts to concrete event type that implements the Event interface.
// The Event is then published to make available to locally subscribed consumers
// to act upon.
func (ps *PubSub) HandleClusterEvent(req *sharedpb.ClusterEventReq) (*sharedpb.ClusterEventResp, error) {
	switch {
	case req == nil:
		return nil, errors.New("nil request")
	case req.Event == nil:
		return nil, errors.New("nil event in request")
	}

	event, err := NewFromProto(req.Event)
	if err != nil {
		return nil, err
	}
	ps.Publish(event)

	return &sharedpb.ClusterEventResp{Sequence: req.Sequence}, nil
}
