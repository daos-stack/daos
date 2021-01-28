//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"time"

	"github.com/daos-stack/daos/src/control/common"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
)

// RankStateInfo describes details of a rank's state.
type RankStateInfo struct {
	InstanceIdx uint32 `json:"instance_idx"`
	ExitErr     error  `json:"-"`
}

func (rsi *RankStateInfo) isExtendedInfo() {}

// GetRankStateInfo returns extended info if of type RankStateInfo.
func (evt *RASEvent) GetRankStateInfo() *RankStateInfo {
	if ei, ok := evt.ExtendedInfo.(*RankStateInfo); ok {
		return ei
	}

	return nil
}

// RankStateInfoFromProto converts event info from proto to native format.
func RankStateInfoFromProto(pbInfo *sharedpb.RASEvent_RankStateInfo) (*RankStateInfo, error) {
	rsi := &RankStateInfo{
		InstanceIdx: pbInfo.RankStateInfo.GetInstance(),
	}
	if pbInfo.RankStateInfo.GetErrored() {
		rsi.ExitErr = common.ExitStatus(pbInfo.RankStateInfo.GetError())
	}

	return rsi, nil
}

// RankStateInfoToProto converts event info from native to proto format.
func RankStateInfoToProto(rsi *RankStateInfo) (*sharedpb.RASEvent_RankStateInfo, error) {
	pbInfo := &sharedpb.RASEvent_RankStateInfo{
		RankStateInfo: &sharedpb.RASEvent_RankStateEventInfo{
			Instance: rsi.InstanceIdx,
		},
	}
	if rsi.ExitErr != nil {
		pbInfo.RankStateInfo.Errored = true
		pbInfo.RankStateInfo.Error = rsi.ExitErr.Error()
	}

	return pbInfo, nil
}

// NewRankDownEvent creates a specific RankDown event from given inputs.
func NewRankDownEvent(hostname string, instanceIdx uint32, rank uint32, exitErr common.ExitStatus) *RASEvent {
	return &RASEvent{
		Timestamp: common.FormatTime(time.Now()),
		Msg:       "DAOS rank exited unexpectedly",
		ID:        RASRankDown,
		Hostname:  hostname,
		Rank:      rank,
		Type:      RASTypeStateChange,
		Severity:  RASSeverityError,
		ExtendedInfo: &RankStateInfo{
			InstanceIdx: instanceIdx,
			ExitErr:     exitErr,
		},
	}
}
