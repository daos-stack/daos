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
	"github.com/dustin/go-humanize"
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
				NetClass:     "infiniband",
			}),
			nil,
		},
		{
			"Generate with defaults",
			"config generate -a foo",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				MinNrSSDs:    1,
				NetClass:     "infiniband",
			}),
			nil,
		},
		{
			"Generate with no nvme",
			"config generate -a foo --min-ssds 0",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				NetClass:     "infiniband",
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
				NetClass:     "infiniband",
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
				NetClass:     "infiniband",
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
			"Generate with deprecated network device class",
			"config generate -a foo --net-class best-available",
			"",
			errors.New("Invalid value"),
		},
		{
			"Generate with unsupported network device class",
			"config generate -a foo --net-class loopback",
			"",
			errors.New("Invalid value"),
		},
		{
			"Generate with tmpfs scm",
			"config generate -a foo --use-tmpfs-scm",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				MinNrSSDs:    1,
				NetClass:     "infiniband",
				UseTmpfsSCM:  true,
			}),
			nil,
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
func TestDaosServer_Auto_confGen(t *testing.T) {
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
	newEngineCfg := func(nn int, paIDs ...int) *engine.Config {
		pciAddrs := make([]string, len(paIDs))
		for i, paID := range paIDs {
			pciAddrs[i] = storage.MockNvmeController(int32(paID)).PciAddr
		}
		return control.DefaultEngineCfg(nn).
			WithPinnedNumaNode(uint(nn)).
			WithFabricInterface(fmt.Sprintf("ib%d", nn)).
			WithFabricInterfacePort(31416+nn*1000).
			WithFabricProvider("ofi+psm2").
			WithFabricNumaNodeIndex(uint(nn)).
			WithStorage(
				storage.NewTierConfig().
					WithNumaNodeIndex(uint(nn)).
					WithStorageClass(storage.ClassDcpm.String()).
					WithScmDeviceList(fmt.Sprintf("/dev/pmem%d", nn)).
					WithScmMountPoint(fmt.Sprintf("/mnt/daos%d", nn)),
				storage.NewTierConfig().
					WithNumaNodeIndex(uint(nn)).
					WithStorageClass(storage.ClassNvme.String()).
					WithBdevDeviceList(pciAddrs...),
			).
			WithStorageNumaNodeIndex(uint(nn)).
			WithTargetCount(18).
			WithHelperStreamCount(4)
	}
	exmplEngineCfgs := []*engine.Config{
		newEngineCfg(0, 2, 4),
		newEngineCfg(1, 1, 3),
	}
	mockMemAvail := humanize.GiByte * 12
	mockRamdiskSize := 4 // RoundDown(12*0.75/2)
	tmpfsEngineCfg0 := newEngineCfg(0, 2, 4)
	tmpfsEngineCfg0.Storage.Tiers[0] = storage.NewTierConfig().
		WithNumaNodeIndex(0).
		WithScmRamdiskSize(uint(mockRamdiskSize)).
		WithStorageClass("ram").
		WithScmMountPoint("/mnt/daos0")
	tmpfsEngineCfg1 := newEngineCfg(1, 1, 3)
	tmpfsEngineCfg1.Storage.Tiers[0] = storage.NewTierConfig().
		WithNumaNodeIndex(1).
		WithScmRamdiskSize(uint(mockRamdiskSize)).
		WithStorageClass("ram").
		WithScmMountPoint("/mnt/daos1")
	tmpfsEngineCfgs := []*engine.Config{
		tmpfsEngineCfg0,
		tmpfsEngineCfg1,
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
		tmpfsSCM     bool
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
			expErr: errors.New("requires nonzero"),
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
			expErr: errors.New("requires nonzero"),
		},
		"single engine; dcpm on numa 1": {
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
				WithNrHugePages(18432).
				WithAccessPoints("localhost:10001").
				WithControlLogFile("/tmp/daos_server.log"),
		},
		"access points set": {
			accessPoints: "moon-111,mars-115,jupiter-119",
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
				WithNrHugePages(18432).
				WithAccessPoints("localhost:10001").
				WithAccessPoints("moon-111:10001", "mars-115:10001", "jupiter-119:10001").
				WithControlLogFile("/tmp/daos_server.log"),
		},
		"unmet min nr ssds": {
			minNrSSDs: 8,
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
			expErr: errors.New("insufficient number of ssds"),
		},
		"unmet nr engines": {
			nrEngines: 8,
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
			expErr: errors.New("insufficient number of pmem"),
		},
		"bad net class": {
			netClass: "foo",
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
			expErr: errors.New("unrecognized net-class"),
		},
		"tmpfs scm": {
			tmpfsSCM: true,
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
				HugePageInfo: control.HugePageInfo{
					PageSizeKb:   2048,
					MemAvailable: int(mockMemAvail / humanize.KiByte),
				},
				NvmeDevices: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
					storage.MockNvmeController(4),
				},
			},
			expCfg: baseConfig("ofi+psm2", tmpfsEngineCfgs).
				WithNrHugePages(18432).
				WithAccessPoints("localhost:10001").
				WithControlLogFile("/tmp/daos_server.log"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			// Mimic go-flags default values.
			if tc.minNrSSDs == 0 {
				tc.minNrSSDs = 1
			}
			if tc.netClass == "" {
				tc.netClass = "infiniband"
			}
			if tc.accessPoints == "" {
				tc.accessPoints = "localhost"
			}

			cmd := &configGenCmd{
				AccessPoints: tc.accessPoints,
				NrEngines:    tc.nrEngines,
				MinNrSSDs:    tc.minNrSSDs,
				NetClass:     tc.netClass,
				UseTmpfsSCM:  tc.tmpfsSCM,
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
