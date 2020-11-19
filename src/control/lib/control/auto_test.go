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

var ioCfgWithSSDs = func(t *testing.T, numa int) *ioserver.Config {
	var pciAddrs []string
	for _, c := range MockServerScanResp(t, "withSpaceUsage").Nvme.Ctrlrs {
		if int(c.Socketid) == numa {
			pciAddrs = append(pciAddrs, c.Pciaddr)
		}
	}
	return ioserver.NewConfig().
		WithScmClass(storage.ScmClassDCPM.String()).
		WithScmMountPoint(fmt.Sprintf("/mnt/daos%d", numa)).
		WithScmDeviceList(fmt.Sprintf("/dev/pmem%d", numa)).
		WithBdevClass(storage.BdevClassNvme.String()).
		WithBdevDeviceList(pciAddrs...)
}

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
			expCheckErr:   errors.New("insufficient number of pmem devices, want 2 got 0"),
		},
		"2 min pmem and 1 pmems present": {
			numPmem:       2,
			hostResponses: hostRespWithScmNs,
			expCheckErr:   errors.New("insufficient number of pmem devices, want 2 got 1"),
		},
		"2 min pmem and 2 pmems present": {
			numPmem:       2,
			hostResponses: hostRespWithScmNss,
			expCheckErr:   errors.New("insufficient number of nvme devices for numa node 0, want 1 got 0"),
		},
		"2 min pmem and 2 pmems present both numa 0": {
			numPmem:       2,
			hostResponses: hostRespWithScmNssNumaZero,
			expCheckErr:   errors.New("bound to unexpected numa nodes"),
		},
		"no min nvme and 1 ctrlr present on 1 numa node": {
			numPmem:       2,
			hostResponses: hostRespWithSingleSSD,
			expCheckErr:   errors.New("insufficient number of nvme devices for numa node 1, want 1 got 0"),
		},
		"no min nvme and multiple ctrlrs present on 2 numa nodes": {
			numPmem:       2,
			hostResponses: hostRespWithSSDs,
			expConfigOut:  config.DefaultServer().WithServers(ioCfgWithSSDs(t, 0), ioCfgWithSSDs(t, 1)),
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

			req := &ConfigGenerateReq{
				NumPmem: tc.numPmem,
				NumNvme: tc.minNvme,
				Client:  mi,
				Log:     log,
			}

			resp := &ConfigGenerateResp{
				// input config param represents two-socket system
				ConfigOut: config.DefaultServer().
					WithServers(ioserver.NewConfig(), ioserver.NewConfig()),
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
		expConfigOut  *config.Server
		expCheckErr   error
	}{
		"invoker error": {
			uErr:          errors.New("unary error"),
			hostResponses: dualHostRespSame(fabIfs1),
			expCheckErr:   errors.New("unary error"),
		},
		"host network scan failed": {
			hostResponses: hostRespRemoteFail,
			expHostErrors: []*MockHostError{
				{"host2", "remote failed"},
				{"host2", "remote failed"},
			},
			expCheckErr: errors.New("1 host had errors"),
		},
		"host network scan failed on multiple hosts": {
			hostResponses: hostRespRemoteFails,
			expHostErrors: []*MockHostError{
				{"host1", "remote failed"},
				{"host2", "remote failed"},
			},
			expCheckErr: errors.New("2 hosts had errors"),
		},
		"host network scan no hosts": {
			hostResponses: []*HostResponse{},
			expCheckErr:   errors.New("no host responses"),
		},
		"unsupported network class in request": {
			netDevClass:   2,
			hostResponses: dualHostResp(fabIfs1, fabIfs2),
			expCheckErr:   errors.New("unsupported net dev class in request"),
		},
		"host network mismatch": {
			hostResponses: dualHostResp(fabIfs1, fabIfs2),
			expCheckErr:   errors.New("network hardware not consistent across hosts"),
		},
		"no min pmem and no numa": {
			hostResponses: dualHostRespSame(fabIfs1),
			expCheckErr:   errors.New("no numa nodes reported on hosts host[1-2]"),
		},
		"no min pmem and no numa on one host": {
			hostResponses: dualHostResp(fabIfs1, fabIfs1wNuma),
			expCheckErr:   errors.New("network hardware not consistent across hosts"),
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

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(security.CertificateConfig{}, config.Server{}),
				cmpopts.IgnoreFields(config.Server{}, "GetDeviceClassFn"),
			}
			cmpOpts = append(cmpOpts, defResCmpOpts()...)

			gotCheckErr := req.checkNetwork(context.Background(), resp)

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
