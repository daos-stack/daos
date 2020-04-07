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

package common

import (
	"fmt"
	"strconv"
	"strings"
)

// MockACLResult mocks an ACLResult.
type MockACLResult struct {
	Acl    []string
	Status int32
	Err    error
}

// ACL returns a properly formed AccessControlList from the mock data
func (m *MockACLResult) ACL() *AccessControlList {
	return &AccessControlList{
		Entries: m.Acl,
	}
}

// MockListPoolsResult mocks list pool results.
type MockListPoolsResult struct {
	Status int32
	Err    error
}

// GetIndex return suitable index value for auto generating mocks.
func GetIndex(varIdx ...int32) int32 {
	if len(varIdx) == 0 {
		varIdx = append(varIdx, 1)
	}

	return varIdx[0]
}

// MockUUID returns mock UUID values for use in tests.
func MockUUID(varIdx ...int32) string {
	idx := GetIndex(varIdx...)
	idxStr := strconv.Itoa(int(idx))

	return fmt.Sprintf("%s-%s-%s-%s-%s",
		strings.Repeat(idxStr, 8),
		strings.Repeat(idxStr, 4),
		strings.Repeat(idxStr, 4),
		strings.Repeat(idxStr, 4),
		strings.Repeat(idxStr, 12),
	)
}
