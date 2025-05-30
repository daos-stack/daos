//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"encoding/json"
	"fmt"
	"net"
	"strings"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

var (
	ErrEmptyGroupMap          = errors.New("empty group map (all ranks excluded?)")
	ErrRaftUnavail            = errors.New("raft service unavailable (not started yet?)")
	ErrUninitialized          = errors.New("system is uninitialized (storage format required?)")
	ErrLeaderStepUpInProgress = errors.New("leader step-up in progress (try again)")
)

// IsNotReady is a convenience function for checking if an error
// indicates that the system is not ready to serve requests.
func IsNotReady(err error) bool {
	return IsUninitialized(err) || IsUnavailable(err)
}

// IsUnavailable returns a boolean indicating whether or not the
// supplied error corresponds to some unavailability state.
func IsUnavailable(err error) bool {
	if err == nil {
		return false
	}
	cause := errors.Cause(err).Error()
	return strings.Contains(cause, ErrRaftUnavail.Error()) || strings.Contains(cause, ErrLeaderStepUpInProgress.Error())
}

// IsEmptyGroupMap returns a boolean indicating whether or not the
// supplied error corresponds to an empty system group map.
func IsEmptyGroupMap(err error) bool {
	if err == nil {
		return false
	}
	return strings.Contains(errors.Cause(err).Error(), ErrEmptyGroupMap.Error())
}

// IsUninitialized returns a boolean indicating whether or not the
// supplied error corresponds to an uninitialized system.
func IsUninitialized(err error) bool {
	if err == nil {
		return false
	}
	return strings.Contains(errors.Cause(err).Error(), ErrUninitialized.Error())
}

// ErrNotReplica indicates that a request was made to a control plane
// instance that is not a designated Management Service replica.
type ErrNotReplica struct {
	Replicas []string
}

func (err *ErrNotReplica) Error() string {
	return fmt.Sprintf("not a %s replica (try one of %s)",
		build.ManagementServiceName, strings.Join(err.Replicas, ","))
}

// IsNotReplica returns a boolean indicating whether or not the
// supplied error is an instance of ErrNotReplica.
func IsNotReplica(err error) bool {
	_, ok := errors.Cause(err).(*ErrNotReplica)
	return ok
}

// ErrNotLeader indicates that a request was made to a control plane
// instance that is not the current Management Service Leader.
type ErrNotLeader struct {
	LeaderHint string
	Replicas   []string
}

func (err *ErrNotLeader) Error() string {
	return fmt.Sprintf("not the %s leader (try %s or one of %s)",
		build.ManagementServiceName, err.LeaderHint, strings.Join(err.Replicas, ","))
}

// IsNotLeader returns a boolean indicating whether or not the
// supplied error is an instance of ErrNotLeader.
func IsNotLeader(err error) bool {
	_, ok := errors.Cause(err).(*ErrNotLeader)
	return ok
}

// ErrMemberExists indicates the failure of an operation that
// expected the given member to not exist.
type ErrMemberExists struct {
	Rank *ranklist.Rank
	UUID *uuid.UUID
}

func (err *ErrMemberExists) Error() string {
	switch {
	case err.Rank != nil:
		return fmt.Sprintf("member with rank %d already exists", *err.Rank)
	case err.UUID != nil:
		return fmt.Sprintf("member with uuid %s already exists", *err.UUID)
	default:
		return "member already exists"
	}
}

func ErrRankExists(r ranklist.Rank) *ErrMemberExists {
	return &ErrMemberExists{Rank: &r}
}

func ErrUuidExists(u uuid.UUID) *ErrMemberExists {
	return &ErrMemberExists{UUID: &u}
}

// IsMemberExists returns a boolean indicating whether or not the
// supplied error is an instance of ErrMemberExists.
func IsMemberExists(err error) bool {
	_, ok := errors.Cause(err).(*ErrMemberExists)
	return ok
}

// ErrJoinFailure indicates the failure of a Join request due
// to some structured error condition.
type ErrJoinFailure struct {
	rankChanged bool
	uuidChanged bool
	isExcluded  bool
	addrChanged bool
	newUUID     *uuid.UUID
	curUUID     *uuid.UUID
	newRank     *ranklist.Rank
	curRank     *ranklist.Rank
	newAddr     *net.TCPAddr
	curAddr     *net.TCPAddr
}

func (err *ErrJoinFailure) Error() string {
	switch {
	case err.rankChanged:
		return fmt.Sprintf("can't rejoin member with uuid %s: rank changed from %d -> %d", *err.curUUID, *err.curRank, *err.newRank)
	case err.uuidChanged:
		return fmt.Sprintf("can't rejoin member with rank %d: uuid changed from %s -> %s", *err.curRank, *err.curUUID, *err.newUUID)
	case err.isExcluded:
		return fmt.Sprintf("member %s (rank %d) has been administratively excluded", err.curUUID, *err.curRank)
	case err.addrChanged:
		return fmt.Sprintf("can't rejoin member %s (rank %d): control address changed from %s -> %s", *err.curUUID, *err.curRank, err.curAddr, err.newAddr)
	default:
		return "unknown join failure"
	}
}

func ErrJoinRankChanged(new, cur ranklist.Rank, uuid uuid.UUID) *ErrJoinFailure {
	return &ErrJoinFailure{
		rankChanged: true,
		curUUID:     &uuid,
		newRank:     &new,
		curRank:     &cur,
	}
}

func ErrJoinUuidChanged(new, cur uuid.UUID, rank ranklist.Rank) *ErrJoinFailure {
	return &ErrJoinFailure{
		uuidChanged: true,
		newUUID:     &new,
		curUUID:     &cur,
		curRank:     &rank,
	}
}

func ErrJoinControlAddrChanged(new, cur *net.TCPAddr, uuid uuid.UUID, rank ranklist.Rank) *ErrJoinFailure {
	return &ErrJoinFailure{
		addrChanged: true,
		curUUID:     &uuid,
		curRank:     &rank,
		newAddr:     new,
		curAddr:     cur,
	}
}

func ErrJoinAdminExcluded(uuid uuid.UUID, rank ranklist.Rank) *ErrJoinFailure {
	return &ErrJoinFailure{
		isExcluded: true,
		curUUID:    &uuid,
		curRank:    &rank,
	}
}

// IsJoinFailure returns a boolean indicating whether or not the
// supplied error is an instance of ErrJoinFailure.
func IsJoinFailure(err error) bool {
	_, ok := errors.Cause(err).(*ErrJoinFailure)
	return ok
}

// ErrMemberNotFound indicates a failure to find a member with the
// given search criterion.
type ErrMemberNotFound struct {
	byRank *ranklist.Rank
	byUUID *uuid.UUID
	byAddr *net.TCPAddr
}

func (err *ErrMemberNotFound) Error() string {
	switch {
	case err.byRank != nil:
		return fmt.Sprintf("unable to find member with rank %d", *err.byRank)
	case err.byUUID != nil:
		return fmt.Sprintf("unable to find member with uuid %s", *err.byUUID)
	case err.byAddr != nil:
		return fmt.Sprintf("unable to find member with addr %s", err.byAddr)
	default:
		return "unable to find member"
	}
}

// IsMemberNotFound returns a boolean indicating whether or not the
// supplied error is an instance of ErrMemberNotFound.
func IsMemberNotFound(err error) bool {
	_, ok := errors.Cause(err).(*ErrMemberNotFound)
	return ok
}

func ErrMemberRankNotFound(r ranklist.Rank) *ErrMemberNotFound {
	return &ErrMemberNotFound{byRank: &r}
}

func ErrMemberUUIDNotFound(u uuid.UUID) *ErrMemberNotFound {
	return &ErrMemberNotFound{byUUID: &u}
}

func ErrMemberAddrNotFound(a *net.TCPAddr) *ErrMemberNotFound {
	return &ErrMemberNotFound{byAddr: a}
}

// ErrPoolNotFound indicates a failure to find a pool service with the
// given search criterion.
type ErrPoolNotFound struct {
	byRank  *ranklist.Rank
	byUUID  *uuid.UUID
	byLabel *string
}

func (err *ErrPoolNotFound) MarshalJSON() ([]byte, error) {
	return json.Marshal(struct {
		Rank  *ranklist.Rank
		UUID  *uuid.UUID
		Label *string
	}{
		Rank:  err.byRank,
		UUID:  err.byUUID,
		Label: err.byLabel,
	})
}

func (err *ErrPoolNotFound) UnmarshalJSON(data []byte) error {
	if err == nil {
		return nil
	}

	var tmp struct {
		Rank  *ranklist.Rank
		UUID  *uuid.UUID
		Label *string
	}
	if err := json.Unmarshal(data, &tmp); err != nil {
		return err
	}
	err.byRank = tmp.Rank
	err.byUUID = tmp.UUID
	err.byLabel = tmp.Label
	return nil
}

func (err *ErrPoolNotFound) Error() string {
	switch {
	case err.byRank != nil:
		return fmt.Sprintf("unable to find pool service with rank %d", *err.byRank)
	case err.byUUID != nil:
		return fmt.Sprintf("unable to find pool service with uuid %s", *err.byUUID)
	case err.byLabel != nil:
		return fmt.Sprintf("unable to find pool service with label %q", *err.byLabel)
	default:
		return "unable to find pool service"
	}
}

// IsPoolNotFound returns a boolean indicating whether or not the
// supplied error is an instance of ErrPoolNotFound.
func IsPoolNotFound(err error) bool {
	_, ok := errors.Cause(err).(*ErrPoolNotFound)
	return ok
}

func ErrPoolRankNotFound(r ranklist.Rank) *ErrPoolNotFound {
	return &ErrPoolNotFound{byRank: &r}
}

func ErrPoolUUIDNotFound(u uuid.UUID) *ErrPoolNotFound {
	return &ErrPoolNotFound{byUUID: &u}
}

func ErrPoolLabelNotFound(l string) *ErrPoolNotFound {
	return &ErrPoolNotFound{byLabel: &l}
}

type errSystemAttrNotFound struct {
	key string
}

func (err *errSystemAttrNotFound) Error() string {
	return fmt.Sprintf("unable to find system attribute with key %q", err.key)
}

func ErrSystemAttrNotFound(key string) *errSystemAttrNotFound {
	return &errSystemAttrNotFound{key: key}
}

func IsErrSystemAttrNotFound(err error) bool {
	_, ok := errors.Cause(err).(*errSystemAttrNotFound)
	return ok
}
