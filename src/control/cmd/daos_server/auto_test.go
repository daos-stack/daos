//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
)

func TestDaosServer_Auto_Commands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Generate with no access point",
			"config generate",
			printCommand(t, &configGenCmd{
				AccessPoints: "localhost",
				MinNrSSDs:    1,
				NetClass:     "best-available",
			}),
			nil,
		},
		{
			"Generate with defaults",
			"config generate -a foo",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				MinNrSSDs:    1,
				NetClass:     "best-available",
			}),
			nil,
		},
		{
			"Generate with no nvme",
			"config generate -a foo --min-ssds 0",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				NetClass:     "best-available",
			}),
			nil,
		},
		{
			"Generate with storage parameters",
			"config generate -a foo --num-engines 2 --min-ssds 4",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				NrEngines:    2,
				MinNrSSDs:    4,
				NetClass:     "best-available",
			}),
			nil,
		},
		{
			"Generate with short option storage parameters",
			"config generate -a foo -e 2 -s 4",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				NrEngines:    2,
				MinNrSSDs:    4,
				NetClass:     "best-available",
			}),
			nil,
		},
		{
			"Generate with ethernet network device class",
			"config generate -a foo --net-class ethernet",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				MinNrSSDs:    1,
				NetClass:     "ethernet",
			}),
			nil,
		},
		{
			"Generate with infiniband network device class",
			"config generate -a foo --net-class infiniband",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				MinNrSSDs:    1,
				NetClass:     "infiniband",
			}),
			nil,
		},
		{
			"Generate with best-available network device class",
			"config generate -a foo --net-class best-available",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				MinNrSSDs:    1,
				NetClass:     "best-available",
			}),
			nil,
		},
		{
			"Generate with unsupported network device class",
			"config generate -a foo --net-class loopback",
			"",
			errors.New("Invalid value"),
		},
		{
			"Nonexistent subcommand",
			"network quack",
			"",
			errors.New("Unknown command"),
		},
	})
}

// The Control API calls made in configGenCmd.confGen() are already well tested so just do some
// sanity checking here to prevent regressions.
func TestDaosServer_confGen(t *testing.T) {
	eth0 := &control.HostFabricInterface{
		Provider: "ofi+tcp", Device: "eth0", NumaNode: 0, NetDevClass: 1, Priority: 2,
	}
	eth1 := &control.HostFabricInterface{
		Provider: "ofi+tcp", Device: "eth1", NumaNode: 1, NetDevClass: 1, Priority: 3,
	}
	ib0 := &control.HostFabricInterface{
		Provider: "ofi+psm2", Device: "ib0", NumaNode: 0, NetDevClass: 32, Priority: 0,
	}
	ib1 := &control.HostFabricInterface{
		Provider: "ofi+psm2", Device: "ib1", NumaNode: 1, NetDevClass: 32, Priority: 1,
	}
	exmplEngineCfg := control.DefaultEngineCfg(0).
		WithPinnedNumaNode(0).
		WithFabricInterface("ib0").
		WithFabricInterfacePort(31416).
		WithFabricProvider("ofi+psm2").
		WithStorage(
			storage.NewTierConfig().
				WithNumaNodeIndex(0).
				WithStorageClass(storage.ClassDcpm.String()).
				WithScmDeviceList("/dev/pmem0").
				WithScmMountPoint("/mnt/daos0"),
			storage.NewTierConfig().
				WithNumaNodeIndex(0).
				WithStorageClass(storage.ClassNvme.String()).
				WithBdevDeviceList(
					storage.MockNvmeController(2).PciAddr,
					storage.MockNvmeController(4).PciAddr),
		).
		WithTargetCount(22).
		WithHelperStreamCount(1)
	exmplEngineCfgs := []*engine.Config{
		exmplEngineCfg,
		control.DefaultEngineCfg(1).
			WithPinnedNumaNode(1).
			WithFabricInterface("ib1").
			WithFabricInterfacePort(32416).
			WithFabricProvider("ofi+psm2").
			WithFabricNumaNodeIndex(1).
			WithStorage(
				storage.NewTierConfig().
					WithNumaNodeIndex(1).
					WithStorageClass(storage.ClassDcpm.String()).
					WithScmDeviceList("/dev/pmem1").
					WithScmMountPoint("/mnt/daos1"),
				storage.NewTierConfig().
					WithNumaNodeIndex(1).
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(
						storage.MockNvmeController(1).PciAddr,
						storage.MockNvmeController(3).PciAddr),
			).
			WithStorageNumaNodeIndex(1).
			WithTargetCount(22).
			WithHelperStreamCount(1),
	}
	baseConfig := func(prov string, ecs []*engine.Config) *config.Server {
		for idx, ec := range ecs {
			ec.WithStorageConfigOutputPath(fmt.Sprintf("/mnt/daos%d/daos_nvme.conf", idx)).
				WithStorageVosEnv("NVME")
		}
		return config.DefaultServer().
			WithControlLogFile("").
			WithFabricProvider(prov).
			WithDisableVMD(false).
			WithEngines(ecs...)
	}

	for name, tc := range map[string]struct {
		accessPoints string
		nrEngines    int
		minNrSSDs    int
		netClass     string
		hf           *control.HostFabric
		hfErr        error
		hs           *control.HostStorage
		hsErr        error
		expCfg       *config.Server
		expErr       error
	}{
		"fetching host fabric fails": {
			hfErr:  errors.New("bad fetch"),
			expErr: errors.New("bad fetch"),
		},
		"nil host fabric returned": {
			expErr: errors.New("nil HostFabric"),
		},
		"empty host fabric returned": {
			hf: &control.HostFabric{},
			hs: &control.HostStorage{
				ScmNamespaces: storage.ScmNamespaces{storage.MockScmNamespace()},
			},
			expErr: errors.New("zero numa nodes"),
		},
		"fetching host storage fails": {
			hf: &control.HostFabric{
				Interfaces: []*control.HostFabricInterface{
					eth0, eth1, ib0, ib1,
				},
				NumaCount:    1,
				CoresPerNuma: 1,
			},
			hsErr:  errors.New("bad fetch"),
			expErr: errors.New("bad fetch"),
		},
		"nil host storage returned": {
			hf: &control.HostFabric{
				Interfaces: []*control.HostFabricInterface{
					eth0, eth1, ib0, ib1,
				},
				NumaCount:    1,
				CoresPerNuma: 1,
			},
			expErr: errors.New("nil HostStorage"),
		},
		"empty host storage returned": {
			hf: &control.HostFabric{
				Interfaces: []*control.HostFabricInterface{
					eth0, eth1, ib0, ib1,
				},
				NumaCount:    1,
				CoresPerNuma: 1,
			},
			hs:     &control.HostStorage{},
			expErr: errors.New("insufficient number of pmem"),
		},
		"single engine; dcpm on numa 1": {
			accessPoints: "localhost",
			minNrSSDs:    1,
			hf: &control.HostFabric{
				Interfaces: []*control.HostFabricInterface{
					eth0, eth1, ib0, ib1,
				},
				NumaCount:    2,
				CoresPerNuma: 24,
			},
			hs: &control.HostStorage{
				ScmNamespaces: storage.ScmNamespaces{
					storage.MockScmNamespace(0),
					storage.MockScmNamespace(1),
				},
				HugePageInfo: control.HugePageInfo{PageSizeKb: 2048},
				NvmeDevices: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
					storage.MockNvmeController(4),
				},
			},
			expCfg: baseConfig("ofi+psm2", exmplEngineCfgs).
				WithNrHugePages(22528).
				WithAccessPoints("localhost:10001").
				WithControlLogFile("/tmp/daos_server.log"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			cmd := &configGenCmd{
				AccessPoints: tc.accessPoints,
				NrEngines:    tc.nrEngines,
				MinNrSSDs:    tc.minNrSSDs,
				NetClass:     tc.netClass,
			}
			cmd.Logger = log

			gf := func(_ context.Context, _ logging.Logger) (*control.HostFabric, error) {
				return tc.hf, tc.hfErr
			}

			gs := func(_ context.Context, _ logging.Logger) (*control.HostStorage, error) {
				return tc.hs, tc.hsErr
			}

			gotCfg, gotErr := cmd.confGen(context.TODO(), gf, gs)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := []cmp.Option{
				cmp.Comparer(func(x, y *storage.BdevDeviceList) bool {
					if x == nil && y == nil {
						return true
					}
					return x.Equals(y)
				}),
				cmpopts.IgnoreUnexported(security.CertificateConfig{}),
			}

			if diff := cmp.Diff(tc.expCfg, gotCfg, cmpOpts...); diff != "" {
				t.Fatalf("unexpected config generated (-want, +got):\n%s\n", diff)
			}
		})
	}
}
