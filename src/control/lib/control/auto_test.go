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
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var (
	engineCfg = func(t *testing.T, numa int) *engine.Config {
		return engine.NewConfig().
			WithScmClass(storage.ScmClassDCPM.String()).
			WithScmMountPoint(fmt.Sprintf("/mnt/daos%d", numa)).
			WithScmDeviceList(fmt.Sprintf("/dev/pmem%d", numa)).
			WithBdevClass(storage.BdevClassNvme.String())
	}
	engineCfgWithSSDs = func(t *testing.T, numa int) *engine.Config {
		var pciAddrs []string
		for _, c := range MockServerScanResp(t, "withSpaceUsage").Nvme.Ctrlrs {
			if int(c.SocketId) == numa {
				pciAddrs = append(pciAddrs, c.PciAddr)
			}
		}

		return engineCfg(t, numa).WithBdevDeviceList(pciAddrs...)
	}
	ib0 = &HostFabricInterface{
		Provider: "ofi+psm2", Device: "ib0", NumaNode: 0, NetDevClass: 32, Priority: 0,
	}
	ib1 = &HostFabricInterface{
		Provider: "ofi+psm2", Device: "ib1", NumaNode: 1, NetDevClass: 32, Priority: 1,
	}
	ib0r = &HostFabricInterface{
		Provider: "ofi+psm2", Device: "ib0", NumaNode: 0, NetDevClass: 32, Priority: 1,
	}
	ib1r = &HostFabricInterface{
		Provider: "ofi+psm2", Device: "ib1", NumaNode: 1, NetDevClass: 32, Priority: 0,
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

func TestControl_AutoConfig_getNetworkDetails(t *testing.T) {
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
		{Provider: "ofi+psm2", Device: "ib1", Numanode: 1, Priority: 0, Netdevclass: 32},
		{Provider: "ofi+psm2", Device: "ib0", Numanode: 0, Priority: 1, Netdevclass: 32},
		{Provider: "ofi+verbs;ofi_rxm", Device: "ib1", Numanode: 1, Priority: 2, Netdevclass: 32},
		{Provider: "ofi+verbs;ofi_rxm", Device: "ib0", Numanode: 0, Priority: 3, Netdevclass: 32},
		{Provider: "ofi+verbs;ofi_rxm", Device: "eth0", Numanode: 0, Priority: 4, Netdevclass: 1},
		{Provider: "ofi+tcp;ofi_rxm", Device: "ib1", Numanode: 1, Priority: 5, Netdevclass: 32},
		{Provider: "ofi+tcp;ofi_rxm", Device: "ib0", Numanode: 0, Priority: 6, Netdevclass: 32},
		{Provider: "ofi+tcp;ofi_rxm", Device: "eth0", Numanode: 0, Priority: 7, Netdevclass: 1},
		{Provider: "ofi+verbs", Device: "ib1", Numanode: 1, Priority: 8, Netdevclass: 32},
		{Provider: "ofi+verbs", Device: "ib0", Numanode: 0, Priority: 9, Netdevclass: 32},
		{Provider: "ofi+tcp", Device: "ib1", Numanode: 1, Priority: 10, Netdevclass: 32},
		{Provider: "ofi+tcp", Device: "ib0", Numanode: 0, Priority: 11, Netdevclass: 32},
		{Provider: "ofi+tcp", Device: "eth0", Numanode: 0, Priority: 12, Netdevclass: 1},
		{Provider: "ofi+sockets", Device: "ib1", Numanode: 1, Priority: 13, Netdevclass: 32},
		{Provider: "ofi+sockets", Device: "ib0", Numanode: 0, Priority: 14, Netdevclass: 32},
		{Provider: "ofi+sockets", Device: "eth0", Numanode: 0, Priority: 15, Netdevclass: 1},
	}
	typicalFabIfs := &ctlpb.NetworkScanResp{Interfaces: typIfs, Numacount: 2, Corespernuma: 24}
	sinIbIfs := []*ctlpb.FabricInterface{
		{Provider: "ofi+psm2", Device: "ib0", Numanode: 0, Priority: 0, Netdevclass: 32},
		{Provider: "ofi+sockets", Device: "ib0", Numanode: 0, Priority: 1, Netdevclass: 32},
		{Provider: "ofi+sockets", Device: "eth0", Numanode: 0, Priority: 2, Netdevclass: 1},
		{Provider: "ofi+sockets", Device: "eth1", Numanode: 1, Priority: 3, Netdevclass: 1},
	}
	sinIbFabIfs := &ctlpb.NetworkScanResp{Interfaces: sinIbIfs, Numacount: 2, Corespernuma: 24}
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
		engineCount     int
		netDevClass     uint32
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
		"engine count unset and zero numa on single host": {
			hostResponses: dualHostResp(fabIfs1, fabIfs1wNuma),
			expErr:        errors.New("network hardware not consistent across hosts"),
		},
		"engine count unset and zero numa": {
			hostResponses: dualHostRespSame(fabIfs1),
			expErr:        errors.New("zero numa nodes reported on hosts host[1-2]"),
		},
		"unsupported network class in request": {
			netDevClass:   2,
			hostResponses: dualHostRespSame(fabIfs1wNuma),
			expErr:        errors.New("unsupported net dev class in request"),
		},
		"engine count unset and dual numa": {
			hostResponses: dualHostRespSame(fabIfs1wNuma),
			expErr:        errors.New("insufficient matching best-available network"),
		},
		"engine count unset and dual numa but only single interface": {
			hostResponses: dualHostRespSame(fabIfs3),
			expErr:        errors.New("insufficient matching best-available network"),
		},
		"single engine set and single interface": {
			engineCount:     1,
			hostResponses:   dualHostRespSame(fabIfs3),
			expIfs:          []*HostFabricInterface{ib0},
			expCoresPerNuma: 24,
		},
		"single engine set and single interface select ethernet": {
			engineCount:     1,
			netDevClass:     nd.Ether,
			hostResponses:   dualHostRespSame(fabIfs3),
			expIfs:          []*HostFabricInterface{eth0},
			expCoresPerNuma: 24,
		},
		"single engine set with dual ib interfaces": {
			engineCount:     1,
			hostResponses:   dualHostRespSame(fabIfs4),
			expIfs:          []*HostFabricInterface{ib0},
			expCoresPerNuma: 24,
		},
		"engine count unset and dual numa with dual ib interfaces": {
			hostResponses:   dualHostRespSame(fabIfs4),
			expIfs:          []*HostFabricInterface{ib0, ib1},
			expCoresPerNuma: 24,
		},
		"engine count unset and dual numa with dual ib interfaces but ethernet selected": {
			netDevClass:   nd.Ether,
			hostResponses: dualHostRespSame(fabIfs4),
			expErr: errors.Errorf(errInsufNrIfaces, nd.DevClassName(nd.Ether), 2, 0,
				make(numaNetIfaceMap)),
		},
		"engine count unset and dual numa with dual eth interfaces": {
			hostResponses:   dualHostRespSame(fabIfs5),
			expIfs:          []*HostFabricInterface{eth0, eth1},
			expCoresPerNuma: 24,
		},
		"engine count unset and dual numa with dual eth interfaces but infiniband selected": {
			netDevClass:   nd.Infiniband,
			hostResponses: dualHostRespSame(fabIfs5),
			expErr: errors.Errorf(errInsufNrIfaces, nd.DevClassName(nd.Infiniband), 2, 0,
				make(numaNetIfaceMap)),
		},
		"multiple engines set with dual ib interfaces": {
			engineCount:   4,
			hostResponses: dualHostRespSame(fabIfs4),
			expErr: errors.Errorf(errInsufNrIfaces, "best-available", 4, 2,
				numaNetIfaceMap{0: ib0, 1: ib1}),
		},
		"single engine with typical fabric scan output": {
			engineCount:     1,
			hostResponses:   dualHostRespSame(typicalFabIfs),
			expIfs:          []*HostFabricInterface{ib0r, ib1r},
			expCoresPerNuma: 24,
		},
		"engine count unset and dual numa with typical fabric scan output": {
			hostResponses:   dualHostRespSame(typicalFabIfs),
			expIfs:          []*HostFabricInterface{ib0r, ib1r},
			expCoresPerNuma: 24,
		},
		"dual engine single ib dual eth": {
			hostResponses:   dualHostRespSame(sinIbFabIfs),
			expIfs:          []*HostFabricInterface{eth0, eth1},
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
				NrEngines: tc.engineCount,
				NetClass:  tc.netDevClass,
				Client:    mi,
				Log:       log,
			}

			netDetails, gotHostErrs, gotErr := getNetworkDetails(context.TODO(), req)
			common.CmpErr(t, tc.expErr, gotErr)
			cmpHostErrs(t, tc.expHostErrs, gotHostErrs)
			if tc.expErr != nil {
				return
			}
			if tc.expHostErrs != nil || gotHostErrs != nil {
				t.Fatal("expected or received host errors without outer error")
			}

			common.AssertEqual(t, len(tc.expIfs), len(netDetails.numaIfaces),
				"unexpected number of network interfaces")
			for nn, iface := range netDetails.numaIfaces {
				if diff := cmp.Diff(tc.expIfs[nn], iface); diff != "" {
					t.Fatalf("unexpected interfaces (-want, +got):\n%s\n", diff)
				}
			}
			common.AssertEqual(t, tc.expCoresPerNuma, netDetails.numaCoreCount,
				"unexpected numa cores")
		})
	}
}

func TestControl_AutoConfig_getStorageDetails(t *testing.T) {
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
	hostRespOneWithScmNs := dualHostResp("pmemSingle", "standard")
	hostRespWithScmNs := dualHostRespSame("pmemSingle")
	hostRespWithScmNss := dualHostRespSame("pmemA")
	hostRespWithScmNssNumaZero := dualHostRespSame("pmemDupNuma")
	hostRespWithSingleSSD := dualHostRespSame("nvmeSingle")
	hostRespWithSSDs := dualHostRespSame("withSpaceUsage")

	for name, tc := range map[string]struct {
		engineCount   int
		minSSDs       int
		disableNVMe   bool
		uErr          error
		hostResponses []*HostResponse
		expErr        error
		expPMems      [][]string
		expSSDs       [][]string
		expHostErrs   []*MockHostError
	}{
		"zero engines": {
			expErr: errors.Errorf(errInvalNrEngines, 1, 0),
		},
		"invoker error": {
			engineCount:   1,
			uErr:          errors.New("unary error"),
			hostResponses: hostRespOneWithScmNs,
			expErr:        errors.New("unary error"),
		},
		"host storage scan failed": {
			engineCount:   1,
			hostResponses: hostRespOneScanFail,
			expHostErrs: []*MockHostError{
				{"host2", "scm scan failed"},
				{"host2", "nvme scan failed"},
			},
			expErr: errors.New("1 host had errors"),
		},
		"host storage scan failed on multiple hosts": {
			engineCount:   1,
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
			engineCount:   1,
			hostResponses: []*HostResponse{},
			expErr:        errors.New("no host responses"),
		},
		"host storage mismatch": {
			engineCount:   1,
			hostResponses: hostRespOneWithScmNs,
			expErr:        errors.New("storage hardware not consistent across hosts"),
		},
		"single engine zero pmems": {
			engineCount:   1,
			hostResponses: hostRespNoScmNs,
			expErr:        errors.Errorf(errInsufNrPMemGroups, make(numaPMemsMap), 1, 0),
		},
		"dual engine single pmem": {
			engineCount:   2,
			hostResponses: hostRespWithScmNs,
			expErr: errors.Errorf(errInsufNrPMemGroups,
				numaPMemsMap{0: []string{
					engineCfgWithSSDs(t, 0).Storage.SCM.DeviceList[0],
				}}, 2, 1),
		},
		"dual engine dual pmems zero ssds": {
			engineCount:   2,
			hostResponses: hostRespWithScmNss,
			expErr:        errors.Errorf(errInsufNrSSDs, 0, 1, 0),
		},
		"single engine dual pmems both numa zero": {
			engineCount:   1,
			hostResponses: hostRespWithScmNssNumaZero,
			expErr:        errors.Errorf(errInsufNrSSDs, 0, 1, 0),
		},
		"dual engine dual pmems both numa zero": {
			engineCount:   2,
			hostResponses: hostRespWithScmNssNumaZero,
			expErr: errors.Errorf(errInsufNrPMemGroups,
				numaPMemsMap{0: []string{
					engineCfgWithSSDs(t, 0).Storage.SCM.DeviceList[0],
					engineCfgWithSSDs(t, 1).Storage.SCM.DeviceList[0],
				}}, 2, 1),
		},
		"single min ssd single ctrlr on single numa node": {
			engineCount:   2,
			hostResponses: hostRespWithSingleSSD,
			expErr:        errors.Errorf(errInsufNrSSDs, 1, 1, 0),
		},
		"single min ssd multiple ctrlrs on dual numa nodes": {
			engineCount:   2,
			hostResponses: hostRespWithSSDs,
			expPMems: [][]string{
				engineCfgWithSSDs(t, 0).Storage.SCM.DeviceList,
				engineCfgWithSSDs(t, 1).Storage.SCM.DeviceList,
			},
			expSSDs: [][]string{
				engineCfgWithSSDs(t, 0).Storage.Bdev.DeviceList,
				engineCfgWithSSDs(t, 1).Storage.Bdev.DeviceList,
			},
		},
		"dual min ssd multiple ctrlrs on dual numa nodes": {
			engineCount:   2,
			minSSDs:       2,
			hostResponses: hostRespWithSSDs,
			expPMems: [][]string{
				engineCfgWithSSDs(t, 0).Storage.SCM.DeviceList,
				engineCfgWithSSDs(t, 1).Storage.SCM.DeviceList,
			},
			expSSDs: [][]string{
				engineCfgWithSSDs(t, 0).Storage.Bdev.DeviceList,
				engineCfgWithSSDs(t, 1).Storage.Bdev.DeviceList,
			},
		},
		"zero min ssd multiple ctrlrs on dual numa nodes": {
			engineCount:   2,
			disableNVMe:   true,
			hostResponses: hostRespWithSSDs,
			expPMems: [][]string{
				engineCfgWithSSDs(t, 0).Storage.SCM.DeviceList,
				engineCfgWithSSDs(t, 1).Storage.SCM.DeviceList,
			},
			expSSDs: [][]string{{}, {}},
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

			if tc.minSSDs == 0 {
				tc.minSSDs = 1 // default set in dmg cmd caller
			}
			if tc.disableNVMe {
				tc.minSSDs = 0 // user specifically requests no ssd
			}

			req := ConfigGenerateReq{
				NrEngines: tc.engineCount,
				MinNrSSDs: tc.minSSDs,
				Client:    mi,
				Log:       log,
			}

			storageDetails, gotHostErrs, gotErr := getStorageDetails(
				context.TODO(), req, tc.engineCount)
			common.CmpErr(t, tc.expErr, gotErr)
			cmpHostErrs(t, tc.expHostErrs, gotHostErrs)
			if tc.expErr != nil {
				return
			}
			if tc.expHostErrs != nil || gotHostErrs != nil {
				t.Fatal("expected or received host errors without outer error")
			}

			common.AssertEqual(t, len(tc.expPMems), len(storageDetails.numaPMems),
				"unexpected number of pmem devices")
			for nn, pmems := range storageDetails.numaPMems {
				if diff := cmp.Diff(tc.expPMems[nn], []string(pmems)); diff != "" {
					t.Fatalf("unexpected pmem paths (-want, +got):\n%s\n", diff)
				}
			}

			common.AssertEqual(t, len(tc.expSSDs), len(storageDetails.numaSSDs),
				"unexpected number of ssds")
			for nn, ssds := range storageDetails.numaSSDs {
				if diff := cmp.Diff(tc.expSSDs[nn], []string(ssds)); diff != "" {
					t.Fatalf("unexpected list of ssds (-want, +got):\n%s\n", diff)
				}
			}
		})
	}
}

func TestControl_AutoConfig_getCPUDetails(t *testing.T) {
	for name, tc := range map[string]struct {
		numaCoreCount int   // physical cores per NUMA node
		ssdListSizes  []int // size of pci-address lists, one for each I/O Engine
		expTgtCounts  []int // one recommended target count per I/O Engine
		expHlprCounts []int // one recommended helper xstream count per I/O Engine
		expErr        error
	}{
		"no cores":           {expErr: errors.Errorf(errInvalNrCores, 0)},
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

			numaSSDs := make(numaSSDsMap)
			for nn, count := range tc.ssdListSizes {
				for i := 0; i < count; i++ {
					numaSSDs[nn] = append(numaSSDs[nn], common.MockPCIAddr(int32(i)))
				}
			}

			nccs, gotErr := getCPUDetails(log, numaSSDs, tc.numaCoreCount)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			for nn := range numaSSDs {
				if diff := cmp.Diff(tc.expTgtCounts[nn], nccs[nn].nrTgts); diff != "" {
					t.Fatalf("unexpected target counts (-want, +got):\n%s\n", diff)
				}
				if diff := cmp.Diff(tc.expHlprCounts[nn], nccs[nn].nrHlprs); diff != "" {
					t.Fatalf("unexpected helper counts (-want, +got):\n%s\n", diff)
				}
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
		engineCount    int               // number of engines to provide in config
		accessPoints   []string          // list of access point host/ip addresses
		numaPMems      numaPMemsMap      // numa to pmem mappings
		numaSSDs       numaSSDsMap       // numa to ssds mappings
		numaIfaces     numaNetIfaceMap   // numa to network interface mappings
		numaCoreCounts numaCoreCountsMap // numa to cpu mappings
		expCfg         *config.Server    // expected config generated
		expErr         error
	}{
		"no engines": {
			numaPMems:    numaPMemsMap{0: []string{"/dev/pmem0"}},
			accessPoints: []string{"hostX:10002"},
			expErr:       errors.Errorf(errInvalNrEngines, 1, 0),
		},
		"single pmem zero ssd with access point": {
			engineCount:    1,
			accessPoints:   []string{"hostX"},
			numaPMems:      numaPMemsMap{0: []string{"/dev/pmem0"}},
			numaIfaces:     numaNetIfaceMap{0: ib0},
			numaSSDs:       numaSSDsMap{0: []string{}},
			numaCoreCounts: numaCoreCountsMap{0: &coreCounts{16, 7}},
			expCfg: baseConfig("ofi+psm2").WithAccessPoints("hostX:10001").WithEngines(
				defaultEngineCfg(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0).
					WithScmDeviceList("/dev/pmem0").
					WithScmMountPoint("/mnt/daos0").
					WithHelperStreamCount(7)),
		},
		"access point with valid port": {
			engineCount:    1,
			accessPoints:   []string{"hostX:10002"},
			numaPMems:      numaPMemsMap{0: []string{"/dev/pmem0"}},
			numaIfaces:     numaNetIfaceMap{0: ib0},
			numaSSDs:       numaSSDsMap{0: []string{}},
			numaCoreCounts: numaCoreCountsMap{0: &coreCounts{16, 7}},
			expCfg: baseConfig("ofi+psm2").WithAccessPoints("hostX:10002").WithEngines(
				defaultEngineCfg(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0).
					WithScmDeviceList("/dev/pmem0").
					WithScmMountPoint("/mnt/daos0").
					WithHelperStreamCount(7)),
		},
		"access point with invalid port": {
			engineCount:    1,
			accessPoints:   []string{"hostX:-10001"},
			numaPMems:      numaPMemsMap{0: []string{"/dev/pmem0"}},
			numaIfaces:     numaNetIfaceMap{0: ib0},
			numaSSDs:       numaSSDsMap{0: []string{}},
			numaCoreCounts: numaCoreCountsMap{0: &coreCounts{16, 7}},
			expErr:         config.FaultConfigBadControlPort,
		},
		"access point ip with valid port": {
			engineCount:    1,
			accessPoints:   []string{"192.168.1.1:10002"},
			numaPMems:      numaPMemsMap{0: []string{"/dev/pmem0"}},
			numaIfaces:     numaNetIfaceMap{0: ib0},
			numaSSDs:       numaSSDsMap{0: []string{}},
			numaCoreCounts: numaCoreCountsMap{0: &coreCounts{16, 7}},
			expCfg: baseConfig("ofi+psm2").WithAccessPoints("192.168.1.1:10002").WithEngines(
				defaultEngineCfg(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0).
					WithScmDeviceList("/dev/pmem0").
					WithScmMountPoint("/mnt/daos0").
					WithHelperStreamCount(7)),
		},
		"access point ip with invalid port": {
			engineCount:    1,
			accessPoints:   []string{"192.168.1.1:-10001"},
			numaPMems:      numaPMemsMap{0: []string{"/dev/pmem0"}},
			numaIfaces:     numaNetIfaceMap{0: ib0},
			numaSSDs:       numaSSDsMap{0: []string{}},
			numaCoreCounts: numaCoreCountsMap{0: &coreCounts{16, 7}},
			expErr:         config.FaultConfigBadControlPort,
		},
		"single pmem single ssd": {
			engineCount:    1,
			accessPoints:   []string{"hostX:10002"},
			numaPMems:      numaPMemsMap{0: []string{"/dev/pmem0"}},
			numaIfaces:     numaNetIfaceMap{0: ib0},
			numaSSDs:       numaSSDsMap{0: []string{common.MockPCIAddr(1)}},
			numaCoreCounts: numaCoreCountsMap{0: &coreCounts{16, 7}},
			expCfg: baseConfig("ofi+psm2").WithAccessPoints("hostX:10002").WithEngines(
				defaultEngineCfg(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0).
					WithScmDeviceList("/dev/pmem0").
					WithScmMountPoint("/mnt/daos0").
					WithBdevDeviceList(common.MockPCIAddr(1)).
					WithHelperStreamCount(7)),
		},
		"dual pmem dual ssd": {
			engineCount:  2,
			accessPoints: []string{"hostX:10002"},
			numaPMems:    numaPMemsMap{0: []string{"/dev/pmem0"}, 1: []string{"/dev/pmem1"}},
			numaIfaces:   numaNetIfaceMap{0: ib0, 1: ib1},
			numaSSDs: numaSSDsMap{
				0: common.MockPCIAddrs(0, 1, 2, 3), 1: common.MockPCIAddrs(4, 5, 6),
			},
			numaCoreCounts: numaCoreCountsMap{
				0: &coreCounts{16, 7}, 1: &coreCounts{15, 6},
			},
			expCfg: baseConfig("ofi+psm2").WithAccessPoints("hostX:10002").WithEngines(
				defaultEngineCfg(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0).
					WithScmDeviceList("/dev/pmem0").
					WithScmMountPoint("/mnt/daos0").
					WithBdevDeviceList(common.MockPCIAddrs(0, 1, 2, 3)...).
					WithHelperStreamCount(7),
				defaultEngineCfg(1).
					WithFabricInterface("ib1").
					WithFabricInterfacePort(
						int(defaultFiPort+defaultFiPortInterval)).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa1).
					WithScmDeviceList("/dev/pmem1").
					WithScmMountPoint("/mnt/daos1").
					WithBdevDeviceList(common.MockPCIAddrs(4, 5, 6)...).
					WithTargetCount(15).
					WithHelperStreamCount(6)),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			nd := &networkDetails{
				engineCount: tc.engineCount,
				numaIfaces:  tc.numaIfaces,
			}
			sd := &storageDetails{
				numaPMems: tc.numaPMems,
				numaSSDs:  tc.numaSSDs,
			}

			gotCfg, gotErr := genConfig(log, tc.accessPoints, nd, sd, tc.numaCoreCounts)
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
