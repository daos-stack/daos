//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// ControlService implements the control plane control service, satisfying
// ctlpb.CtlSvcServer, and is the data container for the service.
type ControlService struct {
	StorageControlService
	harness *IOServerHarness
	srvCfg  *config.Server
	events  *events.PubSub
}

// NewControlService returns ControlService to be used as gRPC control service
// datastore. Initialized with sensible defaults and provided components.
func NewControlService(log logging.Logger, h *IOServerHarness,
	bp *bdev.Provider, sp *scm.Provider,
	cfg *config.Server, e *events.PubSub) *ControlService {

	scs := NewStorageControlService(log, bp, sp, cfg.Servers)

	return &ControlService{
		StorageControlService: *scs,
		harness:               h,
		srvCfg:                cfg,
		events:                e,
	}
}
