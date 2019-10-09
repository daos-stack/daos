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
//	"os"
//	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/netdetect"
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

// An example how I might call into netdetect.ScanFabric to get the results
func (c *NetworkScanService) TestFunction() error {
	c.nsslog.Debugf("NetworkScanService::TestFunction was called\n")
	results, err := netdetect.ScanFabric("ofi+sockets")
	if err != nil {
		//return errors.WithMessage(err, "failed to execute the fabric and device scan")
		return err
	}
	for _, sr := range results {
		// fill in the reply message and stream the results back
		c.nsslog.Infof("\n%v\n\n", sr)
	}
	return nil
}