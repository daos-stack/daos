//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// TestDaosServer_StoragePrepareReformat exercises real file removal through the
// local command while mocking only process and mount checks.
func TestDaosServer_StoragePrepareReformat(t *testing.T) {
	for _, mode := range []string{"PMem", "MD-on-SSD", "MD-on-SSD device"} {
		t.Run(mode, func(t *testing.T) {
			for name, tc := range map[string]struct {
				index          uint32
				noSuperblock   bool
				forceRemoveErr bool
				notMounted     bool
				mountErr       error
				processErr     error
				expErr         error
			}{
				"engine 0": {},
				"engine 1": {
					index: 1,
				},
				"already prepared": {
					index:        1,
					noSuperblock: true,
				},
				"invalid engine index": {
					index:  2,
					expErr: errors.New("engine with index 2 not found"),
				},
				"server running": {
					processErr: errors.New("another daos_server process is already running"),
					expErr:     errors.New("stop daos_server"),
				},
				"storage not mounted": {
					notMounted: true,
					expErr:     errors.New("metadata storage is not mounted"),
				},
				"mount check fails": {
					mountErr: errors.New("mount check failed"),
					expErr:   errors.New("checking metadata mount"),
				},
				"removal fails": {
					forceRemoveErr: true,
					expErr:         errors.New("engine 0: remove"),
				},
			} {
				t.Run(name, func(t *testing.T) {
					if mode == "MD-on-SSD" && (tc.notMounted || tc.mountErr != nil) {
						t.Skip("path-only control metadata does not require a dedicated mount")
					}
					log, buf := logging.NewTestLogger(t.Name())
					defer test.ShowBufferOnFailure(t, buf)

					cfg := config.DefaultServer().WithFabricProvider("ofi+tcp").
						WithMgmtSvcReplicas("127.0.0.1")
					preservedPaths := []string{}
					if mode != "PMem" {
						cfg.Metadata.Path = t.TempDir()
						if mode == "MD-on-SSD device" {
							cfg.Metadata.DevicePath = "/dev/test-metadata"
						}
						preservedPaths = append(preservedPaths,
							filepath.Join(cfg.Metadata.Directory(), "control_raft", "state"))
					}

					sbPaths := []string{}
					for idx := 0; idx < 2; idx++ {
						mntPt := t.TempDir()
						scmCfg := storage.NewTierConfig().WithStorageClass("dcpm").
							WithScmMountPoint(mntPt).
							WithScmDeviceList(fmt.Sprintf("/dev/pmem%d", idx))
						tiers := storage.TierConfigs{scmCfg}
						sbDir := mntPt
						if mode != "PMem" {
							scmCfg.WithStorageClass("ram").WithScmDeviceList()
							tiers = append(tiers, storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList(fmt.Sprintf("0000:%02d:00.0", idx+1)).
								WithBdevDeviceRoles(storage.BdevRoleAll))
							sbDir = filepath.Join(cfg.Metadata.Path, "daos_control",
								fmt.Sprintf("engine%d", idx))
							preservedPaths = append(preservedPaths, filepath.Join(mntPt, "superblock"))
						}
						cfg.Engines = append(cfg.Engines, engine.MockConfig().WithTargetCount(1).
							WithStorage(tiers...).WithFabricInterface("eth0").
							WithFabricInterfacePort(31416+idx))
						if err := os.MkdirAll(sbDir, 0755); err != nil {
							t.Fatal(err)
						}
						preservedPaths = append(preservedPaths, filepath.Join(sbDir, "other-metadata"))
						sbPath := filepath.Join(sbDir, "superblock")
						sbPaths = append(sbPaths, sbPath)
						if tc.noSuperblock && uint32(idx) == tc.index {
							continue
						}
						if tc.forceRemoveErr && uint32(idx) == tc.index {
							if err := os.Mkdir(sbPath, 0755); err != nil {
								t.Fatal(err)
							}
							sbPath = filepath.Join(sbPath, "not-empty")
							preservedPaths = append(preservedPaths, sbPath)
						}
						if err := os.WriteFile(sbPath, []byte("preserve"), 0644); err != nil {
							t.Fatal(err)
						}
					}
					for _, path := range preservedPaths {
						if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
							t.Fatal(err)
						}
						if err := os.WriteFile(path, []byte("preserve"), 0644); err != nil {
							t.Fatal(err)
						}
					}

					cfgPath := filepath.Join(t.TempDir(), "daos_server.yml")
					if err := cfg.SaveToFile(cfgPath); err != nil {
						t.Fatal(err)
					}
					var opts mainOpts
					cmd := &opts.Storage.PrepareReformat
					cmd.checkDupeProcess = func() error { return tc.processErr }
					cmd.sys = system.NewMockSysProvider(log, &system.MockSysConfig{
						IsMountedBool: !tc.notMounted,
						IsMountedErr:  tc.mountErr,
					})

					err := parseOpts([]string{"storage", "prepare-reformat", "--config", cfgPath,
						"--engine-index", fmt.Sprint(tc.index)}, &opts, log)
					test.CmpErr(t, tc.expErr, err)

					// Only a successful run tells the admin how to trigger the format.
					if tc.expErr == nil {
						test.AssertStringContains(t, buf.String(),
							fmt.Sprintf("Engine instance %d prepared for reformat", tc.index),
							"starting daos_server", `"dmg storage format"`)
					} else if strings.Contains(buf.String(), "prepared for reformat") {
						t.Fatalf("unexpected success message after error %q:\n%s", err, buf.String())
					}

					for idx, path := range sbPaths {
						_, statErr := os.Stat(path)
						if statErr != nil && !os.IsNotExist(statErr) {
							t.Fatal(statErr)
						}
						wantRemoved := uint32(idx) == tc.index && (tc.noSuperblock || err == nil)
						test.AssertEqual(t, os.IsNotExist(statErr), wantRemoved, path)
						if !wantRemoved && !tc.forceRemoveErr {
							preservedPaths = append(preservedPaths, path)
						}
					}
					for _, path := range preservedPaths {
						data, err := os.ReadFile(path)
						if err != nil {
							t.Fatal(err)
						}
						test.AssertEqual(t, string(data), "preserve", path)
					}
				})
			}
		})
	}
}

func TestDaosServer_StoragePrepareReformat_Options(t *testing.T) {
	runCmdTests(t, []cmdTest{
		{
			name:   "missing engine index",
			cmd:    "storage prepare-reformat",
			expErr: errors.New("required flag"),
		},
		{
			name: "short options",
			cmd:  "storage prepare-reformat -o /tmp/server.yml -e 1",
			expCmd: printCommand(t, &storagePrepareReformatCmd{
				cfgCmd:      cfgCmd{ConfigPath: "/tmp/server.yml"},
				EngineIndex: 1,
			}),
		},
		{
			name: "long options",
			cmd:  "storage prepare-reformat --config /tmp/server.yml --engine-index 0",
			expCmd: printCommand(t, &storagePrepareReformatCmd{
				cfgCmd: cfgCmd{ConfigPath: "/tmp/server.yml"},
			}),
		},
	})

	t.Run("ignore-config rejected", func(t *testing.T) {
		log, buf := logging.NewTestLogger(t.Name())
		defer test.ShowBufferOnFailure(t, buf)
		var opts mainOpts
		opts.Storage.PrepareReformat.checkDupeProcess = func() error { return nil }
		err := parseOpts([]string{"storage", "prepare-reformat", "--ignore-config",
			"--engine-index", "0"}, &opts, log)
		test.CmpErr(t, errors.New("server config is required"), err)
	})
}
