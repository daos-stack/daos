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

	"github.com/daos-stack/daos/src/control/common"
)

//// RankExit event indicates a termination of a rank process.
//var RankExit rankExit
//
//// RankExitPayload is the data for when a DAOS rank exits.
//type RankExitPayload struct {
//	*RASEvent
//	InstanceIdx uint32
//	ExitErr     error
//}
//
//type rankExit struct {
//	handlers []interface{ Handle(RankExitPayload) }
//}
//
//// Register adds an event handler for this event
//func (re *rankExit) Register(handler interface{ Handle(RankExitPayload) }) {
//	re.handlers = append(re.handlers, handler)
//}
//
//// Trigger sends out an event with the payload
//func (re rankExit) Trigger(payload RankExitPayload) {
//	for _, handler := range re.handlers {
//		go handler.Handle(payload)
//	}
//}

// NewRankExitPayload creates a specific RankExit event payload populated
// with RAS info.
//
// Hostname should be populated by caller.
func NewRankExitEvent(instanceIdx uint32, rank uint32, exitErr error) *RASEvent {
	evt := &RASEvent{
		Name:      RASRankExit.String(),
		Timestamp: common.FormatTime(time.Now()),
		Msg:       RASRankExit.Desc(),
		ID:        RASRankExit,
		Rank:      rank,
		Type:      RASTypeRankStateChange,
		Severity:  RASSeverityInfo,
	}

	if exitErr != nil {
		evt.Severity = RASSeverityError
	}
	// TODO: change function to generate RankExitPayload which when
	// converted will populate EventInfo proto field with relevant oneof for
	// specific payload including the exit error and instance index.

	return evt
}
