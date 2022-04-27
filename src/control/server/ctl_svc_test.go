//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"testing"

	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
	"github.com/daos-stack/daos/src/control/system"
)

// mockControlService takes cfgs for tuneable scm and sys provider behavior but
// default nvmeStorage behavior (cs.nvoe can be subsequently replaced in test).
func mockControlService(t *testing.T, log logging.Logger, cfg *config.Server, bmbc *bdev.MockBackendConfig, smbc *scm.MockBackendConfig, smsc *scm.MockSysConfig) *ControlService {
	t.Helper()

	if cfg == nil {
		cfg = config.DefaultServer().WithEngines(
			engine.MockConfig().WithTargetCount(1),
		)
	}

	// share sys provider between engines to be able to access to same mock config data
	sp := scm.NewMockSysProvider(log, smsc)

	cs := &ControlService{
		StorageControlService: *NewMockStorageControlService(log, cfg.Engines,
			sp,
			scm.NewProvider(log, scm.NewMockBackend(smbc), sp),
			bdev.NewMockProvider(log, bmbc)),
		harness: &EngineHarness{
			log: log,
		},
		events: events.NewPubSub(context.TODO(), log),
		srvCfg: cfg,
	}

	for _, ec := range cfg.Engines {
		trc := new(engine.TestRunnerConfig)
		trc.Running.SetTrue()
		runner := engine.NewTestRunner(trc, ec)

		sp := storage.MockProvider(log, 0, &ec.Storage, sp,
			scm.NewProvider(log, scm.NewMockBackend(smbc), sp),
			bdev.NewMockProvider(log, bmbc))
		ei := NewEngineInstance(log, sp, nil, runner)
		ei.setSuperblock(&Superblock{
			Rank: system.NewRankPtr(ec.Rank.Uint32()),
		})
		ei.ready.SetTrue()
		if err := cs.harness.AddInstance(ei); err != nil {
			t.Fatal(err)
		}
	}

	return cs
}

func mockControlServiceNoSB(t *testing.T, log logging.Logger, cfg *config.Server, bmbc *bdev.MockBackendConfig, smbc *scm.MockBackendConfig, smsc *scm.MockSysConfig) *ControlService {
	cs := mockControlService(t, log, cfg, bmbc, smbc, smsc)

	// don't set a superblock and init with a stopped test runner
	for i, e := range cs.harness.instances {
		ei := e.(*EngineInstance)
		ei.setSuperblock(nil)
		ei.runner = engine.NewTestRunner(nil, cfg.Engines[i])
	}

	return cs
}
