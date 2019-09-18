//
// (C) Copyright 2019 Intel Corporation.
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

package storage

import (
	"fmt"
	"strings"
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	. "github.com/daos-stack/daos/src/control/server/storage/messages"
)

// TestGetState tests the internals of prepScm, pass in mock runCmd to verify
// behaviour. Don't use mockPrepScm as we want to test prepScm logic.
func TestGetState(t *testing.T) {
	var regionsOut string  // variable cmd output
	commands := []string{} // external commands issued
	// ndctl create-namespace command return json format
	pmemOut := `{
   "dev":"namespace%d.0",
   "mode":"fsdax",
   "map":"dev",
   "size":"2964.94 GiB (3183.58 GB)",
   "uuid":"842fc847-28e0-4bb6-8dfc-d24afdba1528",
   "raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264",
   "sector_size":512,
   "blockdev":"pmem%d",
   "numa_node":%d
}
`
	onePmem, _ := parsePmemDevs(fmt.Sprintf(pmemOut, 1, 1, 0))
	twoPmemsJson := "[" + fmt.Sprintf(pmemOut, 1, 1, 0) + "," + fmt.Sprintf(pmemOut, 2, 2, 1) + "]"
	twoPmems, _ := parsePmemDevs(twoPmemsJson)
	createRegionsOut := MsgScmRebootRequired + "\n"
	pmemId := 1

	mockRun := func(in string) (string, error) {
		retString := in

		switch in {
		case cmdScmCreateRegions:
			retString = createRegionsOut // example successful output
		case cmdScmShowRegions:
			retString = regionsOut
		case cmdScmCreateNamespace:
			// stimulate free capacity of region being used
			regionsOut = strings.Replace(regionsOut, "3012.0", "0.0", 1)
			retString = fmt.Sprintf(pmemOut, pmemId, pmemId, pmemId-1)
			pmemId += 1
		case cmdScmListNamespaces:
			retString = twoPmemsJson
		}

		commands = append(commands, in)
		return retString, nil
	}

	tests := []struct {
		desc              string
		errMsg            string
		showRegionOut     string
		expRebootRequired bool
		expPmemDevs       []PmemDev
		expCommands       []string
	}{
		{
			desc:              "modules but no regions",
			showRegionOut:     outScmNoRegions,
			expRebootRequired: true,
			expCommands:       []string{cmdScmShowRegions, cmdScmDeleteGoal, cmdScmCreateRegions},
		},
		{
			desc: "single region with free capacity",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.0 GiB\n" +
				"---ISetID=0x81187f4881f02ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=3012.0 GiB\n" +
				"\n",
			expCommands: []string{cmdScmShowRegions, cmdScmCreateNamespace, cmdScmShowRegions},
			expPmemDevs: onePmem,
		},
		{
			desc: "regions with free capacity",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=3012.0 GiB\n" +
				"---ISetID=0x81187f4881f02ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=3012.0 GiB\n" +
				"\n",
			expCommands: []string{
				cmdScmShowRegions, cmdScmCreateNamespace, cmdScmShowRegions,
				cmdScmCreateNamespace, cmdScmShowRegions,
			},
			expPmemDevs: twoPmems,
		},
		{
			desc: "regions with no capacity",
			showRegionOut: "\n" +
				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.0 GiB\n" +
				"---ISetID=0x81187f4881f02ccb---\n" +
				"   PersistentMemoryType=AppDirect\n" +
				"   FreeCapacity=0.0 GiB\n" +
				"\n",
			expCommands: []string{cmdScmShowRegions, cmdScmListNamespaces},
			expPmemDevs: twoPmems,
		},
	}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)()

			// not using mockPrepScm because we want to exercise
			// prepScm
			ss := NewMockScmProvider(log, nil, nil, []DeviceDiscovery{MockModule()},
				false, newPrepScm(log, mockRun))

			if err := ss.Discover(); err != nil {
				t.Fatal(err)
			}

			// reset to initial values between tests
			regionsOut = tt.showRegionOut
			pmemId = 1
			commands = nil

			scmState, err := ss.prep.GetState()
			if err != nil {
				t.Fatal(tt.desc + " GetState: " + err.Error())
			}

			needsReboot, pmemDevs, err := ss.Prep(scmState)
			if tt.errMsg != "" {
				ExpectError(t, err, tt.errMsg, tt.desc)
				return
			}
			if err != nil {
				t.Fatal(tt.desc + ": " + err.Error())
			}

			AssertEqual(t, commands, tt.expCommands, tt.desc+": unexpected list of commands run")
			AssertEqual(t, needsReboot, tt.expRebootRequired, tt.desc+": unexpected value for is reboot required")
			AssertEqual(t, pmemDevs, tt.expPmemDevs, tt.desc+": unexpected list of pmem device file names")
		})
	}
}
