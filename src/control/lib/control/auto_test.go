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

package control

import (
	"context"
	"fmt"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
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
)

func TestControl_AutoConfig_checkStorage(t *testing.T) {
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
		expHostErrors []*MockHostError
		expConfigOut  *config.Server
		expCheckErr   error
	}{
		"invoker error": {
			uErr:          errors.New("unary error"),
			hostResponses: hostRespOneWithScmNs,
			expCheckErr:   errors.New("unary error"),
		},
		"host storage scan failed": {
			hostResponses: hostRespOneScanFail,
			expHostErrors: []*MockHostError{
				{"host2", "scm scan failed"},
				{"host2", "nvme scan failed"},
			},
			expCheckErr: errors.New("1 host had errors"),
		},
		"host storage scan failed on multiple hosts": {
			hostResponses: hostRespScanFail,
			expHostErrors: []*MockHostError{
				{"host1", "scm scan failed"},
				{"host1", "nvme scan failed"},
				{"host2", "scm scan failed"},
				{"host2", "nvme scan failed"},
			},
			expCheckErr: errors.New("2 hosts had errors"),
		},
		"host storage scan no hosts": {
			hostResponses: []*HostResponse{},
			expCheckErr:   errors.New("no host responses"),
		},
		"host storage mismatch": {
			hostResponses: hostRespOneWithScmNs,
			expCheckErr:   errors.New("storage hardware not consistent across hosts"),
		},
		"no min pmem and 2 numa and 0 pmems present": {
			numPmem:       2,
			hostResponses: hostRespNoScmNs,
			expCheckErr:   errors.Errorf(errInsufNumPmem, 2, 0),
		},
		"2 min pmem and 1 pmems present": {
			numPmem:       2,
			hostResponses: hostRespWithScmNs,
			expCheckErr:   errors.Errorf(errInsufNumPmem, 2, 1),
		},
		"2 min pmem and 2 pmems present": {
			numPmem:       2,
			hostResponses: hostRespWithScmNss,
			expCheckErr:   errors.Errorf(errInsufNumNvme, 0, 1, 0),
		},
		"1 min pmem and 2 pmems present both numa 0": {
			numPmem:       1,
			hostResponses: hostRespWithScmNssNumaZero,
			expCheckErr:   errors.Errorf(errInsufNumNvme, 0, 1, 0),
		},
		"2 min pmem and 2 pmems present both numa 0": {
			numPmem:       2,
			hostResponses: hostRespWithScmNssNumaZero,
			expCheckErr:   errors.New("bound to unexpected numa"),
		},
		"no min nvme and 1 ctrlr present on 1 numa node": {
			numPmem:       2,
			hostResponses: hostRespWithSingleSSD,
			expCheckErr:   errors.Errorf(errInsufNumNvme, 1, 1, 0),
		},
		"no min nvme and multiple ctrlrs present on 2 numa nodes": {
			numPmem:       2,
			hostResponses: hostRespWithSSDs,
			expConfigOut: config.DefaultServer().
				WithServers(ioCfgWithSSDs(t, 0), ioCfgWithSSDs(t, 1)),
		},
		"2 min nvme and multiple ctrlrs present on 2 numa nodes": {
			numPmem:       2,
			minNvme:       2,
			hostResponses: hostRespWithSSDs,
			expConfigOut: config.DefaultServer().
				WithServers(ioCfgWithSSDs(t, 0), ioCfgWithSSDs(t, 1)),
		},
		"0 nvme and multiple ctrlrs present on 2 numa nodes": {
			numPmem:       2,
			reqNoNvme:     true,
			hostResponses: hostRespWithSSDs,
			expConfigOut: config.DefaultServer().
				WithServers(ioCfg(t, 0), ioCfg(t, 1)),
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

			req := &ConfigGenerateReq{
				NumPmem: tc.numPmem,
				NumNvme: tc.minNvme,
				Client:  mi,
				Log:     log,
			}

			// default input config param represents two-socket system
			ioServers := []*ioserver.Config{ioserver.NewConfig()}
			switch tc.numPmem {
			case 0, 1:
			case 2:
				ioServers = append(ioServers, ioserver.NewConfig())
			default:
				t.Fatal("test expecting num-pmem in range 0-2")
			}

			resp := &ConfigGenerateResp{
				ConfigOut: config.DefaultServer().WithServers(ioServers...),
			}

			expResp := &ConfigGenerateResp{
				ConfigOut:      tc.expConfigOut,
				HostErrorsResp: MockHostErrorsResp(t, tc.expHostErrors...),
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(security.CertificateConfig{}, config.Server{}),
				cmpopts.IgnoreFields(config.Server{}, "GetDeviceClassFn"),
			}
			cmpOpts = append(cmpOpts, defResCmpOpts()...)

			gotCheckErr := req.checkStorage(context.Background(), resp)

			if diff := cmp.Diff(expResp.GetHostErrors(), resp.GetHostErrors(), cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}

			common.CmpErr(t, tc.expCheckErr, gotCheckErr)
			if tc.expCheckErr != nil {
				return
			}

			if diff := cmp.Diff(expResp, resp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_AutoConfig_checkNetwork(t *testing.T) {
	if1 := ctlpb.FabricInterface{
		Provider: "test-provider",
		Device:   "test-device",
		Numanode: 42,
	}
	if2 := ctlpb.FabricInterface{
		Provider: "test-provider",
		Device:   "test-device2",
		Numanode: 84,
	}
	ib0 := ctlpb.FabricInterface{
		Provider:    "ofi+psm2",
		Device:      "ib0",
		Numanode:    0,
		Netdevclass: 32,
	}
	ib1 := ctlpb.FabricInterface{
		Provider:    "ofi+psm2",
		Device:      "ib1",
		Numanode:    1,
		Netdevclass: 32,
	}
	eth0 := ctlpb.FabricInterface{
		Provider:    "ofi+sockets",
		Device:      "eth0",
		Numanode:    0,
		Netdevclass: 1,
	}
	eth1 := ctlpb.FabricInterface{
		Provider:    "ofi+sockets",
		Device:      "eth1",
		Numanode:    1,
		Netdevclass: 1,
	}
	fabIfs1 := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{&if1, &if2},
	}
	fabIfs1wNuma := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{&if1, &if2},
		Numacount:  2,
	}
	fabIfs2 := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{&if2},
	}
	fabIfs3 := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{&ib0, &eth0},
		Numacount:  2,
	}
	fabIfs4 := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{&ib0, &ib1},
		Numacount:  2,
	}
	fabIfs5 := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{&eth0, &eth1},
		Numacount:  2,
	}
	hostRespRemoteFail := []*HostResponse{
		{
			Addr:    "host1",
			Message: fabIfs1,
		},
		{
			Addr:    "host2",
			Error:   errors.New("remote failed"),
			Message: fabIfs1,
		},
	}
	hostRespRemoteFails := []*HostResponse{
		{
			Addr:    "host1",
			Error:   errors.New("remote failed"),
			Message: fabIfs1,
		},
		{
			Addr:    "host2",
			Error:   errors.New("remote failed"),
			Message: fabIfs1,
		},
	}
	typicalFabIfs := &ctlpb.NetworkScanResp{
		Interfaces: []*ctlpb.FabricInterface{
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
		},
		Numacount: 2,
	}
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
	numa0 := uint(0)
	numa1 := uint(1)
	baseConfig := func(provider string) *config.Server {
		return config.DefaultServer().
			WithControlLogFile(defaultControlLogFile).
			WithFabricProvider(provider)
	}
	baseIOConfig := func(idx int) *ioserver.Config {
		return ioserver.NewConfig().
			WithTargetCount(defaultTargetCount).
			WithLogFile(fmt.Sprintf("%s.%d.log", defaultIOSrvLogFile, idx))
	}

	for name, tc := range map[string]struct {
		numPmem       int
		netDevClass   uint32
		accessPoints  []string
		uErr          error
		hostResponses []*HostResponse
		expHostErrors []*MockHostError
		expNetErr     error
		expCheckErr   error
		expConfigOut  *config.Server
	}{
		"invoker error": {
			uErr:          errors.New("unary error"),
			hostResponses: dualHostRespSame(fabIfs1),
			expNetErr:     errors.New("unary error"),
		},
		"host network scan failed": {
			hostResponses: hostRespRemoteFail,
			expHostErrors: []*MockHostError{
				{"host2", "remote failed"},
				{"host2", "remote failed"},
			},
			expNetErr: errors.New("1 host had errors"),
		},
		"host network scan failed on multiple hosts": {
			hostResponses: hostRespRemoteFails,
			expHostErrors: []*MockHostError{
				{"host1", "remote failed"},
				{"host2", "remote failed"},
			},
			expNetErr: errors.New("2 hosts had errors"),
		},
		"host network scan no hosts": {
			hostResponses: []*HostResponse{},
			expNetErr:     errors.New("no host responses"),
		},
		"host network mismatch": {
			hostResponses: dualHostResp(fabIfs1, fabIfs2),
			expNetErr:     errors.New("network hardware not consistent across hosts"),
		},
		"no min pmem and no numa on one host": {
			hostResponses: dualHostResp(fabIfs1, fabIfs1wNuma),
			expNetErr:     errors.New("network hardware not consistent across hosts"),
		},
		"unsupported network class in request": {
			netDevClass:   2,
			hostResponses: dualHostRespSame(fabIfs1),
			expCheckErr:   errors.New("unsupported net dev class in request"),
		},
		"no min pmem and no numa": {
			hostResponses: dualHostRespSame(fabIfs1),
			expCheckErr:   errors.New("no numa nodes reported on hosts host[1-2]"),
		},
		"no min pmem and two numa": {
			hostResponses: dualHostRespSame(fabIfs1wNuma),
			expCheckErr:   errors.New("insufficient matching best-available network"),
		},
		"no min pmem and two numa but only single interface": {
			hostResponses: dualHostRespSame(fabIfs3),
			expCheckErr:   errors.New("insufficient matching best-available network"),
		},
		"one min pmem and two numa but only single interface": {
			numPmem:       1,
			hostResponses: dualHostRespSame(fabIfs3),
			expConfigOut: baseConfig("ofi+psm2").WithServers(
				baseIOConfig(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0)),
		},
		"one min pmem and two numa but only single interface select ethernet": {
			numPmem:       1,
			netDevClass:   nd.Ether,
			hostResponses: dualHostRespSame(fabIfs3),
			expConfigOut: baseConfig("ofi+sockets").WithServers(
				baseIOConfig(0).
					WithFabricInterface("eth0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+sockets").
					WithPinnedNumaNode(&numa0)),
		},
		"no min pmem and two numa with dual ib interfaces": {
			hostResponses: dualHostRespSame(fabIfs4),
			expConfigOut: baseConfig("ofi+psm2").WithServers(
				baseIOConfig(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0),
				baseIOConfig(1).
					WithFabricInterface("ib1").
					WithFabricInterfacePort(
						defaultFiPort+defaultFiPortInterval).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa1)),
		},
		"dual ib interfaces but ethernet selected": {
			netDevClass:   nd.Ether,
			hostResponses: dualHostRespSame(fabIfs4),
			expCheckErr:   errors.New("insufficient matching ETHER network"),
		},
		"no min pmem and two numa with dual eth interfaces": {
			hostResponses: dualHostRespSame(fabIfs5),
			expConfigOut: baseConfig("ofi+sockets").WithServers(
				baseIOConfig(0).
					WithFabricInterface("eth0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+sockets").
					WithPinnedNumaNode(&numa0),
				baseIOConfig(1).
					WithFabricInterface("eth1").
					WithFabricInterfacePort(defaultFiPort+defaultFiPortInterval).
					WithFabricProvider("ofi+sockets").
					WithPinnedNumaNode(&numa1)),
		},
		"dual eth interfaces but infiniband selected": {
			netDevClass:   nd.Infiniband,
			hostResponses: dualHostRespSame(fabIfs5),
			expCheckErr:   errors.New("insufficient matching INFINIBAND network"),
		},
		"four min pmem and two numa with dual ib interfaces": {
			numPmem:       4,
			hostResponses: dualHostRespSame(fabIfs4),
			expCheckErr:   errors.New("insufficient matching best-available network"),
		},
		"no min pmem and two numa with typical fabric scan output and access points": {
			accessPoints:  []string{"hostX"},
			hostResponses: dualHostRespSame(typicalFabIfs),
			expConfigOut: baseConfig("ofi+psm2").WithAccessPoints("hostX").WithServers(
				baseIOConfig(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(defaultFiPort).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa0),
				baseIOConfig(1).
					WithFabricInterface("ib1").
					WithFabricInterfacePort(defaultFiPort+defaultFiPortInterval).
					WithFabricProvider("ofi+psm2").
					WithPinnedNumaNode(&numa1)),
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
			req := &ConfigGenerateReq{
				NumPmem:      tc.numPmem,
				NetClass:     tc.netDevClass,
				AccessPoints: tc.accessPoints,
				Client:       mi,
				Log:          log,
			}

			resp := new(ConfigGenerateResp)

			expResp := &ConfigGenerateResp{
				ConfigOut:      tc.expConfigOut,
				HostErrorsResp: MockHostErrorsResp(t, tc.expHostErrors...),
			}

			networkSet, gotNetErr := req.getSingleNetworkSet(context.TODO(), resp)
			common.CmpErr(t, tc.expNetErr, gotNetErr)
			if tc.expNetErr != nil {
				return
			}

			gotCheckErr := req.checkNetwork(networkSet, resp)
			common.CmpErr(t, tc.expCheckErr, gotCheckErr)
			if tc.expCheckErr != nil {
				return
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(security.CertificateConfig{},
					config.Server{}),
				cmpopts.IgnoreFields(config.Server{}, "GetDeviceClassFn"),
			}
			cmpOpts = append(cmpOpts, defResCmpOpts()...)

			diff := cmp.Diff(expResp.GetHostErrors(), resp.GetHostErrors(), cmpOpts...)
			if diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}

			if diff := cmp.Diff(expResp, resp, cmpOpts...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_AutoConfig_checkCpus(t *testing.T) {
	errCores := func(ssds, want, got int) error {
		return errors.Errorf(errInsufNumCores, ssds, want, got)
	}

	for name, tc := range map[string]struct {
		numCores      int
		bdevCounts    []int // one for each I/O Server in resp.ConfigOut
		expTgtCounts  []int
		expHlprCounts []int
		expCheckErr   error
	}{
		"no cores":           {expCheckErr: errors.Errorf(errInvalNumCores, 0)},
		"24 cores no ssds":   {24, []int{0}, []int{16}, []int{7}, nil},
		"24 cores 1 ssds":    {24, []int{1}, []int{16}, []int{7}, nil},
		"24 cores 2 ssds":    {24, []int{2}, []int{16}, []int{7}, nil},
		"24 cores 3 ssds":    {24, []int{3}, []int{12}, []int{11}, nil},
		"24 cores 4 ssds":    {24, []int{4}, []int{16}, []int{7}, nil},
		"24 cores 5 ssds":    {24, []int{5}, []int{15}, []int{8}, nil},
		"24 cores 8 ssds":    {24, []int{8}, []int{16}, []int{7}, nil},
		"24 cores 9 ssds":    {24, []int{9}, []int{18}, []int{5}, nil},
		"24 cores 10 ssds":   {24, []int{10}, []int{20}, []int{3}, nil},
		"24 cores 16 ssds":   {24, []int{16}, []int{16}, []int{7}, nil},
		"18 cores no ssds":   {18, []int{0}, []int{16}, []int{1}, nil},
		"18 cores 1 ssds":    {18, []int{1}, []int{16}, []int{1}, nil},
		"18 cores 2 ssds":    {18, []int{2}, []int{16}, []int{1}, nil},
		"18 cores 3 ssds":    {18, []int{3}, []int{12}, []int{5}, nil},
		"18 cores 4 ssds":    {18, []int{4}, []int{16}, []int{1}, nil},
		"18 cores 5 ssds":    {18, []int{5}, []int{15}, []int{2}, nil},
		"18 cores 8 ssds":    {18, []int{8}, []int{16}, []int{1}, nil},
		"18 cores 9 ssds":    {18, []int{9}, []int{9}, []int{8}, nil},
		"18 cores 10 ssds":   {18, []int{10}, []int{10}, []int{7}, nil},
		"18 cores 16 ssds":   {18, []int{16}, []int{16}, []int{1}, nil},
		"16 cores no ssds":   {16, []int{0}, []int{16}, []int{1}, errCores(0, 17, 16)},
		"16 cores 1 ssds":    {16, []int{1}, []int{16}, []int{1}, errCores(1, 17, 16)},
		"16 cores 2 ssds":    {16, []int{2}, []int{16}, []int{1}, errCores(2, 17, 16)},
		"16 cores 3 ssds":    {16, []int{3}, []int{12}, []int{3}, nil},
		"16 cores 4 ssds":    {16, []int{4}, []int{16}, []int{1}, errCores(4, 17, 16)},
		"16 cores 5 ssds":    {16, []int{5}, []int{15}, []int{0}, nil},
		"16 cores 6 ssds":    {16, []int{6}, []int{12}, []int{3}, nil},
		"16 cores 7 ssds":    {16, []int{7}, []int{14}, []int{1}, nil},
		"16 cores 8 ssds":    {16, []int{8}, []int{16}, []int{1}, errCores(8, 17, 16)},
		"16 cores 9 ssds":    {16, []int{9}, []int{9}, []int{6}, nil},
		"16 cores 10 ssds":   {16, []int{10}, []int{10}, []int{5}, nil},
		"16 cores 16 ssds":   {16, []int{16}, []int{16}, []int{1}, errCores(16, 17, 16)},
		"32 cores 8:12 ssds": {32, []int{8, 12}, []int{16, 12}, []int{15, 11}, nil},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			req := &ConfigGenerateReq{
				Log: log,
			}

			var ioCfgs []*ioserver.Config
			for idx, count := range tc.bdevCounts {
				ioCfgs = append(ioCfgs, ioCfg(t, idx).
					WithBdevDeviceList(common.MockPCIAddrs(count)...))
			}

			resp := &ConfigGenerateResp{
				ConfigOut: config.DefaultServer().WithServers(ioCfgs...),
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(security.CertificateConfig{}, config.Server{}),
				cmpopts.IgnoreFields(config.Server{}, "GetDeviceClassFn"),
			}
			cmpOpts = append(cmpOpts, defResCmpOpts()...)

			gotCheckErr := req.checkCPUs(tc.numCores, resp)
			common.CmpErr(t, tc.expCheckErr, gotCheckErr)
			if tc.expCheckErr != nil {
				return
			}

			if len(tc.expTgtCounts) != len(resp.ConfigOut.Servers) {
				t.Fatal("bad test case, expTgtCounts != nr I/O Servers in cfg")
			}
			if len(tc.expHlprCounts) != len(resp.ConfigOut.Servers) {
				t.Fatal("bad test case, expHlprCounts != nr I/O Servers in cfg")
			}

			for idx, ioCfg := range resp.ConfigOut.Servers {
				common.AssertEqual(t, tc.expTgtCounts[idx],
					ioCfg.TargetCount, "unexpected target count")
				common.AssertEqual(t, tc.expHlprCounts[idx],
					ioCfg.HelperStreamCount, "unexpected helper thread count")
			}
		})
	}
}
