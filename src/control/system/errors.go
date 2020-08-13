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
)

type ErrNotReplica struct {
	Replicas []string
}

func (err *ErrNotReplica) Error() string {
	return fmt.Sprintf("not a system db replica (try one of %s)",
		strings.Join(err.Replicas, ","))
}

func IsNotReplica(err error) bool {
	_, ok := errors.Cause(err).(*ErrNotReplica)
	return ok
}

type ErrNotLeader struct {
	LeaderHint string
	Replicas   []string
}

func (err *ErrNotLeader) Error() string {
	return fmt.Sprintf("not the system db leader (try %s or one of %s)",
		err.LeaderHint, strings.Join(err.Replicas, ","))
}

func IsNotLeader(err error) bool {
	_, ok := errors.Cause(err).(*ErrNotLeader)
	return ok
}

type ErrMemberExists struct {
	Rank Rank
}

func (err *ErrMemberExists) Error() string {
	return fmt.Sprintf("member with rank %d already exists", err.Rank)
}

type ErrMemberNotFound struct {
	byRank *Rank
	byUUID *uuid.UUID
	byAddr net.Addr
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

type ErrPoolNotFound struct {
	byRank *Rank
	byUUID *uuid.UUID
	byAddr net.Addr
}

func (err *ErrPoolNotFound) Error() string {
	switch {
	case err.byRank != nil:
		return fmt.Sprintf("unable to find pool service with rank %d", *err.byRank)
	case err.byUUID != nil:
		return fmt.Sprintf("unable to find pool service with uuid %s", *err.byUUID)
	default:
		return "unable to find pool service"
	}
}
