//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"path/filepath"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var defMemInfo = common.MemInfo{HugepageSizeKiB: 2048}

func TestDaosServer_Auto_Commands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Generate with no access point",
			"config generate",
			printCommand(t, &configGenCmd{
				AccessPoints: "localhost",
				NetClass:     "infiniband",
			}),
			nil,
		},
		{
			"Generate with defaults",
			"config generate -a foo",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				NetClass:     "infiniband",
			}),
			nil,
		},
		{
			"Generate with no nvme",
			"config generate -a foo --scm-only",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				SCMOnly:      true,
				NetClass:     "infiniband",
			}),
			nil,
		},
		{
			"Generate with storage parameters",
			"config generate -a foo --num-engines 2",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				NrEngines:    2,
				NetClass:     "infiniband",
			}),
			nil,
		},
		{
			"Generate with short option storage parameters",
			"config generate -a foo -e 2 -s",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				NrEngines:    2,
				NetClass:     "infiniband",
				SCMOnly:      true,
			}),
			nil,
		},
		{
			"Generate with ethernet network device class",
			"config generate -a foo --net-class ethernet",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
				NetClass:     "ethernet",
			}),
			nil,
		},
		{
			"Generate with infiniband network device class",
			"config generate -a foo --net-class infiniband",
			printCommand(t, &configGenCmd{
				AccessPoints: "foo",
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
			"Generate MD-on-SSD config",
			"config generate -a foo --use-tmpfs-scm --control-metadata-path /opt/daos_md",
			printCommand(t, &configGenCmd{
				AccessPoints:    "foo",
				NetClass:        "infiniband",
				UseTmpfsSCM:     true,
				ExtMetadataPath: "/opt/daos_md",
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
	e0 := control.MockEngineCfg(0, 2, 4).WithTargetCount(18).WithHelperStreamCount(4)
	e1 := control.MockEngineCfg(1, 1, 3).WithTargetCount(18).WithHelperStreamCount(4)
	exmplEngineCfgs := []*engine.Config{e0, e1}
	metadataMountPath := "/mnt/daos_md"
	controlMetadata := storage.ControlMetadata{
		Path: metadataMountPath,
	}
	// Nr hugepages expected with 18+1 (extra MD-on-SSD sys-xstream) targets * 2 engines * 512
	// pages-per-target.
	tmpfsNrHugepages := 19 * 2 * 512
	tmpfsHugeMemGiB := (humanize.MiByte * 2 * tmpfsNrHugepages) / humanize.GiByte
	tmpfsEngRsvdGiB := 2 /* calculated based on 18 targets */
	tmpfsSysRsvdGiB := storage.DefaultSysMemRsvd / humanize.GiByte
	tmpfsRamdiskGiB := storage.MinRamdiskMem / humanize.GiByte
	// Total mem to meet requirements 39GiB hugeMem, 2GiB per engine rsvd, 6GiB sys rsvd, 4GiB
	// per engine RAM-disk.
	tmpfsMemTotalGiB := humanize.GiByte * (tmpfsHugeMemGiB + (2 * tmpfsEngRsvdGiB) + tmpfsSysRsvdGiB +
		(2 * tmpfsRamdiskGiB) + 1 /* add 1GiB buffer */)
	tmpfsEngineCfgs := []*engine.Config{
		control.MockEngineCfgTmpfs(0, tmpfsRamdiskGiB,
			control.MockBdevTierWithRole(0, storage.BdevRoleWAL, 2),
			control.MockBdevTierWithRole(0, storage.BdevRoleMeta|storage.BdevRoleData, 4)).
			WithStorageControlMetadataPath(metadataMountPath).
			WithStorageConfigOutputPath(
				filepath.Join(controlMetadata.EngineDirectory(0), storage.BdevOutConfName),
			).
			WithTargetCount(18).WithHelperStreamCount(4),
		control.MockEngineCfgTmpfs(1, tmpfsRamdiskGiB,
			control.MockBdevTierWithRole(1, storage.BdevRoleWAL, 1),
			control.MockBdevTierWithRole(1, storage.BdevRoleMeta|storage.BdevRoleData, 3)).
			WithStorageControlMetadataPath(metadataMountPath).
			WithStorageConfigOutputPath(
				filepath.Join(controlMetadata.EngineDirectory(1), storage.BdevOutConfName),
			).
			WithTargetCount(18).WithHelperStreamCount(4),
	}

	for name, tc := range map[string]struct {
		accessPoints    string
		nrEngines       int
		scmOnly         bool
		netClass        string
		tmpfsSCM        bool
		extMetadataPath string
		hf              *control.HostFabric
		hfErr           error
		hs              *control.HostStorage
		hsErr           error
		expCfg          *config.Server
		expErr          error
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
				MemInfo:       &defMemInfo,
			},
			expErr: errors.New("zero numa nodes reported"),
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
			expErr: errors.New("nil HostStorage.MemInfo"),
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
				MemInfo: &defMemInfo,
				NvmeDevices: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
					storage.MockNvmeController(4),
				},
			},
			expCfg: control.MockServerCfg("ofi+psm2", exmplEngineCfgs).
				// 18 targets * 2 engines * 512 pages
				WithNrHugepages(18 * 2 * 512).
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
				MemInfo: &defMemInfo,
				NvmeDevices: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
					storage.MockNvmeController(4),
				},
			},
			expCfg: control.MockServerCfg("ofi+psm2", exmplEngineCfgs).
				// 18 targets * 2 engines * 512 pages
				WithNrHugepages(18*2*512).
				WithAccessPoints("localhost:10001").
				WithAccessPoints("moon-111:10001", "mars-115:10001", "jupiter-119:10001").
				WithControlLogFile("/tmp/daos_server.log"),
		},
		"unmet min nr ssds": {
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
				MemInfo:     &defMemInfo,
				NvmeDevices: storage.NvmeControllers{},
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
				MemInfo: &defMemInfo,
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
				MemInfo: &defMemInfo,
				NvmeDevices: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
					storage.MockNvmeController(4),
				},
			},
			expErr: errors.New("unrecognized net-class"),
		},
		"tmpfs scm; no control_metadata path": {
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
				MemInfo: &defMemInfo,
				NvmeDevices: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
					storage.MockNvmeController(4),
				},
			},
			expErr: ErrTmpfsNoExtMDPath,
		},
		"tmpfs scm; low mem": {
			tmpfsSCM:        true,
			extMetadataPath: metadataMountPath,
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
				MemInfo: &common.MemInfo{
					HugepageSizeKiB: 2048,
					MemTotalKiB:     (humanize.GiByte * 12) / humanize.KiByte,
				},
				NvmeDevices: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
					storage.MockNvmeController(4),
				},
			},
			expErr: errors.New("insufficient ram"),
		},
		"tmpfs scm": {
			tmpfsSCM:        true,
			extMetadataPath: metadataMountPath,
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
				MemInfo: &common.MemInfo{
					HugepageSizeKiB: 2048,
					MemTotalKiB:     tmpfsMemTotalGiB / humanize.KiByte,
				},
				NvmeDevices: storage.NvmeControllers{
					storage.MockNvmeController(1),
					storage.MockNvmeController(2),
					storage.MockNvmeController(3),
					storage.MockNvmeController(4),
				},
			},
			expCfg: control.MockServerCfg("ofi+psm2", tmpfsEngineCfgs).
				WithNrHugepages(tmpfsNrHugepages).
				WithAccessPoints("localhost:10001").
				WithControlLogFile("/tmp/daos_server.log").
				WithControlMetadata(controlMetadata),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(name)
			defer test.ShowBufferOnFailure(t, buf)

			// Mimic go-flags default values.
			if tc.netClass == "" {
				tc.netClass = "infiniband"
			}
			if tc.accessPoints == "" {
				tc.accessPoints = "localhost"
			}

			cmd := &configGenCmd{
				AccessPoints:    tc.accessPoints,
				NrEngines:       tc.nrEngines,
				SCMOnly:         tc.scmOnly,
				NetClass:        tc.netClass,
				UseTmpfsSCM:     tc.tmpfsSCM,
				ExtMetadataPath: tc.extMetadataPath,
			}
			cmd.Logger = log

			gf := func(_ context.Context, _ logging.Logger, _ string) (*control.HostFabric, error) {
				return tc.hf, tc.hfErr
			}

			gs := func(_ context.Context, _ logging.Logger, _ bool) (*control.HostStorage, error) {
				return tc.hs, tc.hsErr
			}

			gotCfg, gotErr := cmd.confGen(test.Context(t), gf, gs)
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
