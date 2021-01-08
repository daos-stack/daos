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

import (
	"time"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// PoolSvcInfo describes details of a pool service.
type PoolSvcInfo struct {
	SvcReplicas    []uint32 `json:"svc_reps"`
	RaftLeaderTerm uint64   `json:"version"`
}

func (psi *PoolSvcInfo) isExtendedInfo() {}

// GetPoolSvcInfo returns extended info if of type PoolSvcInfo.
func (evt *RASEvent) GetPoolSvcInfo() *PoolSvcInfo {
	if ei, ok := evt.ExtendedInfo.(*PoolSvcInfo); ok {
		return ei
	}

	return nil
}

// PoolSvcInfoFromProto converts event info from proto to native format.
func PoolSvcInfoFromProto(pbInfo *mgmtpb.RASEvent_PoolSvcInfo) (*PoolSvcInfo, error) {
	psi := new(PoolSvcInfo)

	return psi, convert.Types(pbInfo.PoolSvcInfo, psi)
}

// PoolSvcInfoToProto converts event info from native to proto format.
func PoolSvcInfoToProto(psi *PoolSvcInfo) (*mgmtpb.RASEvent_PoolSvcInfo, error) {
	pbInfo := &mgmtpb.RASEvent_PoolSvcInfo{PoolSvcInfo: &mgmtpb.PoolSvcEventInfo{}}

	return pbInfo, convert.Types(psi, pbInfo.PoolSvcInfo)
}

// NewPoolSvcReplicasUpdateEvent creates a specific PoolSvcRanksUpdate event from given inputs.
func NewPoolSvcReplicasUpdateEvent(hostname string, rank uint32, poolUUID string, svcReplicas []uint32, leaderTerm uint64) *RASEvent {
	return &RASEvent{
		Timestamp: common.FormatTime(time.Now()),
		Msg:       "DAOS pool service replica rank list updated",
		ID:        RASPoolSvcReplicasUpdate,
		Hostname:  hostname,
		Rank:      rank,
		PUUID:     poolUUID,
		Type:      RASTypeStateChange,
		Severity:  RASSeverityError,
		ExtendedInfo: &PoolSvcInfo{
			SvcReplicas:    svcReplicas,
			RaftLeaderTerm: leaderTerm,
		},
	}
}
