//
// (C) Copyright 2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	chkpb "github.com/daos-stack/daos/src/control/common/proto/chk"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/pkg/errors"
)

func TestCheckGetPolicyCommand(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Get policy with no arguments",
			"check get-policy",
			printRequest(t, &control.SystemCheckGetPolicyReq{CheckGetPolicyReq: mgmtpb.CheckGetPolicyReq{Sys: "daos_server-unset"}}),
			nil,
		},
		{
			"Get policy for one class",
			"check get-policy POOL_BAD_LABEL",
			printRequest(t, &control.SystemCheckGetPolicyReq{
				CheckGetPolicyReq: mgmtpb.CheckGetPolicyReq{
					Sys:     "daos_server-unset",
					Classes: []chkpb.CheckInconsistClass{chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL},
				},
			}),
			nil,
		},
		{
			"Get policy for multiple classes",
			"check get-policy POOL_BAD_LABEL,CONT_BAD_LABEL",
			printRequest(t, &control.SystemCheckGetPolicyReq{
				CheckGetPolicyReq: mgmtpb.CheckGetPolicyReq{
					Sys: "daos_server-unset",
					Classes: []chkpb.CheckInconsistClass{
						chkpb.CheckInconsistClass_CIC_CONT_BAD_LABEL,
						chkpb.CheckInconsistClass_CIC_POOL_BAD_LABEL,
					},
				},
			}),
			nil,
		},
		{
			"Get policy for invalid class",
			"check get-policy garbage",
			"",
			errors.New("gettable property"),
		},
		{
			"Get policy latest used",
			"check get-policy --last",
			printRequest(t, &control.SystemCheckGetPolicyReq{
				CheckGetPolicyReq: mgmtpb.CheckGetPolicyReq{
					Sys:      "daos_server-unset",
					LastUsed: true,
				},
			}),
			nil,
		},
	})
}
