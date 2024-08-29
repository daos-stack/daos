//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/uuid"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
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
			errors.New("required arguments"),
		},
		{
			"Set owner user",
			fmt.Sprintf("cont set-owner --user=%s %s %s", testUser, testPoolUUID, testContUUID),
			strings.Join([]string{
				printRequest(t, &control.ContSetOwnerReq{
					PoolID: testPoolUUID.String(),
					ContID: testContUUID.String(),
					User:   testUser,
					Group:  "",
				}),
			}, " "),
			nil,
		},
		{
			"Set owner group",
			fmt.Sprintf("cont set-owner --group=%s %s %s", testGroup, testPoolUUID, testContUUID),
			strings.Join([]string{
				printRequest(t, &control.ContSetOwnerReq{
					PoolID: testPoolUUID.String(),
					ContID: testContUUID.String(),
					User:   "",
					Group:  testGroup,
				}),
			}, " "),
			nil,
		},
		{
			"Set owner user and group",
			fmt.Sprintf("cont set-owner --user=%s --group=%s %s %s", testUser, testGroup,
				testPoolUUID, testContUUID),
			strings.Join([]string{
				printRequest(t, &control.ContSetOwnerReq{
					PoolID: testPoolUUID.String(),
					ContID: testContUUID.String(),
					User:   testUser,
					Group:  testGroup,
				}),
			}, " "),
			nil,
		},
		{
			"Bad owner principal",
			fmt.Sprintf("cont set-owner --user=%s --group=%s %s %s", "bad@@", testGroup,
				testPoolUUID, testContUUID),
			"", errors.New("invalid ACL principal"),
		},
		{
			"Bad group principal",
			fmt.Sprintf("cont set-owner --user=%s --group=%s %s %s", testUser, "bad@@",
				testPoolUUID, testContUUID),
			"", errors.New("invalid ACL principal"),
		},
	})
}
