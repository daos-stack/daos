//
// (C) Copyright 2019-2024 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var (
	testModules = storage.ScmModules{
		mockModule("abcd", 30, 0, 0, 0, 1),
		mockModule("abce", 31, 0, 0, 1, 0),
		mockModule("abcf", 32, 1, 0, 0, 1),
		mockModule("abcg", 33, 1, 0, 1, 0),
	}
	sock0 = uint(0)
	sock1 = uint(1)
)

func mockCmdShowRegionsWithSock(sid int) pmemCmd {
	cmd := cmdShowRegions
	cmd.Args = append(cmd.Args, "-socket", fmt.Sprintf("%d", sid))
	return cmd
}

func mockCmdDeleteGoalsWithSock(sid int) pmemCmd {
	cmd := cmdDeleteGoals
	cmd.Args = append(cmd.Args, "-socket", fmt.Sprintf("%d", sid))
	return cmd
}

func mockCmdCreateRegionsWithSock(sid int) pmemCmd {
	return pmemCmd{
		BinaryName: "ipmctl",
		Args: []string{
			"create", "-f", "-goal", "-socket", fmt.Sprintf("%d", sid),
			"PersistentMemoryType=AppDirect",
		},
	}
}

func mockCmdCreateNamespace(regionID int, bytes int) pmemCmd {
	return pmemCmd{
		BinaryName: "ndctl",
		Args: []string{
			"create-namespace", "--region",
			fmt.Sprintf("region%d", regionID),
			"--size", fmt.Sprintf("%d", bytes),
		},
	}
}

func mockCmdDisableNamespace(name string) pmemCmd {
	cmd := cmdDisableNamespace
	cmd.Args = append(cmd.Args, name)
	return cmd
}

func mockCmdDestroyNamespace(name string) pmemCmd {
	cmd := cmdDestroyNamespace
	cmd.Args = append(cmd.Args, name)
	return cmd
}

func mockCmdListNamespacesWithNUMA(numaID int) pmemCmd {
	cmd := cmdListNamespaces
	cmd.Args = append(cmd.Args, "--numa-node", fmt.Sprintf("%d", numaID))
	return cmd
}

func genNsJSON(t *testing.T, numa, nsNum, size, id string) string {
	t.Helper()
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

func getNsFromJSON(t *testing.T, j string) storage.ScmNamespaces {
	t.Helper()
	nss, err := parseNamespaces(j)
	if err != nil {
		t.Fatal(err)
	}
	return nss
}

const verStr = `Intel(R) Optane(TM) Persistent Memory Command Line Interface Version 02.00.00.3825`

func TestIpmctl_prep(t *testing.T) {
	var (
		ndctlDualNsStr = fmt.Sprintf("[%s,%s]", genNsJSON(t, "0", "0", "3012GiB", "28"),
			genNsJSON(t, "1", "0", "3012GiB", "29"))
		ndctlDualNsPerSockStr = fmt.Sprintf("[%s,%s,%s,%s]",
			genNsJSON(t, "0", "0", "1506GiB", "28"), genNsJSON(t, "0", "1", "1506GiB", "29"),
			genNsJSON(t, "1", "0", "1506GiB", "30"), genNsJSON(t, "1", "1", "1506GiB", "31"))
		dualNS        = getNsFromJSON(t, ndctlDualNsStr)
		dualNSPerSock = getNsFromJSON(t, ndctlDualNsPerSockStr)
	)

	for name, tc := range map[string]struct {
		prepReq     *storage.ScmPrepareRequest
		nilScanResp bool
		scanResp    *storage.ScmScanResponse
		runOut      []string
		runErr      []error
		expErr      error
		expPrepResp *storage.ScmPrepareResponse
		expCalls    []pmemCmd
	}{
		"nil scan response": {
			nilScanResp: true,
			expErr:      errors.New("nil scan response"),
		},
		"no modules": {
			scanResp: &storage.ScmScanResponse{},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: storage.ScmNamespaces{},
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
			},
		},
		"non-interleaved": {
			runOut: []string{
				verStr, mockXMLRegions(t, "not-interleaved"),
			},
			expErr: storage.FaultScmNotInterleaved(sock0),
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions,
			},
		},
		"unhealthy": {
			runOut: []string{
				verStr, mockXMLRegions(t, "unhealthy-2nd-sock"),
			},
			expErr: storage.FaultScmNotHealthy(sock1),
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions,
			},
		},
		"partial capacity": {
			runOut: []string{
				verStr, mockXMLRegions(t, "part-free"),
			},
			expErr: storage.FaultScmPartialCapacity(sock0),
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions,
			},
		},
		"unknown memory mode": {
			runOut: []string{
				verStr, mockXMLRegions(t, "unknown-memtype"),
			},
			expErr: storage.FaultScmUnknownMemoryMode(sock0),
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions,
			},
		},
		"no regions": {
			runOut: []string{
				verStr, outNoPMemRegions, "", "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
				RebootRequired: true,
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
				cmdCreateRegions,
			},
		},
		"no regions; sock selected": {
			prepReq: &storage.ScmPrepareRequest{
				SocketID: &sock1,
			},
			runOut: []string{
				verStr, outNoPMemRegions, "", "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					SocketID: &sock1,
					State:    storage.ScmNoRegions,
				},
				RebootRequired: true,
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, mockCmdShowRegionsWithSock(1),
				mockCmdDeleteGoalsWithSock(1), mockCmdCreateRegionsWithSock(1),
			},
		},
		"no regions; delete goals fails": {
			runOut: []string{
				verStr, outNoPMemRegions, "",
			},
			runErr: []error{
				nil, nil, errors.New("fail"),
			},
			expErr: errors.Errorf("%s: fail", cmdDeleteGoals.String()),
			expCalls: []pmemCmd{
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
			expErr: errors.Errorf("%s: fail", cmdCreateRegions.String()),
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
				cmdCreateRegions,
			},
		},
		"no free capacity": {
			scanResp: &storage.ScmScanResponse{
				Modules:    testModules,
				Namespaces: dualNS,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock"), "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: dualNS,
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"no free capacity; multiple namespaces per socket": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
			},
			scanResp: &storage.ScmScanResponse{
				Modules:    testModules,
				Namespaces: dualNSPerSock,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock"), "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: dualNSPerSock,
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expCalls: []pmemCmd{
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
			expCalls: []pmemCmd{
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
			expCalls: []pmemCmd{
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
			expPrepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
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
			expCalls: []pmemCmd{
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
				verStr, mockXMLRegions(t, "dual-sock"), "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Socket: &storage.ScmSocketState{
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
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"free capacity": {
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock-full-free"), "", ndctlRegionsDual,
				"", "", ndctlDualNsStr, mockXMLRegions(t, "dual-sock-no-free"),
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: dualNS,
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals, cmdListNdctlRegions,
				mockCmdCreateNamespace(0, 1082331758592),
				mockCmdCreateNamespace(1, 1082331758592),
				cmdListNamespaces, cmdShowRegions,
			},
		},
		"free capacity; two namespaces per socket requested": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
			},
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock-full-free"), "", ndctlRegionsDual,
				"", "", "", "", ndctlDualNsPerSockStr,
				mockXMLRegions(t, "dual-sock-no-free"),
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: dualNSPerSock,
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions,
				cmdDeleteGoals, cmdListNdctlRegions,
				mockCmdCreateNamespace(0, 541165879296),
				mockCmdCreateNamespace(0, 541165879296),
				mockCmdCreateNamespace(1, 541165879296),
				mockCmdCreateNamespace(1, 541165879296),
				cmdListNamespaces, cmdShowRegions,
			},
		},
		"free capacity; two namespaces per socket requested; sock 1 select; iset id match": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
				SocketID:              &sock1,
			},
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "sock-one-full-free"), "", ndctlRegionsSwapISet,
				"", "", ndctlNamespaceDualR0, mockXMLRegions(t, "sock-one"),
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: getNsFromJSON(t, ndctlNamespaceDualR0),
				Socket: &storage.ScmSocketState{
					State:    storage.ScmNoFreeCap,
					SocketID: &sock1,
				},
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, mockCmdShowRegionsWithSock(1),
				mockCmdDeleteGoalsWithSock(1), cmdListNdctlRegions,
				mockCmdCreateNamespace(0, 541165879296),
				mockCmdCreateNamespace(0, 541165879296),
				mockCmdListNamespacesWithNUMA(1),
				mockCmdShowRegionsWithSock(1),
			},
		},
		"free capacity; iset id overflow": {
			prepReq: &storage.ScmPrepareRequest{},
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock-full-free"), "", ndctlRegionsNegISet,
				"", "", ndctlDualNsStr, mockXMLRegions(t, "dual-sock-no-free"),
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: dualNS,
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals, cmdListNdctlRegions,
				mockCmdCreateNamespace(0, 1082331758592),
				mockCmdCreateNamespace(1, 1082331758592),
				cmdListNamespaces, cmdShowRegions,
			},
		},
		"free capacity; two namespaces per socket requested; sock 1 select; iset id overflow": {
			prepReq: &storage.ScmPrepareRequest{
				NrNamespacesPerSocket: 2,
				SocketID:              &sock1,
			},
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "sock-one-full-free"), "", ndctlRegionsNegISet,
				"", "", ndctlNamespaceDualR1, mockXMLRegions(t, "sock-one"),
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: getNsFromJSON(t, ndctlNamespaceDualR1),
				Socket: &storage.ScmSocketState{
					State:    storage.ScmNoFreeCap,
					SocketID: &sock1,
				},
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, mockCmdShowRegionsWithSock(1),
				mockCmdDeleteGoalsWithSock(1), cmdListNdctlRegions,
				mockCmdCreateNamespace(1, 541165879296),
				mockCmdCreateNamespace(1, 541165879296),
				mockCmdListNamespacesWithNUMA(0),
				mockCmdShowRegionsWithSock(1),
			},
		},
		"free capacity; create namespace fails": {
			scanResp: &storage.ScmScanResponse{
				Modules: testModules,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock-full-free"), "", ndctlRegionsDual, "",
			},
			runErr: []error{
				nil, nil, nil, nil, errors.New("fail"),
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals, cmdListNdctlRegions,
				mockCmdCreateNamespace(0, 1082331758592),
			},
			expErr: errors.New("fail"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			var calls []pmemCmd
			var callIdx int

			mockRun := func(_ logging.Logger, cmd pmemCmd) (string, error) {
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
				return o, errors.Wrap(e, cmd.String())
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
			if len(tc.runOut) != len(calls) {
				t.Fatal("runOut slice has different number of entries than calls made")
			}
			if len(tc.runErr) > 0 && len(tc.runErr) != len(calls) {
				t.Fatal("runErr slice has different number of entries than calls made")
			}
			for i, s := range calls {
				log.Debugf("call made: %v", s)
				if tc.runOut[i] != "" {
					log.Debugf("output: %s", tc.runOut[i])
				}
			}
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
	var (
		ndctlDualNsStr = fmt.Sprintf("[%s,%s]", genNsJSON(t, "0", "0", "3012GiB", "28"),
			genNsJSON(t, "1", "0", "3012GiB", "29"))
		dualNS = getNsFromJSON(t, ndctlDualNsStr)
	)

	for name, tc := range map[string]struct {
		prepReq     *storage.ScmPrepareRequest
		nilScanResp bool
		scanResp    *storage.ScmScanResponse
		runOut      []string
		runErr      []error
		expPrepResp *storage.ScmPrepareResponse
		expErr      error
		expCalls    []pmemCmd
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
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoModules,
				},
			},
		},
		"single socket selected; get regions fails; invalid xml": {
			prepReq: &storage.ScmPrepareRequest{
				SocketID: &sock1,
			},
			runOut: []string{
				verStr, `text that is invalid xml`,
			},
			expErr: errors.New("parse show region cmd"),
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, mockCmdShowRegionsWithSock(1),
			},
		},
		"get pmem state fails; continue to remove regions": {
			runOut: []string{
				verStr, mockXMLRegions(t, "same-sock"), "", "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: storage.ScmNamespaces{},
				Socket: &storage.ScmSocketState{
					State: storage.ScmUnknownMode,
				},
				RebootRequired: true,
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
				cmdCreateRegions,
			},
		},
		"delete goals fails": {
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock"), "",
			},
			runErr: []error{
				nil, nil, errors.New("fail"),
			},
			expErr: errors.Errorf("%s: fail", cmdDeleteGoals.String()),
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"no regions": {
			runOut: []string{
				verStr, outNoPMemRegions,
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: storage.ScmNamespaces{},
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoRegions,
				},
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
			},
		},
		"no regions; sock selected": {
			prepReq: &storage.ScmPrepareRequest{
				SocketID: &sock1,
			},
			runOut: []string{
				verStr, outNoPMemRegions,
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: storage.ScmNamespaces{},
				Socket: &storage.ScmSocketState{
					SocketID: &sock1,
					State:    storage.ScmNoRegions,
				},
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, mockCmdShowRegionsWithSock(1),
				mockCmdDeleteGoalsWithSock(1),
			},
		},
		"remove regions; without namespaces": {
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock-full-free"), "", "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: storage.ScmNamespaces{},
				Socket: &storage.ScmSocketState{
					State: storage.ScmFreeCap,
				},
				RebootRequired: true,
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
				cmdCreateRegions,
			},
		},
		"remove regions; with namespaces": {
			scanResp: &storage.ScmScanResponse{
				Modules:    testModules,
				Namespaces: dualNS,
			},
			runOut: []string{
				verStr, mockXMLRegions(t, "dual-sock"), "", "", "", "", "", "",
			},
			expPrepResp: &storage.ScmPrepareResponse{
				Namespaces: storage.ScmNamespaces{},
				Socket: &storage.ScmSocketState{
					State: storage.ScmNoFreeCap,
				},
				RebootRequired: true,
			},
			expCalls: []pmemCmd{
				cmdShowIpmctlVersion, cmdShowRegions, cmdDeleteGoals,
				mockCmdDisableNamespace("namespace0.0"),
				mockCmdDestroyNamespace("namespace0.0"),
				mockCmdDisableNamespace("namespace1.0"),
				mockCmdDestroyNamespace("namespace1.0"),
				cmdCreateRegions,
			},
		},
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

			var calls []pmemCmd
			var callIdx int

			mockRun := func(_ logging.Logger, cmd pmemCmd) (string, error) {
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
				return o, errors.Wrap(e, cmd.String())
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
