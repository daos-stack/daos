//
// (C) Copyright 2018-2021 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
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
