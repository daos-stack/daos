//
// (C) Copyright 2020-2022 Intel Corporation.
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
		}
		mbc := &scm.MockBackendConfig{}
		mp := storage.NewProvider(log, 0, &cfg.Storage, sysprov.NewMockSysProvider(log, msc), scm.NewMockProvider(log, mbc, msc), nil, nil)
		ei := NewEngineInstance(log, mp, nil, r).
			WithHostFaultDomain(system.MustCreateFaultDomainFromString("/host1"))
		ei.fsRoot = testDir
		if err := h.AddInstance(ei); err != nil {
			t.Fatal(err)
		}
	}

	for _, e := range h.Instances() {
		if err := e.(*EngineInstance).createSuperblock(false); err != nil {
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
