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

package ras

/*
#include "daos_srv/ras.h"
*/
import "C"

import (
	"encoding/json"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
)

// EventID describes a given RAS event.
type EventID uint32

// EventID constant definitions.
const (
	EventRankFail   EventID = C.RAS_RANK_FAIL
	EventRankNoResp EventID = C.RAS_RANK_NO_RESP
)

func (id EventID) String() string {
	return C.GoString(C.ras_event_id_enum_to_name(uint32(id)))
}

// Desc returns a description of the event.
func (id EventID) Desc() string {
	return C.GoString(C.ras_event_id_enum_to_msg(uint32(id)))
}

// ID returns numeric ID of event.
func (id EventID) ID() uint32 {
	return uint32(id)
}

// EventSeverityID describes the severity of a given RAS event.
type EventSeverityID uint32

// EventSeverityID constant definitions.
const (
	EventSeverityFatal EventSeverityID = C.RAS_SEV_FATAL
	EventSeverityWarn  EventSeverityID = C.RAS_SEV_WARN
	EventSeverityError EventSeverityID = C.RAS_SEV_ERROR
	EventSeverityInfo  EventSeverityID = C.RAS_SEV_INFO
)

func (sev EventSeverityID) String() string {
	return C.GoString(C.ras_event_sev_enum_to_name(uint32(sev)))
}

// EventTypeID describes the type of a given RAS event.
type EventTypeID uint32

// EventTypeID constant definitions.
const (
	EventTypeStateChange EventTypeID = C.RAS_TYPE_STATE_CHANGE
	EventTypeInfoOnly    EventTypeID = C.RAS_TYPE_INFO_ONLY
)

func (typ EventTypeID) String() string {
	return C.GoString(C.ras_event_type_enum_to_name(uint32(typ)))
}

// Event describes details of a specific RAS event.
type Event struct {
	Name        string `json:"name"`
	Timestamp   string `json:"timestamp"`
	Msg         string `json:"msg"`
	Hostname    string `json:"hostname"`
	Data        []byte `json:"data"`
	ID          uint32 `json:"id"`
	Rank        uint32 `json:"rank"`
	InstanceIdx uint32 `json:"instance_idx"`
	Severity    EventSeverityID
	Type        EventTypeID
}

// MarshalJSON marshals ras.Event to JSON.
func (evt *Event) MarshalJSON() ([]byte, error) {
	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON Event
	return json.Marshal(&struct {
		Severity uint32
		Type     uint32
		*toJSON
	}{
		Severity: uint32(evt.Severity),
		Type:     uint32(evt.Type),
		toJSON:   (*toJSON)(evt),
	})
}

// UnmarshalJSON unmarshals ras.Event from JSON.
func (evt *Event) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	// use a type alias to leverage the default unmarshal for
	// most fields
	type fromJSON Event
	from := &struct {
		Severity uint32
		Type     uint32
		*fromJSON
	}{
		fromJSON: (*fromJSON)(evt),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	evt.Severity = EventSeverityID(from.Severity)
	evt.Type = EventTypeID(from.Type)

	return nil
}

// NewRankFailEvent creates a specific RAS event entry.
//
// Hostname should be populated by caller.
func NewRankFailEvent(instanceIdx uint32, rank uint32, exitErr error) *Event {
	evt := &Event{
		Name:        EventRankFail.String(),
		Timestamp:   common.FormatTime(time.Now().UTC()),
		Msg:         EventRankFail.Desc(),
		InstanceIdx: instanceIdx,
		ID:          EventRankFail.ID(),
		Rank:        rank,
		Type:        EventTypeStateChange,
		Severity:    EventSeverityInfo,
	}

	if exitErr != nil {
		evt.Severity = EventSeverityError
		// encode exit error message in event
		// marshal on string will never fail
		errBytes, _ := json.Marshal(exitErr.Error())
		evt.Data = errBytes
	}

	return evt
}

// ProcessEvent evaluate and actions the given RAS event.
func ProcessEvent(log logging.Logger, event mgmtpb.RASEvent) error {
	log.Debugf("processing RAS event: %s", event.GetMsg())

	id := EventID(uint32(event.Id))
	switch id {
	case EventRankFail, EventRankNoResp:
		log.Debugf("processing %s (%s) event", id.String(), id.Desc())
	default:
		return errors.Errorf("unrecognised event ID: %d", event.Id)
	}

	return nil
}
