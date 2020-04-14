//
// (C) Copyright 2019-2020 Intel Corporation.
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

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

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
		"owner/group but no entries": {
			acl: &AccessControlList{
				Owner:      "foo@",
				OwnerGroup: "bar@",
			},
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			AssertEqual(t, tc.acl.Empty(), tc.expResult, "result didn't match")
		})
	}
}

func TestAccessControlList_HasOwner(t *testing.T) {
	for name, tc := range map[string]struct {
		acl       *AccessControlList
		expResult bool
	}{
		"nil": {
			expResult: false,
		},
		"empty": {
			acl:       &AccessControlList{},
			expResult: false,
		},
		"owner with entries": {
			acl: &AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
				Owner: "foo@",
			},
			expResult: true,
		},
		"no owner with entries": {
			acl: &AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
			},
			expResult: false,
		},
		"owner with empty list": {
			acl: &AccessControlList{
				Owner: "bar@",
			},
			expResult: true,
		},
		"group but no owner": {
			acl: &AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
				OwnerGroup: "admins@",
			},
			expResult: false,
		},
		"owner and group": {
			acl: &AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
				Owner:      "admin@",
				OwnerGroup: "admins@",
			},
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			AssertEqual(t, tc.acl.HasOwner(), tc.expResult, "result didn't match")
		})
	}
}

func TestAccessControlList_HasOwnerGroup(t *testing.T) {
	for name, tc := range map[string]struct {
		acl       *AccessControlList
		expResult bool
	}{
		"nil": {
			expResult: false,
		},
		"empty": {
			acl:       &AccessControlList{},
			expResult: false,
		},
		"group with entries": {
			acl: &AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
				OwnerGroup: "foo@",
			},
			expResult: true,
		},
		"no group with entries": {
			acl: &AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
			},
			expResult: false,
		},
		"group with empty list": {
			acl: &AccessControlList{
				OwnerGroup: "bar@",
			},
			expResult: true,
		},
		"owner but no group": {
			acl: &AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
				Owner: "bob@",
			},
			expResult: false,
		},
		"owner and group": {
			acl: &AccessControlList{
				Entries: []string{
					"A::user@:rw",
				},
				Owner:      "admin@",
				OwnerGroup: "admins@",
			},
			expResult: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			AssertEqual(t, tc.acl.HasOwnerGroup(), tc.expResult, "result didn't match")
		})
	}
}

func TestAccessControlListFromPB(t *testing.T) {
	testEntries := []string{
		"A::OWNER@:rw",
		"A:G:GROUP@:rw",
		"A::bob@:r",
	}

	for name, tc := range map[string]struct {
		pbACL     *mgmtpb.ACLResp
		expResult *AccessControlList
	}{
		"nil": {
			expResult: &AccessControlList{},
		},
		"empty": {
			pbACL:     &mgmtpb.ACLResp{},
			expResult: &AccessControlList{},
		},
		"owner": {
			pbACL:     &mgmtpb.ACLResp{OwnerUser: "bob@"},
			expResult: &AccessControlList{Owner: "bob@"},
		},
		"group": {
			pbACL:     &mgmtpb.ACLResp{OwnerGroup: "owners@"},
			expResult: &AccessControlList{OwnerGroup: "owners@"},
		},
		"entries": {
			pbACL:     &mgmtpb.ACLResp{ACL: testEntries},
			expResult: &AccessControlList{Entries: testEntries},
		},
		"fully populated": {
			pbACL: &mgmtpb.ACLResp{
				ACL:        testEntries,
				OwnerUser:  "owner@",
				OwnerGroup: "group@",
			},
			expResult: &AccessControlList{
				Entries:    testEntries,
				Owner:      "owner@",
				OwnerGroup: "group@",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := proto.AccessControlListFromPB(tc.pbACL)

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
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
					Svcreps: []uint32{1},
				},
			},
			expResult: []*PoolDiscovery{
				{
					UUID:        testUUIDs[0],
					SvcReplicas: []uint32{1},
				},
			},
		},
		"multiple pools": {
			pbPools: []*mgmtpb.ListPoolsResp_Pool{
				{
					Uuid:    testUUIDs[0],
					Svcreps: []uint32{1},
				},
				{
					Uuid:    testUUIDs[1],
					Svcreps: []uint32{2},
				},
			},
			expResult: []*PoolDiscovery{
				{
					UUID:        testUUIDs[0],
					SvcReplicas: []uint32{1},
				},
				{
					UUID:        testUUIDs[1],
					SvcReplicas: []uint32{2},
				},
			},
		},
		"multiple svc replica ranks": {
			pbPools: []*mgmtpb.ListPoolsResp_Pool{
				{
					Uuid:    testUUIDs[0],
					Svcreps: []uint32{0, 1, 3, 5},
				},
			},
			expResult: []*PoolDiscovery{
				{
					UUID:        testUUIDs[0],
					SvcReplicas: []uint32{0, 1, 3, 5},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result := proto.PoolDiscoveriesFromPB(tc.pbPools)

			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}
