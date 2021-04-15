//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestStorageCommands(t *testing.T) {
	storageFormatReq := &control.StorageFormatReq{Reformat: true}
	storageFormatReq.SetHostList([]string{})
	systemQueryReq := &control.SystemQueryReq{FailOnUnavailable: true}

	runCmdTests(t, []cmdTest{
		{
			"Format",
			"storage format",
			strings.Join([]string{
				printRequest(t, systemQueryReq),
				printRequest(t, &control.StorageFormatReq{}),
			}, " "),
			nil,
		},
		{
			"Format with reformat",
			"storage format --reformat",
			strings.Join([]string{
				printRequest(t, systemQueryReq),
				printRequest(t, &control.StorageFormatReq{Reformat: true}),
			}, " "),
			nil,
		},
		{
			"Scan summary",
			"storage scan",
			strings.Join([]string{
				printRequest(t, &control.StorageScanReq{NvmeBasic: true}),
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
			"Scan NVMe health with verbose",
			"storage scan --nvme-health --verbose",
			"",
			errors.New("cannot use --verbose"),
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
