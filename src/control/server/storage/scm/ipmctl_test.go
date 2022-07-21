//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"fmt"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var (
	testDevices = []ipmctl.DeviceDiscovery{
		mockDiscovery(0),
		mockDiscovery(0),
		mockDiscovery(1),
		mockDiscovery(1),
	}
	testModules = storage.ScmModules{
		mockModule(testDevices[0]),
		mockModule(testDevices[1]),
		mockModule(testDevices[2]),
		mockModule(testDevices[3]),
	}
	sock0 = uint(0)
	sock1 = uint(1)
)

const verStr = `Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825`

func TestIpmctl_getModules(t *testing.T) {
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
			expResult: testModules,
		},
		"filter modules by socket 0": {
			cfg: &mockIpmctlCfg{
				modules: testDevices,
			},
			sockID:    0,
			expResult: testModules[:2],
		},
		"filter modules by socket 1": {
			cfg: &mockIpmctlCfg{
				modules: testDevices,
			},
			sockID:    1,
			expResult: testModules[2:],
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

func TestIpmctl_prep(t *testing.T) {
	genNsJSON := func(numa, nsNum, size, id string) string {
		nsName := fmt.Sprintf("%s.%s", numa, nsNum)
		bdName := numa
		if nsNum != "0" {
			bdName = nsName
		}
		sizeBytes, err := humanize.ParseBytes(size)
		if err != nil {
			t.Fatal(err)
		}

		return fmt.Sprintf(`{"dev":"namespace%s","mode":"fsdax","map":"dev",`+
			`"size":%d,"uuid":"842fc847-28e0-4bb6-8dfc-d24afdba15%s",`+
			`"raw_uuid":"dedb4b28-dc4b-4ccd-b7d1-9bd475c91264","sector_size":512,`+
			`"blockdev":"pmem%s","numa_node":%s}`, nsName, sizeBytes, id, bdName, numa)
	}
	ndctlDualNsStr := fmt.Sprintf("[%s,%s]", genNsJSON("0", "0", "3012GiB", "28"),
		genNsJSON("1", "0", "3012GiB", "29"))
	ndctlDualNsPerSockStr := fmt.Sprintf("[%s,%s,%s,%s]",
		genNsJSON("0", "0", "1506GiB", "28"), genNsJSON("0", "1", "1506GiB", "29"),
		genNsJSON("1", "0", "1506GiB", "30"), genNsJSON("1", "1", "1506GiB", "31"))
	dualNs, err := parseNamespaces(ndctlDualNsStr)
	if err != nil {
		t.Fatal(err)
	}
	dualNsPerSock, err := parseNamespaces(ndctlDualNsPerSockStr)
	if err != nil {
		t.Fatal(err)
	}

	for name, tc := range map[string]struct {
		prepReq     *storage.ScmPrepareRequest
		nilScanResp bool
		scanResp    *storage.ScmScanResponse
		runOut      []string
		runErr      []error
		expErr      error
		expPrepResp *storage.ScmPrepareResponse
		expCalls    []string
	}{
		"nil scan response": {
			nilScanResp: true,
			expErr:      errors.New("nil scan response"),
		},
		"no modules": {
			scanResp: &storage.ScmScanResponse{},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: storage.ScmNamespaces{},
				State: storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
			},
		},
		"non-interleaved": {
			runOut: []string{
				verStr, mockXMLRegions(t, "not-interleaved"),
			},
			expErr: storage.FaultScmNotInterleaved(sock0),
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions,
			},
		},
		"unhealthy": {
			runOut: []string{
				verStr, mockXMLRegions(t, "unhealthy-2nd-sock"),
			},
			expErr: storage.FaultScmNotHealthy(sock1),
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions,
			},
		},
		"partial capacity": {
			runOut: []string{
				verStr, mockXMLRegions(t, "part-free"),
			},
			expErr: storage.FaultScmPartialCapacity(sock0),
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions,
			},
		},
		"unknown memory mode": {
			runOut: []string{
				verStr, mockXMLRegions(t, "unknown-memtype"),
			},
			expErr: storage.FaultScmUnknownMemoryMode(sock0),
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions,
			},
		},
		"no regions": {
			runOut: []string{
				verStr, outNoPMemRegions, "", "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				State: storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
				RebootRequired: true,
			},
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals, cmdCreateRegions,
			},
		},
		"no regions; delete goals fails": {
			runOut: []string{
				verStr, outNoPMemRegions, "",
			},
			runErr: []error{
				nil, nil, errors.New("fail"),
			},
			expErr: errors.Errorf("cmd %q: fail", cmdDeleteGoals),
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"no regions; create regions fails": {
			runOut: []string{
				verStr, outNoPMemRegions, "", "",
			},
			runErr: []error{
				nil, nil, nil, errors.New("fail"),
			},
			expErr: errors.Errorf("cmd %q: fail", cmdCreateRegions),
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals, cmdCreateRegions,
			},
		},
		"no free capacity": {
			scanResp: &storage.ScmScanResponse{
				Modules:    testModules,
				Namespaces: dualNs,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock"), "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: dualNs,
				State: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"no free capacity; multiple namespaces per socket": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
			},
			scanResp: &storage.ScmScanResponse{
				Modules:    testModules,
				Namespaces: dualNsPerSock,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock"), "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: dualNsPerSock,
				State: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"no free capacity; missing namespace": {
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3012 * humanize.GiByte,
					},
				},
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock"), "",
			},
			expErr: errors.New("namespace major versions (1) to equal num regions (2)"),
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"no free capacity; requested number of namespaces does not match": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
			},
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
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
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock"), "",
			},
			expErr: errors.New("namespaces on numa 0, want 2 got 1"),
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"no free capacity; namespace name-numa mismatch": {
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    1,
						Size:        3012 * humanize.GiByte,
					},
				},
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "sock-one"), "",
			},
			expErr: errors.New("namespace major version (0) to equal numa node (1)"),
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"no free capacity; multiple namespaces per socket; namespaces out of order": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
			},
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
				Namespaces: storage.ScmNamespaces{
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1528",
						BlockDevice: "pmem1",
						Name:        "namespace1.0",
						NumaNode:    1,
						Size:        3012 * humanize.GiByte,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
						BlockDevice: "pmem1.1",
						Name:        "namespace1.1",
						NumaNode:    1,
						Size:        3012 * humanize.GiByte,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3012 * humanize.GiByte,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0.1",
						Name:        "namespace0.1",
						NumaNode:    0,
						Size:        3012 * humanize.GiByte,
					},
				},
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock"), "", "",
			},
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
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1520",
						BlockDevice: "pmem1.1",
						Name:        "namespace1.1",
						NumaNode:    1,
						Size:        3012 * humanize.GiByte,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1527",
						BlockDevice: "pmem0",
						Name:        "namespace0.0",
						NumaNode:    0,
						Size:        3012 * humanize.GiByte,
					},
					{
						UUID:        "842fc847-28e0-4bb6-8dfc-d24afdba1529",
						BlockDevice: "pmem0.1",
						Name:        "namespace0.1",
						NumaNode:    0,
						Size:        3012 * humanize.GiByte,
					},
				},
			},
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"free capacity": {
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock-full-free"), "", "", "",
				ndctlDualNsStr, mockXMLRegions(t, "dual-sock-no-free"),
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: dualNs,
				State: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
				"ndctl create-namespace --region 1 --size 1082331758592",
				"ndctl create-namespace --region 2 --size 1082331758592",
				cmdListNamespaces, cmdShowRegions,
			},
		},
		"free capacity; multiple namespaces requested": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
			},
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock-full-free"), "", "", "", "", "",
				ndctlDualNsPerSockStr, mockXMLRegions(t, "dual-sock-no-free"),
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: dualNsPerSock,
				State: storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
				"ndctl create-namespace --region 1 --size 541165879296",
				"ndctl create-namespace --region 1 --size 541165879296",
				"ndctl create-namespace --region 2 --size 541165879296",
				"ndctl create-namespace --region 2 --size 541165879296",
				cmdListNamespaces, cmdShowRegions,
			},
		},
		"free capacity; create namespace fails": {
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock-full-free"), "", "",
			},
			runErr: []error{
				nil, nil, nil, errors.New("fail"),
			},
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
				"ndctl create-namespace --region 1 --size 1082331758592",
			},
			expErr: errors.New("ndctl create-namespace --region 1 --size 1082331758592: fail"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			var calls []string
			var callIdx int

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

			cr, err := newCmdRunner(log, nil, mockRun, mockLookPath)
			if err != nil {
				t.Fatal(err)
			}

			if tc.prepReq == nil {
				tc.prepReq = &storage.ScmPrepareRequest{}
			}
			if tc.scanResp == nil && !tc.nilScanResp {
				tc.scanResp = &storage.ScmScanResponse{
					Modules: testModules,
				}
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

func TestIpmctl_prepReset(t *testing.T) {
	for name, tc := range map[string]struct {
		prepReq     *storage.ScmPrepareRequest
		nilScanResp bool
		scanResp    *storage.ScmScanResponse
		runOut      []string
		runErr      []error
		expPrepResp *storage.ScmPrepareResponse
		expErr      error
		expCalls    []string
	}{
		"nil scan response": {
			nilScanResp: true,
			expErr:      errors.New("nil scan response"),
		},
		"no modules": {
			scanResp: &storage.ScmScanResponse{
				Modules: storage.ScmModules{},
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: storage.ScmNamespaces{},
				State: storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
			},
		},
		"single socket selected; get regions fails; invalid xml": {
			runOut: []string{
				verStr, `text that is invalid xml`,
			},
			expErr: errors.New("parse show region cmd"),
			expCalls: []string{
				cmdShowIpmctlVersion, cmdShowRegions,
			},
		},
		//		"non-interleaved": {
		//			runOut: []string{
		//				verStr, mockXMLRegions(t, "not-interleaved"),
		//			},
		//			expErr: storage.FaultScmNotInterleaved(sock0),
		//			expCalls: []string{
		//				cmdShowIpmctlVersion, cmdShowRegions,
		//			},
		//		},
		//		"unhealthy": {
		//			runOut: []string{
		//				verStr, mockXMLRegions(t, "unhealthy-2nd-sock"),
		//			},
		//			expErr: storage.FaultScmNotHealthy(sock1),
		//			expCalls: []string{
		//				cmdShowIpmctlVersion, cmdShowRegions,
		//			},
		//		},
		//		"partial capacity": {
		//			runOut: []string{
		//				verStr, mockXMLRegions(t, "part-free"),
		//			},
		//			expErr: storage.FaultScmPartialCapacity(sock0),
		//			expCalls: []string{
		//				cmdShowIpmctlVersion, cmdShowRegions,
		//			},
		//		},
		//		"state unknown": {
		//			scanResp: &storage.ScmScanResponse{
		//				State: storage.ScmStateUnknown,
		//			},
		//			expErr: errors.New("unhandled scm state"),
		//		},
		//		"no regions": {
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
		//			expErr: errors.New("fail"),
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
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			if tc.prepReq == nil {
				tc.prepReq = &storage.ScmPrepareRequest{}
			}
			if tc.scanResp == nil && !tc.nilScanResp {
				tc.scanResp = &storage.ScmScanResponse{
					Modules: testModules,
				}
			}

			var calls []string
			var callIdx int

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

			cr, err := newCmdRunner(log, nil, mockRun, mockLookPath)
			if err != nil {
				t.Fatal(err)
			}

			resp, err := cr.prepReset(*tc.prepReq, tc.scanResp)
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
