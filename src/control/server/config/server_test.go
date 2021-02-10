//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package config

import (
	"bufio"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

const (
	sConfigUncomment = "daos_server_uncomment.yml"
	socketsExample   = "../../../../utils/config/examples/daos_server_sockets.yml"
	psm2Example      = "../../../../utils/config/examples/daos_server_psm2.yml"
	defaultConfig    = "../../../../utils/config/daos_server.yml"
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

		// If we're in a server config, reset the
		// seen map to allow the same params in different
		// server configs.
		if line == "-" {
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

// supply mock external interface, populates config from given file path
func mockConfigFromFile(t *testing.T, path string) *Server {
	t.Helper()
	c := DefaultServer().
		WithProviderValidator(netdetect.ValidateProviderStub).
		WithNUMAValidator(netdetect.ValidateNUMAStub).
		WithGetNetworkDeviceClass(getDeviceClassStub)
	c.Path = path

	if err := c.Load(); err != nil {
		t.Fatalf("failed to load %s: %s", path, err)
	}

	return c
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

func TestServer_ConfigMarshalUnmarshal(t *testing.T) {
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

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(
					Server{},
					security.CertificateConfig{},
				),
				cmpopts.IgnoreFields(Server{}, "GetDeviceClassFn"),
			}
			if diff := cmp.Diff(configA, configB, cmpOpts...); diff != "" {
				t.Fatalf("(-want, +got): %s", diff)
			}
		})
	}
}

func TestServer_ConstructedConfig(t *testing.T) {
	testDir, cleanup := CreateTestDir(t)
	defer cleanup()

	// First, load a config based on the server config with all options uncommented.
	testFile := filepath.Join(testDir, sConfigUncomment)
	uncommentServerConfig(t, testFile)
	defaultCfg := mockConfigFromFile(t, testFile)

	var numaNode0 uint = 0
	var numaNode1 uint = 1

	// Next, construct a config to compare against the first one. It should be
	// possible to construct an identical configuration with the helpers.
	constructed := DefaultServer().
		WithControlPort(10001).
		WithBdevInclude("0000:81:00.1", "0000:81:00.2", "0000:81:00.3").
		WithBdevExclude("0000:81:00.1").
		WithDisableVFIO(true). // vfio enabled by default
		WithDisableVMD(false). // vmd disabled by default
		WithNrHugePages(4096).
		WithControlLogMask(ControlLogLevelError).
		WithControlLogFile("/tmp/daos_control.log").
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
		WithServers(
			ioserver.NewConfig().
				WithRank(0).
				WithTargetCount(20).
				WithHelperStreamCount(20).
				WithServiceThreadCore(1).
				WithScmMountPoint("/mnt/daos/1").
				WithScmClass("ram").
				WithScmRamdiskSize(16).
				WithBdevClass("nvme").
				WithBdevDeviceList("0000:81:00.0").
				WithFabricInterface("qib0").
				WithFabricInterfacePort(20000).
				WithPinnedNumaNode(&numaNode0).
				WithEnvVars("CRT_TIMEOUT=30").
				WithLogFile("/tmp/daos_server1.log").
				WithLogMask("WARN"),
			ioserver.NewConfig().
				WithRank(1).
				WithTargetCount(20).
				WithHelperStreamCount(20).
				WithServiceThreadCore(22).
				WithScmMountPoint("/mnt/daos/2").
				WithScmClass("dcpm").
				WithScmDeviceList("/dev/pmem0").
				WithBdevClass("malloc").
				WithBdevDeviceList("/tmp/daos-bdev1", "/tmp/daos-bdev2").
				WithBdevDeviceCount(1).
				WithBdevFileSize(4).
				WithFabricInterface("qib1").
				WithFabricInterfacePort(20000).
				WithPinnedNumaNode(&numaNode1).
				WithEnvVars("CRT_TIMEOUT=100").
				WithLogFile("/tmp/daos_server2.log").
				WithLogMask("WARN"),
		)
	constructed.Path = testFile // just to avoid failing the cmp

	cmpOpts := []cmp.Option{
		cmpopts.IgnoreUnexported(
			Server{},
			security.CertificateConfig{},
		),
		cmpopts.IgnoreFields(Server{}, "GetDeviceClassFn"),
	}
	if diff := cmp.Diff(defaultCfg, constructed, cmpOpts...); diff != "" {
		t.Fatalf("(-want, +got): %s", diff)
	}
}

func TestServer_ConfigValidation(t *testing.T) {
	noopExtra := func(c *Server) *Server { return c }

	for name, tt := range map[string]struct {
		extraConfig func(c *Server) *Server
		expErr      error
	}{
		"example config": {
			noopExtra,
			nil,
		},
		"nil server entry": {
			func(c *Server) *Server {
				var nilIOServerConfig *ioserver.Config
				return c.WithServers(nilIOServerConfig)
			},
			errors.New("validation"),
		},
		"single access point": {
			func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:1234")
			},
			nil,
		},
		"multiple access points (even)": {
			func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:1234", "5.6.7.8:5678")
			},
			FaultConfigEvenAccessPoints,
		},
		"multiple access points (odd)": {
			func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:1234", "5.6.7.8:5678", "1.5.3.8:6247")
			},
			nil,
		},
		"multiple access points (dupes)": {
			func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:1234", "5.6.7.8:5678", "1.2.3.4:1234")
			},
			FaultConfigEvenAccessPoints,
		},
		"no access points": {
			func(c *Server) *Server {
				return c.WithAccessPoints()
			},
			FaultConfigBadAccessPoints,
		},
		"single access point no port": {
			func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4")
			},
			nil,
		},
		"single access point invalid port": {
			func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4").
					WithControlPort(0)
			},
			FaultConfigBadControlPort,
		},
		"single access point including invalid port": {
			func(c *Server) *Server {
				return c.WithAccessPoints("1.2.3.4:0")
			},
			FaultConfigBadControlPort,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			testDir, cleanup := CreateTestDir(t)
			defer cleanup()

			// First, load a config based on the server config with all options uncommented.
			testFile := filepath.Join(testDir, sConfigUncomment)
			uncommentServerConfig(t, testFile)
			config := mockConfigFromFile(t, testFile)

			// Apply extra config test case
			config = tt.extraConfig(config)

			CmpErr(t, tt.expErr, config.Validate(log))
		})
	}
}

func TestServer_ConfigRelativeWorkingPath(t *testing.T) {
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

func TestServer_WithServersInheritsMainConfig(t *testing.T) {
	testFabric := "test-fabric"
	testModules := "a,b,c"
	testSystemName := "test-system"
	testSocketDir := "test-sockets"

	wantCfg := ioserver.NewConfig().
		WithFabricProvider(testFabric).
		WithModules(testModules).
		WithSocketDir(testSocketDir).
		WithSystemName(testSystemName)

	config := DefaultServer().
		WithFabricProvider(testFabric).
		WithModules(testModules).
		WithSocketDir(testSocketDir).
		WithSystemName(testSystemName).
		WithServers(ioserver.NewConfig())

	if diff := cmp.Diff(wantCfg, config.Servers[0]); diff != "" {
		t.Fatalf("unexpected server config (-want, +got):\n%s\n", diff)
	}
}

func TestServer_ConfigDuplicateValues(t *testing.T) {
	configA := func() *ioserver.Config {
		return ioserver.NewConfig().
			WithLogFile("a").
			WithFabricInterface("a").
			WithFabricInterfacePort(42).
			WithScmClass("ram").
			WithScmRamdiskSize(1).
			WithScmMountPoint("a")
	}
	configB := func() *ioserver.Config {
		return ioserver.NewConfig().
			WithLogFile("b").
			WithFabricInterface("b").
			WithFabricInterfacePort(42).
			WithScmClass("ram").
			WithScmRamdiskSize(1).
			WithScmMountPoint("b")
	}

	for name, tc := range map[string]struct {
		configA *ioserver.Config
		configB *ioserver.Config
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
				WithScmMountPoint(configA().Storage.SCM.MountPoint),
			expErr: FaultConfigDuplicateScmMount(1, 0),
		},
		"duplicate scm_list": {
			configA: configA().
				WithScmClass("dcpm").
				WithScmRamdiskSize(0).
				WithScmDeviceList("a"),
			configB: configB().
				WithScmClass("dcpm").
				WithScmRamdiskSize(0).
				WithScmDeviceList("a"),
			expErr: FaultConfigDuplicateScmDeviceList(1, 0),
		},
		"overlapping bdev_list": {
			configA: configA().
				WithBdevDeviceList("a"),
			configB: configB().
				WithBdevDeviceList("b", "a"),
			expErr: FaultConfigOverlappingBdevDeviceList(1, 0),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			conf := DefaultServer().
				WithFabricProvider("test").
				WithGetNetworkDeviceClass(getDeviceClassStub).
				WithServers(tc.configA, tc.configB)

			gotErr := conf.Validate(log)
			CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestServer_ConfigNetworkDeviceClass(t *testing.T) {
	configA := func() *ioserver.Config {
		return ioserver.NewConfig().
			WithLogFile("a").
			WithScmClass("ram").
			WithScmRamdiskSize(1).
			WithFabricInterfacePort(42).
			WithScmMountPoint("a")
	}
	configB := func() *ioserver.Config {
		return ioserver.NewConfig().
			WithLogFile("b").
			WithScmClass("ram").
			WithScmRamdiskSize(1).
			WithFabricInterfacePort(43).
			WithScmMountPoint("b")
	}

	for name, tc := range map[string]struct {
		configA *ioserver.Config
		configB *ioserver.Config
		expErr  error
	}{
		"successful validation with matching Infiniband": {
			configA: configA().
				WithFabricInterface("ib1"),
			configB: configB().
				WithFabricInterface("ib0"),
		},
		"successful validation with mathching Ethernet": {
			configA: configA().
				WithFabricInterface("eth0"),
			configB: configB().
				WithFabricInterface("eth1"),
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
			log, buf := logging.NewTestLogger(t.Name())
			defer ShowBufferOnFailure(t, buf)

			conf := DefaultServer().
				WithFabricProvider("test").
				WithGetNetworkDeviceClass(getDeviceClassStub).
				WithServers(tc.configA, tc.configB)

			gotErr := conf.Validate(log)
			CmpErr(t, tc.expErr, gotErr)
		})
	}
}
