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
	ErrEmptyGroupMap = errors.New("empty GroupMap")
	ErrRaftUnavail   = errors.New("raft service unavailable (not started yet?)")
)

// IsUnavailable returns a boolean indicating whether or not the
// supplied error corresponds to some unavailability state.
func IsUnavailable(err error) bool {
	return strings.Contains(errors.Cause(err).Error(), ErrRaftUnavail.Error())
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
	Rank Rank
}

func (err *ErrMemberExists) Error() string {
	return fmt.Sprintf("member with rank %d already exists", err.Rank)
}

// IsMemberExists returns a boolean indicating whether or not the
// supplied error is an instance of ErrMemberExists.
func IsMemberExists(err error) bool {
	_, ok := errors.Cause(err).(*ErrMemberExists)
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
