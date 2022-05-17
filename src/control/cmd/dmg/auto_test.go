//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestAuto_ConfigCommands(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			"Generate with no access point",
			"config generate",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no access points"),
		},
		{
			"Generate with defaults",
			"config generate -a foo",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with no nvme",
			"config generate -a foo --min-ssds 0",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with storage parameters",
			"config generate -a foo --num-engines 2 --min-ssds 4",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with short option storage parameters",
			"config generate -a foo -e 2 -s 4",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with ethernet network device class",
			"config generate -a foo --net-class ethernet",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with infiniband network device class",
			"config generate -a foo --net-class infiniband",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with best-available network device class",
			"config generate -a foo --net-class best-available",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
			errors.New("no host responses"),
		},
		{
			"Generate with unsupported network device class",
			"config generate -a foo --net-class loopback",
			strings.Join([]string{
				printRequest(t, &control.NetworkScanReq{}),
			}, " "),
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
  first_core: 0
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
  first_core: 0
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
enable_vmd: false
enable_hotplug: false
nr_hugepages: 6144
control_log_mask: INFO
control_log_file: /tmp/daos_server.log
helper_log_file: ""
firmware_helper_log_file: ""
fault_path: ""
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
		WithNrHugePages(6144).
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
