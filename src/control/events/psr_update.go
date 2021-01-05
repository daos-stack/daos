//
// (C) Copyright 2020 Intel Corporation.
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

import (
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// PoolSvcInfo describes details of a pool service.
type PoolSvcInfo struct {
	PoolUUID       string   `json:"pool_uuid"`
	SvcReplicas    []uint32 `json:"svc_reps"`
	RaftLeaderTerm uint64   `json:"version"`
}

// PoolSvcReplicasUpdate is a custom event type that implements the Event interface.
type PoolSvcReplicasUpdate struct {
	RAS          *RASEvent
	ExtendedInfo *PoolSvcInfo
}

// GetID implements the method on the interface to return event ID.
func (evt *PoolSvcReplicasUpdate) GetID() RASID { return evt.RAS.ID }

// GetType implements the method on the interface to return event type.
func (evt *PoolSvcReplicasUpdate) GetType() RASTypeID { return evt.RAS.Type }

// FromProto unpacks protobuf RAS event into this PoolSvcReplicasUpdate instance,
// extracting ExtendedInfo variant into custom event specific fields.
func (evt *PoolSvcReplicasUpdate) FromProto(pbEvt *mgmtpb.RASEvent) error {
	evt.RAS = &RASEvent{
		Timestamp: pbEvt.Timestamp,
		Msg:       pbEvt.Msg,
		Hostname:  pbEvt.Hostname,
		Rank:      pbEvt.Rank,
		ID:        RASID(pbEvt.Id),
		Severity:  RASSeverityID(pbEvt.Severity),
		Type:      RASTypeID(pbEvt.Type),
	}

	pbInfo := pbEvt.GetPoolSvcInfo()
	if pbInfo == nil {
		return errors.Errorf("unexpected oneof, want %T got %T",
			&mgmtpb.RASEvent_PoolSvcInfo{}, pbInfo)
	}

	evt.ExtendedInfo = new(PoolSvcInfo)
	if err := convert.Types(pbInfo, evt.ExtendedInfo); err != nil {
		return errors.Wrapf(err, "converting %T->%T", pbInfo, evt.ExtendedInfo)
	}

	return nil
}

// ToProto packs this PoolSvcReplicasUpdate instance into a protobuf RAS event, encoding
// custom event specific fields into the equivalent ExtendedInfo oneof variant.
func (evt *PoolSvcReplicasUpdate) ToProto() (*mgmtpb.RASEvent, error) {
	pbEvt := new(mgmtpb.RASEvent)
	if err := convert.Types(evt.RAS, pbEvt); err != nil {
		return nil, errors.Wrapf(err, "converting %T->%T", evt.RAS, pbEvt)
	}

	pbInfo := new(mgmtpb.PoolSvcEventInfo)
	if err := convert.Types(evt.ExtendedInfo, pbInfo); err != nil {
		return nil, errors.Wrapf(err, "converting %T->%T", evt.ExtendedInfo, pbInfo)
	}

	pbEvt.ExtendedInfo = &mgmtpb.RASEvent_PoolSvcInfo{
		PoolSvcInfo: pbInfo,
	}

	return pbEvt, nil
}

// NewPoolSvcReplicasUpdateEvent creates a specific PoolSvcReplicasUpdate event from given inputs.
func NewPoolSvcReplicasUpdateEvent(hostname string, rank uint32, poolUUID string, svcReplicas []uint32, leaderTerm uint64) Event {
	evt := &RASEvent{
		Timestamp: common.FormatTime(time.Now()),
		Msg:       "DAOS pool service replica rank list updated",
		ID:        RASPoolSvcReplicasUpdate,
		Hostname:  hostname,
		Rank:      rank,
		Type:      RASTypeStateChange,
		Severity:  RASSeverityError,
	}

	return &PoolSvcReplicasUpdate{
		RAS: evt,
		ExtendedInfo: &PoolSvcInfo{
			PoolUUID:       poolUUID,
			SvcReplicas:    svcReplicas,
			RaftLeaderTerm: leaderTerm,
		},
	}
}
