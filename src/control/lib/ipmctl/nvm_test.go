//
// (C) Copyright 2018-2020 Intel Corporation.
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

package ipmctl

import (
	"fmt"
	"os"
	"os/user"
	"path"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
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
func getDevices(t *testing.T, mgmt NvmMgmt) []DeviceDiscovery {
	t.Helper()

	devs, err := mgmt.Discover()
	if err != nil {
		t.Fatalf("Discovery failed: %s", err.Error())
	}

	if len(devs) == 0 {
		t.Skip("no NVM devices on system")
	}

	return devs
}

func TestNvmDiscovery(t *testing.T) {
	skipNoPerms(t)

	mgmt := NvmMgmt{}
	_, err := mgmt.Discover()
	if err != nil {
		t.Fatalf("Discovery failed: %s", err.Error())
	}
}

func TestNvmFwInfo(t *testing.T) {
	skipNoPerms(t)

	mgmt := NvmMgmt{}
	devs := getDevices(t, mgmt)

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
	devs := getDevices(t, mgmt)

	for _, d := range devs {
		err := mgmt.UpdateFirmware(d.Uid, filename, false)

		// Got down to NVM API
		common.CmpErr(t, errors.New("update_device_fw"), err)
		fmt.Printf("Update firmware for device %s: %v\n", d.Uid.String(), err)
	}
}
