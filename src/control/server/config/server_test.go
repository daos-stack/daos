//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package config

import (
	"bufio"
	"fmt"
	"io"
	"io/ioutil"
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	. "github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	sConfigUncomment = "daos_server_uncomment.yml"
	tcpExample       = "../../../../utils/config/examples/daos_server_tcp.yml"
	verbsExample     = "../../../../utils/config/examples/daos_server_verbs.yml"
	defaultConfig    = "../../../../utils/config/daos_server.yml"
	legacyConfig     = "../../../../utils/config/examples/daos_server_unittests.yml"
)

var (
	defConfigCmpOpts = []cmp.Option{
		cmpopts.IgnoreUnexported(
			security.CertificateConfig{},
		),
		cmpopts.IgnoreFields(Server{}, "Path"),
		cmp.Comparer(func(x, y *storage.BdevDeviceList) bool {
			if x == nil && y == nil {
				return true
			}
			return x.Equals(y)
		}),
	}

	defHugePageInfo = &common.HugePageInfo{
		PageSizeKb: 2048,
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
	c := DefaultServer()
	c.Path = path

	return c, c.Load()
}

func TestServerConfig_MarshalUnmarshal(t *testing.T) {
	for name, tt := range map[string]struct {
		inPath string
		expErr error
	}{
		"uncommented default config": {inPath: "uncommentedDefault"},
		"socket example config":      {inPath: tcpExample},
		"verbs example config":       {inPath: verbsExample},
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

			configA := DefaultServer()
			configA.Path = tt.inPath
			err := configA.Load()
			if err == nil {
				err = configA.Validate(log, defHugePageInfo.PageSizeKb)
			}

			CmpErr(t, tt.expErr, err)
			if tt.expErr != nil {
				return
			}

			if err := configA.SaveToFile(testFile); err != nil {
				t.Fatal(err)
			}

			configB := DefaultServer()
			if err := configB.SetPath(testFile); err != nil {
				t.Fatal(err)
			}

			err = configB.Load()
			if err == nil {
				err = configB.Validate(log, defHugePageInfo.PageSizeKb)
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

	var bypass = true

	// Next, construct a config to compare against the first one. It should be
	// possible to construct an identical configuration with the helpers.
	constructed := DefaultServer().
		WithControlPort(10001).
		WithBdevInclude("0000:81:00.1", "0000:81:00.2", "0000:81:00.3").
		WithBdevExclude("0000:81:00.1").
		WithDisableVFIO(true).   // vfio enabled by default
		WithDisableVMD(true).    // vmd enabled by default
		WithEnableHotplug(true). // hotplug disabled by default
		WithControlLogMask(common.ControlLogLevelError).
		WithControlLogFile("/tmp/daos_server.log").
		WithHelperLogFile("/tmp/daos_admin.log").
		WithFirmwareHelperLogFile("/tmp/daos_firmware.log").
		WithTelemetryPort(9191).
		WithSystemName("daos_server").
		WithSocketDir("./.daos/daos_server").
		WithFabricProvider("ofi+verbs;ofi_rxm").
		WithCrtCtxShareAddr(0).
		WithCrtTimeout(30).
		WithAccessPoints("hostname1").
		WithFaultCb("./.daos/fd_callback").
		WithFaultPath("/vcdu0/rack1/hostname").
		WithHyperthreads(true) // hyper-threads disabled by default

	// add engines explicitly to test functionality applied in WithEngines()
	constructed.Engines = []*engine.Config{
		engine.MockConfig().
			WithSystemName("daos_server").
			WithSocketDir("./.daos/daos_server").
			WithRank(0).
			WithTargetCount(16).
			WithHelperStreamCount(4).
			WithServiceThreadCore(0).
			WithStorage(
				storage.NewTierConfig().
					WithScmMountPoint("/mnt/daos/1").
					WithStorageClass("ram").
					WithScmRamdiskSize(16),
				storage.NewTierConfig().
					WithStorageClass("nvme").
					WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
					WithBdevBusidRange("0x80-0x8f"),
			).
			WithFabricInterface("ib0").
			WithFabricInterfacePort(20000).
			WithFabricProvider("ofi+verbs;ofi_rxm").
			WithCrtCtxShareAddr(0).
			WithCrtTimeout(30).
			WithPinnedNumaNode(0).
			WithBypassHealthChk(&bypass).
			WithEnvVars("CRT_TIMEOUT=30").
			WithLogFile("/tmp/daos_engine.0.log").
			WithLogMask("INFO").
			WithStorageEnableHotplug(true).
			WithStorageAccelEngine("spdk").
			WithStorageAccelOptMove(true).
			WithStorageAccelOptCRC(true),
		engine.MockConfig().
			WithSystemName("daos_server").
			WithSocketDir("./.daos/daos_server").
			WithRank(1).
			WithTargetCount(16).
			WithHelperStreamCount(4).
			WithServiceThreadCore(22).
			WithStorage(
				storage.NewTierConfig().
					WithScmMountPoint("/mnt/daos/2").
					WithStorageClass("dcpm").
					WithScmDeviceList("/dev/pmem1"),
				storage.NewTierConfig().
					WithStorageClass("file").
					WithBdevDeviceList("/tmp/daos-bdev1", "/tmp/daos-bdev2").
					WithBdevFileSize(16),
			).
			WithFabricInterface("ib1").
			WithFabricInterfacePort(20000).
			WithFabricProvider("ofi+verbs;ofi_rxm").
			WithCrtCtxShareAddr(0).
			WithCrtTimeout(30).
			WithBypassHealthChk(&bypass).
			WithEnvVars("CRT_TIMEOUT=100").
			WithLogFile("/tmp/daos_engine.1.log").
			WithLogMask("INFO").
			WithStorageEnableHotplug(true).
			WithStorageAccelEngine("dml").
			WithStorageAccelOptCRC(true),
	}
	constructed.Path = testFile // just to avoid failing the cmp

	for i := range constructed.Engines {
		t.Logf("constructed: %+v", constructed.Engines[i])
		t.Logf("default: %+v", defaultCfg.Engines[i])
	}

	if diff := cmp.Diff(defaultCfg, constructed, defConfigCmpOpts...); diff != "" {
		t.Fatalf("(-want, +got): %s", diff)
	}
}

func TestServerConfig_Validation(t *testing.T) {
	testDir, cleanup := CreateTestDir(t)
	defer cleanup()

	// First, load a config based on the server config with all options uncommented.
	testFile := filepath.Join(testDir, sConfigUncomment)
	uncommentServerConfig(t, testFile)

	baseCfg := func() *Server {
		config, err := mockConfigFromFile(t, testFile)
		if err != nil {
			t.Fatalf("failed to load %s: %s", testFile, err)
		}
		return config
	}
	defaultEngineCfg := func() *engine.Config {
		return engine.NewConfig().
			WithFabricInterfacePort(1234).
			WithFabricInterface("eth0").
			WithTargetCount(8).
			WithPinnedNumaNode(0).
			WithStorageAccelEngine("native")
	}
	noopExtra := func(c *Server) *Server { return c }

	for name, tt := range map[string]struct {
		extraConfig func(c *Server) *Server
		expConfig   *Server
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
		"different number of ssds": {
			extraConfig: func(c *Server) *Server {
				// add multiple bdevs for engine 0 to create mismatch
				c.Engines[0].Storage.Tiers.BdevConfigs()[0].
					WithBdevDeviceList("0000:10:00.0", "0000:11:00.0", "0000:12:00.0")

				return c
			},
			expErr: FaultConfigBdevCountMismatch(1, 2, 0, 3),
		},
		"different number of targets": {
			extraConfig: func(c *Server) *Server {
				// change engine 0 number of targets to create mismatch
				c.Engines[0].WithTargetCount(1)

				return c
			},
			expErr: FaultConfigTargetCountMismatch(1, 16, 0, 1),
		},
		"different number of helper streams": {
			extraConfig: func(c *Server) *Server {
				// change engine 0 number of helper streams to create mismatch
				c.Engines[0].WithHelperStreamCount(9)

				return c
			},
			expErr: FaultConfigHelperStreamCountMismatch(1, 4, 0, 9),
		},
		"out of range hugepages; low": {
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugePages(-2048)
			},
			expErr: FaultConfigNrHugepagesOutOfRange,
		},
		"out of range hugepages; high": {
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugePages(math.MaxInt32 + 1)
			},
			expErr: FaultConfigNrHugepagesOutOfRange,
		},
		"disabled hugepages; bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugePages(-1).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmRamdiskSize(1).
								WithScmMountPoint("/foo"),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:81:00.0"),
						),
					)
			},
			expErr: FaultConfigHugepagesDisabled,
		},
		"disabled hugepages; emulated bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugePages(-1).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmRamdiskSize(1).
								WithScmMountPoint("/foo"),
							storage.NewTierConfig().
								WithStorageClass("file").
								WithBdevDeviceList("/tmp/daos-bdev").
								WithBdevFileSize(16),
						),
					)
			},
			expErr: FaultConfigHugepagesDisabled,
		},
		"disabled hugepages; no bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugePages(-1).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmRamdiskSize(1).
								WithScmMountPoint("/foo"),
						),
					)
			},
		},
		"insufficient hugepages set in config; bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugePages(2048).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmRamdiskSize(1).
								WithScmMountPoint("/foo"),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:81:00.0"),
						),
					)
			},
			expErr: FaultConfigInsufficientHugePages(4096, 2048),
		},
		"insufficient hugepages set in config; emulated bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugePages(2048).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmRamdiskSize(1).
								WithScmMountPoint("/foo"),
							storage.NewTierConfig().
								WithStorageClass("file").
								WithBdevDeviceList("/tmp/daos-bdev").
								WithBdevFileSize(16),
						),
					)
			},
			expErr: FaultConfigInsufficientHugePages(4096, 2048),
		},
		"insufficient hugepages set in config; no bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugePages(2048).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmRamdiskSize(1).
								WithScmMountPoint("/foo"),
						),
					)
			},
			expConfig: baseCfg().
				WithAccessPoints("hostname1:10001").
				WithNrHugePages(2048).
				WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmRamdiskSize(1).
							WithScmMountPoint("/foo"),
					),
				),
		},
		"zero hugepages set in config; bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmRamdiskSize(1).
							WithScmMountPoint("/foo"),
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("0000:81:00.0"),
					),
				)
			},
			expConfig: baseCfg().
				WithAccessPoints("hostname1:10001").
				WithNrHugePages(4096).
				WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmRamdiskSize(1).
							WithScmMountPoint("/foo"),
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("0000:81:00.0"),
					).
					WithStorageConfigOutputPath("/foo/daos_nvme.conf").
					WithStorageVosEnv("NVME"),
				),
		},
		"zero hugepages set in config; emulated bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmRamdiskSize(1).
							WithScmMountPoint("/foo"),
						storage.NewTierConfig().
							WithStorageClass("file").
							WithBdevDeviceList("/tmp/daos-bdev").
							WithBdevFileSize(16),
					),
				)
			},
			expConfig: baseCfg().
				WithAccessPoints("hostname1:10001").
				WithNrHugePages(4096).
				WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmRamdiskSize(1).
							WithScmMountPoint("/foo"),
						storage.NewTierConfig().
							WithStorageClass("file").
							WithBdevDeviceList("/tmp/daos-bdev").
							WithBdevFileSize(16),
					).
					WithStorageConfigOutputPath("/foo/daos_nvme.conf").
					WithStorageVosEnv("AIO"),
				),
		},
		"zero hugepages set in config; no bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmRamdiskSize(1).
							WithScmMountPoint("/foo"),
					),
				)
			},
			expConfig: baseCfg().
				WithAccessPoints("hostname1:10001").
				WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmRamdiskSize(1).
							WithScmMountPoint("/foo"),
					),
				),
		},
		"acceleration set to spdk; crc opt enabled": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmRamdiskSize(1).
							WithScmMountPoint("/foo"),
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("0000:81:00.0"),
					).
					WithStorageAccelEngine("spdk").
					WithStorageAccelOptCRC(true),
				)
			},
			expConfig: baseCfg().
				WithAccessPoints("hostname1:10001").
				WithNrHugePages(4096).
				WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmRamdiskSize(1).
							WithScmMountPoint("/foo"),
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("0000:81:00.0"),
					).
					WithStorageConfigOutputPath("/foo/daos_nvme.conf").
					WithStorageVosEnv("NVME").
					WithStorageAccelEngine("spdk").
					WithStorageAccelOptCRC(true).
					WithStorageAccelOpts(0b10),
				),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			if tt.extraConfig == nil {
				tt.extraConfig = noopExtra
			}

			// Apply extra config test case
			dupe := tt.extraConfig(baseCfg())

			CmpErr(t, tt.expErr, dupe.Validate(log, defHugePageInfo.PageSizeKb))
			if tt.expErr != nil || tt.expConfig == nil {
				return
			}

			if diff := cmp.Diff(tt.expConfig, dupe, defConfigCmpOpts...); diff != "" {
				t.Fatalf("unexpected config after validation (-want, +got): %s", diff)
			}
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

	cfgFromFile := func(t *testing.T, testFile string, matchText, replaceText []string) (*Server, error) {
		t.Helper()

		if len(matchText) != len(replaceText) {
			return nil, errors.New("number of text matches and replacements must be equal")
		}

		for i, m := range matchText {
			if m == "" {
				continue
			}
			replaceFile(t, testFile, m, replaceText[i])
		}

		return mockConfigFromFile(t, testFile)
	}

	// load a config based on the server config with all options uncommented.
	loadFromDefaultFile := func(t *testing.T, testDir string, matchText, replaceText []string) (*Server, error) {
		t.Helper()

		defaultConfigFile := filepath.Join(testDir, sConfigUncomment)
		uncommentServerConfig(t, defaultConfigFile)

		return cfgFromFile(t, defaultConfigFile, matchText, replaceText)
	}

	// load a config file with a legacy storage config
	loadFromLegacyFile := func(t *testing.T, testDir string, matchText, replaceText []string) (*Server, error) {
		t.Helper()

		lcp := strings.Split(legacyConfig, "/")
		testLegacyConfigFile := filepath.Join(testDir, lcp[len(lcp)-1])
		if err := common.CopyFile(legacyConfig, testLegacyConfigFile); err != nil {
			return nil, err
		}

		return cfgFromFile(t, testLegacyConfigFile, matchText, replaceText)
	}

	loadFromFile := func(t *testing.T, testDir string, matchText, replaceText []string, legacy bool) (*Server, error) {
		if legacy {
			return loadFromLegacyFile(t, testDir, matchText, replaceText)
		}

		return loadFromDefaultFile(t, testDir, matchText, replaceText)
	}

	for name, tt := range map[string]struct {
		inTxt          string
		outTxt         string
		inTxtList      []string
		outTxtList     []string
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
					engine.MockConfig().
						WithFabricInterface("ib0").
						WithFabricInterfacePort(20000).
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmRamdiskSize(1).
								WithScmMountPoint("/mnt/daos/2"),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList(MockPCIAddr(1), MockPCIAddr(1)),
						).
						WithTargetCount(8))
			},
			expValidateErr: errors.New("valid PCI addresses"),
		},
		"bad busid range": {
			// fail first engine storage
			inTxt:       "    bdev_busid_range: 0x80-0x8f",
			outTxt:      "    bdev_busid_range: 0x80-0x8g",
			expParseErr: errors.New("\"0x8g\": invalid syntax"),
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
			inTxt:         "    bdev_list: []",
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
			inTxt:         "    bdev_class: nvme",
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
			inTxt:         "    bdev_list: []",
			outTxt:        "    bdev_list: [0000:80:00.0]",
			expCheck: func(c *Server) error {
				nr := len(c.Engines[0].Storage.Tiers)
				if nr != 2 {
					return errors.Errorf("want %d storage tiers, got %d", 2, nr)
				}
				return nil
			},
		},
		"legacy storage; non-empty bdev_busid_range": {
			legacyStorage: true,
			inTxtList: []string{
				"    bdev_list: []", "    bdev_busid_range: \"\"",
			},
			outTxtList: []string{
				"    bdev_list: [0000:80:00.0]", "    bdev_busid_range: \"0x00-0x80\"",
			},
			expCheck: func(c *Server) error {
				nr := len(c.Engines[0].Storage.Tiers)
				if nr != 2 {
					return errors.Errorf("want %d storage tiers, got %d", 2, nr)
				}

				want := storage.MustNewBdevBusRange("0x00-0x80")
				got := c.Engines[0].Storage.Tiers.BdevConfigs()[0].Bdev.BusidRange
				if want.String() != got.String() {
					return errors.Errorf("want %s bus-id range, got %s", want, got)
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

			if tt.inTxtList != nil {
				if tt.inTxt != "" {
					t.Fatal("bad test params")
				}
			} else {
				tt.inTxtList = []string{tt.inTxt}
			}
			if tt.outTxtList != nil {
				if tt.outTxt != "" {
					t.Fatal("bad test params")
				}
			} else {
				tt.outTxtList = []string{tt.outTxt}
			}

			config, errParse := loadFromFile(t, testDir, tt.inTxtList, tt.outTxtList, tt.legacyStorage)
			CmpErr(t, tt.expParseErr, errParse)
			if tt.expParseErr != nil {
				return
			}
			config = tt.extraConfig(config)

			CmpErr(t, tt.expValidateErr, config.Validate(log, defHugePageInfo.PageSizeKb))

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

			config := DefaultServer()

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

	wantCfg := engine.MockConfig().
		WithFabricProvider(testFabric).
		WithModules(testModules).
		WithSocketDir(testSocketDir).
		WithSystemName(testSystemName)

	config := DefaultServer().
		WithFabricProvider(testFabric).
		WithModules(testModules).
		WithSocketDir(testSocketDir).
		WithSystemName(testSystemName).
		WithEngines(engine.MockConfig())

	if diff := cmp.Diff(wantCfg, config.Engines[0], defConfigCmpOpts...); diff != "" {
		t.Fatalf("unexpected server config (-want, +got):\n%s\n", diff)
	}
}

func TestServerConfig_DuplicateValues(t *testing.T) {
	configA := func() *engine.Config {
		return engine.MockConfig().
			WithLogFile("a").
			WithFabricInterface("ib0").
			WithFabricInterfacePort(42).
			WithStorage(
				storage.NewTierConfig().
					WithStorageClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint("a"),
			).
			WithPinnedNumaNode(0).
			WithTargetCount(8)
	}
	configB := func() *engine.Config {
		return engine.MockConfig().
			WithLogFile("b").
			WithFabricInterface("ib1").
			WithFabricInterfacePort(42).
			WithStorage(
				storage.NewTierConfig().
					WithStorageClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint("b"),
			).
			WithPinnedNumaNode(0).
			WithTargetCount(8)
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
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmDeviceList("a").
						WithScmMountPoint(configA().Storage.Tiers.ScmConfigs()[0].Scm.MountPoint),
				),
			expErr: FaultConfigDuplicateScmMount(1, 0),
		},
		"duplicate scm_list": {
			configA: configA().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint("aa").
						WithScmDeviceList("a"),
				),
			configB: configB().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint("bb").
						WithScmDeviceList("a"),
				),
			expErr: FaultConfigDuplicateScmDeviceList(1, 0),
		},
		"overlapping bdev_list": {
			configA: configA().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(MockPCIAddr(1)),
				),
			configB: configB().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(MockPCIAddr(2), MockPCIAddr(1)),
				),
			expErr: FaultConfigOverlappingBdevDeviceList(1, 0),
		},
		"duplicates in bdev_list": {
			configA: configA().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(MockPCIAddr(1), MockPCIAddr(1)),
				),
			configB: configB().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(MockPCIAddr(2), MockPCIAddr(2)),
				),
			expErr: errors.New("valid PCI addresses"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			conf := DefaultServer().
				WithFabricProvider("test").
				WithEngines(tc.configA, tc.configB)

			gotErr := conf.Validate(log, defHugePageInfo.PageSizeKb)
			CmpErr(t, tc.expErr, gotErr)
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

			AssertTrue(t, strings.Contains(buf.String(), tc.expLogOut),
				fmt.Sprintf("expected %q in %q", tc.expLogOut, buf.String()))
		})
	}
}

func TestConfig_detectEngineAffinity(t *testing.T) {
	genAffFn := func(node uint, err error) EngineAffinityFn {
		return func(logging.Logger, *engine.Config) (uint, error) {
			return node, err
		}
	}

	for name, tc := range map[string]struct {
		cfg         *engine.Config
		affSrcSet   []EngineAffinityFn
		expErr      error
		expDetected uint
	}{
		"first source misses; second hits": {
			cfg: engine.MockConfig(),
			affSrcSet: []EngineAffinityFn{
				genAffFn(0, ErrNoAffinityDetected),
				genAffFn(1, nil),
			},
			expDetected: 1,
		},
		"first source hits": {
			cfg: engine.MockConfig(),
			affSrcSet: []EngineAffinityFn{
				genAffFn(1, nil),
				genAffFn(2, nil),
			},
			expDetected: 1,
		},
		"first source errors": {
			cfg: engine.MockConfig(),
			affSrcSet: []EngineAffinityFn{
				genAffFn(1, errors.New("fatal")),
				genAffFn(2, nil),
			},
			expErr: errors.New("fatal"),
		},
		"no sources hit": {
			cfg: engine.MockConfig(),
			affSrcSet: []EngineAffinityFn{
				genAffFn(1, ErrNoAffinityDetected),
				genAffFn(2, ErrNoAffinityDetected),
			},
			expErr: ErrNoAffinityDetected,
		},
		"no sources defined": {
			cfg:    engine.MockConfig(),
			expErr: ErrNoAffinityDetected,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			detected, err := detectEngineAffinity(log, tc.cfg, tc.affSrcSet...)
			CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			AssertEqual(t, tc.expDetected, detected,
				"unexpected detected numa node")
		})
	}
}

func TestConfig_setEngineAffinity(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg     *engine.Config
		setNUMA uint
		expErr  error
		expNUMA uint
	}{
		"pinned_numa_node set in config overrides detected affinity": {
			cfg: engine.MockConfig().
				WithPinnedNumaNode(2).
				WithFabricInterface("ib1").
				WithFabricProvider("ofi+verbs"),
			setNUMA: 1,
			expNUMA: 2,
		},
		"pinned_numa_node not set in config; detected affinity used": {
			cfg: engine.MockConfig().
				WithFabricInterface("ib1").
				WithFabricProvider("ofi+verbs"),
			setNUMA: 1,
			expNUMA: 1,
		},
		"pinned_numa_node and first_core set": {
			cfg: engine.MockConfig().
				WithPinnedNumaNode(2).
				WithServiceThreadCore(1).
				WithFabricInterface("ib1").
				WithFabricProvider("ofi+verbs"),
			expErr: errors.New("cannot set both"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			err := setEngineAffinity(log, tc.cfg, tc.setNUMA)
			CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			AssertEqual(t, tc.expNUMA, *tc.cfg.PinnedNumaNode,
				"unexpected pinned numa node")
			AssertEqual(t, tc.expNUMA, tc.cfg.Fabric.NumaNodeIndex,
				"unexpected numa node in fabric config")
			AssertEqual(t, tc.expNUMA, tc.cfg.Storage.NumaNodeIndex,
				"unexpected numa node in storage config")
		})
	}
}

func TestConfig_SetEngineAffinities(t *testing.T) {
	baseSrvCfg := func() *Server {
		return DefaultServer()
	}
	genAffFn := func(iface string, node uint) EngineAffinityFn {
		return func(_ logging.Logger, cfg *engine.Config) (uint, error) {
			if iface == cfg.Fabric.Interface {
				return node, nil
			}
			return 0, ErrNoAffinityDetected
		}
	}

	for name, tc := range map[string]struct {
		cfg         *Server
		affSrcSet   []EngineAffinityFn
		expNumaSet  []int
		expFabNumas []int
		expErr      error
	}{
		"no affinity detected (default NUMA nodes)": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs"),
					engine.MockConfig().
						WithFabricInterface("ib1").
						WithFabricProvider("ofi+verbs"),
				),
			expNumaSet: []int{0, 0},
		},
		"engines have first_core set; NUMA nodes should not be set": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs").
						WithServiceThreadCore(1),
					engine.MockConfig().
						WithFabricInterface("ib1").
						WithFabricProvider("ofi+verbs").
						WithServiceThreadCore(2),
				),
			expNumaSet: []int{-1, -1},
		},
		"single engine with pinned_numa_node set": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs").
						WithPinnedNumaNode(1),
				),
			expNumaSet: []int{1},
		},
		"single engine without pinned_numa_node set and no detected affinity": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs"),
				),
			expNumaSet: []int{-1},
		},
		"single engine without pinned_numa_node set and affinity detected as != 0": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs"),
				),
			affSrcSet: []EngineAffinityFn{
				genAffFn("ib0", 1),
			},
			expNumaSet: []int{1},
		},
		"single engine without pinned_numa_node set and affinity detected as 0": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs"),
				),
			affSrcSet: []EngineAffinityFn{
				genAffFn("ib0", 0),
			},
			expNumaSet: []int{-1},
		},
		"multi engine without pinned_numa_node set and affinity for both detected as 0": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs"),
					engine.MockConfig().
						WithFabricInterface("ib1").
						WithFabricProvider("ofi+verbs"),
				),
			affSrcSet: []EngineAffinityFn{
				genAffFn("ib0", 0),
				genAffFn("ib1", 0),
			},
			expNumaSet: []int{0, 0},
		},
		"multi engine without pinned_numa_node set": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs"),
					engine.MockConfig().
						WithFabricInterface("ib1").
						WithFabricProvider("ofi+verbs"),
				),
			affSrcSet: []EngineAffinityFn{
				genAffFn("ib0", 1),
				genAffFn("ib1", 2),
			},
			expNumaSet: []int{1, 2},
		},
		"multi engine with pinned_numa_node set matching detected affinities": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithPinnedNumaNode(1).
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs"),
					engine.MockConfig().
						WithPinnedNumaNode(2).
						WithFabricInterface("ib1").
						WithFabricProvider("ofi+verbs"),
				),
			affSrcSet: []EngineAffinityFn{
				genAffFn("ib0", 1),
				genAffFn("ib1", 2),
			},
			expNumaSet: []int{1, 2},
		},
		"multi engine with pinned_numa_node set overriding detected affinities": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithPinnedNumaNode(2).
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs"),
					engine.MockConfig().
						WithPinnedNumaNode(1).
						WithFabricInterface("ib1").
						WithFabricProvider("ofi+verbs"),
				),
			affSrcSet: []EngineAffinityFn{
				genAffFn("ib0", 1),
				genAffFn("ib1", 2),
			},
			expNumaSet: []int{2, 1},
		},
		"multi engine with first_core set; detected affinities take precedence": {
			cfg: baseSrvCfg().
				WithEngines(
					engine.MockConfig().
						WithServiceThreadCore(1).
						WithFabricInterface("ib0").
						WithFabricProvider("ofi+verbs"),
					engine.MockConfig().
						WithServiceThreadCore(25).
						WithFabricInterface("ib1").
						WithFabricProvider("ofi+verbs"),
				),
			affSrcSet: []EngineAffinityFn{
				genAffFn("ib0", 1),
				genAffFn("ib1", 2),
			},
			expNumaSet:  []int{-1, -1}, // PinnedNumaNode should not be set
			expFabNumas: []int{1, 2},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotNumaSet := make([]int, 0, len(tc.expNumaSet))
			fabNumaSet := make([]int, 0, len(tc.expFabNumas))

			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			gotErr := tc.cfg.SetEngineAffinities(log, tc.affSrcSet...)
			CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			for _, engine := range tc.cfg.Engines {
				fabNumaSet = append(fabNumaSet, int(engine.Fabric.NumaNodeIndex))
				if engine.PinnedNumaNode == nil {
					gotNumaSet = append(gotNumaSet, -1)
					continue
				}
				gotNumaSet = append(gotNumaSet, int(*engine.PinnedNumaNode))
			}

			if diff := cmp.Diff(tc.expNumaSet, gotNumaSet); diff != "" {
				t.Errorf("unexpected engine numa node set (-want +got):\n%s", diff)
			}
			if tc.expFabNumas != nil {
				if diff := cmp.Diff(tc.expFabNumas, fabNumaSet); diff != "" {
					t.Errorf("unexpected fabric numa node set (-want +got):\n%s", diff)
				}
			}
		})
	}
}
