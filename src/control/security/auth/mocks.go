//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package auth

import (
	"os/user"
	"strconv"

	"github.com/pkg/errors"
)

// Mocks

type MockUser struct {
	username   string
	uid        uint32
	groupIDs   []uint32
	groupIDErr error
}

func (u *MockUser) Username() string {
	return u.username
}

func (u *MockUser) GroupIDs() ([]uint32, error) {
	return u.groupIDs, u.groupIDErr
}

func (u *MockUser) Gid() (uint32, error) {
	if len(u.groupIDs) == 0 {
		return 0, errors.New("no mock gids to return")
	}
	return u.groupIDs[0], nil
}

func NewMockExtWithUser(name string, uid uint32, gids ...uint32) *MockExt {
	me := &MockExt{
		LookupUserIDResult: &MockUser{
			uid:      uid,
			username: name,
			groupIDs: gids,
		},
	}

	if len(gids) > 0 {
		for _, gid := range gids {
			me.LookupGroupIDResults = append(me.LookupGroupIDResults, &user.Group{
				Gid: strconv.Itoa(int(gid)),
			})
		}
	}

	return me
}

type MockExt struct {
	LookupUserIDUid        uint32
	LookupUserIDResult     User
	LookupUserIDErr        error
	LookupGroupIDGid       uint32
	LookupGroupIDResults   []*user.Group
	LookupGroupIDCallCount uint32
	LookupGroupIDErr       error
}

func (e *MockExt) Current() (User, error) {
	return e.LookupUserIDResult, e.LookupUserIDErr
}

func (e *MockExt) LookupUserID(uid uint32) (User, error) {
	e.LookupUserIDUid = uid
	return e.LookupUserIDResult, e.LookupUserIDErr
}

func (e *MockExt) LookupGroupID(gid uint32) (*user.Group, error) {
	e.LookupGroupIDGid = gid
	var result *user.Group
	if len(e.LookupGroupIDResults) > 0 {
		resultIdx := int(e.LookupGroupIDCallCount)
		if len(e.LookupGroupIDResults) <= resultIdx {
			resultIdx = len(e.LookupGroupIDResults) - 1
		}
		result = e.LookupGroupIDResults[resultIdx]
	}
	e.LookupGroupIDCallCount++
	return result, e.LookupGroupIDErr
}
