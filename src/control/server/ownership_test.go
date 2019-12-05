//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// c6xuplcrnless required by applicable law or agreed to in writing, software
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

package server

import (
	"os/user"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
)

func TestChangeFilePermissions(t *testing.T) {
	tests := []struct {
		desc        string
		username    string
		groupname   string
		lUsrRet     *user.User  // lookup user
		lGrpRet     *user.Group // lookup group
		lUsrErr     error       // lookup user error
		lGrpErr     error       // lookup group error
		listGrpsRet []string    // list of user's groups
		listGrpsErr error       // list groups error
		expHistory  []string
		errMsg      string
	}{
		{
			desc:     "chown success",
			username: "bob",
			lUsrRet: &user.User{
				Uid: "1001", Gid: "1001", Username: "bob",
			},
			expHistory: []string{
				"os: walk /tmp/daos_sockets chown 1001 1001",
				"os: walk /tmp/daos_control.log chown 1001 1001",
				"os: walk /mnt/daos chown 1001 1001",
				"os: walk /tmp/server.log chown 1001 1001",
			},
		},
		{
			desc:      "drop success uid and gid",
			username:  "bob",
			groupname: "builders",
			lUsrRet: &user.User{
				Uid: "1001", Gid: "1001", Username: "bob",
			},
			lGrpRet: &user.Group{
				Gid: "1002", Name: "builders",
			},
			listGrpsRet: []string{"1001", "1002"},
			expHistory: []string{
				"os: walk /tmp/daos_sockets chown 1001 1002",
				"os: walk /tmp/daos_control.log chown 1001 1002",
				"os: walk /mnt/daos chown 1001 1002",
				"os: walk /tmp/server.log chown 1001 1002",
			},
		},
		{
			desc:      "drop success uid not member of gid",
			username:  "bob",
			groupname: "builders",
			lUsrRet: &user.User{
				Uid: "1001", Gid: "1001", Username: "bob",
			},
			lGrpRet: &user.Group{
				Gid: "1002", Name: "builders",
			},
			listGrpsRet: []string{"1001"},
			errMsg:      "group lookup: user bob not member of group builders",
		},
	}

	for _, tt := range tests {
		ext := mockExt{
			lUsrRet: tt.lUsrRet, lUsrErr: tt.lUsrErr,
			lGrpRet: tt.lGrpRet, lGrpErr: tt.lGrpErr,
			listGrpsRet: tt.listGrpsRet, listGrpsErr: tt.listGrpsErr,
		}

		config := mockConfigFromFile(t, &ext, socketsExample)
		config.UserName = tt.username
		config.GroupName = tt.groupname

		err := changeFileOwnership(config)
		if err != nil {
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, tt.desc)
				continue
			}
			t.Fatalf("%s: %v", tt.desc, err.Error())
		}
		AssertEqual(t, ext.getHistory(), tt.expHistory, tt.desc)
	}
}
