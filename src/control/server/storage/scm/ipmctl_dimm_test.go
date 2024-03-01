//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"os"
	"os/exec"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestIpmctl_getDIMMInfo(t *testing.T) {
	type OutputMap map[string]string
	type ErrorMap map[string]error
	type CallMap map[string]int
	genCmdErrorMap := func() ErrorMap {
		return ErrorMap{}
	}
	sockOneOut := `
  <Dimm>
   <DimmID>0x1001</DimmID>
   <Capacity>502.599 GiB</Capacity>
   <HealthState>Healthy</HealthState>
   <FWVersion>01.00.00.5127</FWVersion>
   <PhysicalID>0x002a</PhysicalID>
   <DimmUID>8089-a2-1839-00001105</DimmUID>
   <SocketID>0x0001</SocketID>
   <MemControllerID>0x0000</MemControllerID>
   <ChannelID>0x0000</ChannelID>
   <ChannelPos>1</ChannelPos>
   <PartNumber>NMA1XXD512GQS</PartNumber>
  </Dimm>
  <Dimm>
   <DimmID>0x1101</DimmID>
   <Capacity>502.599 GiB</Capacity>
   <HealthState>Healthy</HealthState>
   <FWVersion>01.00.00.5127</FWVersion>
   <PhysicalID>0x0030</PhysicalID>
   <DimmUID>8089-a2-1839-00001112</DimmUID>
   <SocketID>0x0001</SocketID>
   <MemControllerID>0x0001</MemControllerID>
   <ChannelID>0x0000</ChannelID>
   <ChannelPos>1</ChannelPos>
   <PartNumber>NMA1XXD512GQS</PartNumber>
  </Dimm>`
	sockOneOutList := `<DimmList>` + sockOneOut + `</DimmList>`
	genCmdOutputMap := func() OutputMap {
		return OutputMap{
			cmdShowIpmctlVersion.String(): `
Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 03.00.00.0468`,
			cmdShowDIMMs.String(): `
<?xml version="1.0"?>
 <DimmList>
  <Dimm>
   <DimmID>0x0001</DimmID>
   <Capacity>502.599 GiB</Capacity>
   <HealthState>Healthy</HealthState>
   <FWVersion>01.00.00.5127</FWVersion>
   <PhysicalID>0x001e</PhysicalID>
   <DimmUID>8089-a2-1839-000010ce</DimmUID>
   <SocketID>0x0000</SocketID>
   <MemControllerID>0x0000</MemControllerID>
   <ChannelID>0x0000</ChannelID>
   <ChannelPos>1</ChannelPos>
   <PartNumber>NMA1XXD512GQS</PartNumber>
  </Dimm>
  <Dimm>
   <DimmID>0x0101</DimmID>
   <Capacity>502.599 GiB</Capacity>
   <HealthState>Healthy</HealthState>
   <FWVersion>01.00.00.5127</FWVersion>
   <PhysicalID>0x0024</PhysicalID>
   <DimmUID>8089-a2-1839-000010e7</DimmUID>
   <SocketID>0x0000</SocketID>
   <MemControllerID>0x0001</MemControllerID>
   <ChannelID>0x0000</ChannelID>
   <ChannelPos>1</ChannelPos>
   <PartNumber>NMA1XXD512GQS</PartNumber>
  </Dimm>
` + sockOneOut + `</DimmList>`,
		}
	}
	one := 1

	for name, tc := range map[string]struct {
		sockSelector *int
		cmdOutputMap OutputMap
		cmdErrorMap  ErrorMap
		expModules   storage.ScmModules
		expErr       error
		expCalls     CallMap
	}{
		"invalid xml": {
			cmdOutputMap: func() OutputMap {
				om := genCmdOutputMap()
				om[cmdShowDIMMs.String()] = `text that is invalid xml`
				return om
			}(),
			expErr: errors.New("parse show dimm cmd"),
		},
		"no permissions": {
			cmdErrorMap: func() ErrorMap {
				em := genCmdErrorMap()
				em[cmdShowDIMMs.String()] = errors.Wrap(&system.RunCmdError{
					Wrapped: &exec.ExitError{
						ProcessState: &os.ProcessState{},
					},
					Stdout: "Sorry, the " + outNoCLIPerms,
				}, cmdShowDIMMs.String())
				return em
			}(),
			expErr: errors.Errorf("ipmctl show -o nvmxml -d DimmID,ChannelID,ChannelPos,MemControllerID,SocketID,PhysicalID,Capacity,DimmUID,PartNumber,FWVersion,HealthState -dimm: exit status 0: stdout: Sorry, the %s; stderr: ", outNoCLIPerms),
		},
		"ipmctl version command fails": {
			cmdErrorMap: func() ErrorMap {
				em := genCmdErrorMap()
				em[cmdShowIpmctlVersion.String()] = errors.New("fail version")
				return em
			}(),
			expErr: errors.New("fail version"),
		},
		"show dimms command fails": {
			cmdErrorMap: func() ErrorMap {
				em := genCmdErrorMap()
				em[cmdShowDIMMs.String()] = errors.Wrap(&system.RunCmdError{
					Wrapped: &exec.ExitError{
						ProcessState: &os.ProcessState{},
					},
				}, cmdShowDIMMs.String())
				return em
			}(),
			expErr: errors.New("ipmctl show -o nvmxml -d DimmID,ChannelID,ChannelPos,MemControllerID,SocketID,PhysicalID,Capacity,DimmUID,PartNumber,FWVersion,HealthState -dimm: exit status 0: stdout: ; stderr: "),
		},
		"no modules": {
			cmdErrorMap: func() ErrorMap {
				em := genCmdErrorMap()
				em[cmdShowDIMMs.String()] = errors.Wrap(&system.RunCmdError{
					Wrapped: &exec.ExitError{
						ProcessState: &os.ProcessState{},
					},
					Stdout: "Initialization failed. " + outNoPMemDIMMs,
				}, cmdShowDIMMs.String())
				return em
			}(),
			expCalls: CallMap{
				cmdShowIpmctlVersion.String(): 1,
				cmdShowDIMMs.String():         1,
			},
			expModules: storage.ScmModules{},
		},
		"multiple modules per socket; dual socket": {
			expCalls: CallMap{
				cmdShowIpmctlVersion.String(): 1,
				cmdShowDIMMs.String():         1,
			},
			expModules: storage.ScmModules{
				mockModule("8089-a2-1839-000010ce", 0x1e, 0x0, 0x0, 0x0, 1),
				mockModule("8089-a2-1839-000010e7", 0x24, 0x0, 0x1, 0x0, 1),
				mockModule("8089-a2-1839-00001105", 0x2a, 0x1, 0x0, 0x0, 1),
				mockModule("8089-a2-1839-00001112", 0x30, 0x1, 0x1, 0x0, 1),
			},
		},
		"multiple modules per socket; sock one selected": {
			sockSelector: &one,
			cmdOutputMap: func() OutputMap {
				om := genCmdOutputMap()
				om[cmdShowDIMMs.String()+" -socket 1"] = sockOneOutList
				return om
			}(),
			expCalls: CallMap{
				cmdShowIpmctlVersion.String():        1,
				cmdShowDIMMs.String() + " -socket 1": 1,
			},
			expModules: storage.ScmModules{
				mockModule("8089-a2-1839-00001105", 0x2a, 0x1, 0x0, 0x0, 1),
				mockModule("8089-a2-1839-00001112", 0x30, 0x1, 0x1, 0x0, 1),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.cmdOutputMap == nil {
				tc.cmdOutputMap = genCmdOutputMap()
			}
			if tc.cmdErrorMap == nil {
				tc.cmdErrorMap = genCmdErrorMap()
			}
			sockSelector := -1
			if tc.sockSelector != nil {
				sockSelector = *tc.sockSelector
			}

			mockCalls := make(CallMap)
			mockRun := func(_ logging.Logger, cmd pmemCmd) (string, error) {
				cmdStr := cmd.String()
				out := tc.cmdOutputMap[cmdStr]
				err := tc.cmdErrorMap[cmdStr]
				mockCalls[cmdStr]++
				return out, err
			}

			cr, err := newCmdRunner(log, nil, mockRun, nil)
			if err != nil {
				t.Fatal(err)
			}

			gotModules, gotErr := cr.getModules(sockSelector)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expModules, gotModules); diff != "" {
				t.Errorf("unexpected dimm info (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expCalls, mockCalls); diff != "" {
				t.Errorf("unexpected cmd calls (-want, +got):\n%s\n", diff)
			}
		})
	}
}
