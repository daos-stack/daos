//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
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
func PoolSvcInfoFromProto(pbInfo *sharedpb.RASEvent_PoolSvcInfo) (*PoolSvcInfo, error) {
	psi := new(PoolSvcInfo)

	return psi, convert.Types(pbInfo.PoolSvcInfo, psi)
}

// PoolSvcInfoToProto converts event info from native to proto format.
func PoolSvcInfoToProto(psi *PoolSvcInfo) (*sharedpb.RASEvent_PoolSvcInfo, error) {
	pbInfo := &sharedpb.RASEvent_PoolSvcInfo{
		PoolSvcInfo: &sharedpb.RASEvent_PoolSvcEventInfo{},
	}

	return pbInfo, convert.Types(psi, pbInfo.PoolSvcInfo)
}

// NewPoolSvcReplicasUpdateEvent creates a specific PoolSvcRanksUpdate event from given inputs.
func NewPoolSvcReplicasUpdateEvent(hostname string, rank uint32, poolUUID string, svcReps []uint32, leaderTerm uint64) *RASEvent {
	return New(&RASEvent{
		Msg:      fmt.Sprintf("DAOS pool service replica list updated to %v", svcReps),
		ID:       RASPoolRepsUpdate,
		Hostname: hostname,
		Rank:     rank,
		PoolUUID: poolUUID,
		Type:     RASTypeStateChange,
		Severity: RASSeverityError,
		ExtendedInfo: &PoolSvcInfo{
			SvcReplicas:    svcReps,
			RaftLeaderTerm: leaderTerm,
		},
	})
}
