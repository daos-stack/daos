//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"encoding/json"
	"fmt"
	"net"
	"strings"
	"time"

	"github.com/dustin/go-humanize/english"
	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

// MemberState represents the activity state of DAOS system members.
type MemberState int

const (
	// MemberStateUnknown is the default invalid state.
	MemberStateUnknown MemberState = 0x0000
	// MemberStateAwaitFormat indicates the member is waiting for format.
	MemberStateAwaitFormat MemberState = 0x0001
	// MemberStateStarting indicates the member has started but is not
	// ready.
	MemberStateStarting MemberState = 0x0002
	// MemberStateReady indicates the member has setup successfully.
	MemberStateReady MemberState = 0x0004
	// MemberStateJoined indicates the member has joined the system.
	MemberStateJoined MemberState = 0x0008
	// MemberStateStopping indicates prep-shutdown successfully run.
	MemberStateStopping MemberState = 0x0010
	// MemberStateStopped indicates process has been stopped.
	MemberStateStopped MemberState = 0x0020
	// MemberStateExcluded indicates rank has been automatically excluded from DAOS system.
	MemberStateExcluded MemberState = 0x0040
	// MemberStateErrored indicates the process stopped with errors.
	MemberStateErrored MemberState = 0x0080
	// MemberStateUnresponsive indicates the process is not responding.
	MemberStateUnresponsive MemberState = 0x0100
	// MemberStateAdminExcluded indicates that the rank has been administratively excluded.
	MemberStateAdminExcluded MemberState = 0x0200
	// MemberStateMax is the last entry indicating end of list.
	MemberStateMax MemberState = 0x0400

	// ExcludedMemberFilter defines the state(s) to be used when determining
	// whether or not a member should be excluded from CaRT group map updates.
	ExcludedMemberFilter = MemberStateAwaitFormat | MemberStateExcluded | MemberStateAdminExcluded
	// AvailableMemberFilter defines the state(s) to be used when determining
	// whether or not a member is available for the purposes of pool creation, etc.
	AvailableMemberFilter = MemberStateReady | MemberStateJoined
	// AllMemberFilter will match all valid member states.
	AllMemberFilter = MemberState(0xFFFF)
)

func (ms MemberState) String() string {
	switch ms {
	case MemberStateAwaitFormat:
		return "AwaitFormat"
	case MemberStateStarting:
		return "Starting"
	case MemberStateReady:
		return "Ready"
	case MemberStateJoined:
		return "Joined"
	case MemberStateStopping:
		return "Stopping"
	case MemberStateStopped:
		return "Stopped"
	case MemberStateExcluded:
		return "Excluded"
	case MemberStateAdminExcluded:
		return "AdminExcluded"
	case MemberStateErrored:
		return "Errored"
	case MemberStateUnresponsive:
		return "Unresponsive"
	default:
		return "Unknown"
	}
}

func MemberStateFromString(in string) MemberState {
	switch strings.ToLower(in) {
	case "awaitformat":
		return MemberStateAwaitFormat
	case "starting":
		return MemberStateStarting
	case "ready":
		return MemberStateReady
	case "joined":
		return MemberStateJoined
	case "stopping":
		return MemberStateStopping
	case "stopped":
		return MemberStateStopped
	case "excluded":
		return MemberStateExcluded
	case "adminexcluded":
		return MemberStateAdminExcluded
	case "errored":
		return MemberStateErrored
	case "unresponsive":
		return MemberStateUnresponsive
	default:
		return MemberStateUnknown
	}
}

// isTransitionIllegal indicates if given state transitions is legal.
//
// Map state combinations to true (illegal) or false (legal) and return negated
// value.
func (ms MemberState) isTransitionIllegal(to MemberState) bool {
	if ms == MemberStateUnknown || ms == MemberStateAdminExcluded {
		return true // no legal transitions
	}
	if ms == to {
		return true // identical state
	}

	return map[MemberState]map[MemberState]bool{
		MemberStateAwaitFormat: {
			MemberStateExcluded: true,
		},
		MemberStateStarting: {
			MemberStateExcluded: true,
		},
		MemberStateReady: {
			MemberStateExcluded: true,
		},
		MemberStateJoined: {
			MemberStateReady: true,
		},
		MemberStateErrored: {
			MemberStateReady:    true,
			MemberStateJoined:   true,
			MemberStateStopping: true,
		},
		MemberStateUnresponsive: {
			MemberStateReady:    true,
			MemberStateJoined:   true,
			MemberStateStopping: true,
		},
	}[ms][to]
}

// MemberStates2Mask returns a state bitmask and a flag indicating whether to include the "Unknown"
// state from an input list of desired member states.
func MemberStates2Mask(desiredStates ...MemberState) (MemberState, bool) {
	var includeUnknown bool
	stateMask := AllMemberFilter

	if len(desiredStates) > 0 {
		stateMask = 0
		for _, s := range desiredStates {
			if s == MemberStateUnknown {
				includeUnknown = true
			}
			stateMask |= s
		}
	}
	if stateMask == AllMemberFilter {
		includeUnknown = true
	}

	return stateMask, includeUnknown
}

// Member refers to a data-plane instance that is a member of this DAOS
// system running on host with the control-plane listening at "Addr".
type Member struct {
	Rank           ranklist.Rank `json:"rank"`
	Incarnation    uint64        `json:"incarnation"`
	UUID           uuid.UUID     `json:"uuid"`
	Addr           *net.TCPAddr  `json:"addr"`
	FabricURI      string        `json:"fabric_uri"`
	FabricContexts uint32        `json:"fabric_contexts"`
	State          MemberState   `json:"-"`
	Info           string        `json:"info"`
	FaultDomain    *FaultDomain  `json:"fault_domain"`
	LastUpdate     time.Time     `json:"last_update"`
}

// MarshalJSON marshals system.Member to JSON.
func (sm *Member) MarshalJSON() ([]byte, error) {
	if sm == nil {
		return nil, errors.New("tried to marshal nil Member")
	}

	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON Member
	return json.Marshal(&struct {
		Addr        string `json:"addr"`
		State       string `json:"state"`
		FaultDomain string `json:"fault_domain"`
		*toJSON
	}{
		Addr:        sm.Addr.String(),
		State:       strings.ToLower(sm.State.String()),
		FaultDomain: sm.FaultDomain.String(),
		toJSON:      (*toJSON)(sm),
	})
}

// UnmarshalJSON unmarshals system.Member from JSON.
func (sm *Member) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	// use a type alias to leverage the default unmarshal for
	// most fields
	type fromJSON Member
	from := &struct {
		Addr        string `json:"addr"`
		State       string `json:"state"`
		FaultDomain string `json:"fault_domain"`
		*fromJSON
	}{
		fromJSON: (*fromJSON)(sm),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	addr, err := net.ResolveTCPAddr("tcp", from.Addr)
	if err != nil {
		return err
	}
	sm.Addr = addr

	sm.State = MemberStateFromString(from.State)

	fd, err := NewFaultDomainFromString(from.FaultDomain)
	if err != nil {
		return err
	}
	sm.FaultDomain = fd

	return nil
}

func (sm *Member) String() string {
	return fmt.Sprintf("%s/%d/%s", sm.Addr, sm.Rank, sm.State)
}

// WithInfo adds info field and returns updated member.
func (sm *Member) WithInfo(msg string) *Member {
	sm.Info = msg
	return sm
}

// WithFaultDomain adds the fault domain field and returns the updated member.
func (sm *Member) WithFaultDomain(fd *FaultDomain) *Member {
	sm.FaultDomain = fd
	return sm
}

// Members is a type alias for a slice of member references
type Members []*Member

// MemberResult refers to the result of an action on a Member.
type MemberResult struct {
	Addr    string
	Rank    ranklist.Rank
	Action  string
	Errored bool
	Msg     string
	State   MemberState `json:"state"`
}

// MarshalJSON marshals system.MemberResult to JSON.
func (mr *MemberResult) MarshalJSON() ([]byte, error) {
	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON MemberResult
	return json.Marshal(&struct {
		State string `json:"state"`
		*toJSON
	}{
		State:  strings.ToLower(mr.State.String()),
		toJSON: (*toJSON)(mr),
	})
}

// UnmarshalJSON unmarshals system.MemberResult from JSON.
func (mr *MemberResult) UnmarshalJSON(data []byte) error {
	if string(data) == "null" {
		return nil
	}

	// use a type alias to leverage the default unmarshal for
	// most fields
	type fromJSON MemberResult
	from := &struct {
		State string `json:"state"`
		*fromJSON
	}{
		fromJSON: (*fromJSON)(mr),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	mr.State = MemberStateFromString(from.State)

	return nil
}

// Equals returns true if dereferenced structs share the same field values.
func (mr *MemberResult) Equals(other *MemberResult) bool {
	if mr == nil {
		return false
	}
	if other == nil {
		return false
	}
	return *mr == *other
}

// NewMemberResult returns a reference to a new member result struct.
//
// Host address and action fields are not always used so not populated here.
func NewMemberResult(rank ranklist.Rank, err error, state MemberState, action ...string) *MemberResult {
	result := MemberResult{Rank: rank, State: state}
	if err != nil {
		result.Errored = true
		result.Msg = err.Error()
		// Any error should result in either an unresponsive or errored state.
		if state != MemberStateUnresponsive {
			result.State = MemberStateErrored
		}
	}
	if len(action) > 0 {
		result.Action = action[0]
	}

	return &result
}

// MemberResults is a type alias for a slice of member result references.
type MemberResults []*MemberResult

// Errors returns an error indicating if and which ranks failed.
func (mrs MemberResults) Errors() error {
	rs, err := ranklist.CreateRankSet("")
	if err != nil {
		return err
	}

	for _, mr := range mrs {
		if mr.Errored {
			rs.Add(mr.Rank)
		}
	}

	if rs.Count() > 0 {
		return errors.Errorf("failed %s %s",
			english.PluralWord(rs.Count(), "rank", "ranks"),
			rs.String())
	}

	return nil
}
