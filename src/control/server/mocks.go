//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// MockMemInfo returns a mock MemInfo result.
func MockMemInfo() *common.MemInfo {
	return &common.MemInfo{
		HugepagesTotal:  1024,
		HugepagesFree:   512,
		HugepagesRsvd:   64,
		HugepagesSurp:   32,
		HugepageSizeKiB: 2048,
		MemTotalKiB:     (humanize.GiByte * 4) / humanize.KiByte,
		MemFreeKiB:      (humanize.GiByte * 1) / humanize.KiByte,
		MemAvailableKiB: (humanize.GiByte * 2) / humanize.KiByte,
	}
}

// NewMockStorageControlService returns a StorageControlService with a mocked storage provider
// consisting of the given sys, scm and bdev providers.
func NewMockStorageControlService(log logging.Logger, ecs []*engine.Config, sys storage.SystemProvider, scm storage.ScmProvider, bdev storage.BdevProvider, getMemInfo common.GetMemInfoFn) *StorageControlService {
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

	if getMemInfo == nil {
		getMemInfo = func() (*common.MemInfo, error) {
			return MockMemInfo(), nil
		}
	}

	return &StorageControlService{
		log:             log,
		instanceStorage: instanceStorage,
		storage:         storage.MockProvider(log, 0, topCfg, sys, scm, bdev, nil),
		getMemInfo:      getMemInfo,
	}
}
