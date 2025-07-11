//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"sync"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// NewMockStorageControlService returns a StorageControlService with a mocked storage provider
// consisting of the given sys, scm and bdev providers.
func NewMockStorageControlService(log logging.Logger, ecs []*engine.Config, sys storage.SystemProvider, scm storage.ScmProvider, bdev storage.BdevProvider, getSysMemInfo common.GetSysMemInfoFn) *StorageControlService {
	topCfg := &storage.Config{
		Tiers: nil,
	}
	if len(ecs) > 0 {
		topCfg.ControlMetadata = ecs[0].Storage.ControlMetadata
	}
	instanceStorage := make(map[uint32]*storage.Config)
	for i, c := range ecs {
		instanceStorage[uint32(i)] = &c.Storage
	}

	if getSysMemInfo == nil {
		getSysMemInfo = func() (*common.SysMemInfo, error) {
			return control.MockSysMemInfo(), nil
		}
	}

	return &StorageControlService{
		log:             log,
		instanceStorage: instanceStorage,
		storage:         storage.MockProvider(log, 0, topCfg, sys, scm, bdev, nil),
		getSysMemInfo:   getSysMemInfo,
	}
}

type mockPublisher struct {
	published []*events.RASEvent
}

func (mp *mockPublisher) Publish(e *events.RASEvent) {
	mp.published = append(mp.published, e)
}

func newMockSubscriber(expCount int) *mockSubscriber {
	return &mockSubscriber{
		expectedRx: expCount,
		finished:   make(chan struct{}),
	}
}

type mockSubscriber struct {
	sync.Mutex
	finished   chan struct{}
	expectedRx int
	rx         []string
}

func (ms *mockSubscriber) OnEvent(_ context.Context, evt *events.RASEvent) {
	ms.Lock()
	defer ms.Unlock()

	evt.Timestamp = "" // Remove TS for event comparison in unittests
	ms.rx = append(ms.rx, evt.String())
	if len(ms.rx) == ms.expectedRx {
		close(ms.finished)
	}
}

func (ms *mockSubscriber) getRx() []string {
	ms.Lock()
	defer ms.Unlock()

	return ms.rx
}
