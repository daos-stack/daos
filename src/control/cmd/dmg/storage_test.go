//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
	nvmeRebindReq := &control.NvmeRebindReq{PCIAddr: "0000:80:00.0"}
	nvmeRebindReq.SetHostList([]string{"foo2.com"})

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
			"",
			errors.New("unknown flag"),
		},
		{
			"Format with force",
			"storage format --force",
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
			"Scan verbose",
			"storage scan --verbose",
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
			"Scan NVMe health with verbose",
			"storage scan --nvme-health --verbose",
			"",
			errors.New("cannot use --verbose"),
		},
		{
			"Rebind NVMe; no PCI address",
			"storage nvme-rebind",
			"",
			errors.New("required flag"),
		},
		{
			"Rebind NVMe; 0 hosts in hostlist",
			"storage nvme-rebind --pci-address 0000:80:00.0",
			"",
			errors.New("expects a single host"),
		},
		{
			"Rebind NVMe; 2 hosts in hostlist",
			"storage nvme-rebind -l foo[1,2].com --pci-address 0000:80:00.0",
			"",
			errors.New("expects a single host"),
		},
		{
			"Rebind NVMe",
			"storage nvme-rebind -l foo2.com --pci-address 0000:80:00.0",
			printRequest(t, nvmeRebindReq),
			nil,
		},
		{
			"Nonexistent subcommand",
			"storage quack",
			"",
			errors.New("Unknown command"),
		},
	})
}
