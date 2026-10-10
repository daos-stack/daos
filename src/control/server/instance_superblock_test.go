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

// TestServer_Instance_needsSuperblock_staleInMemory verifies that needsSuperblock()
// always re-reads the on-disk superblock rather than trusting stale in-memory state,
// so a superblock removed from disk (e.g. by a format --replace or erase operation)
// is correctly detected even if a previous in-memory superblock is still cached.
func TestServer_Instance_needsSuperblock_staleInMemory(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	testDir, cleanup := test.CreateTestDir(t)
	defer cleanup()

	mnt := "mnt"
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
	ei := NewEngineInstance(log, mp, nil, r, nil)
	ei.fsRoot = testDir

	if err := ei.createSuperblock(); err != nil {
		t.Fatalf("createSuperblock(): %s", err)
	}

	needs, err := ei.needsSuperblock()
	if err != nil {
		t.Fatalf("needsSuperblock(): %s", err)
	}
	if needs {
		t.Fatal("expected needsSuperblock() to be false with superblock present on disk")
	}

	// Simulate stale in-memory superblock state (e.g. retained from before a
	// format --replace or system erase removed the on-disk copy) by removing the
	// on-disk file without clearing the cached in-memory superblock.
	if ei._superblock == nil {
		t.Fatal("expected in-memory superblock to be set after createSuperblock()")
	}
	if err := os.Remove(ei.superblockPath()); err != nil {
		t.Fatalf("failed to remove on-disk superblock: %s", err)
	}

	needs, err = ei.needsSuperblock()
	if err != nil {
		t.Fatalf("needsSuperblock(): %s", err)
	}
	if !needs {
		t.Fatal("expected needsSuperblock() to be true after on-disk superblock removed, " +
			"despite stale in-memory superblock still being cached")
	}
	if ei._superblock != nil {
		t.Fatal("expected in-memory superblock to be cleared (nil) after " +
			"needsSuperblock() detected the on-disk superblock was missing")
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
