//
// (C) Copyright 2020 Intel Corporation.
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
// +build firmware

package pretty

import (
	"errors"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func makeHostSet(t *testing.T, hosts string) *hostlist.HostSet {
	t.Helper()

	set, err := hostlist.CreateSet(hosts)
	if err != nil {
		t.Fatal(err)
	}
	return set
}

func TestPretty_PrintSCMFirmwareQueryMap(t *testing.T) {
	for name, tc := range map[string]struct {
		fwMap       control.HostSCMQueryMap
		hostErrors  control.HostErrorsMap
		expPrintStr string
	}{
		"no devices": {
			fwMap: control.HostSCMQueryMap{
				"host1": []*control.SCMQueryResult{},
				"host2": []*control.SCMQueryResult{},
				"host3": []*control.SCMQueryResult{},
			},
			expPrintStr: `
---------
host[1-3]
---------
  No SCM devices detected
`,
		},
		"single host": {
			fwMap: control.HostSCMQueryMap{
				"host1": []*control.SCMQueryResult{
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        12345,
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 5,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "V100",
							StagedVersion:     "V101",
							ImageMaxSizeBytes: 12345,
							UpdateStatus:      storage.ScmUpdateStatusStaged,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device2",
							PhysicalID:      2,
							Capacity:        67890,
							SocketID:        6,
							ControllerID:    7,
							ChannelID:       8,
							ChannelPosition: 9,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "V100",
							StagedVersion:     "V101",
							ImageMaxSizeBytes: 12345,
							UpdateStatus:      storage.ScmUpdateStatusStaged,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        67890,
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Error: errors.New("test error"),
					},
					{
						Module: storage.ScmModule{
							UID:             "Device4",
							PhysicalID:      4,
							Capacity:        67890,
							SocketID:        2,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Error: errors.New("test error"),
					},
				},
			},
			expPrintStr: `
Errors:
  Host  Device:PhyID:Socket:Ctrl:Chan:Pos Error      
  ----  --------------------------------- -----      
  host1 Device3:3:1:2:2:1                 test error 
  host1 Device4:4:2:2:2:1                 test error 
-----
host1
-----
  Firmware status for 2 devices:
    Active Version: V100
    Staged Version: V101
    Maximum Firmware Image Size: 12 KiB
    Last Update Status: Staged
`,
		},
		"multiple hosts": {
			fwMap: control.HostSCMQueryMap{
				"host1": []*control.SCMQueryResult{
					{
						Module: storage.ScmModule{
							UID:             "Device0",
							PhysicalID:      0,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    1,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "B300",
							StagedVersion:     "",
							ImageMaxSizeBytes: (1 << 20),
							UpdateStatus:      storage.ScmUpdateStatusFailed,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        (1 << 31),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 5,
						},
						Error: errors.New("shared error"),
					},
				},
				"host2": []*control.SCMQueryResult{
					{
						Module: storage.ScmModule{
							UID:             "Device2",
							PhysicalID:      2,
							Capacity:        (1 << 30),
							SocketID:        6,
							ControllerID:    7,
							ChannelID:       8,
							ChannelPosition: 9,
						},
						Error: errors.New("shared error"),
					},
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "B300",
							StagedVersion:     "",
							ImageMaxSizeBytes: (1 << 20),
							UpdateStatus:      storage.ScmUpdateStatusFailed,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device4",
							PhysicalID:      4,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 1,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "B300",
							StagedVersion:     "",
							ImageMaxSizeBytes: (1 << 20),
							UpdateStatus:      storage.ScmUpdateStatusFailed,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Host  Device:PhyID:Socket:Ctrl:Chan:Pos Error        
  ----  --------------------------------- -----        
  host1 Device1:1:1:2:3:5                 shared error 
  host2 Device2:2:6:7:8:9                 shared error 
---------
host[1-2]
---------
  Firmware status for 3 devices:
    Active Version: B300
    Staged Version: N/A
    Maximum Firmware Image Size: 1.0 MiB
    Last Update Status: Failed
`,
		},
		"no errors": {
			fwMap: control.HostSCMQueryMap{
				"host1": []*control.SCMQueryResult{
					{
						Module: storage.ScmModule{
							UID:             "Device0",
							PhysicalID:      0,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    1,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "B300",
							StagedVersion:     "",
							ImageMaxSizeBytes: (1 << 20),
							UpdateStatus:      storage.ScmUpdateStatusFailed,
						},
					},
				},
				"host2": []*control.SCMQueryResult{
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "B300",
							StagedVersion:     "",
							ImageMaxSizeBytes: (1 << 20),
							UpdateStatus:      storage.ScmUpdateStatusFailed,
						},
					},
				},
			},
			expPrintStr: `
---------
host[1-2]
---------
  Firmware status for 2 devices:
    Active Version: B300
    Staged Version: N/A
    Maximum Firmware Image Size: 1.0 MiB
    Last Update Status: Failed
`,
		},
		"only errors": {
			fwMap: control.HostSCMQueryMap{
				"host1": []*control.SCMQueryResult{
					{
						Module: storage.ScmModule{
							UID:             "Device0",
							PhysicalID:      0,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    1,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Error: errors.New("oopsie daisy"),
					},
				},
				"host2": []*control.SCMQueryResult{
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Error: errors.New("something bad happened"),
					},
				},
			},
			expPrintStr: `
Errors:
  Host  Device:PhyID:Socket:Ctrl:Chan:Pos Error                  
  ----  --------------------------------- -----                  
  host1 Device0:0:1:1:2:1                 oopsie daisy           
  host2 Device1:1:1:2:2:1                 something bad happened 
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintSCMFirmwareQueryMap(tc.fwMap, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintSCMFirmwareQueryMapVerbose(t *testing.T) {
	for name, tc := range map[string]struct {
		fwMap       control.HostSCMQueryMap
		hostErrors  control.HostErrorsMap
		expPrintStr string
	}{
		"no devices": {
			fwMap: control.HostSCMQueryMap{
				"host1": []*control.SCMQueryResult{},
			},
			expPrintStr: `
-----
host1
-----
  No SCM devices detected
`,
		},
		"single host": {
			fwMap: control.HostSCMQueryMap{
				"host1": []*control.SCMQueryResult{
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        12345,
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 5,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "V100",
							StagedVersion:     "V101",
							ImageMaxSizeBytes: 12345,
							UpdateStatus:      storage.ScmUpdateStatusStaged,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device2",
							PhysicalID:      2,
							Capacity:        67890,
							SocketID:        6,
							ControllerID:    7,
							ChannelID:       8,
							ChannelPosition: 9,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "A113",
							StagedVersion:     "",
							ImageMaxSizeBytes: 54321,
							UpdateStatus:      storage.ScmUpdateStatusSuccess,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        67890,
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Error: errors.New("test error"),
					},
					{
						Module: storage.ScmModule{
							UID:             "Device4",
							PhysicalID:      4,
							Capacity:        67890,
							SocketID:        2,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
					},
				},
			},
			expPrintStr: `
-----
host1
-----
  UID:Device1 PhysicalID:1 Capacity:12 KiB Location:(socket:1 memctrlr:2 chan:3 pos:5)
    Active Version: V100
    Staged Version: V101
    Maximum Firmware Image Size: 12 KiB
    Last Update Status: Staged
  UID:Device2 PhysicalID:2 Capacity:66 KiB Location:(socket:6 memctrlr:7 chan:8 pos:9)
    Active Version: A113
    Staged Version: N/A
    Maximum Firmware Image Size: 53 KiB
    Last Update Status: Success
  UID:Device3 PhysicalID:3 Capacity:66 KiB Location:(socket:1 memctrlr:2 chan:2 pos:1)
    Error: test error
  UID:Device4 PhysicalID:4 Capacity:66 KiB Location:(socket:2 memctrlr:2 chan:2 pos:1)
    Error: No information available
`,
		},
		"multiple hosts": {
			fwMap: control.HostSCMQueryMap{
				"host1": []*control.SCMQueryResult{
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        (1 << 31),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 5,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "B300",
							StagedVersion:     "",
							ImageMaxSizeBytes: (1 << 20),
							UpdateStatus:      storage.ScmUpdateStatusFailed,
						},
					},
				},
				"host2": []*control.SCMQueryResult{
					{
						Module: storage.ScmModule{
							UID:             "Device2",
							PhysicalID:      2,
							Capacity:        (1 << 30),
							SocketID:        6,
							ControllerID:    7,
							ChannelID:       8,
							ChannelPosition: 9,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "",
							StagedVersion:     "",
							ImageMaxSizeBytes: 0,
							UpdateStatus:      storage.ScmUpdateStatusUnknown,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Info: &storage.ScmFirmwareInfo{
							ActiveVersion:     "A113",
							StagedVersion:     "",
							ImageMaxSizeBytes: (1 << 21),
							UpdateStatus:      storage.ScmFirmwareUpdateStatus(0xFFFFFFFF),
						},
					},
				},
			},
			expPrintStr: `
-----
host1
-----
  UID:Device1 PhysicalID:1 Capacity:2.0 GiB Location:(socket:1 memctrlr:2 chan:3 pos:5)
    Active Version: B300
    Staged Version: N/A
    Maximum Firmware Image Size: 1.0 MiB
    Last Update Status: Failed
-----
host2
-----
  UID:Device2 PhysicalID:2 Capacity:1.0 GiB Location:(socket:6 memctrlr:7 chan:8 pos:9)
    Active Version: N/A
    Staged Version: N/A
    Maximum Firmware Image Size: 0 B
    Last Update Status: Unknown
  UID:Device3 PhysicalID:3 Capacity:4.0 GiB Location:(socket:1 memctrlr:2 chan:2 pos:1)
    Active Version: A113
    Staged Version: N/A
    Maximum Firmware Image Size: 2.0 MiB
    Last Update Status: Unknown
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintSCMFirmwareQueryMapVerbose(tc.fwMap, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintSCMFirmwareUpdateMapVerbose(t *testing.T) {
	for name, tc := range map[string]struct {
		fwMap       control.HostSCMUpdateMap
		hostErrors  control.HostErrorsMap
		expPrintStr string
	}{
		"no devices": {
			fwMap: control.HostSCMUpdateMap{
				"host1": []*control.SCMUpdateResult{},
				"host2": []*control.SCMUpdateResult{},
			},
			expPrintStr: `
-----
host1
-----
  No SCM devices detected
-----
host2
-----
  No SCM devices detected
`,
		},
		"single host": {
			fwMap: control.HostSCMUpdateMap{
				"host1": []*control.SCMUpdateResult{
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        12345,
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 5,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device2",
							PhysicalID:      2,
							Capacity:        67890,
							SocketID:        6,
							ControllerID:    7,
							ChannelID:       8,
							ChannelPosition: 9,
						},
						Error: errors.New("test error"),
					},
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        67890,
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
					},
				},
			},
			expPrintStr: `
-----
host1
-----
  UID:Device1 PhysicalID:1 Capacity:12 KiB Location:(socket:1 memctrlr:2 chan:3 pos:5)
    Success - The new firmware was staged. A reboot is required to apply.
  UID:Device2 PhysicalID:2 Capacity:66 KiB Location:(socket:6 memctrlr:7 chan:8 pos:9)
    Error: test error
  UID:Device3 PhysicalID:3 Capacity:66 KiB Location:(socket:1 memctrlr:2 chan:2 pos:1)
    Success - The new firmware was staged. A reboot is required to apply.
`,
		},
		"multiple hosts": {
			fwMap: control.HostSCMUpdateMap{
				"host1": []*control.SCMUpdateResult{
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        (1 << 31),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 5,
						},
					},
				},
				"host2": []*control.SCMUpdateResult{
					{
						Module: storage.ScmModule{
							UID:             "Device2",
							PhysicalID:      2,
							Capacity:        (1 << 30),
							SocketID:        6,
							ControllerID:    7,
							ChannelID:       8,
							ChannelPosition: 9,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Error: errors.New("something went wrong"),
					},
				},
			},
			expPrintStr: `
-----
host1
-----
  UID:Device1 PhysicalID:1 Capacity:2.0 GiB Location:(socket:1 memctrlr:2 chan:3 pos:5)
    Success - The new firmware was staged. A reboot is required to apply.
-----
host2
-----
  UID:Device2 PhysicalID:2 Capacity:1.0 GiB Location:(socket:6 memctrlr:7 chan:8 pos:9)
    Success - The new firmware was staged. A reboot is required to apply.
  UID:Device3 PhysicalID:3 Capacity:4.0 GiB Location:(socket:1 memctrlr:2 chan:2 pos:1)
    Error: something went wrong
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintSCMFirmwareUpdateMapVerbose(tc.fwMap, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintSCMFirmwareUpdateMap(t *testing.T) {
	for name, tc := range map[string]struct {
		fwMap       control.HostSCMUpdateMap
		hostErrors  control.HostErrorsMap
		expPrintStr string
	}{
		"no devices": {
			fwMap: control.HostSCMUpdateMap{
				"host1": []*control.SCMUpdateResult{},
				"host2": []*control.SCMUpdateResult{},
			},
			expPrintStr: `
---------
host[1-2]
---------
  No SCM devices detected
`,
		},
		"single host": {
			fwMap: control.HostSCMUpdateMap{
				"host1": []*control.SCMUpdateResult{
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        12345,
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 5,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device2",
							PhysicalID:      2,
							Capacity:        67890,
							SocketID:        6,
							ControllerID:    7,
							ChannelID:       8,
							ChannelPosition: 9,
						},
						Error: errors.New("test error"),
					},
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        67890,
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
					},
				},
			},
			expPrintStr: `
Errors:
  Host  Device:PhyID:Socket:Ctrl:Chan:Pos Error      
  ----  --------------------------------- -----      
  host1 Device2:2:6:7:8:9                 test error 
-----
host1
-----
  Firmware staged on 2 devices. A reboot is required to apply the update.
`,
		},
		"multiple hosts": {
			fwMap: control.HostSCMUpdateMap{
				"host1": []*control.SCMUpdateResult{
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        (1 << 31),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 5,
						},
					},
				},
				"host2": []*control.SCMUpdateResult{
					{
						Module: storage.ScmModule{
							UID:             "Device2",
							PhysicalID:      2,
							Capacity:        (1 << 30),
							SocketID:        6,
							ControllerID:    7,
							ChannelID:       8,
							ChannelPosition: 9,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Error: errors.New("something went wrong"),
					},
				},
			},
			expPrintStr: `
Errors:
  Host  Device:PhyID:Socket:Ctrl:Chan:Pos Error                
  ----  --------------------------------- -----                
  host2 Device3:3:1:2:2:1                 something went wrong 
---------
host[1-2]
---------
  Firmware staged on 2 devices. A reboot is required to apply the update.
`,
		},
		"no errors": {
			fwMap: control.HostSCMUpdateMap{
				"host1": []*control.SCMUpdateResult{
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        (1 << 31),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 5,
						},
					},
				},
				"host2": []*control.SCMUpdateResult{
					{
						Module: storage.ScmModule{
							UID:             "Device2",
							PhysicalID:      2,
							Capacity:        (1 << 30),
							SocketID:        6,
							ControllerID:    7,
							ChannelID:       8,
							ChannelPosition: 9,
						},
					},
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
					},
				},
			},
			expPrintStr: `
---------
host[1-2]
---------
  Firmware staged on 3 devices. A reboot is required to apply the update.
`,
		},
		"only errors": {
			fwMap: control.HostSCMUpdateMap{
				"host1": []*control.SCMUpdateResult{
					{
						Module: storage.ScmModule{
							UID:             "Device1",
							PhysicalID:      1,
							Capacity:        (1 << 31),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       3,
							ChannelPosition: 5,
						},
						Error: errors.New("oh no"),
					},
				},
				"host2": []*control.SCMUpdateResult{
					{
						Module: storage.ScmModule{
							UID:             "Device2",
							PhysicalID:      2,
							Capacity:        (1 << 30),
							SocketID:        6,
							ControllerID:    7,
							ChannelID:       8,
							ChannelPosition: 9,
						},
						Error: errors.New("oh no"),
					},
					{
						Module: storage.ScmModule{
							UID:             "Device3",
							PhysicalID:      3,
							Capacity:        (1 << 32),
							SocketID:        1,
							ControllerID:    2,
							ChannelID:       2,
							ChannelPosition: 1,
						},
						Error: errors.New("something went wrong"),
					},
				},
			},
			expPrintStr: `
Errors:
  Host  Device:PhyID:Socket:Ctrl:Chan:Pos Error                
  ----  --------------------------------- -----                
  host1 Device1:1:1:2:3:5                 oh no                
  host2 Device2:2:6:7:8:9                 oh no                
  host2 Device3:3:1:2:2:1                 something went wrong 
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintSCMFirmwareUpdateMap(tc.fwMap, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}
