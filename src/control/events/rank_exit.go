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
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
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
func RankStateInfoFromProto(pbInfo *mgmtpb.RASEvent_RankStateInfo) (*RankStateInfo, error) {
	rsi := &RankStateInfo{
		InstanceIdx: pbInfo.RankStateInfo.GetInstance(),
	}
	if pbInfo.RankStateInfo.GetErrored() {
		rsi.ExitErr = common.ExitStatus(pbInfo.RankStateInfo.GetError())
	}

	return rsi, nil
}

// RankStateInfoToProto converts event info from native to proto format.
func RankStateInfoToProto(rsi *RankStateInfo) (*mgmtpb.RASEvent_RankStateInfo, error) {
	pbInfo := &mgmtpb.RASEvent_RankStateInfo{
		RankStateInfo: &mgmtpb.RankStateEventInfo{
			Instance: rsi.InstanceIdx,
		},
	}
	if rsi.ExitErr != nil {
		pbInfo.RankStateInfo.Errored = true
		pbInfo.RankStateInfo.Error = rsi.ExitErr.Error()
	}

	return pbInfo, nil
}

// NewRankExitEvent creates a specific RankExit event from given inputs.
func NewRankExitEvent(hostname string, instanceIdx uint32, rank uint32, exitErr common.ExitStatus) *RASEvent {
	return &RASEvent{
		Timestamp: common.FormatTime(time.Now()),
		Msg:       "DAOS rank exited unexpectedly",
		ID:        RASRankExit,
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
