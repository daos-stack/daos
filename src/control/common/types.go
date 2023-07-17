//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"reflect"
	"strings"
)

// InterfaceIsNil returns true if the interface itself or its underlying value
// is nil.
func InterfaceIsNil(i interface{}) bool {
	// NB: The unsafe version of this, which makes assumptions
	// about the memory layout of interfaces, is a constant
	// 2.54 ns/op.
	// return (*[2]uintptr)(unsafe.Pointer(&i))[1] == 0

	// This version is only slightly slower in the grand scheme
	// of things. 3.96 ns/op for a nil type/value, and 7.98 ns/op
	// for a non-nil value.
	if i == nil {
		return true
	}
	return reflect.ValueOf(i).IsNil()
}

// NormalExit indicates that the process exited without error.
const NormalExit ExitStatus = "process exited with 0"

// ExitStatus implements the error interface and is used to indicate external
// process exit conditions.
type ExitStatus string

func (es ExitStatus) Error() string {
	return string(es)
}

// GetExitStatus ensures that a monitored process always returns an error of
// some sort when it exits so that we can respond appropriately.
func GetExitStatus(err error) error {
	if err != nil {
		return err
	}

	return NormalExit
}

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

// IsTransitionIllegal indicates if given state transitions is legal.
//
// Map state combinations to true (illegal) or false (legal) and return negated
// value.
func (ms MemberState) IsTransitionIllegal(to MemberState) bool {
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
		MemberStateStopping: {
			MemberStateReady: true,
		},
		MemberStateExcluded: {
			MemberStateReady:    true,
			MemberStateJoined:   true,
			MemberStateStopping: true,
			MemberStateErrored:  true,
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
