//
// (C) Copyright 2021-2024 Intel Corporation.
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
credential_config:
  cache_expiration: 10m
  client_user_map:
    1000:
      user: frodo
      group: baggins
      groups: ["ringbearers"]
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

	telemetryRetainWithBadPort := test.CreateTestFile(t, dir, `
name: shire
access_points: ["one:10001", "two:10001"]
port: 4242
runtime_dir: /tmp/runtime
log_file: /home/frodo/logfile
control_log_mask: debug
transport_config:
  allow_insecure: true
telemetry_config:
  telemetry_retain: 1m
  telemetry_port: 0
`)

	telemetryEnabledWithBadPort := test.CreateTestFile(t, dir, `
name: shire
access_points: ["one:10001", "two:10001"]
port: 4242
runtime_dir: /tmp/runtime
log_file: /home/frodo/logfile
control_log_mask: debug
transport_config:
  allow_insecure: true
telemetry_config:
  telemetry_enabled: true
  telemetry_port: 0
`)

	telemetryWithoutHttpsCert := test.CreateTestFile(t, dir, `
name: shire
access_points: ["one:10001", "two:10001"]
port: 4242
runtime_dir: /tmp/runtime
log_file: /home/frodo/logfile
control_log_mask: debug
transport_config:
  allow_insecure: true
telemetry_config:
  allow_insecure: false
  https_cert: ""
`)

	telemetryWithoutHttpsKey := test.CreateTestFile(t, dir, `
name: shire
access_points: ["one:10001", "two:10001"]
port: 4242
runtime_dir: /tmp/runtime
log_file: /home/frodo/logfile
control_log_mask: debug
transport_config:
  allow_insecure: true
telemetry_config:
  allow_insecure: false
  https_key: ""
`)

	for name, tc := range map[string]struct {
		path      string
		expResult *Config
		expErr    error
	}{
		"empty path": {
			expErr: errors.New("no config path"),
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
		"telemetry retain with no port": {
			path:   telemetryRetainWithBadPort,
			expErr: errors.New("telemetry_retain requires telemetry_port"),
		},
		"telemetry enabled with no port": {
			path:   telemetryEnabledWithBadPort,
			expErr: errors.New("telemetry_enabled requires telemetry_port"),
		},
		"telemetry with secure mode with no server certificate": {
			path:   telemetryWithoutHttpsCert,
			expErr: errors.New("For secure mode, https_cert and https_key required under telemetry_config"),
		},
		"telemetry with secure mode with no server key": {
			path:   telemetryWithoutHttpsKey,
			expErr: errors.New("For secure mode, https_cert and https_key required under telemetry_config"),
		},
		"without optional items": {
			path: withoutOptCfg,
			expResult: &Config{
				SystemName:       "shire",
				AccessPoints:     []string{"one:10001", "two:10001"},
				ControlPort:      4242,
				RuntimeDir:       "/tmp/runtime",
				LogFile:          "/home/frodo/logfile",
				LogLevel:         common.DefaultControlLogLevel,
				CredentialConfig: &security.CredentialConfig{},
				TransportConfig: &security.TransportConfig{
					AllowInsecure:     true,
					CertificateConfig: DefaultConfig().TransportConfig.CertificateConfig,
				},
				TelemetryConfig: security.DefaultClientTelemetryConfig(),
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
				CredentialConfig: &security.CredentialConfig{
					CacheExpiration: time.Minute * 10,
					ClientUserMap: map[uint32]*security.MappedClientUser{
						1000: {
							User:   "frodo",
							Group:  "baggins",
							Groups: []string{"ringbearers"},
						},
					},
				},
				TransportConfig: &security.TransportConfig{
					AllowInsecure:     true,
					CertificateConfig: DefaultConfig().TransportConfig.CertificateConfig,
				},
				TelemetryConfig:     security.DefaultClientTelemetryConfig(),
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
