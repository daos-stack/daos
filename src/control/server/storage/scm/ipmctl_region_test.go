//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"encoding/xml"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
)

// TestIpmctl_checkIpmctl verified that bad versions trigger an error.
func TestIpmctl_checkIpmctl(t *testing.T) {
	preTxt := "Intel(R) Optane(TM) Persistent Memory Command Line Interface Version "

	for name, tc := range map[string]struct {
		verOut  string
		badVers []semVer
		expErr  error
	}{
		"no bad versions": {
			verOut:  "02.00.00.3816",
			badVers: []semVer{},
		},
		"good version": {
			verOut:  "02.00.00.3825",
			badVers: badIpmctlVers,
		},
		"bad version": {
			verOut:  "02.00.00.3816",
			badVers: badIpmctlVers,
			expErr:  FaultIpmctlBadVersion("02.00.00.3816"),
		},
		"no version": {
			expErr: errors.New("could not read ipmctl version"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockRun := func(_ string) (string, error) {
				return preTxt + tc.verOut, nil
			}

			cr, err := newCmdRunner(log, nil, mockRun, nil)
			if err != nil {
				t.Fatal(err)
			}
			test.CmpErr(t, tc.expErr, cr.checkIpmctl(tc.badVers))
		})
	}
}

const testXMLRegions = `<?xml version="1.0"?>
 <RegionList>
  <Region>
   <SocketID>0x0000</SocketID>
   <PersistentMemoryType>AppDirect</PersistentMemoryType>
   <Capacity>1008.000 GiB</Capacity>
   <FreeCapacity>0.000 GiB</FreeCapacity>
   <HealthState>Healthy</HealthState>
   <DimmID>0x0001, 0x0011, 0x0101, 0x0111, 0x0201, 0x0211, 0x0301, 0x0311</DimmID>
   <RegionID>0x0001</RegionID>
   <ISetID>0xb8c12120c7bd1110</ISetID>
  </Region>
  <Region>
   <SocketID>0x0001</SocketID>
   <PersistentMemoryType>AppDirect</PersistentMemoryType>
   <Capacity>1008.000 GiB</Capacity>
   <FreeCapacity>504.000 GiB</FreeCapacity>
   <HealthState>Error</HealthState>
   <DimmID>0x1001, 0x1011, 0x1101, 0x1111, 0x1201, 0x1211, 0x1301, 0x1311</DimmID>
   <RegionID>0x0002</RegionID>
   <ISetID>0x4d752120a3731110</ISetID>
  </Region>
 </RegionList>
`

func mockXMLRegions(t *testing.T, variant string) string {
	t.Helper()

	var rl RegionList
	if err := xml.Unmarshal([]byte(testXMLRegions), &rl); err != nil {
		t.Fatal(err)
	}

	switch variant {
	case "same-sock":
		rl.Regions[1].SocketID = rl.Regions[0].SocketID
	default:
	}

	out, err := xml.Marshal(&rl)
	if err != nil {
		t.Fatal(err)
	}

	return string(out)
}

//	stop padtestXMLSameSock := `<?xml version="1.0"?>
// <RegionList>
//  <Region>
//   <SocketID>0x0000</SocketID>
//   <PersistentMemoryType>AppDirect</PersistentMemoryType>
//   <Capacity>1008.000 GiB</Capacity>
//   <FreeCapacity>0.000 GiB</FreeCapacity>
//   <HealthState>Healthy</HealthState>
//   <DimmID>0x0001, 0x0011, 0x0101, 0x0111, 0x0201, 0x0211, 0x0301, 0x0311</DimmID>
//   <RegionID>0x0001</RegionID>
//   <ISetID>0xb8c12120c7bd1110</ISetID>
//  </Region>
//  <Region>
//   <SocketID>0x0000</SocketID>
//   <PersistentMemoryType>AppDirect</PersistentMemoryType>
//   <Capacity>1008.000 GiB</Capacity>
//   <FreeCapacity>504.000 GiB</FreeCapacity>
//   <HealthState>Error</HealthState>
//   <DimmID>0x1001, 0x1011, 0x1101, 0x1111, 0x1201, 0x1211, 0x1301, 0x1311</DimmID>
//   <RegionID>0x0002</RegionID>
//   <ISetID>0x4d752120a3731110</ISetID>
//  </Region>
// </RegionList>
//`

func TestIpmctl_getRegionDetails(t *testing.T) {
	expRegionMap := socketRegionMap{
		0: {
			XMLName: xml.Name{
				Local: "Region",
			},
			ID:                   1,
			SocketID:             0,
			PersistentMemoryType: regionType(ipmctl.RegionTypeAppDirect),
			Capacity:             humanize.GiByte * 1008,
			FreeCapacity:         0,
			Health:               regionHealth(ipmctl.RegionHealthNormal),
		},
		1: {
			XMLName: xml.Name{
				Local: "Region",
			},
			ID:                   2,
			SocketID:             1,
			PersistentMemoryType: regionType(ipmctl.RegionTypeAppDirect),
			Capacity:             humanize.GiByte * 1008,
			FreeCapacity:         humanize.GiByte * 504,
			Health:               regionHealth(ipmctl.RegionHealthError),
		},
	}

	for name, tc := range map[string]struct {
		cmdOut    string
		cmdErr    error
		expErr    error
		expResult socketRegionMap
	}{
		"no permissions": {
			cmdOut: outNoCLIPerms,
			expErr: errors.New("requires root"),
		},
		"no modules": {
			cmdOut: outNoPMemModules,
			expErr: errNoPMemModules,
		},
		"no regions": {
			cmdOut: outNoPMemRegions,
			expErr: errNoPMemRegions,
		},
		"two regions; one per socket": {
			cmdOut:    testXMLRegions,
			expResult: expRegionMap,
		},
		"two regions; same socket": {
			cmdOut: mockXMLRegions(t, "same-sock"),
			expErr: errors.New("unexpected second region"),
		},
		"two regions; socket 0 selected": {
			cmdOut: mockXMLRegions(t, "sock-zero"),
			expResult: socketRegionMap{
				0: expRegionMap[0],
			},
		},
		"two regions; socket 1 selected": {
			cmdOut: mockXMLRegions(t, "sock-one"),
			expResult: socketRegionMap{
				1: expRegionMap[1],
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockRun := func(_ string) (string, error) {
				return tc.cmdOut, tc.cmdErr
			}

			cr, err := newCmdRunner(log, nil, mockRun, nil)
			if err != nil {
				t.Fatal(err)
			}

			gotRegionMap, gotErr := cr.getRegionDetails(sockAny)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(expRegionMap, gotRegionMap); diff != "" {
				t.Errorf("unexpected result of xml parsing (-want, +got):\n%s\n", diff)
			}
		})
	}
}
