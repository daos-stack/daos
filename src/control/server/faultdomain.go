//
// (C) Copyright 2020 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/system"
)

type hostnameGetterFn func() (string, error)

// getDefaultFaultDomain determines the fault domain that should be used for this
// server if none is externally defined.
func getDefaultFaultDomain(getHostname hostnameGetterFn) (*system.FaultDomain, error) {
	hostname, err := getHostname()
	if err != nil {
		return nil, err
	}
	return newFaultDomainFromConfig(system.FaultDomainSeparator + hostname)
}

// getFaultDomain determines the fault domain for the system.
func getFaultDomain(cfg *Configuration) (*system.FaultDomain, error) {
	if cfg == nil {
		return nil, FaultBadConfig
	}

	if cfg.FaultPath != "" {
		return newFaultDomainFromConfig(cfg.FaultPath)
	}

	// TODO DAOS-4449: Try to get fault domain from callback

	return getDefaultFaultDomain(cfg.ext.getHostname)
}

func newFaultDomainFromConfig(domainStr string) (*system.FaultDomain, error) {
	fd, err := system.NewFaultDomainFromString(domainStr)
	if err != nil {
		return nil, FaultConfigFaultDomainInvalid
	}
	return fd, nil
}
