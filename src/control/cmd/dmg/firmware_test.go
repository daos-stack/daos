//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// +build firmware

package main

import (
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
)

func TestFirmwareCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Query with no args defaults to all",
			"firmware query",
			strings.Join([]string{
				printRequest(t, &control.FirmwareQueryReq{
					SCM:  true,
					NVMe: true,
				}),
			}, " "),
			nil,
		},
		{
			"Query with verbose",
			"firmware query --verbose",
			strings.Join([]string{
				printRequest(t, &control.FirmwareQueryReq{
					SCM:  true,
					NVMe: true,
				}),
			}, " "),
			nil,
		},
		{
			"Query with SCM",
			"firmware query --type=scm",
			strings.Join([]string{
				printRequest(t, &control.FirmwareQueryReq{
					SCM: true,
				}),
			}, " "),
			nil,
		},
		{
			"Query with NVMe",
			"firmware query --type=nvme",
			strings.Join([]string{
				printRequest(t, &control.FirmwareQueryReq{
					NVMe: true,
				}),
			}, " "),
			nil,
		},
		{
			"Query with all",
			"firmware query --type=all",
			strings.Join([]string{
				printRequest(t, &control.FirmwareQueryReq{
					SCM:  true,
					NVMe: true,
				}),
			}, " "),
			nil,
		},
		{
			"Query with model ID",
			"firmware query --model=Model1",
			strings.Join([]string{
				printRequest(t, &control.FirmwareQueryReq{
					SCM:     true,
					NVMe:    true,
					ModelID: "Model1",
				}),
			}, " "),
			nil,
		},
		{
			"Query with FW rev",
			"firmware query --fwrev=FW100",
			strings.Join([]string{
				printRequest(t, &control.FirmwareQueryReq{
					SCM:         true,
					NVMe:        true,
					FirmwareRev: "FW100",
				}),
			}, " "),
			nil,
		},
		{
			"Query with device list",
			"firmware query --devices=D1,D2,D3",
			strings.Join([]string{
				printRequest(t, &control.FirmwareQueryReq{
					SCM:     true,
					NVMe:    true,
					Devices: []string{"D1", "D2", "D3"},
				}),
			}, " "),
			nil,
		},
		{
			"Query with invalid type",
			"firmware query --type=none",
			"",
			errors.New("Invalid value `none' for option `-t, --type'. Allowed values are: nvme, scm or all"),
		},
		{
			"Update with no path",
			"firmware update --type=scm",
			"",
			errors.New("the required flag `-p, --path' was not specified"),
		},
		{
			"Update with no type",
			"firmware update --path=/does_not/matter",
			"",
			errors.New("the required flag `-t, --type' was not specified"),
		},
		{
			"Update with invalid type",
			"firmware update --type=all --path=/does_not/matter",
			"",
			errors.New("Invalid value `all' for option `-t, --type'. Allowed values are: nvme or scm"),
		},
		{
			"Update with SCM",
			"firmware update --type=scm --path=/dont/care",
			strings.Join([]string{
				printRequest(t, &control.FirmwareUpdateReq{
					FirmwarePath: "/dont/care",
					Type:         control.DeviceTypeSCM,
				}),
			}, " "),
			nil,
		},
		{
			"Update with verbose option",
			"firmware update --type=scm --path=/dont/care --verbose",
			strings.Join([]string{
				printRequest(t, &control.FirmwareUpdateReq{
					FirmwarePath: "/dont/care",
					Type:         control.DeviceTypeSCM,
				}),
			}, " "),
			nil,
		},
		{
			"Update with NVMe",
			"firmware update --type=nvme --path=/dont/care",
			strings.Join([]string{
				printRequest(t, &control.FirmwareUpdateReq{
					FirmwarePath: "/dont/care",
					Type:         control.DeviceTypeNVMe,
				}),
			}, " "),
			nil,
		},
		{
			"Update with model ID",
			"firmware update --type=scm --path=/dont/care --model=Model1",
			strings.Join([]string{
				printRequest(t, &control.FirmwareUpdateReq{
					FirmwarePath: "/dont/care",
					Type:         control.DeviceTypeSCM,
					ModelID:      "Model1",
				}),
			}, " "),
			nil,
		},
		{
			"Update with FW rev",
			"firmware update --type=scm --path=/dont/care --fwrev=FW100",
			strings.Join([]string{
				printRequest(t, &control.FirmwareUpdateReq{
					FirmwarePath: "/dont/care",
					Type:         control.DeviceTypeSCM,
					FirmwareRev:  "FW100",
				}),
			}, " "),
			nil,
		},
		{
			"Update with device list",
			"firmware update --type=scm --path=/dont/care --devices=D1,D2,D3",
			strings.Join([]string{
				printRequest(t, &control.FirmwareUpdateReq{
					FirmwarePath: "/dont/care",
					Type:         control.DeviceTypeSCM,
					Devices:      []string{"D1", "D2", "D3"},
				}),
			}, " "),
			nil,
		},
	})
}
