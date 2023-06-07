//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/security"
)

func TestAgent_LoadConfig(t *testing.T) {
	dir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	junkFile := test.CreateTestFile(t, dir, "One ring to rule them all\n")
	emptyFile := test.CreateTestFile(t, dir, "")

	withoutOptCfg := test.CreateTestFile(t, dir, `
name: shire
access_points: ["one:10001", "two:10001"]
port: 4242
runtime_dir: /tmp/runtime
log_file: /home/frodo/logfile
transport_config:
  allow_insecure: true
`)

	optCfg := test.CreateTestFile(t, dir, `
name: shire
access_points: ["one:10001", "two:10001"]
port: 4242
runtime_dir: /tmp/runtime
log_file: /home/frodo/logfile
control_log_mask: debug
disable_caching: true
cache_expiration: 30
disable_auto_evict: true
transport_config:
  allow_insecure: true
exclude_fabric_ifaces: ["ib3"]
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

	badLogMaskCfg := test.CreateTestFile(t, dir, `
name: shire
access_points: ["one:10001", "two:10001"]
port: 4242
runtime_dir: /tmp/runtime
log_file: /home/frodo/logfile
control_log_mask: gandalf
transport_config:
  allow_insecure: true
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
				LogLevel:     common.DefaultControlLogLevel,
				TransportConfig: &security.TransportConfig{
					AllowInsecure:     true,
					CertificateConfig: DefaultConfig().TransportConfig.CertificateConfig,
				},
			},
		},
		"bad log mask": {
			path:   badLogMaskCfg,
			expErr: errors.New("not a valid log level"),
		},
		"all options": {
			path: optCfg,
			expResult: &Config{
				SystemName:       "shire",
				AccessPoints:     []string{"one:10001", "two:10001"},
				ControlPort:      4242,
				RuntimeDir:       "/tmp/runtime",
				LogFile:          "/home/frodo/logfile",
				LogLevel:         common.ControlLogLevelDebug,
				DisableCache:     true,
				CacheExpiration:  refreshMinutes(30 * time.Minute),
				DisableAutoEvict: true,
				TransportConfig: &security.TransportConfig{
					AllowInsecure:     true,
					CertificateConfig: DefaultConfig().TransportConfig.CertificateConfig,
				},
				ExcludeFabricIfaces: common.NewStringSet("ib3"),
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

			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, cmpopts.IgnoreUnexported(security.CertificateConfig{})); diff != "" {
				t.Fatalf("(want-, got+):\n%s", diff)
			}
		})
	}
}
