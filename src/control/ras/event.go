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

// Uint32 returns uint32 representation of event ID.
func (id EventID) Uint32() uint32 {
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

// Uint32 returns uint32 representation of event severity.
func (sev EventSeverityID) Uint32() uint32 {
	return uint32(sev)
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

// Uint32 returns uint32 representation of event type.
func (typ EventTypeID) Uint32() uint32 {
	return uint32(typ)
}

// Event describes details of a specific RAS event.
type Event struct {
	Name        string `json:"name"`
	Timestamp   string `json:"timestamp"`
	Msg         string `json:"msg"`
	Hostname    string `json:"hostname"`
	Data        []byte `json:"data"`
	Rank        uint32 `json:"rank"`
	InstanceIdx uint32 `json:"instance_idx"`
	ID          EventID
	Severity    EventSeverityID
	Type        EventTypeID
}

// MarshalJSON marshals ras.Event to JSON.
func (evt *Event) MarshalJSON() ([]byte, error) {
	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON Event
	return json.Marshal(&struct {
		ID       uint32
		Severity uint32
		Type     uint32
		*toJSON
	}{
		ID:       evt.ID.Uint32(),
		Severity: evt.Severity.Uint32(),
		Type:     evt.Type.Uint32(),
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
		ID       uint32
		Severity uint32
		Type     uint32
		*fromJSON
	}{
		fromJSON: (*fromJSON)(evt),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	evt.ID = EventID(from.ID)
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
		ID:          EventRankFail,
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

// ProcessEvent evaluates and actions the given RAS event.
func ProcessEvent(log logging.Logger, evt *Event) error {
	if evt == nil {
		return errors.New("attempt to process nil ras event")
	}
	log.Debugf("processing RAS event %q from rank %d on host %q", evt.Msg, evt.Rank,
		evt.Hostname)

	switch evt.ID {
	case EventRankFail, EventRankNoResp:
		log.Debugf("processing %s (%s) event", evt.ID.String(), evt.ID.Desc())
	default:
		return errors.Errorf("unrecognised event ID: %d", evt.ID)
	}

	return nil
}
