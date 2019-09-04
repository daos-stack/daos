//
// (C) Copyright 2019 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"bytes"
	"os"
	"path"
	"reflect"
	"regexp"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server"
)

func testExpectedError(t *testing.T, expected, actual error) {
	t.Helper()

	errRe := regexp.MustCompile(expected.Error())
	if !errRe.MatchString(actual.Error()) {
		t.Fatalf("error string %q doesn't match expected error %q", actual, expected)
	}
}

func showBufOnFailure(t *testing.T, logBuf bytes.Buffer) {
	if !t.Failed() || logBuf.Len() == 0 {
		return
	}
	t.Logf("logged output: %s", logBuf.String())
}

func genMinimalConfig() *server.Configuration {
	cfg := server.NewConfiguration().
		WithProvider("foo").
		WithServers(
			&server.IOServerConfig{
				FabricIface: "foo0",
			},
		)
	cfg.Path = path.Join(os.Args[0], cfg.Path)
	cfg.NvmeShmID = -1 // Don't generate this
	return cfg
}

func genDefaultExpected() *server.Configuration {
	hostname, _ := os.Hostname()
	return genMinimalConfig().
		WithServers(
			&server.IOServerConfig{
				ScmMount:    "/mnt/daos",
				Hostname:    hostname,
				FabricIface: "foo0",
				CliOpts: []string{
					"-t", "0",
					"-g", "daos_server",
					"-s", "/mnt/daos",
					"-x", "0",
					"-d", "/var/run/daos_server",
				},
				EnvVars: []string{
					"CRT_PHY_ADDR_STR=foo",
					"OFI_INTERFACE=foo0",
					"D_LOG_MASK=",
					"D_LOG_FILE=",
				},
			},
		)
}

func updateOptValue(opts []string, key, value string) {
	for i, opt := range opts {
		if opt == key {
			opts[i+1] = value
		}
	}
}

func TestStartOptions(t *testing.T) {
	defer common.ShowLogOnFailure(t)()

	insecureTransport := server.NewConfiguration().TransportConfig
	insecureTransport.AllowInsecure = true

	for desc, tc := range map[string]struct {
		argList  []string
		expCfgFn func(*server.Configuration) *server.Configuration
		expErr   error
	}{
		"None": {
			argList:  []string{},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration { return cfg },
		},
		"Port (short)": {
			argList: []string{"-p", "42"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				return cfg.WithPort(42)
			},
		},
		"Port (long)": {
			argList: []string{"--port=42"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				return cfg.WithPort(42)
			},
		},
		"Storage Path (short)": {
			argList: []string{"-s", "/foo/bar"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.WithScmMountPath("/foo/bar")
				cfg.Servers[0].ScmMount = "/foo/bar"
				updateOptValue(cfg.Servers[0].CliOpts, "-s", "/foo/bar")
				return cfg
			},
		},
		"Storage Path (long)": {
			argList: []string{"--storage=/foo/bar"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.WithScmMountPath("/foo/bar")
				cfg.Servers[0].ScmMount = "/foo/bar"
				updateOptValue(cfg.Servers[0].CliOpts, "-s", "/foo/bar")
				return cfg
			},
		},
		"Modules (short)": {
			argList: []string{"-m", "foo,bar"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.WithModules("foo,bar")
				cfg.Servers[0].CliOpts = append(cfg.Servers[0].CliOpts,
					[]string{"-m", "foo,bar"}...)
				return cfg
			},
		},
		"Modules (long)": {
			argList: []string{"--modules=foo,bar"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.WithModules("foo,bar")
				cfg.Servers[0].CliOpts = append(cfg.Servers[0].CliOpts,
					[]string{"-m", "foo,bar"}...)
				return cfg
			},
		},
		"Targets (short)": {
			argList: []string{"-t", "42"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.Servers[0].Targets = 42
				updateOptValue(cfg.Servers[0].CliOpts, "-t", "42")
				return cfg
			},
		},
		"Targets (long)": {
			argList: []string{"--targets=42"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.Servers[0].Targets = 42
				updateOptValue(cfg.Servers[0].CliOpts, "-t", "42")
				return cfg
			},
		},
		"XS Helpers (bad)": {
			argList: []string{"-x", "42"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.Servers[0].NrXsHelpers = 2
				// Kind of weird... If the value is crazy, then
				// the desired behavior seems to be to not supply
				// this flag (i.e. use ioserver default).
				opts := cfg.Servers[0].CliOpts
				for i, opt := range opts {
					if opt == "-x" {
						cfg.Servers[0].CliOpts = append(opts[:i], opts[i+2:]...)
						break
					}
				}
				return cfg
			},
		},
		"XS Helpers (short)": {
			argList: []string{"-x", "0"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.Servers[0].NrXsHelpers = 0
				updateOptValue(cfg.Servers[0].CliOpts, "-x", "0")
				return cfg
			},
		},
		"XS Helpers (long)": {
			argList: []string{"--xshelpernr=1"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.Servers[0].NrXsHelpers = 1
				updateOptValue(cfg.Servers[0].CliOpts, "-x", "1")
				return cfg
			},
		},
		"First Core (short)": {
			argList: []string{"-f", "42"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.Servers[0].FirstCore = 42
				cfg.Servers[0].CliOpts = append(cfg.Servers[0].CliOpts,
					[]string{"-f", "42"}...)
				return cfg
			},
		},
		"First Core (long)": {
			argList: []string{"--firstcore=42"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.Servers[0].FirstCore = 42
				cfg.Servers[0].CliOpts = append(cfg.Servers[0].CliOpts,
					[]string{"-f", "42"}...)
				return cfg
			},
		},
		"Server Group (short)": {
			argList: []string{"-g", "foo"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.WithSystemName("foo")
				updateOptValue(cfg.Servers[0].CliOpts, "-g", "foo")
				return cfg
			},
		},
		"Server Group (long)": {
			argList: []string{"--group=foo"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.WithSystemName("foo")
				updateOptValue(cfg.Servers[0].CliOpts, "-g", "foo")
				return cfg
			},
		},
		"Attach Info (short)": {
			argList: []string{"-a", "/foo/bar"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.WithAttach("/foo/bar")
				cfg.Servers[0].CliOpts = append(cfg.Servers[0].CliOpts,
					[]string{"-a", "/foo/bar"}...)
				return cfg
			},
		},
		"Attach Info (long)": {
			argList: []string{"--attach_info=/foo/bar"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.WithAttach("/foo/bar")
				cfg.Servers[0].CliOpts = append(cfg.Servers[0].CliOpts,
					[]string{"-a", "/foo/bar"}...)
				return cfg
			},
		},
		"SocketDir (short)": {
			argList: []string{"-d", "/foo/bar"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.WithSocketDir("/foo/bar")
				updateOptValue(cfg.Servers[0].CliOpts, "-d", "/foo/bar")
				return cfg
			},
		},
		"SocketDir (long)": {
			argList: []string{"--socket_dir=/foo/bar"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				cfg.WithSocketDir("/foo/bar")
				updateOptValue(cfg.Servers[0].CliOpts, "-d", "/foo/bar")
				return cfg
			},
		},
		"Insecure (short)": {
			argList: []string{"-i"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				return cfg.WithTransportConfig(insecureTransport)
			},
		},
		"Insecure (long)": {
			argList: []string{"--insecure"},
			expCfgFn: func(cfg *server.Configuration) *server.Configuration {
				return cfg.WithTransportConfig(insecureTransport)
			},
		},
	} {
		t.Run(desc, func(t *testing.T) {
			var logBuf bytes.Buffer
			log := logging.NewCombinedLogger(t.Name(), &logBuf).
				WithLogLevel(logging.LogLevelDebug)
			defer showBufOnFailure(t, logBuf)

			var gotConfig *server.Configuration
			var opts mainOpts
			opts.Start.start = func(log *logging.LeveledLogger, cfg *server.Configuration) error {
				gotConfig = cfg
				return nil
			}
			opts.Start.config = genMinimalConfig()
			wantConfig := tc.expCfgFn(genDefaultExpected())

			err := parseOpts(append([]string{"start"}, tc.argList...), &opts, log)
			if err != tc.expErr {
				if tc.expErr == nil {
					t.Fatalf("expected nil error, got %+v", err)
				}
				testExpectedError(t, tc.expErr, err)
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(
					server.Configuration{},
					server.IOServerConfig{},
					security.CertificateConfig{},
				),
				cmpopts.SortSlices(func(a, b string) bool { return a < b }),
			}
			if diff := cmp.Diff(wantConfig, gotConfig, cmpOpts...); diff != "" {
				t.Fatalf("(-want +got):\n%s", diff)
			}
		})
	}
}

// TODO(DAOS-3129): Remove this test when we remove the default subcommand.
func TestStartAsDefaultCommand(t *testing.T) {
	var logBuf bytes.Buffer
	log := logging.NewCombinedLogger(t.Name(), &logBuf).
		WithLogLevel(logging.LogLevelDebug)
	defer showBufOnFailure(t, logBuf)

	var opts mainOpts
	var startCalled bool
	var gotConfig *server.Configuration
	opts.Start.start = func(log *logging.LeveledLogger, cfg *server.Configuration) error {
		gotConfig = cfg
		startCalled = true
		return nil
	}
	opts.Start.config = genMinimalConfig()
	insecureTransport := server.NewConfiguration().TransportConfig
	insecureTransport.AllowInsecure = true
	wantConfig := genDefaultExpected().WithTransportConfig(insecureTransport)
	wantConfigPath := "/tmp/foo/bar.yml"

	err := parseOpts([]string{"-i", "-o", wantConfigPath}, &opts, log)
	if err != nil {
		t.Fatal(err)
	}

	if !startCalled {
		t.Fatal("expected start subcommand to be invoked; but it wasn't")
	}

	if opts.ConfigPath != wantConfigPath {
		t.Fatalf("expected config path to be %q, but it was %q",
			wantConfigPath, opts.ConfigPath)
	}

	cmpOpts := []cmp.Option{
		cmpopts.IgnoreUnexported(
			server.Configuration{},
			server.IOServerConfig{},
			security.CertificateConfig{},
		),
		cmpopts.SortSlices(func(a, b string) bool { return a < b }),
	}
	if diff := cmp.Diff(wantConfig, gotConfig, cmpOpts...); diff != "" {
		t.Fatalf("(-want +got):\n%s", diff)
	}
}

func TestStartLoggingOptions(t *testing.T) {
	for desc, tc := range map[string]struct {
		argList   []string
		logFnName string
		input     string
		wantRe    *regexp.Regexp
	}{
		"Debug (Short)": {
			argList:   []string{"-b"},
			logFnName: "Debug",
			input:     "hello",
			wantRe:    regexp.MustCompile(`hello\n$`),
		},
		"Debug (Long)": {
			argList:   []string{"--debug"},
			logFnName: "Debug",
			input:     "hello",
			wantRe:    regexp.MustCompile(`hello\n$`),
		},
		"JSON (Short)": {
			argList:   []string{"-j"},
			logFnName: "Info",
			input:     "hello",
			wantRe:    regexp.MustCompile(`"message":"hello"`),
		},
		"JSON (Long)": {
			argList:   []string{"--json"},
			logFnName: "Info",
			input:     "hello",
			wantRe:    regexp.MustCompile(`"message":"hello"`),
		},
	} {
		t.Run(desc, func(t *testing.T) {
			var logBuf bytes.Buffer
			log := logging.NewCombinedLogger(t.Name(), &logBuf)

			var opts mainOpts
			opts.Start.start = func(log *logging.LeveledLogger, cfg *server.Configuration) error {
				return nil
			}
			opts.Start.config = genMinimalConfig()

			if err := parseOpts(append(tc.argList, "start"), &opts, log); err != nil {
				t.Fatal(err)
			}

			// Normally don't want to use reflection, but in this
			// case it allows us to create a new logger for each
			// test run.
			logFn := reflect.ValueOf(log).MethodByName(tc.logFnName)
			logFn.Call([]reflect.Value{reflect.ValueOf(tc.input)})

			got := logBuf.String()
			if !tc.wantRe.MatchString(got) {
				t.Fatalf("expected %q to match %s", got, tc.wantRe)
			}
		})
	}
}

func TestStartLoggingConfiguration(t *testing.T) {
	for desc, tc := range map[string]struct {
		configFn  func(*server.Configuration) *server.Configuration
		logFnName string
		input     string
		wantRe    *regexp.Regexp
	}{
		"JSON": {
			configFn: func(cfg *server.Configuration) *server.Configuration {
				return cfg.WithControlLogJSON(true)
			},
			logFnName: "Info",
			input:     "hello",
			wantRe:    regexp.MustCompile(`"message":"hello"`),
		},
		"Debug": {
			configFn: func(cfg *server.Configuration) *server.Configuration {
				return cfg.WithControlLogMask(server.ControlLogLevelDebug)
			},
			logFnName: "Debug",
			input:     "hello",
			wantRe:    regexp.MustCompile(`hello`),
		},
		"Error": {
			configFn: func(cfg *server.Configuration) *server.Configuration {
				return cfg.WithControlLogMask(server.ControlLogLevelError)
			},
			logFnName: "Info",
			input:     "hello",
			wantRe:    regexp.MustCompile(`^$`),
		},
	} {
		t.Run(desc, func(t *testing.T) {
			var logBuf bytes.Buffer
			log := logging.NewCombinedLogger(t.Name(), &logBuf)

			var opts mainOpts
			opts.Start.start = func(log *logging.LeveledLogger, cfg *server.Configuration) error {
				return nil
			}
			opts.Start.config = tc.configFn(genMinimalConfig())

			if err := parseOpts([]string{"start"}, &opts, log); err != nil {
				t.Fatal(err)
			}

			logFn := reflect.ValueOf(log).MethodByName(tc.logFnName)
			logFn.Call([]reflect.Value{reflect.ValueOf(tc.input)})

			got := logBuf.String()
			if !tc.wantRe.MatchString(got) {
				// dirty hacks
				if tc.wantRe.String() == regexp.MustCompile(`^$`).String() {
					if strings.Contains(got, tc.input) {
						t.Fatalf("expected %q to not contain %q", got, tc.input)
					}
					return
				}
				t.Fatalf("expected %q to match %s", got, tc.wantRe)
			}
		})
	}
}
