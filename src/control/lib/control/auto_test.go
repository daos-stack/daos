//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	nd "github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/ioserver"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var (
	ioCfg = func(t *testing.T, numa int) *ioserver.Config {
		return ioserver.NewConfig().
			WithScmClass(storage.ScmClassDCPM.String()).
			WithScmMountPoint(fmt.Sprintf("/mnt/daos%d", numa)).
			WithScmDeviceList(fmt.Sprintf("/dev/pmem%d", numa)).
			WithBdevClass(storage.BdevClassNvme.String())
	}
	ioCfgWithSSDs = func(t *testing.T, numa int) *ioserver.Config {
		var pciAddrs []string
		for _, c := range MockServerScanResp(t, "withSpaceUsage").Nvme.Ctrlrs {
			if int(c.Socketid) == numa {
				pciAddrs = append(pciAddrs, c.Pciaddr)
			}
		}

		return ioCfg(t, numa).WithBdevDeviceList(pciAddrs...)
	}
	ib0 = &HostFabricInterface{
		Provider: "ofi+psm2", Device: "ib0", NumaNode: 0, NetDevClass: 32, Priority: 0,
	}
	ib1 = &HostFabricInterface{
		Provider: "ofi+psm2", Device: "ib1", NumaNode: 1, NetDevClass: 32, Priority: 1,
	}
	eth0 = &HostFabricInterface{
		Provider: "ofi+sockets", Device: "eth0", NumaNode: 0, NetDevClass: 1, Priority: 2,
	}
	eth1 = &HostFabricInterface{
		Provider: "ofi+sockets", Device: "eth1", NumaNode: 1, NetDevClass: 1, Priority: 3,
	}
)

func cmpHostErrs(t *testing.T, expErrs []*MockHostError, gotErrs *HostErrorsResp) {
	t.Helper()

	if expErrs != nil {
		expHostErrs := MockHostErrorsResp(t, expErrs...)
		if diff := cmp.Diff(expHostErrs.GetHostErrors(),
			gotErrs.GetHostErrors(), defResCmpOpts()...); diff != "" {

			t.Fatalf("unexpected host errors (-want, +got):\n%s\n", diff)
		}
		return
	}

	if gotErrs != nil {
		t.Fatalf("unexpected host errors %s", gotErrs.Errors())
	}
}

func TestControl_AutoConfig_getNetworkParams(t *testing.T) {
	if1PB := &ctlpb.FabricInterface{
		Provider: "test-provider", Device: "test-device", Numanode: 42,
	}
	if2PB := &ctlpb.FabricInterface{
		Provider: "test-provider", Device: "test-device2", Numanode: 84,
	}
	ib0PB := new(ctlpb.FabricInterface)
	if err := convert.Types(ib0, ib0PB); err != nil {
		t.Fatal(err)
	}
	ib1PB := new(ctlpb.FabricInterface)
	if err := convert.Types(ib1, ib1PB); err != nil {
		t.Fatal(err)
	}
	eth0PB := new(ctlpb.FabricInterface)
	if err := convert.Types(eth0, eth0PB); err != nil {
		t.Fatal(err)
	}
	eth1PB := new(ctlpb.FabricInterface)
	if err := convert.Types(eth1, eth1PB); err != nil {
		t.Fatal(err)
	}
	fabIfs1 := &ctlpb.NetworkScanResp{Interfaces: []*ctlpb.FabricInterface{if1PB, if2PB}}
	fabIfs1wNuma := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{if1PB, if2PB}, Numacount: 2,
	}
	fabIfs2 := &ctlpb.NetworkScanResp{Interfaces: []*ctlpb.FabricInterface{if2PB}}
	fabIfs3 := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{ib0PB, eth0PB}, Numacount: 2, Corespernuma: 24,
	}
	fabIfs4 := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{ib0PB, ib1PB}, Numacount: 2, Corespernuma: 24,
	}
	fabIfs5 := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{eth0PB, eth1PB}, Numacount: 2, Corespernuma: 24,
	}
	hostRespRemoteFail := []*HostResponse{
		{Addr: "host1", Message: fabIfs1},
		{Addr: "host2", Error: errors.New("remote failed"), Message: fabIfs1}}
	hostRespRemoteFails := []*HostResponse{
		{Addr: "host1", Error: errors.New("remote failed"), Message: fabIfs1},
		{Addr: "host2", Error: errors.New("remote failed"), Message: fabIfs1},
	}
	typIfs := []*ctlpb.FabricInterface{
		{Provider: "ofi+psm2", Device: "ib0", Numanode: 0, Priority: 0, Netdevclass: 32},
		{Provider: "ofi+psm2", Device: "ib1", Numanode: 1, Priority: 1, Netdevclass: 32},
		{Provider: "ofi+verbs;ofi_rxm", Device: "ib0", Numanode: 0, Priority: 2, Netdevclass: 32},
		{Provider: "ofi+verbs;ofi_rxm", Device: "ib1", Numanode: 1, Priority: 3, Netdevclass: 32},
		{Provider: "ofi+verbs;ofi_rxm", Device: "eth0", Numanode: 0, Priority: 4, Netdevclass: 1},
		{Provider: "ofi+tcp;ofi_rxm", Device: "ib0", Numanode: 0, Priority: 5, Netdevclass: 32},
		{Provider: "ofi+tcp;ofi_rxm", Device: "ib1", Numanode: 1, Priority: 6, Netdevclass: 32},
		{Provider: "ofi+tcp;ofi_rxm", Device: "eth0", Numanode: 0, Priority: 7, Netdevclass: 1},
		{Provider: "ofi+verbs", Device: "ib0", Numanode: 0, Priority: 8, Netdevclass: 32},
		{Provider: "ofi+verbs", Device: "ib1", Numanode: 1, Priority: 9, Netdevclass: 32},
		{Provider: "ofi+tcp", Device: "ib0", Numanode: 0, Priority: 10, Netdevclass: 32},
		{Provider: "ofi+tcp", Device: "ib1", Numanode: 1, Priority: 11, Netdevclass: 32},
		{Provider: "ofi+tcp", Device: "eth0", Numanode: 0, Priority: 12, Netdevclass: 1},
		{Provider: "ofi+sockets", Device: "ib0", Numanode: 0, Priority: 13, Netdevclass: 32},
		{Provider: "ofi+sockets", Device: "ib1", Numanode: 1, Priority: 14, Netdevclass: 32},
		{Provider: "ofi+sockets", Device: "eth0", Numanode: 0, Priority: 15, Netdevclass: 1},
	}
	typicalFabIfs := &ctlpb.NetworkScanResp{Interfaces: typIfs, Numacount: 2, Corespernuma: 24}
	dualHostResp := func(r1, r2 *ctlpb.NetworkScanResp) []*HostResponse {
		return []*HostResponse{
			{
				Addr:    "host1",
				Message: r1,
			},
			{
				Addr:    "host2",
				Message: r2,
			},
		}
	}
	dualHostRespSame := func(r1 *ctlpb.NetworkScanResp) []*HostResponse {
		return dualHostResp(r1, r1)
	}

	for name, tc := range map[string]struct {
		numPmem         int
		netDevClass     uint32
		accessPoints    []string
		uErr            error
		hostResponses   []*HostResponse
		expHostErrs     []*MockHostError
		expErr          error
		expIfs          []*HostFabricInterface
		expCoresPerNuma int
	}{
		"invoker error": {
			uErr:          errors.New("unary error"),
			hostResponses: dualHostRespSame(fabIfs1),
			expErr:        errors.New("unary error"),
		},
		"host network scan failed": {
			hostResponses: hostRespRemoteFail,
			expHostErrs: []*MockHostError{
				{"host2", "remote failed"},
				{"host2", "remote failed"},
			},
			expErr: errors.New("1 host had errors"),
		},
		"host network scan failed on multiple hosts": {
			hostResponses: hostRespRemoteFails,
			expHostErrs: []*MockHostError{
				{"host1", "remote failed"},
				{"host2", "remote failed"},
			},
			expErr: errors.New("2 hosts had errors"),
		},
		"host network scan no hosts": {
			hostResponses: []*HostResponse{},
			expErr:        errors.New("no host responses"),
		},
		"host network mismatch": {
			hostResponses: dualHostResp(fabIfs1, fabIfs2),
			expErr:        errors.New("network hardware not consistent across hosts"),
		},
		"zero min pmem and zero numa on one host": {
			hostResponses: dualHostResp(fabIfs1, fabIfs1wNuma),
			expErr:        errors.New("network hardware not consistent across hosts"),
		},
		"unsupported network class in request": {
			netDevClass:   2,
			hostResponses: dualHostRespSame(fabIfs1),
			expErr:        errors.New("unsupported net dev class in request"),
		},
		"zero min pmem and zero numa": {
			hostResponses: dualHostRespSame(fabIfs1),
			expErr:        errors.New("zero numa nodes reported on hosts host[1-2]"),
		},
		"zero min pmem and dual numa": {
			hostResponses: dualHostRespSame(fabIfs1wNuma),
			expErr:        errors.New("insufficient matching best-available network"),
		},
		"zero min pmem and dual numa but only single interface": {
			hostResponses: dualHostRespSame(fabIfs3),
			expErr:        errors.New("insufficient matching best-available network"),
		},
		"one min pmem and dual numa but only single interface": {
			numPmem:         1,
			hostResponses:   dualHostRespSame(fabIfs3),
			expIfs:          []*HostFabricInterface{ib0},
			expCoresPerNuma: 24,
		},
		"one min pmem and dual numa but only single interface select ethernet": {
			numPmem:         1,
			netDevClass:     nd.Ether,
			hostResponses:   dualHostRespSame(fabIfs3),
			expIfs:          []*HostFabricInterface{eth0},
			expCoresPerNuma: 24,
		},
		"zero min pmem and dual numa with dual ib interfaces": {
			hostResponses:   dualHostRespSame(fabIfs4),
			expIfs:          []*HostFabricInterface{ib0, ib1},
			expCoresPerNuma: 24,
		},
		"dual ib interfaces but ethernet selected": {
			netDevClass:   nd.Ether,
			hostResponses: dualHostRespSame(fabIfs4),
			expErr: errors.Errorf(errInsufNumIfaces, nd.DevClassName(nd.Ether), 2, 0,
				[]*HostFabricInterface{}),
		},
		"zero min pmem and dual numa with dual eth interfaces": {
			hostResponses:   dualHostRespSame(fabIfs5),
			expIfs:          []*HostFabricInterface{eth0, eth1},
			expCoresPerNuma: 24,
		},
		"dual eth interfaces but infiniband selected": {
			netDevClass:   nd.Infiniband,
			hostResponses: dualHostRespSame(fabIfs5),
			expErr: errors.Errorf(errInsufNumIfaces, nd.DevClassName(nd.Infiniband), 2, 0,
				[]*HostFabricInterface{}),
		},
		"four min pmem and dual numa with dual ib interfaces": {
			numPmem:       4,
			hostResponses: dualHostRespSame(fabIfs4),
			expErr: errors.Errorf(errInsufNumIfaces, "best-available", 4, 2,
				[]*HostFabricInterface{ib0, ib1}),
		},
		"zero min pmem and dual numa with typical fabric scan output and access points": {
			accessPoints:    []string{"hostX"},
			hostResponses:   dualHostRespSame(typicalFabIfs),
			expIfs:          []*HostFabricInterface{ib0, ib1},
			expCoresPerNuma: 24,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError: tc.uErr,
				UnaryResponse: &UnaryResponse{
					Responses: tc.hostResponses,
				},
			})

			if tc.netDevClass == 0 {
				tc.netDevClass = NetDevAny
			}
			req := ConfigGenerateReq{
				NumPmem:      tc.numPmem,
				NetClass:     tc.netDevClass,
				AccessPoints: tc.accessPoints,
				Client:       mi,
				Log:          log,
			}

			gotIfs, gotNetSet, gotHostErrs, gotErr := getNetworkParams(context.TODO(),
				req)
			common.CmpErr(t, tc.expErr, gotErr)
			cmpHostErrs(t, tc.expHostErrs, gotHostErrs)
			if tc.expErr != nil {
				return
			}
			if tc.expHostErrs != nil || gotHostErrs != nil {
				t.Fatal("expected or received host errors without outer error")
			}

			if diff := cmp.Diff(tc.expIfs, gotIfs); diff != "" {
				t.Fatalf("unexpected interfaces (-want, +got):\n%s\n", diff)
			}
			common.AssertEqual(t, tc.expCoresPerNuma,
				int(gotNetSet.HostFabric.CoresPerNuma), "unexpected numa cores")
		})
	}
}

func TestControl_AutoConfig_getStorageParams(t *testing.T) {
	dualHostResp := func(r1, r2 string) []*HostResponse {
		return []*HostResponse{
			{
				Addr:    "host1",
				Message: MockServerScanResp(t, r1),
			},
			{
				Addr:    "host2",
				Message: MockServerScanResp(t, r2),
			},
		}
	}
	dualHostRespSame := func(r1 string) []*HostResponse {
		return dualHostResp(r1, r1)
	}
	hostRespOneScanFail := dualHostResp("standard", "bothFailed")
	hostRespScanFail := dualHostRespSame("bothFailed")
	hostRespNoScmNs := dualHostRespSame("standard")
	hostRespOneWithScmNs := dualHostResp("withNamespace", "standard")
	hostRespWithScmNs := dualHostRespSame("withNamespace")
	hostRespWithScmNss := dualHostRespSame("withNamespaces")
	hostRespWithScmNssNumaZero := dualHostRespSame("withNamespacesNumaZero")
	hostRespWithSingleSSD := dualHostRespSame("withSingleSSD")
	hostRespWithSSDs := dualHostRespSame("withSpaceUsage")

	for name, tc := range map[string]struct {
		numPmem       int
		minNvme       int
		reqNoNvme     bool
		uErr          error
		hostResponses []*HostResponse
		expErr        error
		expPmems      []string
		expBdevLists  [][]string
		expHostErrs   []*MockHostError
	}{
		"invoker error": {
			uErr:          errors.New("unary error"),
			hostResponses: hostRespOneWithScmNs,
			expErr:        errors.New("unary error"),
		},
		"host storage scan failed": {
			hostResponses: hostRespOneScanFail,
			expHostErrs: []*MockHostError{
				{"host2", "scm scan failed"},
				{"host2", "nvme scan failed"},
			},
			expErr: errors.New("1 host had errors"),
		},
		"host storage scan failed on multiple hosts": {
			hostResponses: hostRespScanFail,
			expHostErrs: []*MockHostError{
				{"host1", "scm scan failed"},
				{"host1", "nvme scan failed"},
				{"host2", "scm scan failed"},
				{"host2", "nvme scan failed"},
			},
			expErr: errors.New("2 hosts had errors"),
		},
		"host storage scan no hosts": {
			hostResponses: []*HostResponse{},
			expErr:        errors.New("no host responses"),
		},
		"host storage mismatch": {
			hostResponses: hostRespOneWithScmNs,
			expErr:        errors.New("storage hardware not consistent across hosts"),
		},
		"zero min pmem and dual numa and zero pmems present": {
			numPmem:       2,
			hostResponses: hostRespNoScmNs,
			expErr:        errors.Errorf(errInsufNumPmem, "[]", 2, 0),
		},
		"dual min pmem and single pmems present": {
			numPmem:       2,
			hostResponses: hostRespWithScmNs,
			expErr:        errors.Errorf(errInsufNumPmem, "[/dev/pmem0]", 2, 1),
		},
		"dual min pmem and dual pmems present": {
			numPmem:       2,
			hostResponses: hostRespWithScmNss,
			expErr:        errors.Errorf(errInsufNumNvme, 0, 1, 0),
		},
		"one min pmem and dual pmems present both numa zero": {
			numPmem:       1,
			hostResponses: hostRespWithScmNssNumaZero,
			expErr:        errors.Errorf(errInsufNumNvme, 0, 1, 0),
		},
		"dual min pmem and dual pmems present both numa zero": {
			numPmem:       2,
			hostResponses: hostRespWithScmNssNumaZero,
			expErr:        errors.New("bound to unexpected numa"),
		},
		"zero min nvme and single ctrlr present on single numa node": {
			numPmem:       2,
			hostResponses: hostRespWithSingleSSD,
			expErr:        errors.Errorf(errInsufNumNvme, 1, 1, 0),
		},
		"zero min nvme and multiple ctrlrs present on dual numa nodes": {
			numPmem:       2,
			hostResponses: hostRespWithSSDs,
			expPmems: []string{
				ioCfgWithSSDs(t, 0).Storage.SCM.DeviceList[0],
				ioCfgWithSSDs(t, 1).Storage.SCM.DeviceList[0],
			},
			expBdevLists: [][]string{
				ioCfgWithSSDs(t, 0).Storage.Bdev.DeviceList,
				ioCfgWithSSDs(t, 1).Storage.Bdev.DeviceList,
			},
		},
		"dual min nvme and multiple ctrlrs present on dual numa nodes": {
			numPmem:       2,
			minNvme:       2,
			hostResponses: hostRespWithSSDs,
			expPmems: []string{
				ioCfgWithSSDs(t, 0).Storage.SCM.DeviceList[0],
				ioCfgWithSSDs(t, 1).Storage.SCM.DeviceList[0],
			},
			expBdevLists: [][]string{
				ioCfgWithSSDs(t, 0).Storage.Bdev.DeviceList,
				ioCfgWithSSDs(t, 1).Storage.Bdev.DeviceList,
			},
		},
		"zero nvme and multiple ctrlrs present on dual numa nodes": {
			numPmem:       2,
			reqNoNvme:     true,
			hostResponses: hostRespWithSSDs,
			expPmems: []string{
				ioCfgWithSSDs(t, 0).Storage.SCM.DeviceList[0],
				ioCfgWithSSDs(t, 1).Storage.SCM.DeviceList[0],
			},
			expBdevLists: [][]string{nil, nil},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			mi := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError: tc.uErr,
				UnaryResponse: &UnaryResponse{
					Responses: tc.hostResponses,
				},
			})

			if tc.minNvme == 0 {
				tc.minNvme = 1 // default set in dmg cmd caller
			}
			if tc.reqNoNvme {
				tc.minNvme = 0 // user specifically requests no nvme
			}

			req := ConfigGenerateReq{
				NumPmem: tc.numPmem,
				NumNvme: tc.minNvme,
				Client:  mi,
				Log:     log,
			}

			gotPmems, gotBdevLists, gotHostErrs, gotErr := getStorageParams(
				context.TODO(), req, tc.numPmem)
			common.CmpErr(t, tc.expErr, gotErr)
			cmpHostErrs(t, tc.expHostErrs, gotHostErrs)
			if tc.expErr != nil {
				return
			}
			if tc.expHostErrs != nil || gotHostErrs != nil {
				t.Fatal("expected or received host errors without outer error")
			}

			if diff := cmp.Diff(tc.expPmems, gotPmems); diff != "" {
				t.Fatalf("unexpected pmem paths (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expBdevLists, gotBdevLists); diff != "" {
				t.Fatalf("unexpected bdev lists (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_AutoConfig_getCPUParams(t *testing.T) {
	for name, tc := range map[string]struct {
		coresPerNuma  int   // physical cores per NUMA node
		bdevListSizes []int // size of pci-address lists, one for each I/O Server
		expTgtCounts  []int // one recommended target count per I/O Server
		expHlprCounts []int // one recommended helper xstream count per I/O Server
		expErr        error
	}{
		"no cores":           {expErr: errors.Errorf(errInvalNumCores, 0)},
		"24 cores no ssds":   {24, []int{0}, []int{16}, []int{7}, nil},
		"24 cores 1 ssds":    {24, []int{1}, []int{23}, []int{0}, nil},
		"24 cores 2 ssds":    {24, []int{2}, []int{22}, []int{1}, nil},
		"24 cores 3 ssds":    {24, []int{3}, []int{21}, []int{2}, nil},
		"24 cores 4 ssds":    {24, []int{4}, []int{20}, []int{3}, nil},
		"24 cores 5 ssds":    {24, []int{5}, []int{20}, []int{3}, nil},
		"24 cores 8 ssds":    {24, []int{8}, []int{16}, []int{7}, nil},
		"24 cores 9 ssds":    {24, []int{9}, []int{18}, []int{5}, nil},
		"24 cores 10 ssds":   {24, []int{10}, []int{20}, []int{3}, nil},
		"24 cores 16 ssds":   {24, []int{16}, []int{16}, []int{7}, nil},
		"18 cores no ssds":   {18, []int{0}, []int{16}, []int{1}, nil},
		"18 cores 1 ssds":    {18, []int{1}, []int{17}, []int{0}, nil},
		"18 cores 2 ssds":    {18, []int{2}, []int{16}, []int{1}, nil},
		"18 cores 3 ssds":    {18, []int{3}, []int{15}, []int{2}, nil},
		"18 cores 4 ssds":    {18, []int{4}, []int{16}, []int{1}, nil},
		"18 cores 5 ssds":    {18, []int{5}, []int{15}, []int{2}, nil},
		"18 cores 8 ssds":    {18, []int{8}, []int{16}, []int{1}, nil},
		"18 cores 9 ssds":    {18, []int{9}, []int{9}, []int{8}, nil},
		"18 cores 10 ssds":   {18, []int{10}, []int{10}, []int{7}, nil},
		"18 cores 16 ssds":   {18, []int{16}, []int{16}, []int{1}, nil},
		"16 cores no ssds":   {16, []int{0}, []int{15}, []int{0}, nil},
		"16 cores 1 ssds":    {16, []int{1}, []int{15}, []int{0}, nil},
		"16 cores 2 ssds":    {16, []int{2}, []int{14}, []int{1}, nil},
		"16 cores 3 ssds":    {16, []int{3}, []int{15}, []int{0}, nil},
		"16 cores 4 ssds":    {16, []int{4}, []int{12}, []int{3}, nil},
		"16 cores 5 ssds":    {16, []int{5}, []int{15}, []int{0}, nil},
		"16 cores 6 ssds":    {16, []int{6}, []int{12}, []int{3}, nil},
		"16 cores 7 ssds":    {16, []int{7}, []int{14}, []int{1}, nil},
		"16 cores 8 ssds":    {16, []int{8}, []int{8}, []int{7}, nil},
		"16 cores 9 ssds":    {16, []int{9}, []int{9}, []int{6}, nil},
		"16 cores 10 ssds":   {16, []int{10}, []int{10}, []int{5}, nil},
		"16 cores 16 ssds":   {16, []int{16}, []int{16}, []int{1}, errors.New("need more")},
		"32 cores 8:12 ssds": {32, []int{8, 12}, []int{24, 24}, []int{7, 7}, nil},
		"64 cores 8:8 ssds":  {64, []int{8, 8}, []int{56, 56}, []int{7, 7}, nil},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			bdevLists := make([][]string, len(tc.bdevListSizes))
			for idx, count := range tc.bdevListSizes {
				bdevLists[idx] = common.MockPCIAddrs(count)
			}

			gotTgtCounts, gotHlprCounts, gotErr := getCPUParams(log, bdevLists,
				tc.coresPerNuma)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expTgtCounts, gotTgtCounts); diff != "" {
				t.Fatalf("unexpected target counts (-want, +got):\n%s\n", diff)
			}
			if diff := cmp.Diff(tc.expHlprCounts, gotHlprCounts); diff != "" {
				t.Fatalf("unexpected helper counts (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_AutoConfig_genConfig(t *testing.T) {
	baseConfig := func(provider string) *config.Server {
		return config.DefaultServer().
			WithControlLogFile(defaultControlLogFile).
			WithFabricProvider(provider)
	}
	numa0 := uint(0)
	numa1 := uint(1)

	for name, tc := range map[string]struct {
		accessPoints []string               // list of access point host/ip addresses
		pmemPaths    []string               // one pmem block device per I/O Server
		ifaces       []*HostFabricInterface // one hfi per I/O Server
		bdevLists    [][]string             // one list of pci addresses per I/O Server
		tgtCounts    []int                  // one target count per I/O Server
		hlprCounts   []int                  // one helper xstream count per I/O Server
		expCfg       *config.Server         // expected config generated
		expErr       error
	}{
		"empty inputs": {
			expErr: errors.Errorf(errInvalNumPmem, 1, 0),
		},
		"single pmem zero interfaces": {
			pmemPaths: []string{"/dev/pmem0"},
			expErr:    errors.Errorf(errInsufNumIfaces, "", 1, 0, "[]"),
		},
		"single pmem zero target counts": {
			pmemPaths: []string{"/dev/pmem0"},
			ifaces:    []*HostFabricInterface{ib0},
			expErr:    errors.Errorf(errInvalNumTgtCounts, 1, 0),
		},
		"single pmem zero bdev lists": {
			pmemPaths:  []string{"/dev/pmem0"},
			ifaces:     []*HostFabricInterface{ib0},
			tgtCounts:  []int{16},
			hlprCounts: []int{7},
			expErr:     errors.New("programming error"),
		},
		"single pmem zero nvme with access point": {
			accessPoints: []string{"hostX"},
			pmemPaths:    []string{"/dev/pmem0"},
			ifaces:       []*HostFabricInterface{ib0},
			bdevLists:    [][]string{nil},
			tgtCounts:    []int{16},
			hlprCounts:   []int{7},
			expCfg: baseConfig("ofi+psm2").WithAccessPoints("hostX").WithServers(
				defaultIOSrvCfg(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0).
					WithScmDeviceList("/dev/pmem0").
					WithScmMountPoint("/mnt/daos0").
					WithHelperStreamCount(7)),
		},
		"single pmem single nvme": {
			pmemPaths:  []string{"/dev/pmem0"},
			ifaces:     []*HostFabricInterface{ib0},
			bdevLists:  [][]string{common.MockPCIAddrs(1)},
			tgtCounts:  []int{16},
			hlprCounts: []int{7},
			expCfg: baseConfig("ofi+psm2").WithServers(
				defaultIOSrvCfg(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0).
					WithScmDeviceList("/dev/pmem0").
					WithScmMountPoint("/mnt/daos0").
					WithBdevDeviceList(common.MockPCIAddr(1)).
					WithHelperStreamCount(7)),
		},
		"dual pmem dual nvme": {
			pmemPaths:  []string{"/dev/pmem0", "/dev/pmem1"},
			ifaces:     []*HostFabricInterface{ib0, ib1},
			bdevLists:  [][]string{common.MockPCIAddrs(4), common.MockPCIAddrs(3)},
			tgtCounts:  []int{16, 15},
			hlprCounts: []int{7, 6},
			expCfg: baseConfig("ofi+psm2").WithServers(
				defaultIOSrvCfg(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0).
					WithScmDeviceList("/dev/pmem0").
					WithScmMountPoint("/mnt/daos0").
					WithBdevDeviceList(common.MockPCIAddrs(4)...).
					WithHelperStreamCount(7),
				defaultIOSrvCfg(1).
					WithFabricInterface("ib1").
					WithFabricInterfacePort(
						int(defaultFiPort+defaultFiPortInterval)).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa1).
					WithScmDeviceList("/dev/pmem1").
					WithScmMountPoint("/mnt/daos1").
					WithBdevDeviceList(common.MockPCIAddrs(3)...).
					WithTargetCount(15).
					WithHelperStreamCount(6)),
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			gotCfg, gotErr := genConfig(tc.accessPoints, tc.pmemPaths, tc.ifaces,
				tc.bdevLists, tc.tgtCounts, tc.hlprCounts)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(security.CertificateConfig{},
					config.Server{}),
				cmpopts.IgnoreFields(config.Server{}, "GetDeviceClassFn"),
			}
			cmpOpts = append(cmpOpts, defResCmpOpts()...)

			if diff := cmp.Diff(tc.expCfg, gotCfg, cmpOpts...); diff != "" {
				t.Fatalf("unexpected output config (-want, +got):\n%s\n", diff)
			}
		})
	}
}
