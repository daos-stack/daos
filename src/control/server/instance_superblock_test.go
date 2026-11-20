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
	// (e.g. eraseEngineFormat, ControlService.RemoveSuperblock) are expected to
	// tolerate this rather than treat it as fatal.
	err := ei.RemoveSuperblock()
	if !os.IsNotExist(err) {
		t.Fatalf("expected not-exist error removing already-removed superblock, got: %v", err)
	}
}
