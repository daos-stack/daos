//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package config

import (
	"bufio"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	sConfigUncomment = "daos_server_uncomment.yml"
	tcpExample       = "../../../../utils/config/examples/daos_server_tcp.yml"
	verbsExample     = "../../../../utils/config/examples/daos_server_verbs.yml"
	mdOnSSDExample   = "../../../../utils/config/examples/daos_server_mdonssd.yml"
	defaultConfig    = "../../../../utils/config/daos_server.yml"
	defHpSizeKb      = 2048
)

var (
	defConfigCmpOpts = []cmp.Option{
		cmpopts.SortSlices(func(x, y string) bool { return x < y }),
		cmpopts.IgnoreUnexported(
			security.CertificateConfig{},
			Server{},
		),
		cmpopts.IgnoreFields(Server{}, "Path"),
		cmp.Comparer(func(x, y *storage.BdevDeviceList) bool {
			if x == nil && y == nil {
				return true
			}
			return x.Equals(y)
		}),
	}
)

func baseCfg(t *testing.T, log logging.Logger, testFile string) *Server {
	t.Helper()

	config, err := mockConfigFromFile(t, log, testFile)
	if err != nil {
		t.Fatalf("failed to load %s: %s", testFile, err)
	}

	return config
}

func defaultEngineCfg() *engine.Config {
	return engine.NewConfig().
		WithFabricInterfacePort(1234).
		WithFabricInterface("eth0").
		WithTargetCount(8).
		WithPinnedNumaNode(0)
}

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
func mockConfigFromFile(t *testing.T, log logging.Logger, path string) (*Server, error) {
	t.Helper()
	c := DefaultServer()
	c.Path = path

	return c, c.Load(log)
}

func TestServerConfig_MarshalUnmarshal(t *testing.T) {
	for name, tt := range map[string]struct {
		inPath string
		expErr error
	}{
		"uncommented default config": {inPath: "uncommentedDefault"},
		"tcp example config":         {inPath: tcpExample},
		"verbs example config":       {inPath: verbsExample},
		"mdonssd example config":     {inPath: mdOnSSDExample},
		"default empty config":       {inPath: defaultConfig},
		"nonexistent config": {
			inPath: "/foo/bar/baz.yml",
			expErr: errors.New("reading file: open /foo/bar/baz.yml: no such file or directory"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanup := test.CreateTestDir(t)
			defer cleanup()
			testFile := filepath.Join(testDir, "test.yml")

			if tt.inPath == "uncommentedDefault" {
				tt.inPath = filepath.Join(testDir, sConfigUncomment)
				uncommentServerConfig(t, tt.inPath)
			}

			configA := DefaultServer()
			configA.Path = tt.inPath
			err := configA.Load(log)
			if err == nil {
				err = configA.Validate(log)
			}

			test.CmpErr(t, tt.expErr, err)
			if tt.expErr != nil {
				return
			}

			configAPretty, err := json.MarshalIndent(configA, "", "  ")
			if err != nil {
				t.Fatal(err)
			}
			t.Logf("config A loaded from %s: %+v", tt.inPath, string(configAPretty))

			if err := configA.SaveToFile(testFile); err != nil {
				t.Fatal(err)
			}

			bytes, err := os.ReadFile(testFile)
			if err != nil {
				t.Fatal(errors.WithMessage(err, "reading file"))
			}
			t.Logf("config saved loaded from %s: %+v", testFile, string(bytes))

			configB := DefaultServer()
			if err := configB.SetPath(testFile); err != nil {
				t.Fatal(err)
			}

			err = configB.Load(log)
			if err == nil {
				err = configB.Validate(log)
			}

			if err != nil {
				t.Fatal(err)
			}

			configBPretty, err := json.MarshalIndent(configB, "", "  ")
			if err != nil {
				t.Fatal(err)
			}
			t.Logf("config B loaded from %s: %+v", testFile, string(configBPretty))

			if diff := cmp.Diff(configA, configB, defConfigCmpOpts...); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestServerConfig_Constructed(t *testing.T) {
	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	// First, load a config based on the server config with all options uncommented.
	testFile := filepath.Join(testDir, sConfigUncomment)
	uncommentServerConfig(t, testFile)
	defaultCfg, err := mockConfigFromFile(t, log, testFile)
	if err != nil {
		t.Fatalf("failed to load %s: %s", testFile, err)
	}

	var bypass = true

	// Next, construct a config to compare against the first one. It should be
	// possible to construct an identical configuration with the helpers.
	constructed := DefaultServer().
		WithControlPort(10001).
		WithControlMetadata(storage.ControlMetadata{
			Path:       "/home/daos_server/control_meta",
			DevicePath: "/dev/sdb1",
		}).
		WithBdevExclude("0000:81:00.1").
		WithDisableVFIO(true).    // vfio enabled by default
		WithDisableVMD(true).     // vmd enabled by default
		WithDisableHotplug(true). // hotplug enabled by default
		WithControlLogMask(common.ControlLogLevelError).
		WithControlLogFile("/tmp/daos_server.log").
		WithHelperLogFile("/tmp/daos_server_helper.log").
		WithFirmwareHelperLogFile("/tmp/daos_firmware_helper.log").
		WithTelemetryPort(9191).
		WithSystemName("daos_server").
		WithSocketDir("./.daos/daos_server").
		WithFabricProvider("ofi+verbs;ofi_rxm").
		WithCrtTimeout(30).
		WithMgmtSvcReplicas("hostname1", "hostname2", "hostname3").
		WithFaultCb("./.daos/fd_callback").
		WithFaultPath("/vcdu0/rack1/hostname").
		WithClientEnvVars([]string{"foo=bar"}).
		WithFabricAuthKey("foo:bar").
		WithHyperthreads(true). // hyper-threads disabled by default
		WithSystemRamReserved(5).
		WithAllowNumaImbalance(true)

	// add engines explicitly to test functionality applied in WithEngines()
	constructed.Engines = []*engine.Config{
		engine.MockConfig().
			WithSystemName("daos_server").
			WithSocketDir("./.daos/daos_server").
			WithTargetCount(16).
			WithHelperStreamCount(4).
			WithServiceThreadCore(0).
			WithStorage(
				storage.NewTierConfig().
					WithScmMountPoint("/mnt/daos/1").
					WithStorageClass("ram").
					WithScmDisableHugepages(),
				storage.NewTierConfig().
					WithStorageClass("nvme").
					WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
					WithBdevBusidRange("0x80-0x8f").
					WithBdevDeviceRoles(storage.BdevRoleAll),
			).
			WithFabricInterface("ib0").
			WithFabricInterfacePort(20000).
			WithFabricProvider("ofi+verbs;ofi_rxm").
			WithFabricAuthKey("foo:bar").
			WithCrtTimeout(30).
			WithPinnedNumaNode(0).
			WithBypassHealthChk(&bypass).
			WithEnvVars("CRT_TIMEOUT=30").
			WithLogFile("/tmp/daos_engine.0.log").
			WithLogMask("INFO").
			WithStorageEnableHotplug(false).
			WithStorageAutoFaultyCriteria(true, 100, 200),
		engine.MockConfig().
			WithSystemName("daos_server").
			WithSocketDir("./.daos/daos_server").
			WithTargetCount(16).
			WithHelperStreamCount(4).
			WithServiceThreadCore(22).
			WithStorage(
				storage.NewTierConfig().
					WithScmMountPoint("/mnt/daos/2").
					WithStorageClass("ram"),
				storage.NewTierConfig().
					WithStorageClass("file").
					WithBdevDeviceList("/tmp/daos-bdev1", "/tmp/daos-bdev2").
					WithBdevFileSize(16).
					WithBdevDeviceRoles(storage.BdevRoleAll),
			).
			WithFabricInterface("ib1").
			WithFabricInterfacePort(21000).
			WithFabricProvider("ofi+verbs;ofi_rxm").
			WithFabricAuthKey("foo:bar").
			WithCrtTimeout(30).
			WithBypassHealthChk(&bypass).
			WithEnvVars("CRT_TIMEOUT=100").
			WithLogFile("/tmp/daos_engine.1.log").
			WithLogMask("INFO").
			WithStorageEnableHotplug(false).
			WithStorageAutoFaultyCriteria(false, 0, 0),
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

func TestServerConfig_updateServerConfig(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg       *Server
		nilEngCfg bool
		expEngCfg *engine.Config
	}{
		"nil engCfg": {
			cfg: &Server{
				SystemName: "name",
			},
			nilEngCfg: true,
			expEngCfg: &engine.Config{},
		},
		"basic": {
			cfg: &Server{
				SystemName: "name",
				SocketDir:  "socketdir",
				Modules:    "modules",
				Fabric: engine.FabricConfig{
					Provider:              "provider",
					Interface:             "iface",
					InterfacePort:         1111,
					NumSecondaryEndpoints: []int{2, 3, 4},
				},
			},
			expEngCfg: &engine.Config{
				SystemName: "name",
				SocketDir:  "socketdir",
				Modules:    "modules",
				Storage: storage.Config{
					EnableHotplug: true,
				},
				Fabric: engine.FabricConfig{
					Provider:              "provider",
					Interface:             "iface",
					InterfacePort:         1111,
					NumSecondaryEndpoints: []int{2, 3, 4},
				},
			},
		},
		"multiprovider": {
			cfg: &Server{
				SystemName: "name",
				Fabric: engine.FabricConfig{
					Provider:              "p1 p2 p3",
					NumSecondaryEndpoints: []int{2, 3, 4},
				},
			},
			expEngCfg: &engine.Config{
				SystemName: "name",
				Storage: storage.Config{
					EnableHotplug: true,
				},
				Fabric: engine.FabricConfig{
					Provider:              "p1 p2 p3",
					NumSecondaryEndpoints: []int{2, 3, 4},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			var engCfg *engine.Config
			if !tc.nilEngCfg {
				engCfg = &engine.Config{}
			}

			tc.cfg.updateServerConfig(&engCfg)

			if diff := cmp.Diff(tc.expEngCfg, engCfg); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestServerConfig_MDonSSD_Constructed(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mdOnSSDCfg, err := mockConfigFromFile(t, log, mdOnSSDExample)
	if err != nil {
		t.Fatalf("failed to load %s: %s", mdOnSSDExample, err)
	}

	constructed := DefaultServer().
		WithControlMetadata(storage.ControlMetadata{
			Path: "/var/daos/config",
		}).
		WithControlLogFile("/tmp/daos_server.log").
		WithTelemetryPort(9191).
		WithFabricProvider("ofi+tcp").
		WithMgmtSvcReplicas("example1", "example2", "example3")

	constructed.Engines = []*engine.Config{
		engine.MockConfig().
			WithSystemName("daos_server").
			WithSocketDir("/var/run/daos_server").
			WithTargetCount(4).
			WithHelperStreamCount(1).
			WithStorage(
				storage.NewTierConfig().
					WithScmMountPoint("/mnt/daos").
					WithStorageClass("ram"),
				storage.NewTierConfig().
					WithStorageClass("nvme").
					WithBdevDeviceList("0000:81:00.0").
					WithBdevDeviceRoles(storage.BdevRoleWAL),
				storage.NewTierConfig().
					WithStorageClass("nvme").
					WithBdevDeviceList("0000:82:00.0").
					WithBdevDeviceRoles(storage.BdevRoleMeta),
				storage.NewTierConfig().
					WithStorageClass("nvme").
					WithBdevDeviceList("0000:83:00.0").
					WithBdevDeviceRoles(storage.BdevRoleData),
			).
			WithStorageEnableHotplug(true).
			WithFabricInterface("ib0").
			WithFabricInterfacePort(31316).
			WithFabricProvider("ofi+tcp").
			WithPinnedNumaNode(0).
			WithEnvVars("FI_SOCKETS_CONN_TIMEOUT=2000", "FI_SOCKETS_MAX_CONN_RETRY=1").
			WithLogFile("/tmp/daos_engine.0.log").
			WithLogMask("INFO"),
	}

	for i := range constructed.Engines {
		t.Logf("constructed: %+v", constructed.Engines[i])
		t.Logf("default: %+v", mdOnSSDCfg.Engines[i])
	}

	if diff := cmp.Diff(mdOnSSDCfg, constructed, defConfigCmpOpts...); diff != "" {
		t.Fatalf("(-want, +got): %s", diff)
	}

	if err := mdOnSSDCfg.Validate(log); err != nil {
		t.Fatalf("failed to validate %s: %s", mdOnSSDExample, err)
	}
}

func TestServerConfig_Validation(t *testing.T) {
	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	// First, load a config based on the server config with all options uncommented.
	testFile := filepath.Join(testDir, sConfigUncomment)
	uncommentServerConfig(t, testFile)

	testMetadataDir := filepath.Join(testDir, "control_md")

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
		"no MS replica": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas()
			},
			expErr: FaultConfigBadMgmtSvcReplicas,
		},
		"single MS replica": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4:1234")
			},
		},
		"multiple MS replicas (even)": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4:1234", "5.6.7.8:5678")
			},
			expErr: FaultConfigEvenMgmtSvcReplicas,
		},
		"multiple MS replicas (odd)": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4:1234", "5.6.7.8:5678", "1.5.3.8:6247")
			},
		},
		"multiple MS replicas (dupes)": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4", "5.6.7.8", "1.2.3.4")
			},
			expErr: FaultConfigBadMgmtSvcReplicas,
		},
		"multiple MS replicas (dupes with ports)": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4:1234", "5.6.7.8:5678", "1.2.3.4:1234")
			},
			expErr: FaultConfigBadMgmtSvcReplicas,
		},
		"multiple MS replicas (dupes with and without ports)": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4:10001", "5.6.7.8:5678", "1.2.3.4")
			},
			expErr: FaultConfigBadMgmtSvcReplicas,
		},
		"multiple MS replicas (dupes with different ports)": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4:10002", "5.6.7.8:5678", "1.2.3.4")
			},
		},
		"no MS replicas": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas()
			},
			expErr: FaultConfigBadMgmtSvcReplicas,
		},
		"single MS replica no port": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4")
			},
		},
		"single MS replica invalid port": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4").
					WithControlPort(0)
			},
			expErr: FaultConfigBadControlPort,
		},
		"single MS replica including invalid port (alphanumeric)": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4:0a0")
			},
			expErr: FaultConfigBadControlPort,
		},
		"single MS replica including invalid port (zero)": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4:0")
			},
			expErr: FaultConfigBadControlPort,
		},
		"single MS replica including negative port": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("1.2.3.4:-10002")
			},
			expErr: FaultConfigBadControlPort,
		},
		"single MS replica hostname including negative port": {
			extraConfig: func(c *Server) *Server {
				return c.WithMgmtSvcReplicas("hostX:-10002")
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
		"different number of bdevs": {
			extraConfig: func(c *Server) *Server {
				// add multiple bdevs for engine 0 to create mismatch
				c.Engines[0].Storage.Tiers.BdevConfigs()[0].
					WithBdevDeviceList("0000:10:00.0", "0000:11:00.0", "0000:12:00.0")
				return c
			},
			// No failure because validation now occurs on server start-up.
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
				return c.WithNrHugepages(-2048)
			},
			expErr: FaultConfigNrHugepagesOutOfRange(-2048, math.MaxInt32),
		},
		"out of range hugepages; high": {
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugepages(math.MaxInt32 + 1)
			},
			expErr: FaultConfigNrHugepagesOutOfRange(math.MaxInt32+1, math.MaxInt32),
		},
		"out of range scm_size; low": {
			extraConfig: func(c *Server) *Server {
				c.Engines[0].Storage.Tiers.ScmConfigs()[0].Scm.RamdiskSize = 3
				return c
			},
			expErr: storage.FaultConfigRamdiskUnderMinMem(humanize.GiByte*3,
				storage.MinRamdiskMem),
		},
		"zero system ram reserved": {
			extraConfig: func(c *Server) *Server {
				return c.WithSystemRamReserved(0)
			},
			expErr: FaultConfigSysRsvdZero,
		},
		"control metadata multi-engine": {
			extraConfig: func(c *Server) *Server {
				return c.WithControlMetadata(storage.ControlMetadata{
					Path:       testMetadataDir,
					DevicePath: "/dev/something",
				}).
					WithEngines(
						defaultEngineCfg().
							WithFabricInterfacePort(1234).
							WithStorage(
								storage.NewTierConfig().
									WithScmMountPoint("/mnt/daos/1").
									WithStorageClass("ram").
									WithScmDisableHugepages(),
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
									WithBdevBusidRange("0x80-0x8f").
									WithBdevDeviceRoles(storage.BdevRoleAll),
							),
						defaultEngineCfg().
							WithFabricInterfacePort(5678).
							WithStorage(
								storage.NewTierConfig().
									WithScmMountPoint("/mnt/daos/2").
									WithStorageClass("ram").
									WithScmDisableHugepages(),
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:91:00.0", "0000:92:00.0").
									WithBdevBusidRange("0x90-0x9f").
									WithBdevDeviceRoles(storage.BdevRoleAll),
							),
					)
			},
			expConfig: baseCfg(t, log, testFile).
				WithMgmtSvcReplicas("hostname1:10001", "hostname2:10001", "hostname3:10001").
				WithControlMetadata(storage.ControlMetadata{
					Path:       testMetadataDir,
					DevicePath: "/dev/something",
				}).
				WithEngines(
					defaultEngineCfg().
						WithFabricInterfacePort(1234).
						WithStorage(
							storage.NewTierConfig().
								WithScmMountPoint("/mnt/daos/1").
								WithStorageClass("ram").
								WithScmDisableHugepages(),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
								WithBdevBusidRange("0x80-0x8f").
								WithBdevDeviceRoles(storage.BdevRoleAll),
						).
						WithStorageVosEnv("NVME").
						WithStorageControlMetadataPath(testMetadataDir).
						WithStorageControlMetadataDevice("/dev/something").
						WithStorageConfigOutputPath(filepath.Join(
							testMetadataDir,
							storage.ControlMetadataSubdir,
							"engine0",
							"daos_nvme.conf",
						)), // NVMe conf should end up in metadata dir
					defaultEngineCfg().
						WithStorageIndex(1).
						WithFabricInterfacePort(5678).
						WithStorage(
							storage.NewTierConfig().
								WithScmMountPoint("/mnt/daos/2").
								WithStorageClass("ram").
								WithScmDisableHugepages(),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:91:00.0", "0000:92:00.0").
								WithBdevBusidRange("0x90-0x9f").
								WithBdevDeviceRoles(storage.BdevRoleAll),
						).
						WithStorageVosEnv("NVME").
						WithStorageControlMetadataPath(testMetadataDir).
						WithStorageControlMetadataDevice("/dev/something").
						WithStorageConfigOutputPath(filepath.Join(
							testMetadataDir,
							storage.ControlMetadataSubdir,
							"engine1",
							"daos_nvme.conf",
						)), // NVMe conf should end up in metadata dir
				),
		},
		"md-on-ssd enabled with role assignment": {
			extraConfig: func(c *Server) *Server {
				return c.WithControlMetadata(storage.ControlMetadata{
					Path:       testMetadataDir,
					DevicePath: "/dev/something",
				}).
					WithEngines(
						defaultEngineCfg().
							WithFabricInterfacePort(1234).
							WithStorage(
								storage.NewTierConfig().
									WithScmMountPoint("/mnt/daos/1").
									WithStorageClass("ram").
									WithScmDisableHugepages(),
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
									WithBdevDeviceRoles(storage.BdevRoleAll),
							),
					)
			},
			expConfig: baseCfg(t, log, testFile).
				WithMgmtSvcReplicas("hostname1:10001", "hostname2:10001", "hostname3:10001").
				WithControlMetadata(storage.ControlMetadata{
					Path:       testMetadataDir,
					DevicePath: "/dev/something",
				}).
				WithEngines(
					defaultEngineCfg().
						WithFabricInterfacePort(1234).
						WithStorage(
							storage.NewTierConfig().
								WithScmMountPoint("/mnt/daos/1").
								WithStorageClass("ram").
								WithScmDisableHugepages(),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
								WithBdevDeviceRoles(storage.BdevRoleAll),
						).
						WithStorageVosEnv("NVME").
						WithStorageControlMetadataPath(testMetadataDir).
						WithStorageControlMetadataDevice("/dev/something").
						WithStorageConfigOutputPath(filepath.Join(
							testMetadataDir,
							storage.ControlMetadataSubdir,
							"engine0",
							"daos_nvme.conf",
						)), // NVMe conf should end up in metadata dir
				),
		},
		"md-on-ssd enabled with role assignment on one engine only": {
			extraConfig: func(c *Server) *Server {
				return c.WithControlMetadata(storage.ControlMetadata{
					Path:       testMetadataDir,
					DevicePath: "/dev/something",
				}).
					WithEngines(
						defaultEngineCfg().
							WithFabricInterfacePort(1234).
							WithStorage(
								storage.NewTierConfig().
									WithScmMountPoint("/mnt/daos/0").
									WithStorageClass("ram").
									WithScmDisableHugepages(),
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:80:00.0").
									WithBdevDeviceRoles(storage.BdevRoleAll),
							),
						defaultEngineCfg().
							WithFabricInterfacePort(2234).
							WithStorage(
								storage.NewTierConfig().
									WithScmMountPoint("/mnt/daos/1").
									WithStorageClass("ram").
									WithScmDisableHugepages(),
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:81:00.0"),
							),
					)
			},
			expErr: storage.FaultBdevConfigControlMetadataNoRoles,
		},
		"control metadata has path only": {
			extraConfig: func(c *Server) *Server {
				return c.
					WithControlMetadata(storage.ControlMetadata{
						Path: testMetadataDir,
					}).
					WithEngines(
						defaultEngineCfg().
							WithStorage(
								storage.NewTierConfig().
									WithScmMountPoint("/mnt/daos/1").
									WithStorageClass("ram").
									WithScmDisableHugepages(),
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
									WithBdevBusidRange("0x80-0x8f").
									WithBdevDeviceRoles(storage.BdevRoleAll),
							),
					)
			},
			expConfig: baseCfg(t, log, testFile).
				WithMgmtSvcReplicas("hostname1:10001", "hostname2:10001", "hostname3:10001").
				WithControlMetadata(storage.ControlMetadata{
					Path: testMetadataDir,
				}).
				WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithScmMountPoint("/mnt/daos/1").
							WithStorageClass("ram").
							WithScmDisableHugepages(),
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
							WithBdevBusidRange("0x80-0x8f").
							WithBdevDeviceRoles(storage.BdevRoleAll),
					).
					WithStorageVosEnv("NVME").
					WithStorageControlMetadataPath(testMetadataDir).
					WithStorageControlMetadataDevice("").
					WithStorageConfigOutputPath(filepath.Join(
						testMetadataDir,
						storage.ControlMetadataSubdir,
						"engine0",
						"daos_nvme.conf",
					)), // NVMe conf should end up in metadata dir
				),
		},
		"control metadata has device only": {
			extraConfig: func(c *Server) *Server {
				return c.WithControlMetadata(storage.ControlMetadata{
					DevicePath: "/dev/sdb0",
				}).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmMountPoint("/foo"),
						))
			},
			expErr: FaultConfigControlMetadataNoPath,
		},
		"control metadata with no roles specified": {
			extraConfig: func(c *Server) *Server {
				return c.
					WithControlMetadata(storage.ControlMetadata{
						Path: testMetadataDir,
					}).
					WithEngines(
						defaultEngineCfg().
							WithStorage(
								storage.NewTierConfig().
									WithScmMountPoint("/mnt/daos/1").
									WithStorageClass("ram").
									WithScmDisableHugepages(),
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:81:00.0"),
							),
					)
			},
			expErr: storage.FaultBdevConfigControlMetadataNoRoles,
		},
		"roles specified with no control metadata path": {
			extraConfig: func(c *Server) *Server {
				return c.
					WithControlMetadata(storage.ControlMetadata{}).
					WithEngines(
						defaultEngineCfg().
							WithStorage(
								storage.NewTierConfig().
									WithScmMountPoint("/mnt/daos/1").
									WithStorageClass("ram").
									WithScmDisableHugepages(),
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:81:00.0").
									WithBdevDeviceRoles(storage.BdevRoleAll),
							),
					)
			},
			expErr: storage.FaultBdevConfigRolesNoControlMetadata,
		},
		"bdev_exclude addresses clash with bdev_list": {
			extraConfig: func(c *Server) *Server {
				c.BdevExclude = c.Engines[0].Storage.GetBdevs().Strings()
				return c
			},
			expErr: FaultConfigBdevExcludeClash,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tt.extraConfig == nil {
				tt.extraConfig = noopExtra
			}

			// Apply test case changes to basic config
			cfg := tt.extraConfig(baseCfg(t, log, testFile))

			log.Debugf("baseCfg metadata: %+v", cfg.Metadata)

			test.CmpErr(t, tt.expErr, cfg.Validate(log))
			if tt.expErr != nil || tt.expConfig == nil {
				return
			}

			if diff := cmp.Diff(tt.expConfig, cfg, defConfigCmpOpts...); diff != "" {
				t.Fatalf("unexpected config after validation (-want, +got): %s", diff)
			}
		})
	}
}

func TestServerConfig_getMinNrHugepages(t *testing.T) {
	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	// First, load a config based on the server config with all options uncommented.
	testFile := filepath.Join(testDir, sConfigUncomment)
	uncommentServerConfig(t, testFile)

	for name, tc := range map[string]struct {
		extraConfig     func(c *Server) *Server
		zeroHpSize      bool
		expMinHugepages int
		expMaxHugepages int
		expErr          error
	}{
		"zero hugepage size": {
			extraConfig: func(c *Server) *Server {
				return c
			},
			zeroHpSize: true,
			expErr:     errors.New("invalid system hugepage size"),
		},
		"unset in cfg; bdevs configured; single target count": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithTargetCount(1).
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("dcpm").
							WithScmDeviceList("/dev/pmem1"),
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("0000:81:00.0"),
					),
					defaultEngineCfg().
						WithTargetCount(1).
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("dcpm").
								WithScmDeviceList("/dev/pmem1"),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:d0:00.0"),
						),
				)
			},
			expMinHugepages: 2048,
		},
		"unset in cfg; bdevs configured; single target count; md-on-ssd": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithTargetCount(1).
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmMountPoint("/foo"),
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("0000:81:00.0").
							WithBdevDeviceRoles(storage.BdevRoleAll),
					),
					defaultEngineCfg().
						WithTargetCount(1).
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmMountPoint("/foo"),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:d0:00.0").
								WithBdevDeviceRoles(storage.BdevRoleAll),
						),
				)
			},
			expMinHugepages: 2048,
		},
		"unset in cfg; bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmMountPoint("/foo"),
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("0000:81:00.0"),
					),
				)
			},
			expMinHugepages: 4096,
		},
		"unset in cfg; bdevs configured; target count exceeds max": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithTargetCount(33).
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmMountPoint("/foo"),
						storage.NewTierConfig().
							WithStorageClass("nvme").
							WithBdevDeviceList("0000:81:00.0"),
					),
				)
			},
			expMinHugepages: 16896,
		},
		"unset in cfg; emulated bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmMountPoint("/foo"),
						storage.NewTierConfig().
							WithStorageClass("file").
							WithBdevDeviceList("/tmp/daos-bdev").
							WithBdevFileSize(16),
					),
				)
			},
			expMinHugepages: 4096,
		},
		"unset in cfg; no bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmMountPoint("/foo"),
					),
				)
			},
		},
		"md-on-ssd enabled with explicit role assignment": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(
					defaultEngineCfg().
						WithFabricInterfacePort(1234).
						WithStorage(
							storage.NewTierConfig().
								WithScmMountPoint("/mnt/daos/1").
								WithStorageClass("ram").
								WithScmDisableHugepages(),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
								WithBdevDeviceRoles(storage.BdevRoleAll),
						),
				)
			},
			// 512 pages * (8 targets + 1 sys-xstream for MD-on-SSD)
			expMinHugepages: 4608,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			// Apply test case changes to basic config
			cfg := tc.extraConfig(baseCfg(t, log, testFile))

			hugepageSizeKiB := defHpSizeKb
			if tc.zeroHpSize {
				hugepageSizeKiB = 0
			}

			minHugepages, err := cfg.getMinNrHugepages(log, hugepageSizeKiB)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expMinHugepages, minHugepages,
				"unexpected number of minimum hugepages calculated from config")
		})
	}
}

func TestServerConfig_SetNrHugepages(t *testing.T) {
	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	// First, load a config based on the server config with all options uncommented.
	testFile := filepath.Join(testDir, sConfigUncomment)
	uncommentServerConfig(t, testFile)

	for name, tc := range map[string]struct {
		extraConfig       func(c *Server) *Server
		expErr            error
		expCfgNrHugepages int
	}{
		"disabled hugepages; nr_hugepages requested": {
			extraConfig: func(c *Server) *Server {
				return c.WithDisableHugepages(true).
					WithNrHugepages(16896)
			},
			expErr: FaultConfigHugepagesDisabledWithNrSet,
		},
		"disabled hugepages; bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithDisableHugepages(true).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmMountPoint("/foo"),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:81:00.0"),
						),
					)
			},
			expErr: FaultConfigHugepagesDisabledWithNvmeBdevs,
		},
		"disabled hugepages; emulated bdevs configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithDisableHugepages(true).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								// 80gib total - (8gib huge + 6gib sys +
								// 1gib engine)
								WithScmRamdiskSize(65).
								WithScmMountPoint("/foo"),
							storage.NewTierConfig().
								WithStorageClass("file").
								WithBdevDeviceList("/tmp/daos-bdev").
								WithBdevFileSize(16),
						),
					)
			},
		},
		"disabled hugepages; no bdevs in scm-only config": {
			extraConfig: func(c *Server) *Server {
				return c.WithDisableHugepages(true).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmMountPoint("/foo"),
						),
					)
			},
		},
		"unset in config; no bdevs in scm-only config": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmMountPoint("/foo"),
					),
				)
			},
			expCfgNrHugepages: ScanMinHugepageCount,
		},
		"insufficient hugepages set in config; no engines configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines().WithNrHugepages(ScanMinHugepageCount - 1)
			},
			expCfgNrHugepages: ScanMinHugepageCount,
		},
		"sufficient hugepages set in config; no engines configured": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines().WithNrHugepages(ScanMinHugepageCount + 1)
			},
			expCfgNrHugepages: ScanMinHugepageCount + 1,
		},
		"md-on-ssd enabled with explicit role assignment; zero total system hugepages": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(
					defaultEngineCfg().
						WithFabricInterfacePort(1234).
						WithStorage(
							storage.NewTierConfig().
								WithScmMountPoint("/mnt/daos/1").
								WithStorageClass("ram").
								WithScmDisableHugepages(),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
								WithBdevDeviceRoles(storage.BdevRoleAll),
						),
				)
			},
			// Min: 512 pages * (8 targets + 1 sys-xstream for MD-on-SSD).
			expCfgNrHugepages: 4608,
		},
		"md-on-ssd enabled with explicit role assignment; manual nr_hugepages in cfg": {
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugepages(4000).
					WithEngines(defaultEngineCfg().
						WithFabricInterfacePort(1234).
						WithStorage(
							storage.NewTierConfig().
								WithScmMountPoint("/mnt/daos/1").
								WithStorageClass("ram").
								WithScmDisableHugepages(),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
								WithBdevDeviceRoles(storage.BdevRoleAll),
						),
					)
			},
			expCfgNrHugepages: 4000,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			// Apply test case changes to basic config
			cfg := tc.extraConfig(baseCfg(t, log, testFile))

			test.CmpErr(t, tc.expErr, cfg.SetNrHugepages(log, defHpSizeKb))
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expCfgNrHugepages, cfg.NrHugepages,
				"unexpected number of hugepages set in config")
		})
	}
}

func TestServerConfig_SetRamdiskSize(t *testing.T) {
	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	// First, load a config based on the server config with all options uncommented.
	testFile := filepath.Join(testDir, sConfigUncomment)
	uncommentServerConfig(t, testFile)

	for name, tc := range map[string]struct {
		extraConfig    func(c *Server) *Server
		memTotBytes    uint64
		expRamdiskSize int
		expErr         error
	}{
		"zero mem reported": {
			extraConfig: func(c *Server) *Server {
				return c
			},
			expErr: errors.New("requires nonzero total mem"),
		},
		"out of range scm_size; high": {
			// 16896 hugepages / 512 pages-per-gib = 33 gib huge mem
			// 33 huge mem + 5 sys rsv + 2 engine rsv = 40 gib reserved mem
			// 60 total - 40 reserved = 20 for tmpfs (10 gib per engine)
			memTotBytes: humanize.GiByte * 60,
			extraConfig: func(c *Server) *Server {
				// 10gib is max remainder after reserved, 11gib is too high
				c.Engines[0].Storage.Tiers.ScmConfigs()[0].Scm.RamdiskSize = 11
				return c.WithNrHugepages(16896)
			},
			expErr: FaultConfigRamdiskOverMaxMem(humanize.GiByte*11, humanize.GiByte*9, 0),
		},
		"low mem": {
			// 46 total - 40 reserved = 6 for tmpfs (3 gib per engine - too low)
			memTotBytes: humanize.GiByte * 46,
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugepages(16896)
			},
			// error indicates min RAM needed = 40 + 4 gib per engine
			expErr: storage.FaultRamdiskLowMem("Total", storage.MinRamdiskMem,
				humanize.GiByte*50, humanize.GiByte*46),
		},
		"custom value set": {
			memTotBytes: humanize.GiByte * 60,
			extraConfig: func(c *Server) *Server {
				// set custom value between min and max
				c.Engines[0].Storage.Tiers.ScmConfigs()[0].Scm.RamdiskSize = 6
				c.Engines[1].Storage.Tiers.ScmConfigs()[0].Scm.RamdiskSize = 6
				return c.WithNrHugepages(16896)
			},
			expRamdiskSize: 6,
		},
		"auto-calculated value set": {
			memTotBytes: humanize.GiByte * 60,
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugepages(16896)
			},
			expRamdiskSize: 9,
		},
		"custom system_ram_reserved value set": {
			// 33 huge mem + 2 sys rsv + 2 engine rsv = 37 gib reserved mem
			// 60 total - 37 reserved = 23 for tmpfs (11 gib per engine after rounding)
			memTotBytes: humanize.GiByte * 60,
			extraConfig: func(c *Server) *Server {
				c.SystemRamReserved = 2
				return c.WithNrHugepages(16896)
			},
			expRamdiskSize: 10,
		},
		"no scm configured on second engine": {
			memTotBytes: humanize.GiByte * 80,
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugepages(4096).
					WithEngines(
						defaultEngineCfg().
							WithStorage(
								storage.NewTierConfig().
									WithStorageClass("ram").
									WithScmMountPoint("/foo"),
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:81:00.0"),
							),
						defaultEngineCfg().
							WithStorage(
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:81:00.0"),
							),
					)
			},
			expErr: errors.New("unexpected number of scm tiers"),
		},
		"bdevs configured": {
			memTotBytes: humanize.GiByte * 80,
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugepages(4096).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmMountPoint("/foo"),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:81:00.0"),
						),
					)
			},
			// 80gib total - (8gib huge + 5gib sys + 1gib engine)
			expRamdiskSize: 66,
		},
		"emulated engines configured": {
			memTotBytes: humanize.GiByte * 80,
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugepages(4096).
					WithEngines(defaultEngineCfg().
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmMountPoint("/foo"),
							storage.NewTierConfig().
								WithStorageClass("file").
								WithBdevDeviceList("/tmp/daos-bdev").
								WithBdevFileSize(16),
						),
					)
			},
			// 80gib total - (8gib huge + 5gib sys + 1gib engine)
			expRamdiskSize: 66,
		},
		"no bdevs configured": {
			memTotBytes: humanize.GiByte * 80,
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(defaultEngineCfg().
					WithStorage(
						storage.NewTierConfig().
							WithStorageClass("ram").
							WithScmMountPoint("/foo"),
					),
				)
			},
			// 80gib total - (0gib huge + 5gib sys + 1gib engine)
			expRamdiskSize: 74,
		},
		"md-on-ssd enabled with explicit role assignment": {
			memTotBytes: humanize.GiByte * 80,
			extraConfig: func(c *Server) *Server {
				return c.WithNrHugepages(4608).
					WithEngines(
						defaultEngineCfg().
							WithFabricInterfacePort(1234).
							WithStorage(
								storage.NewTierConfig().
									WithScmMountPoint("/mnt/daos/1").
									WithStorageClass("ram").
									WithScmDisableHugepages(),
								storage.NewTierConfig().
									WithStorageClass("nvme").
									WithBdevDeviceList("0000:81:00.0", "0000:82:00.0").
									WithBdevDeviceRoles(storage.BdevRoleAll),
							),
					)
			},
			// 80gib total - (9gib huge + 5gib sys + 1gib engine)
			expRamdiskSize: 65,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			// Apply test case changes to basic config
			cfg := tc.extraConfig(baseCfg(t, log, testFile))

			val := tc.memTotBytes / humanize.KiByte
			if val > math.MaxInt {
				t.Fatal("int overflow")
			}
			smi := &common.SysMemInfo{}
			smi.HugepageSizeKiB = 2048
			smi.MemTotalKiB = int(val)

			test.CmpErr(t, tc.expErr, cfg.SetRamdiskSize(log, smi))
			if tc.expErr != nil {
				return
			}

			if len(cfg.Engines) == 0 {
				t.Fatal("no engines in config")
			}
			for _, ec := range cfg.Engines {
				scmTiers := ec.Storage.Tiers.ScmConfigs()
				if len(scmTiers) != 1 {
					t.Fatal("unexpected number of scm tiers")
				}
				if scmTiers[0].Class != storage.ClassRam {
					t.Fatal("expected scm tier to have class RAM")
				}
				test.AssertEqual(t, tc.expRamdiskSize, int(scmTiers[0].Scm.RamdiskSize),
					"unexpected ramdisk size set in config")
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
	t.Helper()

	// open original file
	f, err := os.Open(name)
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()

	// create temp file
	tmp, err := os.CreateTemp("", "replace-*")
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
		t.Fatalf("no occurrences of %q in file %q", oldTxt, name)
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

	cfgFromFile := func(t *testing.T, log logging.Logger, testFile string, matchText, replaceText []string) (*Server, error) {
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

		return mockConfigFromFile(t, log, testFile)
	}

	// load a config based on the server config with all options uncommented.
	loadFromFile := func(t *testing.T, log logging.Logger, testDir string, matchText, replaceText []string) (*Server, error) {
		t.Helper()

		defaultConfigFile := filepath.Join(testDir, sConfigUncomment)
		uncommentServerConfig(t, defaultConfigFile)

		return cfgFromFile(t, log, defaultConfigFile, matchText, replaceText)
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
		"duplicates in bdev_list from config": {
			extraConfig: func(c *Server) *Server {
				return c.WithEngines(
					engine.MockConfig().
						WithFabricInterface("ib0").
						WithFabricInterfacePort(20000).
						WithStorage(
							storage.NewTierConfig().
								WithStorageClass("ram").
								WithScmMountPoint("/mnt/daos/2"),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList(test.MockPCIAddr(1), test.MockPCIAddr(1)),
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
		"additional empty storage tier": {
			// Add empty storage tier to engine-0 and verify it is ignored.
			inTxt:  "    - wal",
			outTxt: "    - wal\n  -",
			expCheck: func(c *Server) error {
				nr := len(c.Engines[0].Storage.Tiers)
				if nr != 2 {
					return errors.Errorf("want 2 storage tiers, got %d", nr)
				}
				return nil
			},
		},
		"no bdev_list": {
			inTxt:          "    bdev_list: [\"0000:81:00.0\", \"0000:82:00.0\"]  # generate regular nvme.conf",
			outTxt:         "",
			expValidateErr: errors.New("valid PCI addresses"),
		},
		"no bdev_class": {
			inTxt:          "    class: nvme",
			outTxt:         "",
			expValidateErr: errors.New("no storage class"),
		},
		"non-empty bdev_list; hugepages disabled": {
			inTxt:  "disable_hugepages: false",
			outTxt: "disable_hugepages: true",
			expCheck: func(c *Server) error {
				if !c.DisableHugepages {
					return errors.Errorf("expected hugepages to be disabled")
				}
				return nil
			},
		},
		"check default system_ram_reserved": {
			inTxt:  "system_ram_reserved: 5",
			outTxt: "",
			expCheck: func(c *Server) error {
				if c.SystemRamReserved != storage.DefaultSysMemRsvd/humanize.GiByte {
					return errors.Errorf("unexpected system_ram_reserved, want %d got %d",
						storage.DefaultSysMemRsvd/humanize.GiByte, c.SystemRamReserved)
				}
				return nil
			},
		},
		"access_points and mgmt_svc_replicas both defined": {
			inTxt:       "disable_hugepages: false",
			outTxt:      "access_points: [foo.com]",
			expParseErr: errors.New(msgAPsMSReps),
		},
		"enable_hotplug and disable_hotplug both set": {
			inTxt:          "disable_hugepages: false",
			outTxt:         "enable_hotplug: true",
			expValidateErr: FaultConfigEnableHotplugDeprecated,
		},
		"enable_hotplug false setting allowed": {
			inTxt:  "disable_hotplug: true",
			outTxt: "enable_hotplug: false",
			expCheck: func(c *Server) error {
				if c.Engines[0].Storage.EnableHotplug {
					return errors.New("expecting hotplug to be disabled")
				}
				return nil
			},
		},
		"enable_hotplug true setting allowed": {
			inTxt:  "disable_hotplug: true",
			outTxt: "enable_hotplug: true",
			expCheck: func(c *Server) error {
				if !c.Engines[0].Storage.EnableHotplug {
					return errors.New("expecting hotplug to be enabled")
				}
				return nil
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			testDir, cleanup := test.CreateTestDir(t)
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

			config, errParse := loadFromFile(t, log, testDir, tt.inTxtList, tt.outTxtList)
			test.CmpErr(t, tt.expParseErr, errParse)
			if tt.expParseErr != nil {
				return
			}

			config = tt.extraConfig(config)
			test.CmpErr(t, tt.expValidateErr, config.Validate(log))

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
			testDir, cleanup := test.CreateTestDir(t)
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
		WithSystemName(testSystemName).
		WithStorageEnableHotplug(true)

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

func TestServerConfig_validateMultiEngineConfig(t *testing.T) {
	configA := func() *engine.Config {
		return engine.MockConfig().
			WithLogFile("a").
			WithFabricInterface("ib0").
			WithFabricInterfacePort(42).
			WithStorage(
				storage.NewTierConfig().
					WithStorageClass("ram").
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
					WithScmMountPoint("b"),
			).
			WithPinnedNumaNode(1).
			WithTargetCount(8)
	}

	for name, tc := range map[string]struct {
		configA *engine.Config
		configB *engine.Config
		expErr  error
		expLog  string
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
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1)),
				),
			configB: configB().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(2),
							test.MockPCIAddr(1)),
				),
			expErr: FaultConfigOverlappingBdevDeviceList(1, 0),
		},
		"duplicates in bdev_list": {
			configA: configA().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1),
							test.MockPCIAddr(2)),
				),
			configB: configB().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(2),
							test.MockPCIAddr(1)),
				),
			expErr: errors.New("engine 1 overlaps with entries in engine 0"),
		},
		"mismatched scm_class": {
			configA: configA(),
			configB: configB().
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassDcpm.String()).
						WithScmMountPoint("bb").
						WithScmDeviceList("a"),
				),
			expErr: FaultConfigScmDiffClass(1, 0),
		},
		"mismatched nr bdev_list": {
			configA: configA().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(1)),
				),
			configB: configB().
				AppendStorage(
					storage.NewTierConfig().
						WithStorageClass(storage.ClassNvme.String()).
						WithBdevDeviceList(test.MockPCIAddr(2),
							test.MockPCIAddr(3)),
				),
			expLog: "engine 1 has 2 but engine 0 has 1",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			conf := DefaultServer().
				WithFabricProvider("test").
				WithMgmtSvcReplicas(
					fmt.Sprintf("localhost:%d", build.DefaultControlPort)).
				WithEngines(tc.configA, tc.configB)

			gotErr := conf.Validate(log)
			test.CmpErr(t, tc.expErr, gotErr)

			if tc.expLog != "" {
				hasEntry := strings.Contains(buf.String(), tc.expLog)
				test.AssertTrue(t, hasEntry, "expected entries not found in log")
			}
		})
	}
}

func TestServerConfig_SaveActiveConfig(t *testing.T) {
	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	t.Logf("test dir: %s", testDir)

	for name, tc := range map[string]struct {
		cfgPath   string
		expLogOut string
	}{
		"successful write": {
			cfgPath:   testDir,
			expLogOut: fmt.Sprintf("config saved to %s/%s", testDir, ConfigOut),
		},
		"missing directory": {
			cfgPath:   filepath.Join(testDir, "non-existent/"),
			expLogOut: "could not be saved",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			cfg := DefaultServer().WithSocketDir(tc.cfgPath)

			cfg.SaveActiveConfig(log)

			test.AssertTrue(t, strings.Contains(buf.String(), tc.expLogOut),
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
			defer test.ShowBufferOnFailure(t, buf)

			detected, err := detectEngineAffinity(log, tc.cfg, tc.affSrcSet...)
			test.CmpErr(t, tc.expErr, err)
			if tc.expErr != nil {
				return
			}

			test.AssertEqual(t, tc.expDetected, detected,
				"unexpected detected numa node")
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
		"no affinity sources": {
			affSrcSet: []EngineAffinityFn{},
			expErr:    errors.New("requires at least one"),
		},
		"no affinity detected (default NUMA nodes)": {
			cfg: baseSrvCfg().WithEngines(
				engine.MockConfig().
					WithFabricInterface("ib0").
					WithFabricProvider("ofi+verbs"),
				engine.MockConfig().
					WithFabricInterface("ib1").
					WithFabricProvider("ofi+verbs"),
			),
			affSrcSet: []EngineAffinityFn{
				genAffFn("", 0),
			},
			expNumaSet: []int{0, 0},
		},
		"engines have first_core set; NUMA nodes should not be set": {
			cfg: baseSrvCfg().WithEngines(
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
			cfg: baseSrvCfg().WithEngines(
				engine.MockConfig().
					WithFabricInterface("ib0").
					WithFabricProvider("ofi+verbs").
					WithPinnedNumaNode(1),
			),
			expNumaSet: []int{1},
		},
		"single engine without pinned_numa_node set and no detected affinity": {
			cfg: baseSrvCfg().WithEngines(
				engine.MockConfig().
					WithFabricInterface("ib0").
					WithFabricProvider("ofi+verbs"),
			),
			affSrcSet: []EngineAffinityFn{
				genAffFn("", 0),
			},
			expNumaSet: []int{-1},
		},
		"single engine without pinned_numa_node set and affinity detected as != 0": {
			cfg: baseSrvCfg().WithEngines(
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
			cfg: baseSrvCfg().WithEngines(
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
			cfg: baseSrvCfg().WithEngines(
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
			cfg: baseSrvCfg().WithEngines(
				engine.MockConfig().
					WithFabricInterface("ib0").
					WithFabricProvider("ofi+verbs"),
				engine.MockConfig().
					WithFabricInterface("ib1").
					WithFabricProvider("ofi+verbs"),
			),
			expNumaSet: []int{1, 2},
		},
		"multi engine with pinned_numa_node set matching detected affinities": {
			cfg: baseSrvCfg().WithEngines(
				engine.MockConfig().
					WithPinnedNumaNode(1).
					WithFabricInterface("ib0").
					WithFabricProvider("ofi+verbs"),
				engine.MockConfig().
					WithPinnedNumaNode(2).
					WithFabricInterface("ib1").
					WithFabricProvider("ofi+verbs"),
			),
			expNumaSet: []int{1, 2},
		},
		"multi engine with pinned_numa_node set overriding detected affinities": {
			cfg: baseSrvCfg().WithEngines(
				engine.MockConfig().
					WithPinnedNumaNode(2).
					WithFabricInterface("ib0").
					WithFabricProvider("ofi+verbs"),
				engine.MockConfig().
					WithPinnedNumaNode(1).
					WithFabricInterface("ib1").
					WithFabricProvider("ofi+verbs"),
			),
			expNumaSet: []int{2, 1},
		},
		"multi engine with first_core set; detected affinities overridden": {
			cfg: baseSrvCfg().WithEngines(
				engine.MockConfig().
					WithServiceThreadCore(1).
					WithFabricInterface("ib0").
					WithFabricProvider("ofi+verbs"),
				engine.MockConfig().
					WithServiceThreadCore(25).
					WithFabricInterface("ib1").
					WithFabricProvider("ofi+verbs"),
			),
			expNumaSet:  []int{-1, -1}, // PinnedNumaNode should not be set
			expFabNumas: []int{0, 0},
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotNumaSet := make([]int, 0, len(tc.expNumaSet))
			fabNumaSet := make([]int, 0, len(tc.expFabNumas))

			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			if tc.affSrcSet == nil {
				tc.affSrcSet = []EngineAffinityFn{
					genAffFn("ib0", 1),
					genAffFn("ib1", 2),
				}
			}

			gotErr := tc.cfg.SetEngineAffinities(log, tc.affSrcSet...)
			test.CmpErr(t, tc.expErr, gotErr)
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
