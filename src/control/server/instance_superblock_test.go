//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	sysprov "github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

func TestServer_Instance_createSuperblock(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	h := NewEngineHarness(log)
	for _, mnt := range []string{"one", "two"} {
		if err := os.MkdirAll(filepath.Join(testDir, mnt), 0777); err != nil {
			t.Fatal(err)
		}
		cfg := engine.MockConfig().
			WithSystemName(t.Name()).
			WithStorage(
				storage.NewTierConfig().
					WithStorageClass("ram").
					WithScmRamdiskSize(1).
					WithScmMountPoint(mnt),
			)
		r := engine.NewRunner(log, cfg)
		msc := &sysprov.MockSysConfig{
			IsMountedBool: true,
			RealReadFile:  true,
		}
		mbc := &scm.MockBackendConfig{}
		mp := storage.NewProvider(log, 0, &cfg.Storage,
			sysprov.NewMockSysProvider(log, msc),
			scm.NewMockProvider(log, mbc, msc), nil, nil)
		ei := NewEngineInstance(log, mp, nil, r, nil).
			WithHostFaultDomain(system.MustCreateFaultDomainFromString("/host1"))
		ei.fsRoot = testDir
		if err := h.AddInstance(ei); err != nil {
			t.Fatal(err)
		}
	}

	for _, e := range h.Instances() {
		if err := e.(*EngineInstance).createSuperblock(); err != nil {
			t.Fatal(err)
		}
	}

	h.started.SetTrue()
	mi := h.instances[0].(*EngineInstance)
	if mi._superblock == nil {
		t.Fatal("instance superblock is nil after createSuperblock()")
	}
	if mi._superblock.System != t.Name() {
		t.Fatalf("expected superblock system name to be %q, got %q", t.Name(), mi._superblock.System)
	}

	for idx, e := range h.Instances() {
		i := e.(*EngineInstance)

		test.AssertEqual(t, i.hostFaultDomain.String(), i._superblock.HostFaultDomain, fmt.Sprintf("instance %d", idx))

		if i == mi {
			continue
		}
		if i._superblock.UUID == mi._superblock.UUID {
			t.Fatal("second instance has same superblock as first")
		}
	}
}

func TestServer_Instance_superblockPath(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg     *engine.Config
		expPath string
	}{
		"control metadata configured": {
			cfg: engine.MockConfig().
				WithSystemName(t.Name()).
				WithStorageControlMetadataPath("/etc/daos").
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(1).
						WithScmMountPoint("/mnt/scm"),
				),
			expPath: "/etc/daos/daos_control/engine1/superblock",
		},
		"fall back to scm": {
			cfg: engine.MockConfig().
				WithSystemName(t.Name()).
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("ram").
						WithScmRamdiskSize(1).
						WithScmMountPoint("/mnt/scm1"),
				),
			expPath: "/mnt/scm1/superblock",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			sp := storage.NewProvider(log, 1, &tc.cfg.Storage, nil, nil, nil, nil)
			ei := newTestEngine(log, false, sp, tc.cfg)

			result := ei.superblockPath()

			test.AssertEqual(t, tc.expPath, result, "")
		})
	}
}

func TestServer_Instance_RemoveSuperblock(t *testing.T) {
	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	if err := os.MkdirAll(filepath.Join(testDir, "one"), 0777); err != nil {
		t.Fatal(err)
	}
	cfg := engine.MockConfig().
		WithStorage(
			storage.NewTierConfig().
				WithStorageClass("ram").
				WithScmRamdiskSize(1).
				WithScmMountPoint("one"),
		)
	sp := storage.NewProvider(log, 0, &cfg.Storage, nil, nil, nil, nil)
	ei := newTestEngine(log, false, sp, cfg)
	ei.fsRoot = testDir
	ei._superblock = &Superblock{System: t.Name()}

	sbPath := ei.superblockPath()
	if err := os.WriteFile(sbPath, []byte("fake-superblock"), 0644); err != nil {
		t.Fatal(err)
	}

	if err := ei.RemoveSuperblock(); err != nil {
		t.Fatalf("unexpected error removing existing superblock: %s", err)
	}
	if ei._superblock != nil {
		t.Fatal("expected in-memory superblock to be cleared")
	}
	if _, err := os.Stat(sbPath); !os.IsNotExist(err) {
		t.Fatalf("expected superblock file to be removed, stat err: %v", err)
	}

	// Removing an already-removed superblock surfaces a not-exist error; callers
	// (e.g. eraseEngineFormat, PrepareEngineReformat) are expected to tolerate this
	// rather than treat it as fatal.
	err := ei.RemoveSuperblock()
	if !os.IsNotExist(err) {
		t.Fatalf("expected not-exist error removing already-removed superblock, got: %v", err)
	}
}

func TestServer_PrepareEngineReformat(t *testing.T) {
	for _, layout := range []string{"PMem", "MD-on-SSD"} {
		t.Run(layout, func(t *testing.T) {
			for name, tc := range map[string]struct {
				running      bool
				noMetaDir    bool
				noSuperblock bool
				expErr       error
			}{
				"engine running": {
					running: true,
					expErr:  errors.New("engine 1 is still running"),
				},
				"metadata directory missing": {
					noMetaDir: true,
					expErr:    errors.New("engine 1 metadata directory"),
				},
				"success": {},
				"superblock already removed": {
					noSuperblock: true,
				},
			} {
				t.Run(name, func(t *testing.T) {
					log, buf := logging.NewTestLogger(t.Name())
					defer test.ShowBufferOnFailure(t, buf)

					scmTier := storage.NewTierConfig().
						WithScmMountPoint(filepath.Join(t.TempDir(), "scm"))
					cfg := engine.MockConfig()
					if layout == "PMem" {
						cfg.WithStorage(scmTier.WithStorageClass("dcpm").
							WithScmDeviceList("/dev/pmem1"))
					} else {
						cfg.WithStorage(scmTier.WithStorageClass("ram").WithScmRamdiskSize(1),
							storage.NewTierConfig().
								WithStorageClass("nvme").
								WithBdevDeviceList("0000:01:00.0").
								WithBdevDeviceRoles(storage.BdevRoleAll)).
							WithStorageControlMetadataPath(t.TempDir())
					}
					cfg.Index = 1

					sp := storage.NewProvider(log, int(cfg.Index), &cfg.Storage,
						nil, nil, nil, nil)
					ei := NewEngineInstance(log, sp, nil, nil, nil)
					setupTestEngineWithConfig(t, ei, 3, cfg, !tc.running)

					if !tc.noMetaDir {
						if err := os.MkdirAll(sp.ControlMetadataEnginePath(), 0755); err != nil {
							t.Fatal(err)
						}
					}
					sbPath := ei.superblockPath()
					if !tc.noMetaDir && !tc.noSuperblock {
						if err := os.WriteFile(sbPath, []byte("x"), 0644); err != nil {
							t.Fatal(err)
						}
					}

					err := PrepareEngineReformat(ei)
					test.CmpErr(t, tc.expErr, err)

					// A refused request must leave the engine and its superblock untouched.
					expRemoved := tc.expErr == nil
					test.AssertEqual(t, !expRemoved, ei.hasSuperblock(),
						"in-memory superblock")
					if tc.noMetaDir {
						return
					}
					_, statErr := os.Stat(sbPath)
					test.AssertEqual(t, expRemoved, os.IsNotExist(statErr),
						"superblock file removed")
				})
			}
		})
	}
}
