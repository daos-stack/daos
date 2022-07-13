//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestIpmctl_getModules(t *testing.T) {
	testDevices := []ipmctl.DeviceDiscovery{
		mockDiscovery(0),
		mockDiscovery(0),
		mockDiscovery(1),
	}

	expModules := storage.ScmModules{}
	for _, dev := range testDevices {
		mod := mockModule(&dev)
		expModules = append(expModules, &mod)
	}

	for name, tc := range map[string]struct {
		cfg       *mockIpmctlCfg
		sockID    int
		expErr    error
		expResult storage.ScmModules
	}{
		"ipmctl GetModules failed": {
			cfg: &mockIpmctlCfg{
				getModulesErr: errors.New("mock GetModules"),
			},
			sockID: sockAny,
			expErr: errors.New("failed to discover pmem modules: mock GetModules"),
		},
		"no modules": {
			cfg:       &mockIpmctlCfg{},
			sockID:    sockAny,
			expResult: storage.ScmModules{},
		},
		"get modules with no socket filter": {
			cfg: &mockIpmctlCfg{
				modules: testDevices,
			},
			sockID:    sockAny,
			expResult: expModules,
		},
		"filter modules by socket 0": {
			cfg: &mockIpmctlCfg{
				modules: testDevices,
			},
			sockID:    0,
			expResult: expModules[:2],
		},
		"filter modules by socket 1": {
			cfg: &mockIpmctlCfg{
				modules: testDevices,
			},
			sockID:    1,
			expResult: expModules[2:],
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mockBinding := newMockIpmctl(tc.cfg)
			cr, err := newCmdRunner(log, mockBinding, nil, nil)
			if err != nil {
				t.Fatal(err)
			}

			result, err := cr.getModules(tc.sockID)

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result); diff != "" {
				t.Errorf("wrong firmware info (-want, +got):\n%s\n", diff)
			}
		})
	}
}

// TestIpmctl_getPMemState verifies the appropriate PMem state is returned for either a specific
// socket region or all regions when either a specific socket is requested or a state is specific to
// a particular socket.
func TestIpmctl_getPMemState(t *testing.T) {
	verOut := `Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825`

	for name, tc := range map[string]struct {
		runOut   []string
		runErr   []error
		expErr   error
		expState storage.ScmState
		expSock0 bool
		expSock1 bool
	}{
		"get regions fails": {
			runOut: []string{
				verOut, `text that is invalid xml`,
			},
			expErr: errors.New("parse show region cmd"),
		},
		"zero modules": {
			runOut: []string{
				verOut, outNoPMemModules,
			},
			expState: storage.ScmNoModules,
		},
		"modules but no regions": {
			runOut: []string{
				verOut, outNoPMemRegions,
			},
			expState: storage.ScmNoRegions,
		},
		"single region with uncorrectable error": {
			runOut: []string{
				verOut, mockXMLRegions(t, "unhealthy"),
			},
			expSock0: true,
			expState: storage.ScmNotHealthy,
		},
		"single region with free capacity": {
			runOut: []string{
				verOut, mockXMLRegions(t, "full-free"),
			},
			expState: storage.ScmFreeCap,
		},
		"single region with no free capacity": {
			runOut: []string{
				verOut, mockXMLRegions(t, "no-free"),
			},
			expState: storage.ScmNoFreeCap,
		},
		"second region has uncorrectable error": {
			runOut: []string{
				verOut, mockXMLRegions(t, "unhealthy-2nd-sock"),
			},
			expSock1: true,
			expState: storage.ScmNotHealthy,
		},
		"second region has free capacity": {
			runOut: []string{
				verOut, mockXMLRegions(t, "full-free-2nd-sock"),
			},
			expState: storage.ScmFreeCap,
		},
		"two regions with no free capacity": {
			runOut: []string{
				verOut, mockXMLRegions(t, "dual-sock"),
			},
			expState: storage.ScmNoFreeCap,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			callIdx := 0

			mockRun := func(in string) (string, error) {
				out := ""
				if len(tc.runOut) > callIdx {
					out = tc.runOut[callIdx]
				}

				var err error = nil
				if len(tc.runErr) > callIdx {
					err = tc.runErr[callIdx]
				}

				callIdx++

				return out, err
			}

			cr, err := newCmdRunner(log, nil, mockRun, nil)
			if err != nil {
				t.Fatal(err)
			}

			resp, err := cr.getPMemState(sockAny)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			expResp := &storage.ScmSocketState{
				State: tc.expState,
			}

			if tc.expSock0 {
				s := uint(0)
				expResp.SocketID = &s
			} else if tc.expSock1 {
				s := uint(1)
				expResp.SocketID = &s
			}

			t.Logf("socket state: %+v", expResp)

			if diff := cmp.Diff(expResp, resp); diff != "" {
				t.Fatalf("unexpected scm state (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestIpmctl_prep(t *testing.T) {
	verStr := "Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825"
	//	genNsJSON := func(numa, nsNum, size, id string) string {
	//		nsName := fmt.Sprintf("%s.%s", numa, nsNum)
	//		bdName := numa
	//		if nsNum != "0" {
	//			bdName = nsName
	//		}
	//		sizeBytes, err := humanize.ParseBytes(size)
	//		if err != nil {
	//			t.Fatal(err)
	//		}
	//
	//		return fmt.Sprintf(`{"dev":"namespace%s","mode":"fsdax","map":"dev",`+
	//			`"size":%d,"uuid":"842fc847-28e0-4bb6-8dfc-d24afdba15%s",`+
	//			`"raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264","sector_size":512,`+
	//			`"blockdev":"pmem%s","numa_node":%s}`, nsName, sizeBytes, id, bdName, numa)
	//	}

	//	ndctlNsStr := fmt.Sprintf("[%s]", genNsJSON("1", "0", "3012GiB", "28"))
	//	ndctl2NsStr := fmt.Sprintf("[%s,%s]", genNsJSON("1", "0", "3012GiB", "28"),
	//		genNsJSON("0", "0", "3012GiB", "29"))
	// ndctl1of4NsStr := fmt.Sprintf("[%s]", genNsJSON("0", "0", "1506GiB", "28"))
	// ndctl2of4NsStr := fmt.Sprintf("[%s,%s]", genNsJSON("0", "0", "1506GiB", "28"),
	// 	genNsJSON("0", "1", "1506GiB", "29"))
	// ndctl3of4NsStr := fmt.Sprintf("[%s,%s,%s]", genNsJSON("0", "0", "1506GiB", "28"),
	// 	genNsJSON("0", "1", "1506GiB", "29"), genNsJSON("1", "0", "1506GiB", "30"))
	// ndctl4of4NsStr := fmt.Sprintf("[%s,%s,%s,%s]", genNsJSON("0", "0", "1506GiB", "28"),
	// 	genNsJSON("0", "1", "1506GiB", "29"), genNsJSON("1", "0", "1506GiB", "30"),
	// 	genNsJSON("1", "1", "1506GiB", "31"))

	sock0 := uint(0)
	sock1 := uint(1)

	for name, tc := range map[string]struct {
		prepReq     *storage.ScmPrepareRequest
		scanResp    *storage.ScmScanResponse
		runOut      []string
		runErr      []error
		regions     []ipmctl.PMemRegion
		regionsErr  error
		expErr      error
		expPrepResp *storage.ScmPrepareResponse
		expCalls    []string
	}{
		"nil scan response": {
			expErr: errors.New("nil scan response"),
		},
		"state not provided": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmStateUnknown,
				},
			},
			expErr: errors.New("no valid scm state"),
		},
		"state no modules": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
			},
			expPrepResp: &storage.ScmPrepareResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
			},
		},
		"state non-interleaved; no socket id": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNotInterleaved,
				},
			},
			expErr: errors.New("expecting socket id"),
		},
		"state non-interleaved": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					SocketID: &sock0,
					State:    storage.ScmNotInterleaved,
				},
			},
			expErr: storage.FaultScmNotInterleaved(sock0),
		},
		"state unhealthy; no socket id": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNotHealthy,
				},
			},
			expErr: errors.New("expecting socket id"),
		},
		"state unhealthy": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					SocketID: &sock1,
					State:    storage.ScmNotHealthy,
				},
			},
			expErr: storage.FaultScmNotHealthy(sock1),
		},
		"state partial capacity; no socket id": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmPartFreeCap,
				},
			},
			expErr: errors.New("expecting socket id"),
		},
		"state partial capacity": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					SocketID: &sock1,
					State:    storage.ScmPartFreeCap,
				},
			},
			expErr: storage.FaultScmPartialCapacity(sock1),
		},
		"state unknown memory mode; no socket id": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmUnknownMode,
				},
			},
			expErr: errors.New("expecting socket id"),
		},
		"state unknown memory mode": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					SocketID: &sock1,
					State:    storage.ScmUnknownMode,
				},
			},
			expErr: storage.FaultScmUnknownMemoryMode(sock1),
		},
		"state no regions": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
			},
			expPrepResp: &storage.ScmPrepareResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
				RebootRequired: true,
			},
			expCalls: []string{
				"ipmctl version", "ipmctl delete -goal",
				"ipmctl create -f -goal PersistentMemoryType=AppDirect",
			},
		},
		"state no regions; delete goals fails": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
			},
			runOut: []string{
				verStr, "",
			},
			runErr: []error{
				nil, errors.New("fail"),
			},
			expErr: errors.New("fail"),
			expCalls: []string{
				"ipmctl version", "ipmctl delete -goal",
			},
		},
		"state no regions; create regions fails": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
			},
			runErr:   []error{errors.New("cmd failed")},
			expCalls: []string{"ipmctl version"},
			expErr:   errors.New("cmd failed"),
		},
		"state partial free capacity": {
			scanResp: &storage.ScmScanResponse{
				State: storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
			},
			runOut: []string{
				verStr,
				"",
				mockXMLRegions(t, "part-free-2nd-sock"),
				//				ndctlNsStr,
				//				ndctl2NsStr,
				//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
				//					"   SocketID=0x0000\n" +
				//					"   PersistentMemoryType=AppDirect\n" +
				//					"   FreeCapacity=0.0 GiB\n" +
				//					"---ISetID=0x81187f4881f02ccc---\n" +
				//					"   SocketID=0x0001\n" +
				//					"   PersistentMemoryType=AppDirect\n" +
				//					"   FreeCapacity=0.0 GiB\n",
				//				ndctl2NsStr,
			},
			// TODO DAOS-10173: re-enable when bindings can be used instead of cli
			//regions: []ipmctl.PMemRegion{{Type: uint32(ipmctl.RegionTypeAppDirect)}},
			expPrepResp: &storage.ScmPrepareResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3012 * humanize.GiByte,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3012 * humanize.GiByte,
					},
				},
			},
			expCalls: []string{
				"ndctl create-namespace",
				"ipmctl show -a -region",
				"ndctl create-namespace",
				"ipmctl show -a -region",
				"ndctl list -N -v",
			},
		},
		//		"state free capacity; multiple namespaces requested": {
		//			prepReq: &storage.ScmPrepareRequest{
		//				NrNamespacesPerSocket: 2,
		//			},
		//			scanResp: &storage.ScmScanResponse{
		//				State: storage.ScmSocketState{
		//					State: storage.ScmFreeCap,
		//				},
		//			},
		//			runOut: []string{
		//				verStr,
		//				"",
		//				mockXMLRegions(t, "dual-sock"),
		//				ndctl1of4NsStr,
		//				ndctl2of4NsStr,
		//				ndctl3of4NsStr,
		//				ndctl4of4NsStr,
		//				mockXMLRegions(t, "dual-sock-no-free"),
		//				ndctl4of4NsStr,
		//			},
		//			// TODO DAOS-10173: re-enable when bindings can be used instead of cli
		//			//regions: []ipmctl.PMemRegion{{Type: uint32(ipmctl.RegionTypeAppDirect)}},
		//			expPrepResp: &storage.ScmPrepareResponse{
		//				State: storage.ScmSocketState{
		//					State: storage.ScmNoFreeCap,
		//				},
		//				Namespaces: storage.ScmNamespaces{
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
		//						BlockDevice: "pmem0",
		//						Name:        "namespace0.0",
		//						NumaNode:    0,
		//						Size:        1506 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
		//						BlockDevice: "pmem0.1",
		//						Name:        "namespace0.1",
		//						NumaNode:    0,
		//						Size:        1506 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1530",
		//						BlockDevice: "pmem1",
		//						Name:        "namespace1.0",
		//						NumaNode:    1,
		//						Size:        1506 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1531",
		//						BlockDevice: "pmem1.1",
		//						Name:        "namespace1.1",
		//						NumaNode:    1,
		//						Size:        1506 * humanize.GiByte,
		//					},
		//				},
		//			},
		//			expCalls: []string{
		//				"ipmctl version", "ipmctl delete -goal",
		//				"ipmctl show -d PersistentMemoryType,FreeCapacity -region",
		//				fmt.Sprintf("ndctl create-namespace --region 0 --size %d", 1506*humanize.GiByte),
		//				fmt.Sprintf("ndctl create-namespace --region 0 --size %d", 1506*humanize.GiByte),
		//				fmt.Sprintf("ndctl create-namespace --region 1 --size %d", 1506*humanize.GiByte),
		//				fmt.Sprintf("ndctl create-namespace --region 1 --size %d", 1506*humanize.GiByte),
		//				"ipmctl show -d PersistentMemoryType,FreeCapacity -region",
		//				"ndctl list -N -v",
		//			},
		//		},
		//		"state free capacity; create namespaces fails": {
		//			scanResp: &storage.ScmScanResponse{
		//				State: storage.ScmFreeCap,
		//			},
		//			runErr:   []error{errors.New("cmd failed")},
		//			expErr:   errors.New("cmd failed"),
		//			expCalls: []string{"ndctl create-namespace"},
		//		},
		//		// TODO DAOS-10173: re-enable
		//		//"state free capacity; get regions fails": {
		//		//	scanResp: &storage.ScmScanResponse{
		//		//		State: storage.ScmFreeCap,
		//		//	},
		//		//	runOut:     []string{ndctlNsStr},
		//		//	regionsErr: errors.New("fail"),
		//		//	expCalls:   []string{"ndctl create-namespace"},
		//		//	expErr:     errors.New("discover PMem regions: fail"),
		//		//},
		//		"state no free capacity; missing namespace": {
		//			scanResp: &storage.ScmScanResponse{
		//				State: storage.ScmNoFreeCap,
		//				Namespaces: storage.ScmNamespaces{
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
		//						BlockDevice: "pmem1",
		//						Name:        "namespace1.0",
		//						NumaNode:    1,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//				},
		//			},
		//			runOut: []string{
		//				verStr,
		//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
		//					"   SocketID=0x0000\n" +
		//					"   PersistentMemoryType=AppDirect\n" +
		//					"   FreeCapacity=0.0 GiB\n" +
		//					"---ISetID=0x81187f4881f02ccc---\n" +
		//					"   SocketID=0x0001\n" +
		//					"   PersistentMemoryType=AppDirect\n" +
		//					"   FreeCapacity=0.0 GiB\n",
		//			},
		//			expErr: errors.New("want 2 PMem namespaces but got 1"),
		//			expCalls: []string{
		//				"ipmctl version",
		//				"ipmctl show -a -region",
		//			},
		//		},
		//		"state no free capacity; requested number of namespaces does not match": {
		//			prepReq: &storage.ScmPrepareRequest{
		//				NrNamespacesPerSocket: 2,
		//			},
		//			scanResp: &storage.ScmScanResponse{
		//				State: storage.ScmNoFreeCap,
		//				Namespaces: storage.ScmNamespaces{
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
		//						BlockDevice: "pmem1",
		//						Name:        "namespace1.0",
		//						NumaNode:    1,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
		//						BlockDevice: "pmem0",
		//						Name:        "namespace0.0",
		//						NumaNode:    0,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//				},
		//			},
		//			runOut: []string{
		//				verStr,
		//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
		//					"   SocketID=0x0000\n" +
		//					"   PersistentMemoryType=AppDirect\n" +
		//					"   FreeCapacity=0.0 GiB\n" +
		//					"---ISetID=0x81187f4881f02ccc---\n" +
		//					"   SocketID=0x0001\n" +
		//					"   PersistentMemoryType=AppDirect\n" +
		//					"   FreeCapacity=0.0 GiB\n",
		//			},
		//			expErr: errors.New("want 4 PMem namespaces but got 2"),
		//			expCalls: []string{
		//				"ipmctl version",
		//				"ipmctl show -a -region",
		//			},
		//		},
		//		"state no free capacity": {
		//			scanResp: &storage.ScmScanResponse{
		//				State: storage.ScmNoFreeCap,
		//				Namespaces: storage.ScmNamespaces{
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
		//						BlockDevice: "pmem1",
		//						Name:        "namespace1.0",
		//						NumaNode:    1,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
		//						BlockDevice: "pmem0",
		//						Name:        "namespace0.0",
		//						NumaNode:    0,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//				},
		//			},
		//			runOut: []string{
		//				verStr,
		//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
		//					"   SocketID=0x0000\n" +
		//					"   PersistentMemoryType=AppDirect\n" +
		//					"   FreeCapacity=0.0 GiB\n" +
		//					"---ISetID=0x81187f4881f02ccc---\n" +
		//					"   SocketID=0x0001\n" +
		//					"   PersistentMemoryType=AppDirect\n" +
		//					"   FreeCapacity=0.0 GiB\n",
		//			},
		//			expPrepResp: &storage.ScmPrepareResponse{
		//				State: storage.ScmNoFreeCap,
		//				Namespaces: storage.ScmNamespaces{
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
		//						BlockDevice: "pmem1",
		//						Name:        "namespace1.0",
		//						NumaNode:    1,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
		//						BlockDevice: "pmem0",
		//						Name:        "namespace0.0",
		//						NumaNode:    0,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//				},
		//			},
		//			expCalls: []string{
		//				"ipmctl version",
		//				"ipmctl show -a -region",
		//			},
		//		},
		//		"state no free capacity; multiple namespaces per socket; requested number does not match": {
		//			prepReq: &storage.ScmPrepareRequest{
		//				NrNamespacesPerSocket: 1,
		//			},
		//			scanResp: &storage.ScmScanResponse{
		//				State: storage.ScmNoFreeCap,
		//				Namespaces: storage.ScmNamespaces{
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
		//						BlockDevice: "pmem1",
		//						Name:        "namespace1.0",
		//						NumaNode:    1,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
		//						BlockDevice: "pmem1.1",
		//						Name:        "namespace1.1",
		//						NumaNode:    1,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
		//						BlockDevice: "pmem0",
		//						Name:        "namespace0.0",
		//						NumaNode:    0,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
		//						BlockDevice: "pmem0.1",
		//						Name:        "namespace0.1",
		//						NumaNode:    0,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//				},
		//			},
		//			runOut: []string{
		//				verStr,
		//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
		//					"   SocketID=0x0000\n" +
		//					"   PersistentMemoryType=AppDirect\n" +
		//					"   FreeCapacity=0.0 GiB\n" +
		//					"---ISetID=0x81187f4881f02ccc---\n" +
		//					"   SocketID=0x0001\n" +
		//					"   PersistentMemoryType=AppDirect\n" +
		//					"   FreeCapacity=0.0 GiB\n",
		//			},
		//			expErr: errors.New("want 2 PMem namespaces but got 4"),
		//			expCalls: []string{
		//				"ipmctl version",
		//				"ipmctl show -a -region",
		//			},
		//		},
		//		"state no free capacity; multiple namespaces per socket; one region has no capacity": {
		//			prepReq: &storage.ScmPrepareRequest{
		//				NrNamespacesPerSocket: 2,
		//			},
		//			scanResp: &storage.ScmScanResponse{
		//				State: storage.ScmNoFreeCap,
		//				Namespaces: storage.ScmNamespaces{
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
		//						BlockDevice: "pmem0",
		//						Name:        "namespace0.0",
		//						NumaNode:    0,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//				},
		//			},
		//			runOut: []string{
		//				verStr,
		//				"---ISetID=0x2aba7f4828ef2ccc---\n" +
		//					"   SocketID=0x0000\n" +
		//					"   PersistentMemoryType=AppDirect\n" +
		//					"   FreeCapacity=0.0 GiB\n" +
		//					"---ISetID=0x81187f4881f02ccc---\n" +
		//					"   SocketID=0x0001\n" +
		//					"   PersistentMemoryType=AppDirect\n" +
		//					"   FreeCapacity=3012.0 GiB\n",
		//			},
		//			expErr: errors.New("want 4 PMem namespaces but got 1"),
		//			expCalls: []string{
		//				"ipmctl version",
		//				"ipmctl show -a -region",
		//			},
		//		},
		//		"state no free capacity; multiple namespaces per socket": {
		//			prepReq: &storage.ScmPrepareRequest{
		//				NrNamespacesPerSocket: 2,
		//			},
		//			scanResp: &storage.ScmScanResponse{
		//				State: storage.ScmNoFreeCap,
		//				Namespaces: storage.ScmNamespaces{
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
		//						BlockDevice: "pmem1",
		//						Name:        "namespace1.0",
		//						NumaNode:    1,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
		//						BlockDevice: "pmem1.1",
		//						Name:        "namespace1.1",
		//						NumaNode:    1,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
		//						BlockDevice: "pmem0",
		//						Name:        "namespace0.0",
		//						NumaNode:    0,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
		//						BlockDevice: "pmem0.1",
		//						Name:        "namespace0.1",
		//						NumaNode:    0,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//				},
		//			},
		//			runOut: []string{
		//				verStr,
		//				dualSockNoFreeCap,
		//			},
		//			expPrepResp: &storage.ScmPrepareResponse{
		//				State: storage.ScmNoFreeCap,
		//				Namespaces: storage.ScmNamespaces{
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
		//						BlockDevice: "pmem1",
		//						Name:        "namespace1.0",
		//						NumaNode:    1,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
		//						BlockDevice: "pmem1.1",
		//						Name:        "namespace1.1",
		//						NumaNode:    1,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
		//						BlockDevice: "pmem0",
		//						Name:        "namespace0.0",
		//						NumaNode:    0,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//					{
		//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
		//						BlockDevice: "pmem0.1",
		//						Name:        "namespace0.1",
		//						NumaNode:    0,
		//						Size:        3012 * humanize.GiByte,
		//					},
		//				},
		//			},
		//			expCalls: []string{
		//				"ipmctl version",
		//				"ipmctl show -a -region",
		//			},
		//		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			var calls []string
			var callIdx int

			mockBinding := newMockIpmctl(&mockIpmctlCfg{
				regions:       tc.regions,
				getRegionsErr: tc.regionsErr,
			})

			mockRun := func(cmd string) (string, error) {
				calls = append(calls, cmd)

				o := verStr
				if callIdx < len(tc.runOut) {
					o = tc.runOut[callIdx]
				}
				var e error = nil
				if callIdx < len(tc.runErr) {
					e = tc.runErr[callIdx]
				}

				log.Debugf("mockRun call %d: ret/err %+v/%v", callIdx, o, e)
				callIdx++
				return o, e
			}

			mockLookPath := func(string) (string, error) {
				return "", nil
			}

			cr, err := newCmdRunner(log, mockBinding, mockRun, mockLookPath)
			if err != nil {
				t.Fatal(err)
			}

			if tc.prepReq == nil {
				tc.prepReq = &storage.ScmPrepareRequest{}
			}

			resp, err := cr.prep(*tc.prepReq, tc.scanResp)
			log.Debugf("calls made %+q", calls)
			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expPrepResp, resp); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expCalls, calls); diff != "" {
				t.Fatalf("unexpected cli calls (-want, +got):\n%s\n", diff)
			}
		})
	}
}

//func TestIpmctl_prepReset(t *testing.T) {
//	verStr := "Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825"
//
//	for name, tc := range map[string]struct {
//		scanResp    *storage.ScmScanResponse
//		runOut      string
//		runErr      error
//		regions     []ipmctl.PMemRegion
//		regionsErr  error
//		expErr      error
//		expPrepResp *storage.ScmPrepareResponse
//		expCalls    []string
//	}{
//		"state unknown": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmStateUnknown,
//			},
//			expErr: errors.New("unhandled scm state"),
//		},
//		"state no regions": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoRegions,
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State: storage.ScmNoRegions,
//			},
//		},
//		"state regions": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmFreeCap,
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State:          storage.ScmFreeCap,
//				RebootRequired: true,
//			},
//			expCalls: []string{
//				"ipmctl version", "ipmctl create -f -goal MemoryMode=100",
//			},
//		},
//		"state regions; delete goals fails": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmFreeCap,
//			},
//			expErr:      errors.New("fail"),
//		},
//		"state regions; remove regions fails": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmFreeCap,
//			},
//			runErr: errors.New("cmd failed"),
//			expErr: errors.New("cmd failed"),
//			expCalls: []string{
//				"ipmctl version",
//			},
//		},
//		"state no free capacity": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3012 * humanize.GiByte,
//					},
//				},
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State:          storage.ScmNoFreeCap,
//				RebootRequired: true,
//			},
//			expCalls: []string{
//				"ndctl disable-namespace namespace1.0",
//				"ndctl destroy-namespace namespace1.0",
//				"ipmctl create -f -goal MemoryMode=100",
//			},
//		},
//		"state no free capacity; remove namespace fails": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3012 * humanize.GiByte,
//					},
//				},
//			},
//			runErr:   errors.New("cmd failed"),
//			expErr:   errors.New("cmd failed"),
//			expCalls: []string{"ndctl disable-namespace namespace1.0"},
//		},
//		"state no free capacity; multiple namespaces per socket": {
//			scanResp: &storage.ScmScanResponse{
//				State: storage.ScmNoFreeCap,
//				Namespaces: storage.ScmNamespaces{
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
//						BlockDevice: "pmem1",
//						Name:        "namespace1.0",
//						NumaNode:    1,
//						Size:        3012 * humanize.GiByte,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
//						BlockDevice: "pmem1.1",
//						Name:        "namespace1.1",
//						NumaNode:    1,
//						Size:        3012 * humanize.GiByte,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
//						BlockDevice: "pmem0",
//						Name:        "namespace0.0",
//						NumaNode:    0,
//						Size:        3012 * humanize.GiByte,
//					},
//					{
//						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
//						BlockDevice: "pmem0.1",
//						Name:        "namespace0.1",
//						NumaNode:    0,
//						Size:        3012 * humanize.GiByte,
//					},
//				},
//			},
//			expPrepResp: &storage.ScmPrepareResponse{
//				State:          storage.ScmNoFreeCap,
//				RebootRequired: true,
//			},
//			expCalls: []string{
//				"ndctl disable-namespace namespace1.0",
//				"ndctl destroy-namespace namespace1.0",
//				"ndctl disable-namespace namespace1.1",
//				"ndctl destroy-namespace namespace1.1",
//				"ndctl disable-namespace namespace0.0",
//				"ndctl destroy-namespace namespace0.0",
//				"ndctl disable-namespace namespace0.1",
//				"ndctl destroy-namespace namespace0.1",
//				"ipmctl create -f -goal MemoryMode=100",
//			},
//		},
//	} {
//		t.Run(name, func(t *testing.T) {
//			log, buf := logging.NewTestLogger(t.Name())
//			defer test.ShowBufferOnFailure(t, buf)
//
//			var calls []string
//
//			if tc.runOut == "" {
//				tc.runOut = verStr
//			}
//
//			mockBinding := newMockIpmctl(&mockIpmctlCfg{
//				regions:       tc.regions,
//				getRegionsErr: tc.regionsErr,
//			})
//
//			mockRun := func(cmd string) (string, error) {
//				calls = append(calls, cmd)
//				return tc.runOut, tc.runErr
//			}
//
//			mockLookPath := func(string) (string, error) {
//				return "", nil
//			}
//
//			cr, err := newCmdRunner(log, mockBinding, mockRun, mockLookPath)
//			if err != nil {
//				t.Fatal(err)
//			}
//
//			resp, err := cr.prepReset(tc.scanResp)
//			test.CmpErr(t, tc.expErr, err)
//
//			if diff := cmp.Diff(tc.expPrepResp, resp); diff != "" {
//				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
//			}
//			if diff := cmp.Diff(tc.expCalls, calls); diff != "" {
//				t.Fatalf("unexpected cli calls (-want, +got):\n%s\n", diff)
//			}
//		})
//	}
//}
