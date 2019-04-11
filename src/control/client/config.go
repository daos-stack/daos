//
// (C) Copyright 2018-2019 Intel Corporation.
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

package client

import (
	"io/ioutil"
	"strconv"
	"github.com/daos-stack/daos/src/control/log"
)

const (
	providerEnvKey = "CRT_PHY_ADDR_STR"
	ofi_interface_env = "OFI_INTERFACE"
	ofi_port_env = "OFI_PORT"
	daos_agent_drpc_sock_env = "DAOS_AGENT_DRPC_SOCK"
)

func (c *ClientConfiguration) LoadConfig() error {
	bytes, err := ioutil.ReadFile(c.Path)
	if err != nil {
		return err
	}
	if err = c.parse(bytes); err != nil {
		return err
	}

	return nil
}

func ProcessEnvOverrides(c *ClientConfiguration) int {
	var env_vars_found = 0
	if c.ext.getenv(providerEnvKey) != "" {
		log.Debugf("Provider key found .. reading environment vars")
		ofi_interface := c.ext.getenv(ofi_interface_env)
		if ofi_interface != "" {
			log.Debugf("OFI_INTERFACE found: %s", ofi_interface)
			c.FabricIface = ofi_interface
			env_vars_found ++
		}
		ofi_port := c.ext.getenv(ofi_port_env)
		if ofi_port != "" {
			log.Debugf("OFI_PORT found: %s", ofi_port)
			port, err := strconv.Atoi(ofi_port)
			if err != nil {
				log.Debugf("Error while converting OFI_PORT to integer.  Not using OFI_PORT")
			}
			if err == nil {
				c.FabricIfacePort = port
				env_vars_found ++
			}
		}
		daos_agent_drpc_sock := c.ext.getenv(daos_agent_drpc_sock_env)
		if daos_agent_drpc_sock != "" {
			log.Debugf("DAOS_AGENT_DRPC_SOCK found: %s", daos_agent_drpc_sock)
			c.DAOS_AGENT_DRPC_SOCK = daos_agent_drpc_sock
			env_vars_found ++
		}
	}
	return env_vars_found
}

