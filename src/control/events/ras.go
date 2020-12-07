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

/*
#include "daos_srv/ras.h"
*/
import "C"

import (
	"encoding/json"
)

// RASID describes a given RAS event.
type RASID uint32

// RASID constant definitions.
const (
	RASRankExit   RASID = C.RAS_RANK_EXIT
	RASRankNoResp RASID = C.RAS_RANK_NO_RESP
)

func (id RASID) String() string {
	return C.GoString(C.ras_event_id_enum_to_name(uint32(id)))
}

// Desc returns a description of the event.
func (id RASID) Desc() string {
	return C.GoString(C.ras_event_id_enum_to_msg(uint32(id)))
}

// Uint32 returns uint32 representation of event ID.
func (id RASID) Uint32() uint32 {
	return uint32(id)
}

// RASSeverityID describes the severity of a given RAS event.
type RASSeverityID uint32

// RASSeverityID constant definitions.
const (
	RASSeverityFatal RASSeverityID = C.RAS_SEV_FATAL
	RASSeverityWarn  RASSeverityID = C.RAS_SEV_WARN
	RASSeverityError RASSeverityID = C.RAS_SEV_ERROR
	RASSeverityInfo  RASSeverityID = C.RAS_SEV_INFO
)

func (sev RASSeverityID) String() string {
	return C.GoString(C.ras_event_sev_enum_to_name(uint32(sev)))
}

// Uint32 returns uint32 representation of event severity.
func (sev RASSeverityID) Uint32() uint32 {
	return uint32(sev)
}

// RASTypeID describes the type of a given RAS event.
type RASTypeID uint32

// RASTypeID constant definitions.
const (
	RASTypeRankStateChange RASTypeID = C.RAS_TYPE_RANK_STATE_CHANGE
	RASTypeInfoOnly        RASTypeID = C.RAS_TYPE_INFO_ONLY
)

func (typ RASTypeID) String() string {
	return C.GoString(C.ras_event_type_enum_to_name(uint32(typ)))
}

// Uint32 returns uint32 representation of event type.
func (typ RASTypeID) Uint32() uint32 {
	return uint32(typ)
}

// RASEvent describes details of a specific RAS event.
type RASEvent struct {
	Name      string `json:"name"`
	Timestamp string `json:"timestamp"`
	Msg       string `json:"msg"`
	Hostname  string `json:"hostname"`
	Data      []byte `json:"data"`
	Rank      uint32 `json:"rank"`
	ID        RASID
	Severity  RASSeverityID
	Type      RASTypeID
}

// MarshalJSON marshals RASEvent to JSON.
func (evt *RASEvent) MarshalJSON() ([]byte, error) {
	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON RASEvent
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

// UnmarshalJSON unmarshals RASEvent from JSON.
func (evt *RASEvent) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	// use a type alias to leverage the default unmarshal for
	// most fields
	type fromJSON RASEvent
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

	evt.ID = RASID(from.ID)
	evt.Severity = RASSeverityID(from.Severity)
	evt.Type = RASTypeID(from.Type)

	return nil
}
