//
// (C) Copyright 2019-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
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
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

func testExpectedError(t *testing.T, expected, actual error) {
	t.Helper()

	errRe := regexp.MustCompile(expected.Error())
	if !errRe.MatchString(actual.Error()) {
		t.Fatalf("error string %q doesn't match expected error %q", actual, expected)
	}
}

func genMinimalConfig() *config.Server {
	cfg := config.DefaultServer().
		WithFabricProvider("foo").
		WithProviderValidator(netdetect.ValidateProviderStub).
		WithNUMAValidator(netdetect.ValidateNUMAStub).
		WithGetNetworkDeviceClass(getDeviceClassStub).
		WithServers(
			ioserver.NewConfig().
				WithScmClass("ram").
				WithScmRamdiskSize(1).
				WithScmMountPoint("/mnt/daos").
				WithFabricInterface("foo0").
				WithFabricInterfacePort(42),
		)
	cfg.Path = path.Join(os.Args[0], cfg.Path)
	return cfg
}

func genDefaultExpected() *config.Server {
	hostname, _ := os.Hostname()
	return genMinimalConfig().
		WithServers(
			ioserver.NewConfig().
				WithHostname(hostname).
				WithScmClass("ram").
				WithScmRamdiskSize(1).
				WithScmMountPoint("/mnt/daos").
				WithFabricInterface("foo0").
				WithFabricInterfacePort(42),
		)
}

func cmpArgs(t *testing.T, wantConfig, gotConfig *ioserver.Config) {
	t.Helper()

	wantArgs, err := wantConfig.CmdLineArgs()
	if err != nil {
		t.Fatal(err)
	}
	gotArgs, err := gotConfig.CmdLineArgs()
	if err != nil {
		t.Fatal(err)
	}

	if diff := cmp.Diff(wantArgs, gotArgs); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}

func cmpEnv(t *testing.T, wantConfig, gotConfig *ioserver.Config) {
	t.Helper()

	wantEnv, err := wantConfig.CmdLineEnv()
	if err != nil {
		t.Fatal(err)
	}
	gotEnv, err := gotConfig.CmdLineEnv()
	if err != nil {
		t.Fatal(err)
	}

	cmpOpts := []cmp.Option{
		cmpopts.SortSlices(func(a, b string) bool { return a < b }),
	}
	if diff := cmp.Diff(wantEnv, gotEnv, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got)\n%s", diff)
	}
}

func getDeviceClassStub(netdev string) (uint32, error) {
	return 0, nil
}

func TestStartOptions(t *testing.T) {
	insecureTransport := config.DefaultServer().TransportConfig
	insecureTransport.AllowInsecure = true

	for desc, tc := range map[string]struct {
		argList  []string
		expCfgFn func(*config.Server) *config.Server
		expErr   error
	}{
		"None": {
			argList:  []string{},
			expCfgFn: func(cfg *config.Server) *config.Server { return cfg },
		},
		"Port (short)": {
			argList: []string{"-p", "42"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				return cfg.WithControlPort(42)
			},
		},
		"Port (long)": {
			argList: []string{"--port=42"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				return cfg.WithControlPort(42)
			},
		},
		"Storage Path (short)": {
			argList: []string{"-s", "/foo/bar"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				cfg.Servers[0].WithScmMountPoint("/foo/bar")
				return cfg
			},
		},
		"Storage Path (long)": {
			argList: []string{"--storage=/foo/bar"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				cfg.Servers[0].WithScmMountPoint("/foo/bar")
				return cfg
			},
		},
		"Modules (short)": {
			argList: []string{"-m", "foo,bar"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				return cfg.WithModules("foo,bar")
			},
		},
		"Modules (long)": {
			argList: []string{"--modules=foo,bar"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				return cfg.WithModules("foo,bar")
			},
		},
		"Targets (short)": {
			argList: []string{"-t", "42"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				cfg.Servers[0].WithTargetCount(42)
				return cfg
			},
		},
		"Targets (long)": {
			argList: []string{"--targets=42"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				cfg.Servers[0].WithTargetCount(42)
				return cfg
			},
		},
		"XS Helpers (short)": {
			argList: []string{"-x", "0"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				cfg.Servers[0].WithHelperStreamCount(0)
				return cfg
			},
		},
		"XS Helpers (long)": {
			argList: []string{"--xshelpernr=1"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				cfg.Servers[0].WithHelperStreamCount(1)
				return cfg
			},
		},
		"First Core (short)": {
			argList: []string{"-f", "42"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				cfg.Servers[0].WithServiceThreadCore(42)
				return cfg
			},
		},
		"First Core (long)": {
			argList: []string{"--firstcore=42"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				cfg.Servers[0].WithServiceThreadCore(42)
				return cfg
			},
		},
		"Server Group (short)": {
			argList: []string{"-g", "foo"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				return cfg.WithSystemName("foo")
			},
		},
		"Server Group (long)": {
			argList: []string{"--group=foo"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				return cfg.WithSystemName("foo")
			},
		},
		"SocketDir (short)": {
			argList: []string{"-d", "/foo/bar"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				return cfg.WithSocketDir("/foo/bar")
			},
		},
		"SocketDir (long)": {
			argList: []string{"--socket_dir=/foo/bar"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				return cfg.WithSocketDir("/foo/bar")
			},
		},
		"Insecure (short)": {
			argList: []string{"-i"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				return cfg.WithTransportConfig(insecureTransport)
			},
		},
		"Insecure (long)": {
			argList: []string{"--insecure"},
			expCfgFn: func(cfg *config.Server) *config.Server {
				return cfg.WithTransportConfig(insecureTransport)
			},
		},
	} {
		t.Run(desc, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			var gotConfig *config.Server
			var opts mainOpts
			opts.Start.start = func(log *logging.LeveledLogger, cfg *config.Server) error {
				gotConfig = cfg
				return nil
			}

			opts.Start.config = genMinimalConfig().
				WithProviderValidator(netdetect.ValidateProviderStub).
				WithNUMAValidator(netdetect.ValidateNUMAStub).
				WithGetNetworkDeviceClass(getDeviceClassStub)
			wantConfig := tc.expCfgFn(genDefaultExpected().
				WithProviderValidator(netdetect.ValidateProviderStub).
				WithNUMAValidator(netdetect.ValidateNUMAStub).
				WithGetNetworkDeviceClass(getDeviceClassStub))

			err := parseOpts(append([]string{"start"}, tc.argList...), &opts, log)
			if err != tc.expErr {
				if tc.expErr == nil {
					t.Fatalf("expected nil error, got %+v", err)
				}
				testExpectedError(t, tc.expErr, err)
			}

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(
					config.Server{},
					security.CertificateConfig{},
				),
				cmpopts.IgnoreFields(config.Server{}, "GetDeviceClassFn"),
				cmpopts.SortSlices(func(a, b string) bool { return a < b }),
			}
			if diff := cmp.Diff(wantConfig, gotConfig, cmpOpts...); diff != "" {
				t.Fatalf("(-want +got):\n%s", diff)
			}

			cmpArgs(t, wantConfig.Servers[0], gotConfig.Servers[0])
			cmpEnv(t, wantConfig.Servers[0], gotConfig.Servers[0])
		})
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
		"JSON Logs (Short)": {
			argList:   []string{"-J"},
			logFnName: "Info",
			input:     "hello",
			wantRe:    regexp.MustCompile(`"message":"hello"`),
		},
		"JSON Logs (Long)": {
			argList:   []string{"--json-logging"},
			logFnName: "Info",
			input:     "hello",
			wantRe:    regexp.MustCompile(`"message":"hello"`),
		},
	} {
		t.Run(desc, func(t *testing.T) {
			var logBuf bytes.Buffer
			log := logging.NewCombinedLogger(t.Name(), &logBuf)

			var opts mainOpts
			opts.Start.start = func(log *logging.LeveledLogger, cfg *config.Server) error {
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
		configFn  func(*config.Server) *config.Server
		logFnName string
		input     string
		wantRe    *regexp.Regexp
	}{
		"JSON": {
			configFn: func(cfg *config.Server) *config.Server {
				return cfg.WithControlLogJSON(true)
			},
			logFnName: "Info",
			input:     "hello",
			wantRe:    regexp.MustCompile(`"message":"hello"`),
		},
		"Debug": {
			configFn: func(cfg *config.Server) *config.Server {
				return cfg.WithControlLogMask(config.ControlLogLevelDebug)
			},
			logFnName: "Debug",
			input:     "hello",
			wantRe:    regexp.MustCompile(`hello`),
		},
		"Error": {
			configFn: func(cfg *config.Server) *config.Server {
				return cfg.WithControlLogMask(config.ControlLogLevelError)
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
			opts.Start.start = func(log *logging.LeveledLogger, cfg *config.Server) error {
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
