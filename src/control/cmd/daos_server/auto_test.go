//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestDaosServer_Auto_Commands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Generate with no access point",
			"config generate",
			printCommand(t, func() *configGenCmd {
				cmd := &configGenCmd{}
				cmd.AccessPoints = "localhost"
				cmd.NetClass = "infiniband"
				return cmd
			}()),
			nil,
		},
		{
			"Generate with defaults",
			"config generate -a foo",
			printCommand(t, func() *configGenCmd {
				cmd := &configGenCmd{}
				cmd.AccessPoints = "foo"
				cmd.NetClass = "infiniband"
				return cmd
			}()),
			nil,
		},
		{
			"Generate with no nvme",
			"config generate -a foo --scm-only",
			printCommand(t, func() *configGenCmd {
				cmd := &configGenCmd{}
				cmd.AccessPoints = "foo"
				cmd.NetClass = "infiniband"
				cmd.SCMOnly = true
				return cmd
			}()),
			nil,
		},
		{
			"Generate with storage parameters",
			"config generate -a foo --num-engines 2",
			printCommand(t, func() *configGenCmd {
				cmd := &configGenCmd{}
				cmd.AccessPoints = "foo"
				cmd.NetClass = "infiniband"
				cmd.NrEngines = 2
				return cmd
			}()),
			nil,
		},
		{
			"Generate with short option storage parameters",
			"config generate -a foo -e 2 -s",
			printCommand(t, func() *configGenCmd {
				cmd := &configGenCmd{}
				cmd.AccessPoints = "foo"
				cmd.NetClass = "infiniband"
				cmd.NrEngines = 2
				cmd.SCMOnly = true
				return cmd
			}()),
			nil,
		},
		{
			"Generate with ethernet network device class",
			"config generate -a foo --net-class ethernet",
			printCommand(t, func() *configGenCmd {
				cmd := &configGenCmd{}
				cmd.AccessPoints = "foo"
				cmd.NetClass = "ethernet"
				return cmd
			}()),
			nil,
		},
		{
			"Generate with infiniband network device class",
			"config generate -a foo --net-class infiniband",
			printCommand(t, func() *configGenCmd {
				cmd := &configGenCmd{}
				cmd.AccessPoints = "foo"
				cmd.NetClass = "infiniband"
				return cmd
			}()),
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
			"Generate tmpfs non-MD-on-SSD config",
			"config generate -a foo --use-tmpfs-scm",
			printCommand(t, func() *configGenCmd {
				cmd := &configGenCmd{}
				cmd.AccessPoints = "foo"
				cmd.NetClass = "infiniband"
				cmd.UseTmpfsSCM = true
				return cmd
			}()),
			nil,
		},
		{
			"Generate MD-on-SSD config",
			"config generate -a foo --use-tmpfs-scm --control-metadata-path /opt/daos_md",
			printCommand(t, func() *configGenCmd {
				cmd := &configGenCmd{}
				cmd.AccessPoints = "foo"
				cmd.NetClass = "infiniband"
				cmd.UseTmpfsSCM = true
				cmd.ExtMetadataPath = "/opt/daos_md"
				return cmd
			}()),
			nil,
		},
		{
			"Nonexistent subcommand",
			"network quack",
			"",
			errors.New("Unknown command"),
		},
		{
			"Config flag unsupported",
			"config generate -o /foo",
			"",
			errors.New("unknown flag"),
		},
	})
}

func TestDaosServer_Auto_confGenCmd_Convert(t *testing.T) {
	cmd := &configGenCmd{}
	cmd.NrEngines = 1
	cmd.NetProvider = "ofi+tcp"
	cmd.SCMOnly = true
	cmd.AccessPoints = "foo,bar"
	cmd.NetClass = "infiniband"
	cmd.UseTmpfsSCM = true
	cmd.ExtMetadataPath = "/opt/daos_md"
	cmd.FabricPorts = "12345,13345"

	req := new(control.ConfGenerateReq)
	if err := convert.Types(cmd, req); err != nil {
		t.Fatal(err)
	}

	expReq := &control.ConfGenerateReq{
		NrEngines:       1,
		NetProvider:     "ofi+tcp",
		SCMOnly:         true,
		AccessPoints:    []string{"foo", "bar"},
		NetClass:        hardware.Infiniband,
		UseTmpfsSCM:     true,
		ExtMetadataPath: "/opt/daos_md",
		FabricPorts:     []int{12345, 13345},
	}

	if diff := cmp.Diff(expReq, req); diff != "" {
		t.Fatalf("unexpected request converted (-want, +got):\n%s\n", diff)
	}
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

	tmpfsEngineCfgs := []*engine.Config{
		control.MockEngineCfgTmpfs(0, 0, control.MockBdevTier(0, 2, 4)).
			WithTargetCount(18).WithHelperStreamCount(4),
		control.MockEngineCfgTmpfs(1, 0, control.MockBdevTier(1, 1, 3)).
			WithTargetCount(18).WithHelperStreamCount(4),
	}

	metadataMountPath := "/mnt/daos_md"
	controlMetadata := storage.ControlMetadata{
		Path: metadataMountPath,
	}
	mdOnSSDEngineCfgs := []*engine.Config{
		control.MockEngineCfgTmpfs(0, 0,
			control.MockBdevTierWithRole(0, storage.BdevRoleWAL, 2),
			control.MockBdevTierWithRole(0, storage.BdevRoleMeta|storage.BdevRoleData, 4)).
			WithStorageControlMetadataPath(metadataMountPath).
			WithStorageConfigOutputPath(
				filepath.Join(controlMetadata.EngineDirectory(0), storage.BdevOutConfName),
			).
			WithTargetCount(18).WithHelperStreamCount(4),
		control.MockEngineCfgTmpfs(1, 0,
			control.MockBdevTierWithRole(1, storage.BdevRoleWAL, 1),
			control.MockBdevTierWithRole(1, storage.BdevRoleMeta|storage.BdevRoleData, 3)).
			WithStorageControlMetadataPath(metadataMountPath).
			WithStorageConfigOutputPath(
				filepath.Join(controlMetadata.EngineDirectory(1), storage.BdevOutConfName),
			).
			WithTargetCount(18).WithHelperStreamCount(4),
	}

	var defCoresPerNuma uint32 = 26
	var defNumaCount uint32 = 2
	defMemInfo := common.MemInfo{
		HugepageSizeKiB: 2048,
		MemTotalKiB:     1, // Avoid failing non-zero check.
	}
	defHostFabric := &control.HostFabric{
		Interfaces: []*control.HostFabricInterface{
			eth0, eth1, ib0, ib1,
		},
		NumaCount:    defNumaCount,
		CoresPerNuma: defCoresPerNuma,
	}
	defHostStorage := &control.HostStorage{
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
		expOutPrefix    string
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
		"dual engine; dcpm": {
			hf: defHostFabric,
			hs: defHostStorage,
			expCfg: control.MockServerCfg("ofi+psm2", exmplEngineCfgs).
				WithAccessPoints("localhost:10001").
				WithControlLogFile("/tmp/daos_server.log"),
		},
		"access points set": {
			accessPoints: "moon-111,mars-115,jupiter-119",
			hf:           defHostFabric,
			hs:           defHostStorage,
			expCfg: control.MockServerCfg("ofi+psm2", exmplEngineCfgs).
				WithAccessPoints("localhost:10001").
				WithAccessPoints("moon-111:10001", "mars-115:10001", "jupiter-119:10001").
				WithControlLogFile("/tmp/daos_server.log"),
		},
		"unmet min nr ssds": {
			hf: defHostFabric,
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
			hf:        defHostFabric,
			hs:        defHostStorage,
			expErr:    errors.New("insufficient number of pmem"),
		},
		"bad net class": {
			netClass: "foo",
			hf:       defHostFabric,
			hs:       defHostStorage,
			expErr:   errors.New("unrecognized net-class"),
		},
		"tmpfs scm; no control_metadata path": {
			tmpfsSCM: true,
			hf:       defHostFabric,
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
			expCfg: control.MockServerCfg("ofi+psm2", tmpfsEngineCfgs).
				WithAccessPoints("localhost:10001").
				WithControlLogFile("/tmp/daos_server.log"),
		},
		"dcpm scm; control_metadata path set": {
			extMetadataPath: metadataMountPath,
			hf:              defHostFabric,
			hs:              defHostStorage,
			expErr:          errors.New("only supported with scm class ram"),
		},
		"tmpfs scm; md-on-ssd": {
			tmpfsSCM:        true,
			extMetadataPath: metadataMountPath,
			hf:              defHostFabric,
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
			expCfg: control.MockServerCfg("ofi+psm2", mdOnSSDEngineCfgs).
				WithAccessPoints("localhost:10001").
				WithControlLogFile("/tmp/daos_server.log").
				WithControlMetadata(controlMetadata),
		},
		"tmpfs scm; md-on-ssd; no logging to stdout": {
			tmpfsSCM:        true,
			extMetadataPath: metadataMountPath,
			hf:              defHostFabric,
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
			expOutPrefix: "port: 10001",
		},
		"dcpm scm; vmd; 2 domains-per-engine; 4 ssds-per-domain": {
			hf: defHostFabric,
			hs: &control.HostStorage{
				ScmNamespaces: storage.ScmNamespaces{
					storage.MockScmNamespace(0),
					storage.MockScmNamespace(1),
				},
				MemInfo: &defMemInfo,
				NvmeDevices: storage.NvmeControllers{
					&storage.NvmeController{PciAddr: "4a0005:01:00.0"},
					&storage.NvmeController{PciAddr: "4a0005:02:00.0"},
					&storage.NvmeController{PciAddr: "4a0005:03:00.0"},
					&storage.NvmeController{PciAddr: "4a0005:04:00.0"},
					&storage.NvmeController{PciAddr: "640005:01:00.0"},
					&storage.NvmeController{PciAddr: "640005:02:00.0"},
					&storage.NvmeController{PciAddr: "640005:03:00.0"},
					&storage.NvmeController{PciAddr: "640005:04:00.0"},
					&storage.NvmeController{PciAddr: "970005:01:00.0", SocketID: 1},
					&storage.NvmeController{PciAddr: "970005:02:00.0", SocketID: 1},
					&storage.NvmeController{PciAddr: "970005:03:00.0", SocketID: 1},
					&storage.NvmeController{PciAddr: "970005:04:00.0", SocketID: 1},
					&storage.NvmeController{PciAddr: "e20005:01:00.0", SocketID: 1},
					&storage.NvmeController{PciAddr: "e20005:02:00.0", SocketID: 1},
					&storage.NvmeController{PciAddr: "e20005:03:00.0", SocketID: 1},
					&storage.NvmeController{PciAddr: "e20005:04:00.0", SocketID: 1},
				},
			},
			expCfg: control.MockServerCfg("ofi+psm2", []*engine.Config{
				control.DefaultEngineCfg(0).
					WithPinnedNumaNode(0).
					WithFabricInterface("ib0").
					WithFabricInterfacePort(31416).
					WithFabricProvider("ofi+psm2").
					WithFabricNumaNodeIndex(0).
					WithStorageNumaNodeIndex(0).
					WithTargetCount(16).
					WithHelperStreamCount(4).
					WithStorage(
						storage.NewTierConfig().
							WithNumaNodeIndex(0).
							WithStorageClass(storage.ClassDcpm.String()).
							WithScmDeviceList("/dev/pmem0").
							WithScmMountPoint("/mnt/daos0"),
						storage.NewTierConfig().
							WithNumaNodeIndex(0).
							WithStorageClass(storage.ClassNvme.String()).
							WithBdevDeviceList("0000:4a:00.5", "0000:64:00.5"),
					),
				control.DefaultEngineCfg(1).
					WithPinnedNumaNode(1).
					WithFabricInterface("ib1").
					WithFabricInterfacePort(32416).
					WithFabricProvider("ofi+psm2").
					WithFabricNumaNodeIndex(1).
					WithStorageNumaNodeIndex(1).
					WithTargetCount(16).
					WithHelperStreamCount(4).
					WithStorage(
						storage.NewTierConfig().
							WithNumaNodeIndex(1).
							WithStorageClass(storage.ClassDcpm.String()).
							WithScmDeviceList("/dev/pmem1").
							WithScmMountPoint("/mnt/daos1"),
						storage.NewTierConfig().
							WithNumaNodeIndex(1).
							WithStorageClass(storage.ClassNvme.String()).
							WithBdevDeviceList("0000:97:00.5", "0000:e2:00.5"),
					),
			}).
				WithAccessPoints("localhost:10001").
				WithControlLogFile("/tmp/daos_server.log"),
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

			cmd := &configGenCmd{}
			cmd.AccessPoints = tc.accessPoints
			cmd.NrEngines = tc.nrEngines
			cmd.SCMOnly = tc.scmOnly
			cmd.NetClass = tc.netClass
			cmd.UseTmpfsSCM = tc.tmpfsSCM
			cmd.ExtMetadataPath = tc.extMetadataPath
			log.SetLevel(logging.LogLevelInfo)
			cmd.Logger = log

			gf := func(_ context.Context, _ logging.Logger, _ string) (*control.HostFabric, error) {
				return tc.hf, tc.hfErr
			}

			gs := func(_ context.Context, _ logging.Logger, _ bool) (*control.HostStorage, error) {
				return tc.hs, tc.hsErr
			}

			if tc.expOutPrefix != "" {
				gotErr := cmd.confGenPrint(test.Context(t), gf, gs)
				if gotErr != nil {
					t.Fatal(gotErr)
				}
				if len(buf.String()) == 0 {
					t.Fatal("no output from config generate print function")
				}
				outFirstLine := strings.Split(buf.String(), "\n")[0]
				test.AssertTrue(t, strings.HasSuffix(outFirstLine, tc.expOutPrefix),
					fmt.Sprintf("test: %s, expected %q to be included in the "+
						"first line of output: %q", name, tc.expOutPrefix,
						outFirstLine))
				return
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

// TestDaosServer_Auto_Commands_JSON verifies that the JSON-output flag is disabled for config
// generate commands.
func TestDaosServer_Auto_Commands_JSON(t *testing.T) {
	log, buf := logging.NewTestCommandLineLogger()

	runJSONCmdTests(t, log, buf, []jsonCmdTest{
		{
			"Config generate; JSON",
			"config generate -j",
			nil,
			nil,
			errJSONOutputNotSupported,
		},
	})
}
