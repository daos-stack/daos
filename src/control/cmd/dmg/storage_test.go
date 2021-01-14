//
// (C) Copyright 2019-2021 Intel Corporation.
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
	"context"
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

func TestStorageCommands(t *testing.T) {
	storageFormatReq := &control.StorageFormatReq{Reformat: true}
	storageFormatReq.SetHostList([]string{})

	runCmdTests(t, []cmdTest{
		{
			"Format",
			"storage format",
			strings.Join([]string{
				printRequest(t, &control.StorageFormatReq{}),
			}, " "),
			nil,
		},
		{
			"Format with reformat",
			"storage format --reformat",
			strings.Join([]string{
				printRequest(t, &control.SystemQueryReq{}),
				printRequest(t, &control.StorageFormatReq{Reformat: true}),
			}, " "),
			nil,
		},
		{
			"Scan",
			"storage scan",
			strings.Join([]string{
				printRequest(t, &control.StorageScanReq{}),
			}, " "),
			nil,
		},
		{
			"Scan NVMe health short",
			"storage scan -n",
			printRequest(t, &control.StorageScanReq{NvmeHealth: true}),
			nil,
		},
		{
			"Scan NVMe health long",
			"storage scan --nvme-health",
			printRequest(t, &control.StorageScanReq{NvmeHealth: true}),
			nil,
		},
		{
			"Scan NVMe meta data short",
			"storage scan -m",
			printRequest(t, &control.StorageScanReq{NvmeMeta: true}),
			nil,
		},
		{
			"Scan NVMe meta data long",
			"storage scan --nvme-meta",
			printRequest(t, &control.StorageScanReq{NvmeMeta: true}),
			nil,
		},
		{
			"Prepare without force",
			"storage prepare",
			"",
			errors.New("consent not given"),
		},
		{
			"Prepare with nvme-only and scm-only",
			"storage prepare --force --nvme-only --scm-only",
			"",
			errors.New("nvme-only and scm-only options should not be set together"),
		},
		{
			"Prepare with scm-only",
			"storage prepare --force --scm-only",
			strings.Join([]string{
				printRequest(t, &control.StoragePrepareReq{
					SCM: &control.ScmPrepareReq{},
				}),
			}, " "),
			nil,
		},
		{
			"Prepare with nvme-only",
			"storage prepare --force --nvme-only",
			strings.Join([]string{
				printRequest(t, &control.StoragePrepareReq{
					NVMe: &control.NvmePrepareReq{},
				}),
			}, " "),
			nil,
		},
		{
			"Prepare with non-existent option",
			"storage prepare --force --nvme",
			"",
			errors.New("unknown flag `nvme'"),
		},
		{
			"Prepare with force and reset",
			"storage prepare --force --reset",
			strings.Join([]string{
				printRequest(t, &control.StoragePrepareReq{
					NVMe: &control.NvmePrepareReq{Reset: true},
					SCM:  &control.ScmPrepareReq{Reset: true},
				}),
			}, " "),
			nil,
		},
		{
			"Prepare with force",
			"storage prepare --force",
			strings.Join([]string{
				printRequest(t, &control.StoragePrepareReq{
					NVMe: &control.NvmePrepareReq{Reset: false},
					SCM:  &control.ScmPrepareReq{Reset: false},
				}),
			}, " "),
			nil,
		},
		{
			"Set FAULTY device status (force)",
			"storage set nvme-faulty --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d -f",
			printRequest(t, &control.SmdQueryReq{
				UUID:      "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				SetFaulty: true,
			}),
			nil,
		},
		{
			"Set FAULTY device status (without force)",
			"storage set nvme-faulty --uuid abcd",
			"StorageSetFaulty",
			errors.New("consent not given"),
		},
		{
			"Set FAULTY device status (with > 1 host)",
			"-l host-[1-2] storage set nvme-faulty -f --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			"StorageSetFaulty",
			errors.New("> 1 host"),
		},
		{
			"Set FAULTY device status without device specified",
			"storage set nvme-faulty",
			"StorageSetFaulty",
			errors.New("the required flag `-u, --uuid' was not specified"),
		},
		{
			"Nonexistent subcommand",
			"storage quack",
			"",
			errors.New("Unknown command"),
		},
		{
			"Reuse a FAULTY device",
			"storage replace nvme --old-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d --new-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				UUID:        "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				ReplaceUUID: "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				NoReint:     false,
			}),
			nil,
		},
		{
			"Replace an evicted device with a new device",
			"storage replace nvme --old-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d --new-uuid 2ccb8afb-5d32-454e-86e3-762ec5dca7be",
			printRequest(t, &control.SmdQueryReq{
				UUID:        "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				ReplaceUUID: "2ccb8afb-5d32-454e-86e3-762ec5dca7be",
				NoReint:     false,
			}),
			nil,
		},
		{
			"Try to replace a device without a new device UUID specified",
			"storage replace nvme --old-uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			"StorageReplaceNvme",
			errors.New("the required flag `--new-uuid' was not specified"),
		},
		{
			"Identify a device",
			"storage identify vmd --uuid 842c739b-86b5-462f-a7ba-b4a91b674f3d",
			printRequest(t, &control.SmdQueryReq{
				UUID:     "842c739b-86b5-462f-a7ba-b4a91b674f3d",
				Identify: true,
			}),
			nil,
		},
	})
}

func TestDmg_Storage_shouldReformatSystem(t *testing.T) {
	for name, tc := range map[string]struct {
		reformat, expSysReformat bool
		uErr, expErr             error
		members                  []*mgmtpb.SystemMember
	}{
		"no reformat": {},
		"failed member query": {
			reformat: true,
			uErr:     errors.New("system failed"),
			expErr:   errors.New("system failed"),
		},
		"empty membership": {
			reformat: true,
		},
		"rank not stopped": {
			reformat: true,
			members: []*mgmtpb.SystemMember{
				{Rank: 0, State: uint32(system.MemberStateStopped)},
				{Rank: 1, State: uint32(system.MemberStateJoined)},
			},
			expErr: errors.New("system reformat requires the following 1 rank to be stopped: 1"),
		},
		"ranks not stopped": {
			reformat: true,
			members: []*mgmtpb.SystemMember{
				{Rank: 0, State: uint32(system.MemberStateJoined)},
				{Rank: 1, State: uint32(system.MemberStateStopped)},
				{Rank: 5, State: uint32(system.MemberStateJoined)},
				{Rank: 2, State: uint32(system.MemberStateJoined)},
				{Rank: 4, State: uint32(system.MemberStateJoined)},
				{Rank: 3, State: uint32(system.MemberStateJoined)},
				{Rank: 6, State: uint32(system.MemberStateStopped)},
			},
			expErr: errors.New("system reformat requires the following 5 ranks to be stopped: 0,2-5"),
		},
		"system reformat": {
			reformat: true,
			members: []*mgmtpb.SystemMember{
				{Rank: 0, State: uint32(system.MemberStateStopped)},
				{Rank: 0, State: uint32(system.MemberStateStopped)},
			},
			expSysReformat: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryError: tc.uErr,
				UnaryResponse: control.MockMSResponse("host1", nil,
					&mgmtpb.SystemQueryResp{Members: tc.members}),
			})
			cmd := storageFormatCmd{}
			cmd.log = log
			cmd.Reformat = tc.reformat
			cmd.ctlInvoker = mi

			sysReformat, err := cmd.shouldReformatSystem(context.Background())
			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expSysReformat, sysReformat, name)
		})
	}
}
