//
// (C) Copyright 2019 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
)

// NetworkScanService encapsulates the network part of the control service
type NetworkScanService struct {
	nsslog          logging.Logger
	srvCfgs         []*ioserver.Config
}

// DefaultNetworkScanService returns a initialized *NetworkScanService
// with default behaviour
func DefaultNetworkScanService(log logging.Logger, cfg *Configuration) (*NetworkScanService, error) {
	return NewNetworkScanService(log, cfg.Servers), nil
}

// NewStorageControlService returns an initialized *StorageControlService
func NewNetworkScanService(log logging.Logger, srvCfgs []*ioserver.Config) *NetworkScanService {
	return &NetworkScanService{
		nsslog:          log,
		srvCfgs:         srvCfgs,
	}
}
