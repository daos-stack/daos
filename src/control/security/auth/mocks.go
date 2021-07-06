//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package auth

import (
	"os/user"

	"github.com/pkg/errors"
)

// Mocks

type MockUser struct {
	username   string
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
		result = e.LookupGroupIDResults[e.LookupGroupIDCallCount]
	}
	e.LookupGroupIDCallCount++
	return result, e.LookupGroupIDErr
}
