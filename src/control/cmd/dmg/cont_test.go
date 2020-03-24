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

package main

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/uuid"

	"github.com/daos-stack/daos/src/control/client"
)

func TestContSetOwnerCommand(t *testing.T) {
	testPoolUUID := uuid.New()
	testContUUID := uuid.New()

	testUser := "testuser@"
	testGroup := "testgroup@"

	runCmdTests(t, []cmdTest{
		{
			"Set owner with no arguments",
			"cont set-owner",
			"",
			errMissingFlag,
		},
		{
			"Set owner user",
			fmt.Sprintf("cont set-owner --pool=%s --cont=%s --user=%s", testPoolUUID, testContUUID, testUser),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("ContSetOwner-%+v", client.ContSetOwnerReq{
					PoolUUID: testPoolUUID.String(),
					ContUUID: testContUUID.String(),
					User:     testUser,
					Group:    "",
				}),
			}, " "),
			nil,
		},
		{
			"Set owner group",
			fmt.Sprintf("cont set-owner --pool=%s --cont=%s --group=%s", testPoolUUID, testContUUID, testGroup),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("ContSetOwner-%+v", client.ContSetOwnerReq{
					PoolUUID: testPoolUUID.String(),
					ContUUID: testContUUID.String(),
					User:     "",
					Group:    testGroup,
				}),
			}, " "),
			nil,
		},
		{
			"Set owner user and group",
			fmt.Sprintf("cont set-owner --pool=%s --cont=%s --user=%s --group=%s",
				testPoolUUID, testContUUID, testUser, testGroup),
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("ContSetOwner-%+v", client.ContSetOwnerReq{
					PoolUUID: testPoolUUID.String(),
					ContUUID: testContUUID.String(),
					User:     testUser,
					Group:    testGroup,
				}),
			}, " "),
			nil,
		},
	})
}
