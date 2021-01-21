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

	switch RASID(pbEvt.Id) {
	case RASRankDown, RASPoolRepsUpdate:
		return evt, evt.FromProto(pbEvt)
	default:
		return nil, errors.Errorf("unsupported event ID: %d", pbEvt.Id)
	}
}

// RASID identifies a given RAS event.
type RASID uint32

// RASID constant definitions matching those used when creating events either in
// the control or data (iosrv) planes.
const (
	RASRankDown       RASID = C.RAS_RANK_DOWN
	RASRankNoResponse RASID = C.RAS_RANK_NO_RESPONSE
	RASPoolRepsUpdate RASID = C.RAS_POOL_REPS_UPDATE
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
	}

	return
}
