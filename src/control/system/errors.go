//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package system

import (
	"fmt"
	"net"
	"strings"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
)

var (
	ErrEmptyGroupMap = errors.New("empty group map (all ranks excluded?)")
	ErrRaftUnavail   = errors.New("raft service unavailable (not started yet?)")
	ErrUninitialized = errors.New("system is uninitialized (storage format required?)")
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
	return strings.Contains(errors.Cause(err).Error(), ErrRaftUnavail.Error())
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
	Rank *Rank
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

func ErrRankExists(r Rank) *ErrMemberExists {
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
	newUUID     *uuid.UUID
	curUUID     *uuid.UUID
	newRank     *Rank
	curRank     *Rank
}

func (err *ErrJoinFailure) Error() string {
	switch {
	case err.rankChanged:
		return fmt.Sprintf("can't rejoin member with uuid %s: rank changed from %d -> %d", *err.curUUID, *err.curRank, *err.newRank)
	case err.uuidChanged:
		return fmt.Sprintf("can't rejoin member with rank %d: uuid changed from %s -> %s", *err.curRank, *err.curUUID, *err.newUUID)
	case err.isExcluded:
		return fmt.Sprintf("member %s (rank %d) has been administratively excluded", err.curUUID, *err.curRank)
	default:
		return "unknown join failure"
	}
}

func ErrRankChanged(new, cur Rank, uuid uuid.UUID) *ErrJoinFailure {
	return &ErrJoinFailure{
		rankChanged: true,
		curUUID:     &uuid,
		newRank:     &new,
		curRank:     &cur,
	}
}

func ErrUuidChanged(new, cur uuid.UUID, rank Rank) *ErrJoinFailure {
	return &ErrJoinFailure{
		uuidChanged: true,
		newUUID:     &new,
		curUUID:     &cur,
		curRank:     &rank,
	}
}

func ErrAdminExcluded(uuid uuid.UUID, rank Rank) *ErrJoinFailure {
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
	byRank *Rank
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

func ErrMemberRankNotFound(r Rank) *ErrMemberNotFound {
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
	byRank  *Rank
	byUUID  *uuid.UUID
	byLabel *string
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

func ErrPoolRankNotFound(r Rank) *ErrPoolNotFound {
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
