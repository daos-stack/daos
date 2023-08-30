//
// (C) Copyright 2020-2021 Intel Corporation.
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
			errMissingFlag,
		},
		{
			"Set owner user",
			fmt.Sprintf("cont set-owner --pool=%s --cont=%s --user=%s", testPoolUUID, testContUUID, testUser),
			strings.Join([]string{
				printRequest(t, &control.ContSetOwnerReq{
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
				printRequest(t, &control.ContSetOwnerReq{
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
				printRequest(t, &control.ContSetOwnerReq{
					PoolUUID: testPoolUUID.String(),
					ContUUID: testContUUID.String(),
					User:     testUser,
					Group:    testGroup,
				}),
			}, " "),
			nil,
		},
		{
			"Bad owner principal",
			fmt.Sprintf("cont set-owner --pool=%s --cont=%s --user=%s --group=%s",
				testPoolUUID, testContUUID, "bad@@", testGroup),
			"", errors.New("invalid ACL principal"),
		},
		{
			"Bad group principal",
			fmt.Sprintf("cont set-owner --pool=%s --cont=%s --user=%s --group=%s",
				testPoolUUID, testContUUID, testUser, "bad@@"),
			"", errors.New("invalid ACL principal"),
		},
	})
}
