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

func newMockControlServiceFromBackends(t *testing.T, log logging.Logger, cfg *config.Server, bmb *bdev.MockBackend, smb *scm.MockBackend, smsc *system.MockSysConfig, notStarted ...bool) *ControlService {
	t.Helper()

	if cfg == nil {
		cfg = config.DefaultServer().WithEngines(engine.MockConfig().WithTargetCount(1))
	}

	// Share sys provider between engines to be able to access to same mock config data.
	bp := bdev.NewProvider(log, bmb)
	syp := system.NewMockSysProvider(log, smsc)
	mp := mount.NewProvider(log, syp)
	sp := scm.NewProvider(log, smb, syp, mp)

	mscs := NewMockStorageControlService(log, cfg.Engines, syp, sp, bp, nil)

	cs := &ControlService{
		StorageControlService: *mscs,
		harness:               &EngineHarness{log: log},
		events:                events.NewPubSub(test.Context(t), log),
		srvCfg:                cfg,
	}

	started := make([]bool, len(cfg.Engines))
	for idx := range started {
		started[idx] = true
	}
	switch len(notStarted) {
	case 0: // Not specified so start all engines.
	case 1:
		if notStarted[0] {
			// If single true notStarted bool, don't start any engines.
			for idx := range started {
				started[idx] = false
			}
		}
	case len(cfg.Engines): // One notStarted bool specified for each engine.
		for idx := range started {
			started[idx] = !notStarted[idx]
		}
	default:
		t.Fatal("len notStarted != len cfg.Engines")
	}

	for idx, ec := range cfg.Engines {
		trc := new(engine.TestRunnerConfig)
		if started[idx] {
			trc.Running.SetTrue()
		}
		runner := engine.NewTestRunner(trc, ec)
		storProv := storage.MockProvider(log, 0, &ec.Storage, syp, sp, bp, nil)

		ei := NewEngineInstance(log, storProv, nil, runner)
		ei.setSuperblock(&Superblock{
			Rank: ranklist.NewRankPtr(uint32(idx)),
		})
		if started[idx] {
			ei.ready.SetTrue()
		}
		if err := cs.harness.AddInstance(ei); err != nil {
			t.Fatal(err)
		}
	}

	return cs
}

// mockControlService takes cfgs for tuneable scm and sys provider behavior but
// default nvmeStorage behavior.
func mockControlService(t *testing.T, log logging.Logger, cfg *config.Server, bmbc *bdev.MockBackendConfig, smbc *scm.MockBackendConfig, smsc *system.MockSysConfig, notStarted ...bool) *ControlService {
	t.Helper()

	bmb := bdev.NewMockBackend(bmbc)
	smb := scm.NewMockBackend(smbc)

	return newMockControlServiceFromBackends(t, log, cfg, bmb, smb, smsc, notStarted...)
}
