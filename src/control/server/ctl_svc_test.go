//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/mount"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// mockControlService takes cfgs for tuneable scm and sys provider behavior but
// default nvmeStorage behavior (cs.nvoe can be subsequently replaced in test).
func mockControlService(t *testing.T, log logging.Logger, cfg *config.Server, bmbc *bdev.MockBackendConfig, smbc *scm.MockBackendConfig, smsc *system.MockSysConfig, notStarted ...bool) *ControlService {
	t.Helper()

	started := true
	if len(notStarted) > 0 && notStarted[0] {
		started = false
	}

	if cfg == nil {
		cfg = config.DefaultServer().WithEngines(
			engine.MockConfig().WithTargetCount(1),
		)
	}

	// share sys provider between engines to be able to access to same mock config data
	sp := system.NewMockSysProvider(log, smsc)
	mounter := mount.NewProvider(log, sp)

	ctx := test.Context(t)
	cs := &ControlService{
		StorageControlService: *NewMockStorageControlService(log, cfg.Engines,
			sp,
			scm.NewProvider(log, scm.NewMockBackend(smbc), sp, mounter),
			bdev.NewMockProvider(log, bmbc)),
		harness: &EngineHarness{
			log: log,
		},
		events: events.NewPubSub(ctx, log),
		srvCfg: cfg,
	}

	for idx, ec := range cfg.Engines {
		trc := new(engine.TestRunnerConfig)
		if started {
			trc.Running.SetTrue()
		}
		runner := engine.NewTestRunner(trc, ec)

		sp := storage.MockProvider(log, 0, &ec.Storage, sp,
			scm.NewProvider(log, scm.NewMockBackend(smbc), sp, mounter),
			bdev.NewMockProvider(log, bmbc))
		ei := NewEngineInstance(log, sp, nil, runner)
		ei.setSuperblock(&Superblock{
			Rank: ranklist.NewRankPtr(uint32(idx)),
		})
		if started {
			ei.ready.SetTrue()
		}
		if err := cs.harness.AddInstance(ei); err != nil {
			t.Fatal(err)
		}
	}

	return cs
}

func mockControlServiceNoSB(t *testing.T, log logging.Logger, cfg *config.Server, bmbc *bdev.MockBackendConfig, smbc *scm.MockBackendConfig, smsc *system.MockSysConfig) *ControlService {
	cs := mockControlService(t, log, cfg, bmbc, smbc, smsc)

	// don't set a superblock and init with a stopped test runner
	for i, e := range cs.harness.instances {
		ei := e.(*EngineInstance)
		ei.setSuperblock(nil)
		ei.runner = engine.NewTestRunner(nil, cfg.Engines[i])
	}

	return cs
}
