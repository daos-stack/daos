//
// (C) Copyright 2019-2023 Intel Corporation.
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
		cfg = config.DefaultServer().WithEngines(engine.MockConfig().WithTargetCount(1))
	}

	// share sys provider between engines to be able to access to same mock config data
	sysProv := system.NewMockSysProvider(log, smsc)
	mounter := mount.NewProvider(log, sysProv)
	scmProv := scm.NewProvider(log, scm.NewMockBackend(smbc), sysProv, mounter)
	bdevProv := bdev.NewMockProvider(log, bmbc)

	mscs := NewMockStorageControlService(log, cfg.Engines, sysProv, scmProv, bdevProv, nil)

	cs := &ControlService{
		StorageControlService: *mscs,
		harness:               &EngineHarness{log: log},
		events:                events.NewPubSub(test.Context(t), log),
		srvCfg:                cfg,
	}

	for idx, ec := range cfg.Engines {
		trc := new(engine.TestRunnerConfig)
		if started {
			trc.Running.SetTrue()
		}
		runner := engine.NewTestRunner(trc, ec)
		storProv := storage.MockProvider(log, 0, &ec.Storage, sysProv, scmProv, bdevProv,
			nil)

		ei := NewEngineInstance(log, storProv, nil, runner)
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
