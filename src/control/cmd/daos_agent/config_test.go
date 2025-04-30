//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"regexp"
	"strings"
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

	badFilterCfg := test.CreateTestFile(t, dir, `
name: shire
access_points: ["one:10001", "two:10001"]
port: 4242
runtime_dir: /tmp/runtime
log_file: /home/frodo/logfile
transport_config:
  allow_insecure: true
include_fabric_ifaces: ["ib0"]
exclude_fabric_ifaces: ["ib3"]
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
			},
		},
		"bad log mask": {
			path:   badLogMaskCfg,
			expErr: errors.New("not a valid log level"),
		},
		"bad filter config": {
			path:   badFilterCfg,
			expErr: errors.New("cannot specify both exclude_fabric_ifaces and include_fabric_ifaces"),
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

			cmpOpts := cmp.Options{
				cmpopts.IgnoreUnexported(security.CertificateConfig{}),
				cmpopts.IgnoreUnexported(TelemetryConfig{}),
			}
			test.CmpErr(t, tc.expErr, err)
			if diff := cmp.Diff(tc.expResult, result, cmpOpts...); diff != "" {
				t.Fatalf("(want-, got+):\n%s", diff)
			}
		})
	}
}

func TestAgent_ReadConfig(t *testing.T) {
	cfgWith := func(cfg *Config, xfrm func(*Config) *Config) *Config {
		if xfrm != nil {
			cfg = xfrm(cfg)
		}
		return cfg
	}

	for name, tc := range map[string]struct {
		input  string
		expCfg *Config
		expErr error
	}{
		"empty": {
			expCfg: DefaultConfig(),
		},
		"telemetry enabled with no port": {
			input: `
telemetry_enabled: true
`,
			expErr: errors.New("telemetry_enabled requires telemetry_port"),
		},
		"telemetry retain set without enable": {
			input: `
telemetry_retain: 10m
`,
			expErr: errors.New("telemetry_retain requires telemetry_enabled: true"),
		},
		"telemetry enable pattern without enable": {
			input: `
telemetry_enabled_procs: foo
`,
			expErr: errors.New("cannot specify telemetry_enabled_procs without telemetry_enabled"),
		},
		"invalid enable pattern": {
			input: `
telemetry_enabled_procs: "*"
`,
			expErr: errors.New("invalid regular expression"),
		},
		"telemetry enabled and disabled patterns specified": {
			input: `
telemetry_port: 1234
telemetry_enabled: true
telemetry_enabled_procs: foo
telemetry_disabled_procs: bar
`,
			expErr: errors.New("cannot specify both telemetry_enabled_procs and telemetry_disabled_procs"),
		},
		"telemetry disabled pattern without enable": {
			input: `
telemetry_disabled_procs: foo
`,
			expErr: errors.New("cannot specify telemetry_disabled_procs without telemetry_enabled"),
		},
		"invalid disabled pattern": {
			input: `
telemetry_disabled_procs: "*"
`,
			expErr: errors.New("invalid regular expression"),
		},
		"empty disabled pattern": {
			input: `
telemetry_disabled_procs: ""
`,
			expErr: errors.New("empty regular expression"),
		},
		"invalid retain value": {
			input: `
telemetry_retain: foo
`,
			expErr: errors.New("time.Duration"),
		},
		"minimal telemetry config": {
			input: `
telemetry_port: 1234
telemetry_enabled: true
`,
			expCfg: cfgWith(DefaultConfig(), func(cfg *Config) *Config {
				cfg.Telemetry.Port = 1234
				cfg.Telemetry.Enabled = true
				return cfg
			}),
		},
		"telemetry config with retain duration": {
			input: `
telemetry_port: 1234
telemetry_enabled: true
telemetry_retain: 10s
`,
			expCfg: cfgWith(DefaultConfig(), func(cfg *Config) *Config {
				cfg.Telemetry.Port = 1234
				cfg.Telemetry.Enabled = true
				cfg.Telemetry.Retain = 10 * time.Second
				return cfg
			}),
		},
		"telemetry config with enabled pattern": {
			input: `
telemetry_port: 1234
telemetry_enabled: true
telemetry_enabled_procs: ^foo$
`,
			expCfg: cfgWith(DefaultConfig(), func(cfg *Config) *Config {
				cfg.Telemetry.Port = 1234
				cfg.Telemetry.Enabled = true
				cfg.Telemetry.RegPattern = (*ConfigRegexp)(regexp.MustCompile("^foo$"))
				return cfg
			}),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotCfg, gotErr := ReadConfig(strings.NewReader(tc.input))

			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			cmpOpts := cmp.Options{
				cmpopts.IgnoreUnexported(security.CertificateConfig{}),
				cmp.Comparer(func(x, y *ConfigRegexp) bool {
					if x == nil && y == nil {
						return true
					}
					if x == nil || y == nil {
						return false
					}
					return x.String() == y.String()
				}),
			}
			if diff := cmp.Diff(tc.expCfg, gotCfg, cmpOpts...); diff != "" {
				t.Fatalf("(want-, got+):\n%s", diff)
			}
		})
	}
}
