//
// (C) Copyright 2018-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

// ControlService implements the control plane control service, satisfying
// ctlpb.CtlSvcServer, and is the data container for the service.
type ControlService struct {
	ctlpb.UnimplementedCtlSvcServer
	StorageControlService
	harness *EngineHarness
	srvCfg  *config.Server
	events  *events.PubSub
	fabric  *hardware.FabricScanner
}

// NewControlService returns ControlService to be used as gRPC control service
// datastore. Initialized with sensible defaults and provided components.
func NewControlService(log logging.Logger, h *EngineHarness,
	cfg *config.Server, e *events.PubSub, f *hardware.FabricScanner) *ControlService {

	scs := NewStorageControlService(log, cfg.Engines)

	return &ControlService{
		StorageControlService: *scs,
		harness:               h,
		srvCfg:                cfg,
		events:                e,
		fabric:                f,
	}
}
