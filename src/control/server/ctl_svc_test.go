//
// (C) Copyright 2019-2021 Intel Corporation.
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
			engine.NewConfig().WithTargetCount(1),
		)
	}

	cs := &ControlService{
		StorageControlService: *NewMockStorageControlService(log,
			cfg.Engines,
			scm.NewMockSysProvider(smsc),
			scm.NewMockProvider(log, smbc, smsc),
			bdev.NewMockProvider(log, bmbc)),
		harness: &EngineHarness{
			log: log,
		},
		events: events.NewPubSub(context.TODO(), log),
		srvCfg: cfg,
	}

	for _, engineCfg := range cfg.Engines {
		rCfg := new(engine.TestRunnerConfig)
		rCfg.Running.SetTrue()
		runner := engine.NewTestRunner(rCfg, engineCfg)

		storageProvider := storage.MockProvider(log, 0, &engineCfg.Storage, cs.storage.Sys, cs.storage.Scm, cs.storage.Bdev)
		instance := NewEngineInstance(log, storageProvider, nil, runner)
		instance.setSuperblock(&Superblock{
			Rank: system.NewRankPtr(engineCfg.Rank.Uint32()),
		})
		if err := cs.harness.AddInstance(instance); err != nil {
			t.Fatal(err)
		}
	}

	return cs
}

func mockControlServiceNoSB(t *testing.T, log logging.Logger, cfg *config.Server, bmbc *bdev.MockBackendConfig, smbc *scm.MockBackendConfig, smsc *scm.MockSysConfig) *ControlService {
	cs := mockControlService(t, log, cfg, bmbc, smbc, smsc)

	// don't set a superblock and init with a stopped test runner
	for i, e := range cs.harness.instances {
		srv := e.(*EngineInstance)
		srv.setSuperblock(nil)
		srv.runner = engine.NewTestRunner(nil, cfg.Engines[i])
	}

	return cs
}
