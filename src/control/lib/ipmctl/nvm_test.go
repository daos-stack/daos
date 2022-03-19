//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ipmctl

import (
	"fmt"
	"os"
	"os/user"
	"path"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

// NVM API calls will fail if not run as root. We should just skip the tests.
func skipNoPerms(t *testing.T) {
	t.Helper()
	u, err := user.Current()
	if err != nil {
		t.Fatalf("can't determine current user: %v", err)
	}
	if u.Uid != "0" {
		// Alert the user even if they're not running the tests in verbose mode
		fmt.Printf("%s must be run as root\n", t.Name())
		t.Skip("test doesn't have NVM API permissions")
	}
}

// Fetch all devices in the system - and skip the test if there are none
func discoverDevices(t *testing.T, log logging.Logger, mgmt NvmMgmt) []DeviceDiscovery {
	t.Helper()

	devs, err := mgmt.GetModules(log)
	if err != nil {
		t.Fatalf("Discovery failed: %s", err.Error())
	}

	if len(devs) == 0 {
		t.Skip("no NVM devices on system")
	}

	return devs
}

func TestNvmDiscovery(t *testing.T) {
	log, buf := logging.NewTestLogger("discovery")
	defer common.ShowBufferOnFailure(t, buf)

	skipNoPerms(t)

	mgmt := NvmMgmt{}
	_, err := mgmt.GetModules(log)
	if err != nil {
		t.Fatalf("Discovery failed: %s", err.Error())
	}
}

func TestNvmFwInfo(t *testing.T) {
	log, buf := logging.NewTestLogger("firmware")
	defer common.ShowBufferOnFailure(t, buf)

	skipNoPerms(t)

	mgmt := NvmMgmt{}
	devs := discoverDevices(t, log, mgmt)

	for _, d := range devs {
		fwInfo, err := mgmt.GetFirmwareInfo(d.Uid)
		if err != nil {
			t.Errorf("Failed to get FW info for device %s: %v", d.Uid.String(), err)
			continue
		}

		fmt.Printf("Device %s: %+v\n", d.Uid.String(), fwInfo)
	}
}

func TestNvmFwUpdate_BadFile(t *testing.T) {
	for _, tt := range []struct {
		desc      string
		inputPath string
		expErr    error
	}{
		{
			desc:   "empty path",
			expErr: errors.New("firmware path is required"),
		},
		{
			desc:      "non-existent path",
			inputPath: "/not/a/real/path.bin",
			expErr:    errors.New("unable to access firmware file"),
		},
	} {
		t.Run(tt.desc, func(t *testing.T) {
			var devUID DeviceUID // don't care - this test shouldn't reach the API

			mgmt := NvmMgmt{}
			err := mgmt.UpdateFirmware(devUID, tt.inputPath, false)

			common.CmpErr(t, tt.expErr, err)
		})
	}
}

func TestNvmFwUpdate(t *testing.T) {
	log, buf := logging.NewTestLogger("firmware")
	defer common.ShowBufferOnFailure(t, buf)

	skipNoPerms(t)

	dir, cleanup := common.CreateTestDir(t)
	defer cleanup()

	// Actual DIMM will reject this junk file.
	// We just need it to get down to the API.
	filename := path.Join(dir, "fake.bin")
	f, err := os.Create(filename)
	if err != nil {
		t.Fatal("Failed to create a fake FW file")
	}
	if _, err := f.WriteString("notrealFW"); err != nil {
		t.Fatal(err)
	}
	f.Close()

	mgmt := NvmMgmt{}
	devs := discoverDevices(t, log, mgmt)

	for _, d := range devs {
		err := mgmt.UpdateFirmware(d.Uid, filename, false)

		// Got down to NVM API
		common.CmpErr(t, errors.New("update_device_fw"), err)
		fmt.Printf("Update firmware for device %s: %v\n", d.Uid.String(), err)
	}
}

// The actual test functions are in nvm_ctest.go file so that they can use cgo (import "C").
// These wrappers are here for gotest to find.

func TestGetModules(t *testing.T) {
	testGetModules(t)
}

func TestGetRegions(t *testing.T) {
	testGetRegions(t)
}
