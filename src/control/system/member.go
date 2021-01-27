//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"encoding/json"
	"fmt"
	"net"

	"github.com/google/uuid"
	"github.com/pkg/errors"
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
	// MemberStateEvicted indicates rank has been evicted from DAOS system.
	MemberStateEvicted MemberState = 0x0040
	// MemberStateErrored indicates the process stopped with errors.
	MemberStateErrored MemberState = 0x0080
	// MemberStateUnresponsive indicates the process is not responding.
	MemberStateUnresponsive MemberState = 0x0100

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
	case MemberStateEvicted:
		return "Evicted"
	case MemberStateErrored:
		return "Errored"
	case MemberStateUnresponsive:
		return "Unresponsive"
	default:
		return "Unknown"
	}
}

// isTransitionIllegal indicates if given state transitions is legal.
//
// Map state combinations to true (illegal) or false (legal) and return negated
// value.
func (ms MemberState) isTransitionIllegal(to MemberState) bool {
	if ms == MemberStateUnknown {
		return true // no legal transitions
	}
	if ms == to {
		return true // identical state
	}
	return map[MemberState]map[MemberState]bool{
		MemberStateAwaitFormat: {
			MemberStateEvicted: true,
		},
		MemberStateStarting: {
			MemberStateEvicted: true,
		},
		MemberStateReady: {
			MemberStateEvicted: true,
		},
		MemberStateJoined: {
			MemberStateReady: true,
		},
		MemberStateStopping: {
			MemberStateReady: true,
		},
		MemberStateEvicted: {
			MemberStateReady:    true,
			MemberStateJoined:   true,
			MemberStateStopping: true,
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

// Member refers to a data-plane instance that is a member of this DAOS
// system running on host with the control-plane listening at "Addr".
type Member struct {
	Rank           Rank
	UUID           uuid.UUID
	Addr           *net.TCPAddr
	FabricURI      string
	FabricContexts uint32
	state          MemberState
	Info           string
	FaultDomain    *FaultDomain
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
		Addr        string
		State       int
		FaultDomain string
		*toJSON
	}{
		Addr:        sm.Addr.String(),
		State:       int(sm.state),
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
		Addr        string
		State       int
		FaultDomain string
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

	sm.state = MemberState(from.State)

	fd, err := NewFaultDomainFromString(from.FaultDomain)
	if err != nil {
		return err
	}
	sm.FaultDomain = fd

	return nil
}

func (sm *Member) String() string {
	return fmt.Sprintf("%s/%d/%s", sm.Addr, sm.Rank, sm.State())
}

// State retrieves member state.
func (sm *Member) State() MemberState {
	return sm.state
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

// RankFaultDomain generates the fault domain representing this Member rank.
func (sm *Member) RankFaultDomain() *FaultDomain {
	rankDomain := fmt.Sprintf("rank%d", uint32(sm.Rank))
	return sm.FaultDomain.MustCreateChild(rankDomain) // "rankX" string can't fail
}

// NewMember returns a reference to a new member struct.
func NewMember(rank Rank, uuidStr, uri string, addr *net.TCPAddr, state MemberState) *Member {
	// FIXME: Either require a valid uuid.UUID to be supplied
	// or else change the return signature to include an error
	newUUID := uuid.MustParse(uuidStr)
	return &Member{Rank: rank, UUID: newUUID, FabricURI: uri, Addr: addr,
		state: state, FaultDomain: MustCreateFaultDomain()}
}

// Members is a type alias for a slice of member references
type Members []*Member

// MemberResult refers to the result of an action on a Member.
type MemberResult struct {
	Addr    string
	Rank    Rank
	Action  string
	Errored bool
	Msg     string
	State   MemberState
}

// MarshalJSON marshals system.MemberResult to JSON.
func (mr *MemberResult) MarshalJSON() ([]byte, error) {
	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON MemberResult
	return json.Marshal(&struct {
		State int
		*toJSON
	}{
		State:  int(mr.State),
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
		State int
		*fromJSON
	}{
		fromJSON: (*fromJSON)(mr),
	}

	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	mr.State = MemberState(from.State)

	return nil
}

// NewMemberResult returns a reference to a new member result struct.
//
// Host address and action fields are not always used so not populated here.
func NewMemberResult(rank Rank, err error, state MemberState) *MemberResult {
	result := MemberResult{Rank: rank, State: state}
	if err != nil {
		result.Errored = true
		result.Msg = err.Error()
	}

	return &result
}

// MemberResults is a type alias for a slice of member result references.
type MemberResults []*MemberResult

// HasErrors returns true if any of the member results errored.
func (smr MemberResults) HasErrors() bool {
	for _, res := range smr {
		if res.Errored {
			return true
		}
	}

	return false
}
