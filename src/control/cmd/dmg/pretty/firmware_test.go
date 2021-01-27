//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
// +build firmware

package pretty

import (
	"errors"
	"fmt"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestPretty_PrintSCMFirmwareQueryMap(t *testing.T) {
	for name, tc := range map[string]struct {
		fwMap       control.HostSCMQueryMap
		hostErrors  control.HostErrorsMap
		expPrintStr string
	}{
		"nil map": {
			fwMap:       nil,
			expPrintStr: "",
		},
		"no devices": {
			fwMap: control.HostSCMQueryMap{
				"host1": []*control.SCMQueryResult{},
				"host2": []*control.SCMQueryResult{},
				"host3": []*control.SCMQueryResult{},
			},
			expPrintStr: `
===================
SCM Device Firmware
===================
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
===================
SCM Device Firmware
===================
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
===================
SCM Device Firmware
===================
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
===================
SCM Device Firmware
===================
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
===================
SCM Device Firmware
===================
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
		"nil map": {
			fwMap:       nil,
			expPrintStr: "",
		},
		"no devices": {
			fwMap: control.HostSCMQueryMap{
				"host1": []*control.SCMQueryResult{},
			},
			expPrintStr: `
===================
SCM Device Firmware
===================
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
===================
SCM Device Firmware
===================
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
===================
SCM Device Firmware
===================
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
    Success - The new firmware was staged. A power cycle is required to apply.
  UID:Device2 PhysicalID:2 Capacity:66 KiB Location:(socket:6 memctrlr:7 chan:8 pos:9)
    Error: test error
  UID:Device3 PhysicalID:3 Capacity:66 KiB Location:(socket:1 memctrlr:2 chan:2 pos:1)
    Success - The new firmware was staged. A power cycle is required to apply.
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
    Success - The new firmware was staged. A power cycle is required to apply.
-----
host2
-----
  UID:Device2 PhysicalID:2 Capacity:1.0 GiB Location:(socket:6 memctrlr:7 chan:8 pos:9)
    Success - The new firmware was staged. A power cycle is required to apply.
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
						Error: errors.New("test error"),
					},
				},
			},
			expPrintStr: `
Errors:
  Host  Device:PhyID:Socket:Ctrl:Chan:Pos Error      
  ----  --------------------------------- -----      
  host1 Device2:2:6:7:8:9                 test error 
  host1 Device3:3:1:2:2:1                 test error 
-----
host1
-----
  Firmware staged on 1 device. A power cycle is required to apply the update.
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
  Firmware staged on 2 devices. A power cycle is required to apply the update.
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
  Firmware staged on 3 devices. A power cycle is required to apply the update.
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

func getNvmeControllerWithFWRev(idx int32, fwRev string) *storage.NvmeController {
	return &storage.NvmeController{
		Model:    fmt.Sprintf("model%d", idx),
		Serial:   fmt.Sprintf("serial%d", idx),
		PciAddr:  fmt.Sprintf("0000:80:00.%d", idx),
		FwRev:    fwRev,
		SocketID: idx,
	}
}

func TestPretty_PrintNVMeFirmwareQueryMap(t *testing.T) {
	for name, tc := range map[string]struct {
		fwMap       control.HostNVMeQueryMap
		hostErrors  control.HostErrorsMap
		expPrintStr string
	}{
		"nil map": {
			fwMap:       nil,
			expPrintStr: "",
		},
		"no devices": {
			fwMap: control.HostNVMeQueryMap{
				"host1": []*control.NVMeQueryResult{},
				"host2": []*control.NVMeQueryResult{},
				"host3": []*control.NVMeQueryResult{},
			},
			expPrintStr: `
====================
NVMe Device Firmware
====================
  ---------
  host[1-3]
  ---------
    No NVMe device controllers detected
`,
		},
		"single host": {
			fwMap: control.HostNVMeQueryMap{
				"host1": []*control.NVMeQueryResult{
					{
						Device: *getNvmeControllerWithFWRev(1, "FW100"),
					},
					{
						Device: *getNvmeControllerWithFWRev(2, "FW100"),
					},
					{
						Device: *getNvmeControllerWithFWRev(3, "FW200"),
					},
					{
						Device: *getNvmeControllerWithFWRev(4, "FW100"),
					},
				},
			},
			expPrintStr: `
====================
NVMe Device Firmware
====================
  -----
  host1
  -----
    Firmware status for 3 devices:
      Revision: FW100
  -----
  host1
  -----
    Firmware status for 1 device:
      Revision: FW200
`,
		},
		"multiple hosts": {
			fwMap: control.HostNVMeQueryMap{
				"host1": []*control.NVMeQueryResult{
					{
						Device: *getNvmeControllerWithFWRev(1, "FW100"),
					},
					{
						Device: *getNvmeControllerWithFWRev(2, "FW100"),
					},
				},
				"host2": []*control.NVMeQueryResult{
					{
						Device: *getNvmeControllerWithFWRev(3, "FW100"),
					},
					{
						Device: *getNvmeControllerWithFWRev(2, "FW200"),
					},
					{
						Device: *getNvmeControllerWithFWRev(1, "FW100"),
					},
				},
			},
			expPrintStr: `
====================
NVMe Device Firmware
====================
  ---------
  host[1-2]
  ---------
    Firmware status for 4 devices:
      Revision: FW100
  -----
  host2
  -----
    Firmware status for 1 device:
      Revision: FW200
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintNVMeFirmwareQueryMap(tc.fwMap, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintNVMeFirmwareQueryMapVerbose(t *testing.T) {
	for name, tc := range map[string]struct {
		fwMap       control.HostNVMeQueryMap
		hostErrors  control.HostErrorsMap
		expPrintStr string
	}{
		"nil map": {
			fwMap:       nil,
			expPrintStr: "",
		},
		"no devices": {
			fwMap: control.HostNVMeQueryMap{
				"host1": []*control.NVMeQueryResult{},
				"host2": []*control.NVMeQueryResult{},
			},
			expPrintStr: `
====================
NVMe Device Firmware
====================
  -----
  host1
  -----
    No NVMe device controllers detected
  -----
  host2
  -----
    No NVMe device controllers detected
`,
		},
		"single host": {
			fwMap: control.HostNVMeQueryMap{
				"host1": []*control.NVMeQueryResult{
					{
						Device: *getNvmeControllerWithFWRev(1, "FW100"),
					},
					{
						Device: *getNvmeControllerWithFWRev(2, "FW200"),
					},
					{
						Device: *getNvmeControllerWithFWRev(3, "FW100"),
					},
				},
			},
			expPrintStr: `
====================
NVMe Device Firmware
====================
  -----
  host1
  -----
    Device PCI Address: 0000:80:00.1
      Revision: FW100
    Device PCI Address: 0000:80:00.2
      Revision: FW200
    Device PCI Address: 0000:80:00.3
      Revision: FW100
`,
		},
		"multiple hosts": {
			fwMap: control.HostNVMeQueryMap{
				"host1": []*control.NVMeQueryResult{
					{
						Device: *getNvmeControllerWithFWRev(1, "FW100"),
					},
				},
				"host2": []*control.NVMeQueryResult{
					{
						Device: *getNvmeControllerWithFWRev(1, "FW200"),
					},
					{
						Device: *getNvmeControllerWithFWRev(2, "FW200"),
					},
				},
			},
			expPrintStr: `
====================
NVMe Device Firmware
====================
  -----
  host1
  -----
    Device PCI Address: 0000:80:00.1
      Revision: FW100
  -----
  host2
  -----
    Device PCI Address: 0000:80:00.1
      Revision: FW200
    Device PCI Address: 0000:80:00.2
      Revision: FW200
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintNVMeFirmwareQueryMapVerbose(tc.fwMap, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintNVMeFirmwareUpdateMap(t *testing.T) {
	for name, tc := range map[string]struct {
		fwMap       control.HostNVMeUpdateMap
		hostErrors  control.HostErrorsMap
		expPrintStr string
	}{
		"no devices": {
			fwMap: control.HostNVMeUpdateMap{
				"host1": []*control.NVMeUpdateResult{},
				"host2": []*control.NVMeUpdateResult{},
			},
			expPrintStr: `
---------
host[1-2]
---------
  No NVMe device controllers detected
`,
		},
		"single host": {
			fwMap: control.HostNVMeUpdateMap{
				"host1": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
					},
					{
						DevicePCIAddr: "pciaddr1",
						Error:         errors.New("test error"),
					},
					{
						DevicePCIAddr: "pciaddr2",
					},
				},
			},
			expPrintStr: `
Errors:
  Host  Device Addr Error      
  ----  ----------- -----      
  host1 pciaddr1    test error 
-----
host1
-----
  Firmware updated on 2 NVMe device controllers.
`,
		},
		"multiple hosts": {
			fwMap: control.HostNVMeUpdateMap{
				"host1": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
					},
				},
				"host2": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
					},
					{
						DevicePCIAddr: "pciaddr1",
						Error:         errors.New("something went wrong"),
					},
				},
			},
			expPrintStr: `
Errors:
  Host  Device Addr Error                
  ----  ----------- -----                
  host2 pciaddr1    something went wrong 
---------
host[1-2]
---------
  Firmware updated on 2 NVMe device controllers.
`,
		},
		"no errors": {
			fwMap: control.HostNVMeUpdateMap{
				"host1": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
					},
				},
				"host2": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
					},
					{
						DevicePCIAddr: "pciaddr1",
					},
				},
			},
			expPrintStr: `
---------
host[1-2]
---------
  Firmware updated on 3 NVMe device controllers.
`,
		},
		"only errors": {
			fwMap: control.HostNVMeUpdateMap{
				"host1": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
						Error:         errors.New("oh no"),
					},
				},
				"host2": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
						Error:         errors.New("oh no"),
					},
					{

						DevicePCIAddr: "pciaddr1",
						Error:         errors.New("something went wrong"),
					},
				},
			},
			expPrintStr: `
Errors:
  Host  Device Addr Error                
  ----  ----------- -----                
  host1 pciaddr0    oh no                
  host2 pciaddr0    oh no                
  host2 pciaddr1    something went wrong 
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintNVMeFirmwareUpdateMap(tc.fwMap, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestPretty_PrintNVMeFirmwareUpdateMapVerbose(t *testing.T) {
	for name, tc := range map[string]struct {
		fwMap       control.HostNVMeUpdateMap
		hostErrors  control.HostErrorsMap
		expPrintStr string
	}{
		"no devices": {
			fwMap: control.HostNVMeUpdateMap{
				"host1": []*control.NVMeUpdateResult{},
				"host2": []*control.NVMeUpdateResult{},
			},
			expPrintStr: `
-----
host1
-----
  No NVMe device controllers detected
-----
host2
-----
  No NVMe device controllers detected
`,
		},
		"single host": {
			fwMap: control.HostNVMeUpdateMap{
				"host1": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
					},
					{
						DevicePCIAddr: "pciaddr1",
						Error:         errors.New("test error"),
					},
					{
						DevicePCIAddr: "pciaddr2",
					},
				},
			},
			expPrintStr: `
-----
host1
-----
  Device PCI Address: pciaddr0
    Success - The NVMe device controller firmware was updated.
  Device PCI Address: pciaddr1
    Error: test error
  Device PCI Address: pciaddr2
    Success - The NVMe device controller firmware was updated.
`,
		},
		"multiple hosts": {
			fwMap: control.HostNVMeUpdateMap{
				"host1": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
					},
				},
				"host2": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
					},
					{
						DevicePCIAddr: "pciaddr1",
						Error:         errors.New("something went wrong"),
					},
				},
			},
			expPrintStr: `
-----
host1
-----
  Device PCI Address: pciaddr0
    Success - The NVMe device controller firmware was updated.
-----
host2
-----
  Device PCI Address: pciaddr0
    Success - The NVMe device controller firmware was updated.
  Device PCI Address: pciaddr1
    Error: something went wrong
`,
		},
		"no errors": {
			fwMap: control.HostNVMeUpdateMap{
				"host1": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
					},
				},
				"host2": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
					},
					{
						DevicePCIAddr: "pciaddr1",
					},
				},
			},
			expPrintStr: `
-----
host1
-----
  Device PCI Address: pciaddr0
    Success - The NVMe device controller firmware was updated.
-----
host2
-----
  Device PCI Address: pciaddr0
    Success - The NVMe device controller firmware was updated.
  Device PCI Address: pciaddr1
    Success - The NVMe device controller firmware was updated.
`,
		},
		"only errors": {
			fwMap: control.HostNVMeUpdateMap{
				"host1": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
						Error:         errors.New("oh no"),
					},
				},
				"host2": []*control.NVMeUpdateResult{
					{
						DevicePCIAddr: "pciaddr0",
						Error:         errors.New("oh no"),
					},
					{

						DevicePCIAddr: "pciaddr1",
						Error:         errors.New("something went wrong"),
					},
				},
			},
			expPrintStr: `
-----
host1
-----
  Device PCI Address: pciaddr0
    Error: oh no
-----
host2
-----
  Device PCI Address: pciaddr0
    Error: oh no
  Device PCI Address: pciaddr1
    Error: something went wrong
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			var bld strings.Builder
			if err := PrintNVMeFirmwareUpdateMapVerbose(tc.fwMap, &bld); err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(strings.TrimLeft(tc.expPrintStr, "\n"), bld.String()); diff != "" {
				t.Fatalf("unexpected format string (-want, +got):\n%s\n", diff)
			}
		})
	}
}
