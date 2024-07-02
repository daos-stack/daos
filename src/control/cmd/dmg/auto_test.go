//
// (C) Copyright 2020-2024 Intel Corporation.
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

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var cgReqCalls []string

func runConfGenCmdTests(t *testing.T, cmdTests []cmdTest) {
	t.Helper()

	for _, st := range cmdTests {
		t.Run(st.name, func(t *testing.T) {
			t.Helper()
			cgReqCalls = nil // Clear before running test case.

			runCmdTest(t, st.cmd, "", st.expectedErr) // Invoker calls not checked.

			if st.expectedCalls == "" && cgReqCalls == nil {
				return
			}

			// Validate ConfGenerate control API requests.
			callsStr := strings.Join(cgReqCalls, " ")
			if diff := cmp.Diff(st.expectedCalls, callsStr); diff != "" {
				t.Fatalf("unexpected other calls (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestAuto_ConfigCommands(t *testing.T) {
	printCGRReq := func(t *testing.T, req control.ConfGenerateRemoteReq) string {
		req2 := req
		req2.Log = nil
		req2.Client = nil
		return fmt.Sprintf("%T-%+v", req2, req2)
	}

	mockConfGenRemCall := func(_ context.Context, req control.ConfGenerateRemoteReq) (*control.ConfGenerateRemoteResp, error) {
		cgReqCalls = append(cgReqCalls, printCGRReq(t, req))
		return &control.ConfGenerateRemoteResp{}, nil
	}

	// Mock external API calls and restore after tests.
	origGenRemCall := confGenRemoteCall
	confGenRemoteCall = mockConfGenRemCall
	defer func() {
		confGenRemoteCall = origGenRemCall
	}()

	runConfGenCmdTests(t, []cmdTest{
		{
			"Generate with no access point",
			"config generate",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{"localhost:10001"},
				}
				req.ConfGenerateReq.NetClass = hardware.Infiniband
				req.ConfGenerateReq.AccessPoints = []string{"localhost"}
				return req
			}()),
			nil,
		},
		{
			"Generate with hostlist",
			"config generate -a foo -l bar-[1-10]",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{
						"bar-1", "bar-2", "bar-3", "bar-4", "bar-5",
						"bar-6", "bar-7", "bar-8", "bar-9", "bar-10",
					},
				}
				req.ConfGenerateReq.NetClass = hardware.Infiniband
				req.ConfGenerateReq.AccessPoints = []string{"foo"}
				return req
			}()),
			nil,
		},
		{
			"Generate with defaults",
			"config generate -a foo",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{"localhost:10001"},
				}
				req.ConfGenerateReq.NetClass = hardware.Infiniband
				req.ConfGenerateReq.AccessPoints = []string{"foo"}
				return req
			}()),
			nil,
		},
		{
			"Generate with no nvme",
			"config generate -a foo --scm-only",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{"localhost:10001"},
				}
				req.ConfGenerateReq.NetClass = hardware.Infiniband
				req.ConfGenerateReq.AccessPoints = []string{"foo"}
				req.ConfGenerateReq.SCMOnly = true
				return req
			}()),
			nil,
		},
		{
			"Generate with storage parameters",
			"config generate -a foo --num-engines 2",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{"localhost:10001"},
				}
				req.ConfGenerateReq.NetClass = hardware.Infiniband
				req.ConfGenerateReq.AccessPoints = []string{"foo"}
				req.ConfGenerateReq.NrEngines = 2
				return req
			}()),
			nil,
		},
		{
			"Generate with short option storage parameters",
			"config generate -a foo -e 2 -s",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{"localhost:10001"},
				}
				req.ConfGenerateReq.NetClass = hardware.Infiniband
				req.ConfGenerateReq.AccessPoints = []string{"foo"}
				req.ConfGenerateReq.NrEngines = 2
				req.ConfGenerateReq.SCMOnly = true
				return req
			}()),
			nil,
		},
		{
			"Generate with ethernet network device class",
			"config generate -a foo --net-class ethernet",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{"localhost:10001"},
				}
				req.ConfGenerateReq.NetClass = hardware.Ether
				req.ConfGenerateReq.AccessPoints = []string{"foo"}
				return req
			}()),
			nil,
		},
		{
			"Generate with infiniband network device class",
			"config generate -a foo --net-class infiniband",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{"localhost:10001"},
				}
				req.ConfGenerateReq.NetClass = hardware.Infiniband
				req.ConfGenerateReq.AccessPoints = []string{"foo"}
				return req
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
			"Generate with custom network fabric ports",
			"config generate -a foo --fabric-ports 12345,13345",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{"localhost:10001"},
				}
				req.ConfGenerateReq.NetClass = hardware.Infiniband
				req.ConfGenerateReq.AccessPoints = []string{"foo"}
				req.ConfGenerateReq.FabricPorts = []int{12345, 13345}
				return req
			}()),
			nil,
		},
		{
			"Generate tmpfs non-MD-on-SSD config",
			"config generate -a foo --use-tmpfs-scm",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{"localhost:10001"},
				}
				req.ConfGenerateReq.NetClass = hardware.Infiniband
				req.ConfGenerateReq.AccessPoints = []string{"foo"}
				req.ConfGenerateReq.UseTmpfsSCM = true
				return req
			}()),
			nil,
		},
		{
			"Generate MD-on-SSD config",
			"config generate -a foo --use-tmpfs-scm --control-metadata-path /opt/daos",
			printCGRReq(t, func() control.ConfGenerateRemoteReq {
				req := control.ConfGenerateRemoteReq{
					HostList: []string{"localhost:10001"},
				}
				req.ConfGenerateReq.NetClass = hardware.Infiniband
				req.ConfGenerateReq.AccessPoints = []string{"foo"}
				req.ConfGenerateReq.UseTmpfsSCM = true
				req.ConfGenerateReq.ExtMetadataPath = "/opt/daos"
				return req
			}()),
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

func TestAuto_confGenCmd_Convert(t *testing.T) {
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
	if err := convert.Types(cmd.ConfGenCmd, req); err != nil {
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

// The Control API calls made in ConfigGenCmd.confGen() are already well tested so just do some
// sanity checking here to prevent regressions.
func TestAuto_confGen(t *testing.T) {
	ib0 := &ctlpb.FabricInterface{
		Provider: "ofi+psm2", Device: "ib0", Numanode: 0, Netdevclass: 32, Priority: 0,
	}
	ib1 := &ctlpb.FabricInterface{
		Provider: "ofi+psm2", Device: "ib1", Numanode: 1, Netdevclass: 32, Priority: 1,
	}
	netHostResp := &control.HostResponse{
		Addr: "host1",
		Message: &ctlpb.NetworkScanResp{
			Numacount:    2,
			Corespernuma: 24,
			Interfaces:   []*ctlpb.FabricInterface{ib0, ib1},
		},
	}
	storHostResp := &control.HostResponse{
		Addr:    "host1",
		Message: control.MockServerScanResp(t, "withSpaceUsage"),
	}
	storRespHighMem := control.MockServerScanResp(t, "withSpaceUsage")
	// Total mem to meet requirements 34GiB hugeMem, 2GiB per engine rsvd, 16GiB sys rsvd,
	// 5GiB per engine for tmpfs.
	storRespHighMem.MemInfo.MemTotalKb = (humanize.GiByte * (34 + 4 + 16 + 10)) / humanize.KiByte
	mockRamdiskSize := 5
	storHostRespHighMem := &control.HostResponse{
		Addr:    "host1",
		Message: storRespHighMem,
	}
	e0 := control.MockEngineCfg(0, 2, 4, 6, 8).WithHelperStreamCount(4)
	e1 := control.MockEngineCfg(1, 1, 3, 5, 7).WithHelperStreamCount(4)
	exmplEngineCfgs := []*engine.Config{e0, e1}
	tmpfsEngineCfgs := []*engine.Config{
		control.MockEngineCfgTmpfs(0, mockRamdiskSize+1, control.MockBdevTier(0, 2, 4, 6, 8)).
			WithHelperStreamCount(4),
		control.MockEngineCfgTmpfs(1, mockRamdiskSize+1, control.MockBdevTier(1, 1, 3, 5, 7)).
			WithHelperStreamCount(4),
	}
	metadataMountPath := "/mnt/daos_md"
	controlMetadata := storage.ControlMetadata{
		Path: metadataMountPath,
	}
	mdonssdEngineCfgs := []*engine.Config{
		control.MockEngineCfgTmpfs(0, mockRamdiskSize,
			control.MockBdevTierWithRole(0, storage.BdevRoleWAL, 2),
			control.MockBdevTierWithRole(0, storage.BdevRoleMeta|storage.BdevRoleData, 4, 6, 8)).
			WithStorageControlMetadataPath(metadataMountPath).
			WithStorageConfigOutputPath(
				filepath.Join(controlMetadata.EngineDirectory(0), storage.BdevOutConfName),
			).
			WithHelperStreamCount(4),
		control.MockEngineCfgTmpfs(1, mockRamdiskSize,
			control.MockBdevTierWithRole(1, storage.BdevRoleWAL, 1),
			control.MockBdevTierWithRole(1, storage.BdevRoleMeta|storage.BdevRoleData, 3, 5, 7)).
			WithStorageControlMetadataPath(metadataMountPath).
			WithStorageConfigOutputPath(
				filepath.Join(controlMetadata.EngineDirectory(1), storage.BdevOutConfName),
			).
			WithHelperStreamCount(4),
	}

	for name, tc := range map[string]struct {
		hostlist         []string
		accessPoints     string
		nrEngines        int
		scmOnly          bool
		netClass         string
		tmpfsSCM         bool
		extMetadataPath  string
		uErr             error
		hostResponsesSet [][]*control.HostResponse
		expCfg           *config.Server
		expErr           error
		expOutPrefix     string
	}{
		"no host responses": {
			expErr: errors.New("no host responses"),
		},
		"fetching host unary error": {
			uErr:   errors.New("bad fetch"),
			expErr: errors.New("bad fetch"),
		},
		"fetching host fabric error": {
			hostResponsesSet: [][]*control.HostResponse{
				{&control.HostResponse{Addr: "host1", Error: errors.New("server fail")}},
			},
			expErr: errors.New("1 host"),
		},
		"fetching host storage error": {
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{&control.HostResponse{Addr: "host1", Error: errors.New("server fail")}},
			},
			expErr: errors.New("1 host"),
		},
		"dcpm scm": {
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{storHostResp},
			},
			expCfg: control.MockServerCfg("ofi+psm2", exmplEngineCfgs).
				// 16 targets * 2 engines * 512 pages
				WithNrHugepages(16 * 2 * 512).
				WithAccessPoints("localhost:10001").
				WithControlLogFile("/tmp/daos_server.log"),
		},
		"dcpm scm; access points set": {
			accessPoints: "moon-111,mars-115,jupiter-119",
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{storHostResp},
			},
			expCfg: control.MockServerCfg("ofi+psm2", exmplEngineCfgs).
				// 16 targets * 2 engines * 512 pages
				WithNrHugepages(16*2*512).
				WithAccessPoints("moon-111:10001", "mars-115:10001", "jupiter-119:10001").
				WithControlLogFile("/tmp/daos_server.log"),
		},
		"dcpm scm; unmet min nr ssds": {
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{
					&control.HostResponse{
						Addr:    "host1",
						Message: control.MockServerScanResp(t, "nvmeSingle"),
					},
				},
			},
			expErr: errors.New("insufficient number of ssds"),
		},
		"dcpm scm; unmet nr engines": {
			nrEngines: 8,
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{storHostResp},
			},
			expErr: errors.New("insufficient number of pmem"),
		},
		"dcpm scm; bad net class": {
			netClass: "foo",
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{storHostResp},
			},
			expErr: errors.New("unrecognized net-class"),
		},
		"tmpfs scm; no control_metadata path": {
			tmpfsSCM: true,
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{storHostRespHighMem},
			},
			expCfg: control.MockServerCfg("ofi+psm2", tmpfsEngineCfgs).
				// 16 targets * 2 engines * 512 pages
				WithNrHugepages(16 * 2 * 512).
				WithControlLogFile("/tmp/daos_server.log"),
		},
		"dcpm scm; control_metadata path set": {
			extMetadataPath: metadataMountPath,
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{storHostResp},
			},
			expErr: errors.New("only supported with scm class ram"),
		},
		"tmpfs scm; md-on-ssd; low mem": {
			tmpfsSCM:        true,
			extMetadataPath: metadataMountPath,
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{storHostResp},
			},
			expErr: errors.New("insufficient ram"),
		},
		"tmpfs scm; md-on-ssd": {
			tmpfsSCM:        true,
			extMetadataPath: metadataMountPath,
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{storHostRespHighMem},
			},
			expCfg: control.MockServerCfg("ofi+psm2", mdonssdEngineCfgs).
				// 16+1 (MD-on-SSD extra sys-XS) targets * 2 engines * 512 pages
				WithNrHugepages(17 * 2 * 512).
				WithControlLogFile("/tmp/daos_server.log").
				WithControlMetadata(controlMetadata),
		},
		"tmpfs scm; md-on-ssd; no logging to stdout": {
			tmpfsSCM:        true,
			extMetadataPath: metadataMountPath,
			hostResponsesSet: [][]*control.HostResponse{
				{netHostResp},
				{storHostRespHighMem},
			},
			expCfg: control.MockServerCfg("ofi+psm2", mdonssdEngineCfgs).
				// 16+1 (MD-on-SSD extra sys-XS) targets * 2 engines * 512 pages
				WithNrHugepages(17 * 2 * 512).
				WithControlLogFile("/tmp/daos_server.log").
				WithControlMetadata(controlMetadata),
			expOutPrefix: "port: 10001",
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
			cmd.hostlist = tc.hostlist

			if tc.hostResponsesSet == nil {
				tc.hostResponsesSet = [][]*control.HostResponse{{}}
			}
			mic := control.MockInvokerConfig{}
			for _, r := range tc.hostResponsesSet {
				mic.UnaryResponseSet = append(mic.UnaryResponseSet,
					&control.UnaryResponse{Responses: r})
			}
			mic.UnaryError = tc.uErr
			cmd.ctlInvoker = control.NewMockInvoker(log, &mic)

			if tc.expOutPrefix != "" {
				gotErr := cmd.confGenPrint(test.Context(t))
				if gotErr != nil {
					t.Fatal(gotErr)
				}
				if len(buf.String()) == 0 {
					t.Fatal("no output from config generate print function")
				}
				outFirstLine := strings.Split(buf.String(), "\n")[0]
				test.AssertTrue(t, strings.Contains(outFirstLine, tc.expOutPrefix),
					fmt.Sprintf("test: %s, expected %q to be included in the "+
						"first line of output: %q", name, tc.expOutPrefix,
						outFirstLine))
				return
			}

			gotCfg, gotErr := cmd.confGen(test.Context(t))
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

// TestAuto_ConfigWrite verifies that output from config generate command matches documented
// parameters in utils/config/daos_server.yml and that private parameters are not displayed.
// Typical auto-generated output is taken from src/control/lib/control/auto_test.go.
func TestAuto_ConfigWrite(t *testing.T) {
	const (
		defaultFiPort         = 31416
		defaultFiPortInterval = 1000
		defaultTargetCount    = 16
		defaultEngineLogFile  = "/tmp/daos_engine"
		defaultControlLogFile = "/tmp/daos_server.log"

		expOut = `port: 10001
transport_config:
  allow_insecure: false
  client_cert_dir: /etc/daos/certs/clients
  ca_cert: /etc/daos/certs/daosCA.crt
  cert: /etc/daos/certs/server.crt
  key: /etc/daos/certs/server.key
engines:
- targets: 12
  nr_xs_helpers: 2
  log_file: /tmp/daos_engine.0.log
  storage:
  - class: dcpm
    scm_mount: /mnt/daos0
    scm_list:
    - /dev/pmem0
  - class: nvme
    bdev_list:
    - "0000:00:00.0"
    - "0000:01:00.0"
    - "0000:02:00.0"
    - "0000:03:00.0"
  provider: ofi+verbs
  fabric_iface: ib0
  fabric_iface_port: "31416"
  pinned_numa_node: 0
- targets: 6
  nr_xs_helpers: 0
  log_file: /tmp/daos_engine.1.log
  storage:
  - class: dcpm
    scm_mount: /mnt/daos1
    scm_list:
    - /dev/pmem1
  - class: nvme
    bdev_list:
    - "0000:04:00.0"
    - "0000:05:00.0"
    - "0000:06:00.0"
  provider: ofi+verbs
  fabric_iface: ib1
  fabric_iface_port: "32416"
  pinned_numa_node: 1
disable_vfio: false
disable_vmd: false
enable_hotplug: false
nr_hugepages: 6144
system_ram_reserved: 16
disable_hugepages: false
control_log_mask: INFO
control_log_file: /tmp/daos_server.log
core_dump_filter: 19
name: daos_server
socket_dir: /var/run/daos_server
provider: ofi+verbs
access_points:
- hostX:10002
fault_cb: ""
hyperthreads: false
`
	)

	typicalAutoGenOutCfg := config.DefaultServer().
		WithControlLogFile(defaultControlLogFile).
		WithFabricProvider("ofi+verbs").
		WithAccessPoints("hostX:10002").
		WithNrHugepages(6144).
		WithDisableVMD(false).
		WithEngines(
			engine.MockConfig().
				WithTargetCount(defaultTargetCount).
				WithLogFile(fmt.Sprintf("%s.%d.log", defaultEngineLogFile, 0)).
				WithFabricInterface("ib0").
				WithFabricInterfacePort(defaultFiPort).
				WithFabricProvider("ofi+verbs").
				WithPinnedNumaNode(0).
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmDeviceList("/dev/pmem0").
						WithScmMountPoint("/mnt/daos0"),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddrs(0, 1, 2, 3)...),
				).
				WithStorageConfigOutputPath("/mnt/daos0/daos_nvme.conf").
				WithStorageVosEnv("NVME").
				WithTargetCount(12).
				WithHelperStreamCount(2),
			engine.MockConfig().
				WithTargetCount(defaultTargetCount).
				WithLogFile(fmt.Sprintf("%s.%d.log", defaultEngineLogFile, 1)).
				WithFabricInterface("ib1").
				WithFabricInterfacePort(
					int(defaultFiPort+defaultFiPortInterval)).
				WithFabricProvider("ofi+verbs").
				WithPinnedNumaNode(1).
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmDeviceList("/dev/pmem1").
						WithScmMountPoint("/mnt/daos1"),
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddrs(4, 5, 6)...),
				).
				WithIndex(1).
				WithStorageConfigOutputPath("/mnt/daos1/daos_nvme.conf").
				WithStorageVosEnv("NVME").
				WithTargetCount(6).
				WithHelperStreamCount(0),
		)

	bytes, err := yaml.Marshal(typicalAutoGenOutCfg)
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(expOut, string(bytes)); diff != "" {
		t.Fatalf("unexpected output config (-want, +got):\n%s\n", diff)
	}
}
