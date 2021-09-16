//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/common"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
)

// EngineStateInfo describes details of a engine's state.
type EngineStateInfo struct {
	InstanceIdx uint32 `json:"instance_idx"`
	ExitErr     error  `json:"-"`
}

func (rsi *EngineStateInfo) isExtendedInfo() {}

// GetEngineStateInfo returns extended info if of type EngineStateInfo.
func (evt *RASEvent) GetEngineStateInfo() *EngineStateInfo {
	if ei, ok := evt.ExtendedInfo.(*EngineStateInfo); ok {
		return ei
	}

	return nil
}

// EngineStateInfoFromProto converts event info from proto to native format.
func EngineStateInfoFromProto(pbInfo *sharedpb.RASEvent_EngineStateInfo) (*EngineStateInfo, error) {
	rsi := &EngineStateInfo{
		InstanceIdx: pbInfo.EngineStateInfo.GetInstance(),
	}
	if pbInfo.EngineStateInfo.GetErrored() {
		rsi.ExitErr = common.ExitStatus(pbInfo.EngineStateInfo.GetError())
	}

	return rsi, nil
}

// EngineStateInfoToProto converts event info from native to proto format.
func EngineStateInfoToProto(rsi *EngineStateInfo) (*sharedpb.RASEvent_EngineStateInfo, error) {
	pbInfo := &sharedpb.RASEvent_EngineStateInfo{
		EngineStateInfo: &sharedpb.RASEvent_EngineStateEventInfo{
			Instance: rsi.InstanceIdx,
		},
	}
	if rsi.ExitErr != nil {
		pbInfo.EngineStateInfo.Errored = true
		pbInfo.EngineStateInfo.Error = rsi.ExitErr.Error()
	}

	return pbInfo, nil
}

// NewEngineDiedEvent creates a specific EngineDied event from given inputs.
func NewEngineDiedEvent(hostname string, instanceIdx uint32, rank uint32, exitErr common.ExitStatus, exPid uint64) *RASEvent {
	return fill(&RASEvent{
		Msg:      fmt.Sprintf("DAOS engine %d exited unexpectedly: %s", instanceIdx, exitErr),
		ID:       RASEngineDied,
		Hostname: hostname,
		Rank:     rank,
		Type:     RASTypeStateChange,
		Severity: RASSeverityError,
		ProcID:   exPid, // pid of exited engine
		ExtendedInfo: &EngineStateInfo{
			InstanceIdx: instanceIdx,
			ExitErr:     exitErr,
		},
	})
}

// NewEngineFormatRequiredEvent creates a EngineFormatRequired event from given inputs.
func NewEngineFormatRequiredEvent(hostname string, instanceIdx uint32, formatType string) *RASEvent {
	return fill(&RASEvent{
		Msg:      fmt.Sprintf("DAOS engine %d requires a %s format", instanceIdx, formatType),
		ID:       RASEngineFormatRequired,
		Hostname: hostname,
		Type:     RASTypeInfoOnly,
		Severity: RASSeverityNotice,
		ExtendedInfo: &EngineStateInfo{
			InstanceIdx: instanceIdx,
		},
	})
}
