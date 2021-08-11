//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/security"
)

func TestAgent_LoadConfig(t *testing.T) {
	dir, cleanup := common.CreateTestDir(t)
	defer cleanup()

	junkFile := common.CreateTestFile(t, dir, "One ring to rule them all\n")
	emptyFile := common.CreateTestFile(t, dir, "")

	withoutOptCfg := common.CreateTestFile(t, dir, `
name: shire
access_points: ["one:10001", "two:10001"]
port: 4242
runtime_dir: /tmp/runtime
log_file: /home/frodo/logfile
transport_config:
  allow_insecure: true
`)

	fabricCfg := common.CreateTestFile(t, dir, `
name: shire
access_points: ["one:10001", "two:10001"]
port: 4242
runtime_dir: /tmp/runtime
log_file: /home/frodo/logfile
transport_config:
  allow_insecure: true
fabric_ifaces:
-
  numa_node: 0
  devices:
  -
     iface: ib0
     domain: mlx5_0
  -
     iface: ib1
     domain: mlx5_1
-
  numa_node: 1
  devices:
  -
     iface: ib2
     domain: mlx5_2
  -
     iface: ib3
     domain: mlx5_3
`)

	for name, tc := range map[string]struct {
		path      string
		expResult *Config
		expErr    error
	}{
		"empty path": {
			expErr: errors.New("no path"),
		},
		"bad path": {
			path:   "/not/real/path",
			expErr: errors.New("no such file"),
		},
		"not a config file": {
			path:   junkFile,
			expErr: errors.New("yaml: unmarshal error"),
		},
		"empty config file": {
			path:      emptyFile,
			expResult: DefaultConfig(),
		},
		"without optional items": {
			path: withoutOptCfg,
			expResult: &Config{
				SystemName:   "shire",
				AccessPoints: []string{"one:10001", "two:10001"},
				ControlPort:  4242,
				RuntimeDir:   "/tmp/runtime",
				LogFile:      "/home/frodo/logfile",
				TransportConfig: &security.TransportConfig{
					AllowInsecure:     true,
					CertificateConfig: DefaultConfig().TransportConfig.CertificateConfig,
				},
			},
		},
		"manual fabric config": {
			path: fabricCfg,
			expResult: &Config{
				SystemName:   "shire",
				AccessPoints: []string{"one:10001", "two:10001"},
				ControlPort:  4242,
				RuntimeDir:   "/tmp/runtime",
				LogFile:      "/home/frodo/logfile",
				TransportConfig: &security.TransportConfig{
					AllowInsecure:     true,
					CertificateConfig: DefaultConfig().TransportConfig.CertificateConfig,
				},
				FabricInterfaces: []*NUMAFabricConfig{
					{
						NUMANode: 0,
						Interfaces: []*FabricInterfaceConfig{
							{
								Interface: "ib0",
								Domain:    "mlx5_0",
							},
							{
								Interface: "ib1",
								Domain:    "mlx5_1",
							},
						},
					},
					{
						NUMANode: 1,
						Interfaces: []*FabricInterfaceConfig{
							{
								Interface: "ib2",
								Domain:    "mlx5_2",
							},
							{
								Interface: "ib3",
								Domain:    "mlx5_3",
							},
						},
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, err := LoadConfig(tc.path)

			common.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, cmpopts.IgnoreUnexported(security.CertificateConfig{})); diff != "" {
				t.Fatalf("(want-, got+):\n%s", diff)
			}
		})
	}
}
