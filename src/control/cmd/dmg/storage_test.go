package main

import (
	"fmt"
	"strings"
	"testing"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

func TestStorageCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			// FIXME: This arguably should result in an error,
			// but we can't see the io.EOF error because it's
			// swallowed in getConsent()
			"Format without force",
			"storage format",
			"ConnectClients",
			nil,
			cmdSuccess,
		},
		{
			"Format with force",
			"storage format --force",
			"ConnectClients FormatStorage",
			nil,
			cmdSuccess,
		},
		{
			"Update with missing arguments",
			"storage fwupdate",
			"",
			nil,
			errMissingFlag,
		},
		{
			// Likewise here, this should probably result in a failure
			"Update without force",
			"storage fwupdate --nvme-model foo --nvme-fw-path bar --nvme-fw-rev 123",
			"ConnectClients",
			nil,
			cmdSuccess,
		},
		{
			"Update with force",
			"storage fwupdate --force --nvme-model foo --nvme-fw-path bar --nvme-fw-rev 123",
			strings.Join([]string{
				"ConnectClients",
				fmt.Sprintf("UpdateStorage-%s", &pb.UpdateStorageReq{
					Nvme: &pb.UpdateNvmeReq{
						Model:    "foo",
						Startrev: "123",
						Path:     "bar",
					},
				}),
			}, " "),
			nil,
			cmdSuccess,
		},
		{
			"Scan",
			"storage scan",
			"ConnectClients ScanStorage",
			nil,
			cmdSuccess,
		},
		{
			"Nonexistent subcommand",
			"storage quack",
			"",
			nil,
			fmt.Errorf("Unknown command"),
		},
	})
}
