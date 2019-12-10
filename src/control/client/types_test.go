//
// (C) Copyright 2019 Intel Corporation.
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

package client

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

func TestAccessControlList_String(t *testing.T) {
	for name, tc := range map[string]struct {
		acl    *AccessControlList
		expStr string
	}{
		"nil": {
			expStr: "# Entries:\n#   None\n",
		},
		"empty": {
			acl:    &AccessControlList{},
			expStr: "# Entries:\n#   None\n",
		},
		"single": {
			acl: &AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
			},
			expStr: "# Entries:\nA::user@:rw\n",
		},
		"multiple": {
			acl: &AccessControlList{
				Entries: []string{
					"A::OWNER@:rw",
					"A:G:GROUP@:rw",
					"A:G:readers@:r",
				},
			},
			expStr: "# Entries:\nA::OWNER@:rw\nA:G:GROUP@:rw\nA:G:readers@:r\n",
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.acl.String(), tc.expStr, "string output didn't match")
		})
	}
}

func TestAccessControlList_Empty(t *testing.T) {
	for name, tc := range map[string]struct {
		acl       *AccessControlList
		expResult bool
	}{
		"nil": {
			expResult: true,
		},
		"empty": {
			acl:       &AccessControlList{},
			expResult: true,
		},
		"single": {
			acl: &AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
			},
			expResult: false,
		},
		"multiple": {
			acl: &AccessControlList{
				Entries: []string{
					"A::OWNER@:rw",
					"A:G:GROUP@:rw",
					"A:G:readers@:r",
				},
			},
			expResult: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			common.AssertEqual(t, tc.acl.Empty(), tc.expResult, "result didn't match")
		})
	}
}

func TestPoolDiscoveriesFromPB(t *testing.T) {
	testUUIDs := []string{
		"12345678-1234-1234-1234-123456789abc",
		"87654321-4321-4321-4321-cba987654321",
	}

	for name, tc := range map[string]struct {
		pbPools   []*mgmtpb.ListPoolsResp_Pool
		expResult []*PoolDiscovery
	}{
		"empty": {
			pbPools:   []*mgmtpb.ListPoolsResp_Pool{},
			expResult: []*PoolDiscovery{},
		},
		"single pool": {
			pbPools: []*mgmtpb.ListPoolsResp_Pool{
				{
					Uuid:    testUUIDs[0],
					Svcreps: []int32{1},
				},
			},
			expResult: []*PoolDiscovery{
				{
					UUID:        testUUIDs[0],
					SvcReplicas: []int{1},
				},
			},
		},
		"multiple pools": {
			pbPools: []*mgmtpb.ListPoolsResp_Pool{
				{
					Uuid:    testUUIDs[0],
					Svcreps: []int32{1},
				},
				{
					Uuid:    testUUIDs[1],
					Svcreps: []int32{2},
				},
			},
			expResult: []*PoolDiscovery{
				{
					UUID:        testUUIDs[0],
					SvcReplicas: []int{1},
				},
				{
					UUID:        testUUIDs[1],
					SvcReplicas: []int{2},
				},
			},
		},
		"multiple svc replica ranks": {
			pbPools: []*mgmtpb.ListPoolsResp_Pool{
				{
					Uuid:    testUUIDs[0],
					Svcreps: []int32{0, 1, 3, 5},
				},
			},
			expResult: []*PoolDiscovery{
				{
					UUID:        testUUIDs[0],
					SvcReplicas: []int{0, 1, 3, 5},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := poolDiscoveriesFromPB(tc.pbPools)

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
