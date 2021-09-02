//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package config

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	sConfigUncomment = "daos_server_uncomment.yml"
	socketsExample   = "../../../../utils/config/examples/daos_server_sockets.yml"
	psm2Example      = "../../../../utils/config/examples/daos_server_psm2.yml"
	defaultConfig    = "../../../../utils/config/daos_server.yml"
	legacyConfig     = "../../../../utils/config/examples/daos_server_unittests.yml"
)

var (
	defConfigCmpOpts = []cmp.Option{
		cmpopts.IgnoreUnexported(
			Server{},
			security.CertificateConfig{},
		),
		cmpopts.IgnoreFields(Server{}, "GetDeviceClassFn", "Path"),
	}
)

// uncommentServerConfig removes leading comment chars from daos_server.yml
// lines in order to verify parsing of all available params.
func uncommentServerConfig(t *testing.T, outFile string) {
	in, err := os.Open(defaultConfig)
	if err != nil {
		t.Fatal(err)
	}
	defer in.Close()

	out, err := os.Create(outFile)
	if err != nil {
		t.Fatal(err)
	}
	defer out.Close()

	// Keep track of keys we've already seen in order
	// to avoid writing duplicate parameters.
	seenKeys := make(map[string]struct{})

	scn := bufio.NewScanner(in)
	for scn.Scan() {
		line := scn.Text()
		line = strings.TrimPrefix(line, "#")

		fields := strings.Fields(line)
		if len(fields) < 1 {
			continue
		}
		key := fields[0]

		// If we're in a server or a storage tier config, reset the
		// seen map to allow the same params in different
		// server configs.
		lineTmp := strings.TrimLeft(line, " ")
		if lineTmp == "-" {
			seenKeys = make(map[string]struct{})
		}
		if _, seen := seenKeys[key]; seen && strings.HasSuffix(key, ":") {
			continue
		}
		seenKeys[key] = struct{}{}

		line += "\n"
		if _, err := out.WriteString(line); err != nil {
			t.Fatal(err)
		}
	}
}

// mockConfigFromFile returns a populated server config file from the
// file at the given path.
func mockConfigFromFile(t *testing.T, path string) (*Server, error) {
	t.Helper()
	c := DefaultServer().
		WithProviderValidator(netdetect.ValidateProviderStub).
		WithNUMAValidator(netdetect.ValidateNUMAStub).
		WithGetNetworkDeviceClass(getDeviceClassStub)
	c.Path = path

	return c, c.Load()
}

func getDeviceClassStub(netdev string) (uint32, error) {
	switch netdev {
	case "eth0":
		return netdetect.Ether, nil
	case "eth1":
		return netdetect.Ether, nil
	case "ib0":
		return netdetect.Infiniband, nil
	case "ib1":
		return netdetect.Infiniband, nil
	default:
		return 0, nil
	}
}

func TestServerConfig_MarshalUnmarshal(t *testing.T) {
	for name, tt := range map[string]struct {
		inPath string
		expErr error
	}{
		"uncommented default config": {inPath: "uncommentedDefault"},
		"socket example config":      {inPath: socketsExample},
		"psm2 example config":        {inPath: psm2Example},
		"default empty config":       {inPath: defaultConfig},
		"nonexistent config": {
			inPath: "/foo/bar/baz.yml",
			expErr: errors.New("reading file: open /foo/bar/baz.yml: no such file or directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			testDir, cleanup := CreateTestDir(t)
			defer cleanup()
			testFile := filepath.Join(testDir, "test.yml")

			if tt.inPath == "uncommentedDefault" {
				tt.inPath = filepath.Join(testDir, sConfigUncomment)
				uncommentServerConfig(t, tt.inPath)
			}

			configA := DefaultServer().
				WithProviderValidator(netdetect.ValidateProviderStub).
				WithNUMAValidator(netdetect.ValidateNUMAStub).
				WithGetNetworkDeviceClass(getDeviceClassStub)
			configA.Path = tt.inPath
			err := configA.Load()
			if err == nil {
				err = configA.Validate(log)
			}

			CmpErr(t, tt.expErr, err)
			if tt.expErr != nil {
				return
			}

			if err := configA.SaveToFile(testFile); err != nil {
				t.Fatal(err)
			}

			configB := DefaultServer().
				WithProviderValidator(netdetect.ValidateProviderStub).
				WithNUMAValidator(netdetect.ValidateNUMAStub).
				WithGetNetworkDeviceClass(getDeviceClassStub)
			if err := configB.SetPath(testFile); err != nil {
				t.Fatal(err)
			}

			err = configB.Load()
			if err == nil {
				err = configB.Validate(log)
			}

			if err != nil {
				t.Fatal(err)
			}

			if diff := cmp.Diff(configA, configB, defConfigCmpOpts...); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestServerConfig_Constructed(t *testing.T) {
	testDir, cleanup := CreateTestDir(t)
	defer cleanup()

	// First, load a config based on the server config with all options uncommented.
	testFile := filepath.Join(testDir, sConfigUncomment)
	uncommentServerConfig(t, testFile)
	defaultCfg, err := mockConfigFromFile(t, testFile)
	if err != nil {
		t.Fatalf("failed to load %s: %s", testFile, err)
	}

	var numaNode0 uint = 0
	var numaNode1 uint = 1
	var bypass = true

	// Next, construct a config to compare against the first one. It should be
	// possible to construct an identical configuration with the helpers.
	constructed := DefaultServer().
		WithControlPort(10001).
		WithBdevInclude("0000:81:00.1", "0000:81:00.2", "0000:81:00.3").
		WithBdevExclude("0000:81:00.1").
		WithDisableVFIO(true). // vfio enabled by default
		WithEnableVMD(true).   // vmd disabled by default
		WithNrHugePages(4096).
		WithControlLogMask(ControlLogLevelError).
		WithControlLogFile("/tmp/daos_server.log").
		WithHelperLogFile("/tmp/daos_admin.log").
		WithFirmwareHelperLogFile("/tmp/daos_firmware.log").
		WithSystemName("daos_server").
		WithSocketDir("./.daos/daos_server").
		WithFabricProvider("ofi+verbs;ofi_rxm").
		WithCrtCtxShareAddr(1).
		WithCrtTimeout(30).
		WithAccessPoints("hostname1").
		WithFaultCb("./.daos/fd_callback").
		WithFaultPath("/vcdu0/rack1/hostname").
		WithHyperthreads(true). // hyper-threads disabled by default
		WithProviderValidator(netdetect.ValidateProviderStub).
		WithNUMAValidator(netdetect.ValidateNUMAStub).
		WithGetNetworkDeviceClass(getDeviceClassStub).
		WithEngines(
			engine.NewConfig().
				WithRank(0).
				WithTargetCount(16).
				WithHelperStreamCount(6).
				WithServiceThreadCore(0).
				WithStorage(
					storage.NewTierConfig().
						WithScmMountPoint("/mnt/daos/1").
						WithScmClass("ram").
						WithScmRamdiskSize(16),
					storage.NewTierConfig().
						WithBdevClass("nvme").
						WithBdevDeviceList("0000:81:00.0"),
				).
				WithFabricInterface("qib0").
				WithFabricInterfacePort(20000).
				WithPinnedNumaNode(&numaNode0).
				WithBypassHealthChk(&bypass).
				WithEnvVars("CRT_TIMEOUT=30").
				WithLogFile("/tmp/daos_engine.0.log").
				WithLogMask("WARN"),
			engine.NewConfig().
				WithRank(1).
				WithTargetCount(16).
				WithHelperStreamCount(6).
				WithServiceThreadCore(22).
				WithStorage(
					storage.NewTierConfig().
						WithScmMountPoint("/mnt/daos/2").
						WithScmClass("dcpm").
						WithScmDeviceList("/dev/pmem1"),
					storage.NewTierConfig().
						WithBdevClass("file").
						WithBdevDeviceList("/tmp/daos-bdev1", "/tmp/daos-bdev2").
						WithBdevFileSize(16),
				).
				WithFabricInterface("qib1").
				WithFabricInterfacePort(20000).
				WithPinnedNumaNode(&numaNode1).
				WithEnvVars("CRT_TIMEOUT=100").
				WithLogFile("/tmp/daos_engine.1.log").
				WithLogMask("WARN"),
		)
	constructed.Path = testFile // just to avoid failing the cmp

	if diff := cmp.Diff(defaultCfg, constructed, defConfigCmpOpts...); diff != "" {
		t.Fatalf("(-want, +got): %s", diff)
	}
}

func TestServerConfig_Validation(t *testing.T) {
	noopExtra := func(c *Server) *Server { return c }

	for name, tt := range map[string]struct {
		extraConfig func(c *Server) *Server
		expErr      error
	}{
		"example config": {},
		"nil engine entry": {
			extraConfig: func(c *Server) *Server {
				var nilEngineConfig *engine.Config
				return c.WithEngines(nilEngineConfig)
			},
			expErr: errors.New("validation"),
		},
		"no engine entries": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines()
			},
		},
		"no fabric provider": {
			extraConfig: func(c *Server) *Server {
				return c.WithFabricProvider("")
			},
			expErr: FaultConfigNoProvider,
		},
		"no access point": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints()
			},
			expErr: FaultConfigBadAccessPoints,
		},
		"single access point": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:1234")
			},
		},
		"multiple access points (even)": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:1234", "5.6.7.8:5678")
			},
			expErr: FaultConfigEvenAccessPoints,
		},
		"multiple access points (odd)": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:1234", "5.6.7.8:5678", "1.5.3.8:6247")
			},
		},
		"multiple access points (dupes)": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4", "5.6.7.8", "1.2.3.4")
			},
			expErr: FaultConfigBadAccessPoints,
		},
		"multiple access points (dupes with ports)": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:1234", "5.6.7.8:5678", "1.2.3.4:1234")
			},
			expErr: FaultConfigBadAccessPoints,
		},
		"multiple access points (dupes with and without ports)": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:10001", "5.6.7.8:5678", "1.2.3.4")
			},
			expErr: FaultConfigBadAccessPoints,
		},
		"multiple access points (dupes with different ports)": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:10002", "5.6.7.8:5678", "1.2.3.4")
			},
		},
		"no access points": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints()
			},
			expErr: FaultConfigBadAccessPoints,
		},
		"single access point no port": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4")
			},
		},
		"single access point invalid port": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4").
					WithControlPort(0)
			},
			expErr: FaultConfigBadControlPort,
		},
		"single access point including invalid port (alphanumeric)": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:0a0")
			},
			expErr: FaultConfigBadControlPort,
		},
		"single access point including invalid port (zero)": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:0")
			},
			expErr: FaultConfigBadControlPort,
		},
		"single access point including negative port": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:-10002")
			},
			expErr: FaultConfigBadControlPort,
		},
		"single access point hostname including negative port": {
			extraConfig: func(c *Server) *Server {
				return c.WithAccessPoints("hostX:-10002")
			},
			expErr: FaultConfigBadControlPort,
		},
		"good control port": {
			extraConfig: func(c *Server) *Server {
				return c.WithControlPort(1234)
			},
		},
		"bad control port (zero)": {
			extraConfig: func(c *Server) *Server {
				return c.WithControlPort(0)
			},
			expErr: FaultConfigBadControlPort,
		},
		"good telemetry port": {
			extraConfig: func(c *Server) *Server {
				return c.WithTelemetryPort(1234)
			},
		},
		"good telemetry port (zero)": {
			extraConfig: func(c *Server) *Server {
				return c.WithTelemetryPort(0)
			},
		},
		"bad telemetry port (negative)": {
			extraConfig: func(c *Server) *Server {
				return c.WithTelemetryPort(-123)
			},
			expErr: FaultConfigBadTelemetryPort,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			if tt.extraConfig == nil {
				tt.extraConfig = noopExtra
			}

			testDir, cleanup := CreateTestDir(t)
			defer cleanup()

			// First, load a config based on the server config with all options uncommented.
			testFile := filepath.Join(testDir, sConfigUncomment)
			uncommentServerConfig(t, testFile)
			config, err := mockConfigFromFile(t, testFile)
			if err != nil {
				t.Fatalf("failed to load %s: %s", testFile, err)
			}

			// Apply extra config test case
			config = tt.extraConfig(config)

			CmpErr(t, tt.expErr, config.Validate(log))
		})
	}
}

// replaceLine only matches when oldTxt is exactly the same as a file line.
func replaceLine(r io.Reader, w io.Writer, oldTxt, newTxt string) (int, error) {
	var changedLines int

	// use scanner to read line by line
	sc := bufio.NewScanner(r)
	for sc.Scan() {
		line := sc.Text()
		if line == oldTxt {
			line = newTxt
			changedLines++
		}
		if _, err := io.WriteString(w, line+"\n"); err != nil {
			return changedLines, err
		}
	}

	return changedLines, sc.Err()
}

func replaceFile(t *testing.T, name, oldTxt, newTxt string) {
	// open original file
	f, err := os.Open(name)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	// create temp file
	tmp, err := ioutil.TempFile("", "replace-*")
	if err != nil {
		t.Fatal(err)
	}
	defer tmp.Close()

	// replace while copying from f to tmp
	linesChanged, err := replaceLine(f, tmp, oldTxt, newTxt)
	if err != nil {
		t.Fatal(err)
	}
	if linesChanged == 0 {
		t.Fatalf("no recurrences of %q in file %q", oldTxt, name)
	}

	// make sure the tmp file was successfully written to
	if err := tmp.Close(); err != nil {
		t.Fatal(err)
	}

	// close the file we're reading from
	if err := f.Close(); err != nil {
		t.Fatal(err)
	}

	// overwrite the original file with the temp file
	if err := os.Rename(tmp.Name(), name); err != nil {
		t.Fatal(err)
	}
}

func TestServerConfig_Parsing(t *testing.T) {
	noopExtra := func(c *Server) *Server { return c }

	cfgFromFile := func(t *testing.T, testFile, matchText, replaceText string) (*Server, error) {
		t.Helper()

		if matchText != "" {
			replaceFile(t, testFile, matchText, replaceText)
		}

		return mockConfigFromFile(t, testFile)
	}

	// load a config based on the server config with all options uncommented.
	loadFromDefaultFile := func(t *testing.T, testDir, matchText, replaceText string) (*Server, error) {
		t.Helper()

		defaultConfigFile := filepath.Join(testDir, sConfigUncomment)
		uncommentServerConfig(t, defaultConfigFile)

		return cfgFromFile(t, defaultConfigFile, matchText, replaceText)
	}

	// load a config file with a legacy storage config
	loadFromLegacyFile := func(t *testing.T, testDir, matchText, replaceText string) (*Server, error) {
		t.Helper()

		lcp := strings.Split(legacyConfig, "/")
		testLegacyConfigFile := filepath.Join(testDir, lcp[len(lcp)-1])
		if err := common.CopyFile(legacyConfig, testLegacyConfigFile); err != nil {
			return nil, err
		}

		return cfgFromFile(t, testLegacyConfigFile, matchText, replaceText)
	}

	loadFromFile := func(t *testing.T, testDir, matchText, replaceText string, legacy bool) (*Server, error) {
		if legacy {
			return loadFromLegacyFile(t, testDir, matchText, replaceText)
		}

		return loadFromDefaultFile(t, testDir, matchText, replaceText)
	}

	for name, tt := range map[string]struct {
		inTxt          string
		outTxt         string
		legacyStorage  bool
		extraConfig    func(c *Server) *Server
		expParseErr    error
		expValidateErr error
		expCheck       func(c *Server) error
	}{
		"bad engine section": {
			inTxt:       "engines:",
			outTxt:      "engine:",
			expParseErr: errors.New("field engine not found"),
		},
		"use legacy servers conf directive rather than engines": {
			inTxt:          "engines:",
			outTxt:         "servers:",
			expValidateErr: errors.New("use \"engines\" instead"),
		},
		"specify legacy servers conf directive in addition to engines": {
			inTxt:  "engines:",
			outTxt: "servers:",
			extraConfig: func(c *Server) *Server {
				var nilEngineConfig *engine.Config
				return c.WithEngines(nilEngineConfig)
			},
			expValidateErr: errors.New("use \"engines\" instead"),
		},
		"duplicates in bdev_list from config": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(
					engine.NewConfig().
						WithFabricInterface("qib0").
						WithFabricInterfacePort(20000).
						WithStorage(
							storage.NewTierConfig().
								WithScmClass("ram").
								WithScmRamdiskSize(1).
								WithScmMountPoint("/mnt/daos/2"),
							storage.NewTierConfig().
								WithBdevClass("nvme").
								WithBdevDeviceList(MockPCIAddr(1), MockPCIAddr(1)),
						))
			},
			expValidateErr: errors.New("bdev_list contains duplicate pci"),
		},
		"legacy storage; empty bdev_list": {
			legacyStorage: true,
			expCheck: func(c *Server) error {
				nr := len(c.Engines[0].Storage.Tiers)
				if nr != 1 {
					return errors.Errorf("want %d storage tiers, got %d", 1, nr)
				}
				return nil
			},
		},
		"legacy storage; no bdev_list": {
			legacyStorage: true,
			inTxt:         "  bdev_list: []",
			outTxt:        "",
			expCheck: func(c *Server) error {
				nr := len(c.Engines[0].Storage.Tiers)
				if nr != 1 {
					return errors.Errorf("want %d storage tiers, got %d", 1, nr)
				}
				return nil
			},
		},
		"legacy storage; no bdev_class": {
			legacyStorage: true,
			inTxt:         "  bdev_class: nvme",
			outTxt:        "",
			expCheck: func(c *Server) error {
				nr := len(c.Engines[0].Storage.Tiers)
				if nr != 1 {
					return errors.Errorf("want %d storage tiers, got %d", 1, nr)
				}
				return nil
			},
		},
		"legacy storage; non-empty bdev_list": {
			legacyStorage: true,
			inTxt:         "  bdev_list: []",
			outTxt:        "  bdev_list: [0000:80:00.0]",
			expCheck: func(c *Server) error {
				nr := len(c.Engines[0].Storage.Tiers)
				if nr != 2 {
					return errors.Errorf("want %d storage tiers, got %d", 2, nr)
				}
				return nil
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			testDir, cleanup := CreateTestDir(t)
			defer cleanup()

			if tt.extraConfig == nil {
				tt.extraConfig = noopExtra
			}

			config, errParse := loadFromFile(t, testDir, tt.inTxt, tt.outTxt, tt.legacyStorage)
			CmpErr(t, tt.expParseErr, errParse)
			if tt.expParseErr != nil {
				return
			}
			config = tt.extraConfig(config)
			log.Debugf("%+v", config)

			CmpErr(t, tt.expValidateErr, config.Validate(log))

			if tt.expCheck != nil {
				if err := tt.expCheck(config); err != nil {
					t.Fatal(err)
				}
			}
		})
	}
}

func TestServerConfig_RelativeWorkingPath(t *testing.T) {
	for name, tt := range map[string]struct {
		inPath    string
		expErrMsg string
	}{
		"path exists":         {inPath: "uncommentedDefault"},
		"path does not exist": {expErrMsg: "no such file or directory"},
	} {
		t.Run(name, func(t *testing.T) {
			testDir, cleanup := CreateTestDir(t)
			defer cleanup()
			testFile := filepath.Join(testDir, "test.yml")

			if tt.inPath == "uncommentedDefault" {
				tt.inPath = filepath.Join(testDir, sConfigUncomment)
				uncommentServerConfig(t, testFile)
			}

			cwd, err := os.Getwd()
			if err != nil {
				t.Fatal(err)
			}
			var pathToRoot string
			depth := strings.Count(cwd, "/")
			for i := 0; i < depth; i++ {
				pathToRoot += "../"
			}

			relPath := filepath.Join(pathToRoot, testFile)
			t.Logf("abs: %s, cwd: %s, rel: %s", testFile, cwd, relPath)

			config := DefaultServer().
				WithProviderValidator(netdetect.ValidateProviderStub).
				WithNUMAValidator(netdetect.ValidateNUMAStub).
				WithGetNetworkDeviceClass(getDeviceClassStub)

			err = config.SetPath(relPath)
			if err != nil {
				if tt.expErrMsg == "" {
					t.Fatal(err)
				}
				if !strings.Contains(err.Error(), tt.expErrMsg) {
					t.Fatalf("want contains: %s, got %s", tt.expErrMsg, err)
				}
			} else {
				if tt.expErrMsg != "" {
					t.Fatalf("want contains: %s, got %s", tt.expErrMsg, err)
				}
			}
		})
	}
}

func TestServerConfig_WithEnginesInheritsMain(t *testing.T) {
	testFabric := "test-fabric"
	testModules := "a,b,c"
	testSystemName := "test-system"
	testSocketDir := "test-sockets"

	wantCfg := engine.NewConfig().
		WithFabricProvider(testFabric).
		WithModules(testModules).
		WithSocketDir(testSocketDir).
		WithSystemName(testSystemName)

	config := DefaultServer().
		WithFabricProvider(testFabric).
		WithModules(testModules).
		WithSocketDir(testSocketDir).
		WithSystemName(testSystemName).
		WithEngines(engine.NewConfig())

	if diff := cmp.Diff(wantCfg, config.Engines[0]); diff != "" {
		t.Fatalf("unexpected server config (-want, +got):\n%s\n", diff)
	}
}

func TestServerConfig_DuplicateValues(t *testing.T) {
	configA := func() *engine.Config {
		return engine.NewConfig().
			WithLogFile("a").
			WithFabricInterface("a").
			WithFabricInterfacePort(42).
			WithStorage(
				storage.NewTierConfig().
					WithScmClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint("a"),
			)
	}
	configB := func() *engine.Config {
		return engine.NewConfig().
			WithLogFile("b").
			WithFabricInterface("b").
			WithFabricInterfacePort(42).
			WithStorage(
				storage.NewTierConfig().
					WithScmClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint("b"),
			)
	}

	for name, tc := range map[string]struct {
		configA *engine.Config
		configB *engine.Config
		expErr  error
	}{
		"successful validation": {
			configA: configA(),
			configB: configB(),
		},
		"duplicate fabric config": {
			configA: configA(),
			configB: configB().
				WithFabricInterface(configA().Fabric.Interface),
			expErr: FaultConfigDuplicateFabric(1, 0),
		},
		"duplicate log_file": {
			configA: configA(),
			configB: configB().
				WithLogFile(configA().LogFile),
			expErr: FaultConfigDuplicateLogFile(1, 0),
		},
		"duplicate scm_mount": {
			configA: configA(),
			configB: configB().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass(storage.ClassDcpm.String()).
						WithScmDeviceList("a").
						WithScmMountPoint(configA().Storage.Tiers.ScmConfigs()[0].Scm.MountPoint),
				),
			expErr: FaultConfigDuplicateScmMount(1, 0),
		},
		"duplicate scm_list": {
			configA: configA().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass(storage.ClassDcpm.String()).
						WithScmMountPoint("aa").
						WithScmDeviceList("a"),
				),
			configB: configB().
				WithStorage(
					storage.NewTierConfig().
						WithScmClass(storage.ClassDcpm.String()).
						WithScmMountPoint("bb").
						WithScmDeviceList("a"),
				),
			expErr: FaultConfigDuplicateScmDeviceList(1, 0),
		},
		"overlapping bdev_list": {
			configA: configA().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass(storage.ClassNvme.String()).
						WithBdevDeviceList(MockPCIAddr(1)),
				),
			configB: configB().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass(storage.ClassNvme.String()).
						WithBdevDeviceList(MockPCIAddr(2), MockPCIAddr(1)),
				),
			expErr: FaultConfigOverlappingBdevDeviceList(1, 0),
		},
		"duplicates in bdev_list": {
			configA: configA().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass(storage.ClassNvme.String()).
						WithBdevDeviceList(MockPCIAddr(1), MockPCIAddr(1)),
				),
			configB: configB().
				WithStorage(
					storage.NewTierConfig().
						WithBdevClass(storage.ClassNvme.String()).
						WithBdevDeviceList(MockPCIAddr(2), MockPCIAddr(2)),
				),
			expErr: errors.New("bdev_list contains duplicate pci addresses"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			conf := DefaultServer().
				WithFabricProvider("test").
				WithGetNetworkDeviceClass(getDeviceClassStub).
				WithEngines(tc.configA, tc.configB)

			gotErr := conf.Validate(log)
			CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestServerConfig_NetworkDeviceClass(t *testing.T) {
	configA := func() *engine.Config {
		return engine.NewConfig().
			WithLogFile("a").
			WithStorage(
				storage.NewTierConfig().
					WithScmClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint("a"),
			).
			WithFabricInterfacePort(42)
	}
	configB := func() *engine.Config {
		return engine.NewConfig().
			WithLogFile("b").
			WithStorage(
				storage.NewTierConfig().
					WithScmClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint("b"),
			).
			WithFabricInterfacePort(43)
	}

	for name, tc := range map[string]struct {
		configA      *engine.Config
		configB      *engine.Config
		expNetDevCls uint32
		expErr       error
	}{
		"successful validation with matching Infiniband": {
			configA: configA().
				WithFabricInterface("ib1"),
			configB: configB().
				WithFabricInterface("ib0"),
			expNetDevCls: netdetect.Infiniband,
		},
		"successful validation with matching Ethernet": {
			configA: configA().
				WithFabricInterface("eth0"),
			configB: configB().
				WithFabricInterface("eth1"),
			expNetDevCls: netdetect.Ether,
		},
		"mismatching net dev class with primary server as ib0 / Infiniband": {
			configA: configA().
				WithFabricInterface("ib0"),
			configB: configB().
				WithFabricInterface("eth0"),
			expErr: FaultConfigInvalidNetDevClass(1, netdetect.Infiniband, netdetect.Ether, "eth0"),
		},
		"mismatching net dev class with primary server as eth0 / Ethernet": {
			configA: configA().
				WithFabricInterface("eth0"),
			configB: configB().
				WithFabricInterface("ib0"),
			expErr: FaultConfigInvalidNetDevClass(1, netdetect.Ether, netdetect.Infiniband, "ib0"),
		},
		"mismatching net dev class with primary server as ib1 / Infiniband": {
			configA: configA().
				WithFabricInterface("ib1"),
			configB: configB().
				WithFabricInterface("eth1"),
			expErr: FaultConfigInvalidNetDevClass(1, netdetect.Infiniband, netdetect.Ether, "eth1"),
		},
		"mismatching net dev class with primary server as eth1 / Ethernet": {
			configA: configA().
				WithFabricInterface("eth1"),
			configB: configB().
				WithFabricInterface("ib0"),
			expErr: FaultConfigInvalidNetDevClass(1, netdetect.Ether, netdetect.Infiniband, "ib0"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotNetDevCls, gotErr := DefaultServer().
				WithFabricProvider("test").
				WithGetNetworkDeviceClass(getDeviceClassStub).
				WithEngines(tc.configA, tc.configB).
				CheckFabric(context.Background())

			CmpErr(t, tc.expErr, gotErr)
			if gotErr != nil {
				return
			}

			AssertEqual(t, tc.expNetDevCls, gotNetDevCls,
				"unexpected config network device class")
		})
	}
}

func TestServerConfig_SaveActiveConfig(t *testing.T) {
	testDir, cleanup := CreateTestDir(t)
	defer cleanup()

	t.Logf("test dir: %s", testDir)

	for name, tc := range map[string]struct {
		cfgPath   string
		expLogOut string
	}{
		"successful write": {
			cfgPath:   testDir,
			expLogOut: fmt.Sprintf("config saved to %s/%s", testDir, configOut),
		},
		"missing directory": {
			cfgPath:   filepath.Join(testDir, "non-existent/"),
			expLogOut: "could not be saved",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			cfg := DefaultServer().WithSocketDir(tc.cfgPath)

			cfg.SaveActiveConfig(log)

			common.AssertTrue(t, strings.Contains(buf.String(), tc.expLogOut),
				fmt.Sprintf("expected %q in %q", tc.expLogOut, buf.String()))
		})
	}
}
