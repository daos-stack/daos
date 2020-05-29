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

package auth

import (
	"os/user"
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

func (u *MockUser) Gid() uint32 {
	if len(u.groupIDs) == 0 {
		return 1
	}
	return u.groupIDs[0]
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
