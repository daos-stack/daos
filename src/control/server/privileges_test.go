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

func TestDropPrivileges(t *testing.T) {
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
		sUIDErr     error       // set uid error
		sGIDErr     error       // set gid error
		expHistory  []string
		errMsg      string
	}{
		{
			desc:     "drop success uid",
			username: "bob",
			lUsrRet: &user.User{
				Uid: "1001", Gid: "1001", Username: "bob",
			},
			expHistory: []string{
				"os: chown /mnt/daos 1001 1001",
				"C: setgid 1001", "C: setuid 1001",
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
				"os: chown /mnt/daos 1001 1002",
				"C: setgid 1002", "C: setuid 1001",
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
			expHistory: []string{
				"os: chown /mnt/daos 1001 1001",
				"C: setgid 1001", "C: setuid 1001",
			},
		},
	}

	for _, tt := range tests {
		ext := mockExt{
			lUsrRet: tt.lUsrRet, lUsrErr: tt.lUsrErr,
			lGrpRet: tt.lGrpRet, lGrpErr: tt.lGrpErr,
			listGrpsRet: tt.listGrpsRet, listGrpsErr: tt.listGrpsErr,
			sUIDErr: tt.sUIDErr, sGIDErr: tt.sGIDErr,
		}

		config := mockConfigFromFile(t, &ext, socketsExample)
		config.UserName = tt.username
		config.GroupName = tt.groupname

		// TODO: verify chown gets called
		err := dropPrivileges(&config)
		if err != nil {
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, tt.desc)
				continue
			}
			t.Fatal(err)
		}
		AssertEqual(t, ext.getHistory(), tt.expHistory, tt.desc)
	}
}
