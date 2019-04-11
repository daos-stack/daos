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
	"os"
	"os/exec"

	"gopkg.in/yaml.v2"
)

// External interface provides methods to support various os operations.
type External interface {
	getenv(string) string
}

type ext struct{}

// runCommand executes command in subshell (to allow redirection) and returns
// error result.
func (e *ext) runCommand(cmd string) error {
	return exec.Command("sh", "-c", cmd).Run()
}

// getEnv wraps around os.GetEnv and implements External.getEnv().
func (e *ext) getenv(key string) string {
	return os.Getenv(key)
}

type ClientConfiguration struct {
	SystemName      string          `yaml:"name"`
	SocketDir       string          `yaml:"socket_dir"`
	AccessPoints    []string        `yaml:"access_points"`
	Port            int             `yaml:"port"`
	CaCert          string          `yaml:"ca_cert"`
	Cert            string          `yaml:"cert"`
	Key             string          `yaml:"key"`
	Provider        string          `yaml:"provider"`
	FabricIface     string          `yaml:"fabric_iface"`
	FabricIfacePort int             `yaml:"fabric_iface_port"`
	DAOS_AGENT_DRPC_SOCK string
	Path            string
	ext             External
}

// parse decodes YAML representation of configure struct and checks for Group
func (c *ClientConfiguration) parse(data []byte) error {
	return yaml.Unmarshal(data, c)
}

// newDefaultConfiguration creates a new instance of configuration struct
// populated with defaults.
func newDefaultConfiguration(ext External) ClientConfiguration {
	return ClientConfiguration{
		SystemName:     "daos",
		SocketDir:      "/var/run/daos_agent",
		AccessPoints:   []string{"localhost"},
		Port:           10000,
	        CaCert:         "./.daos/ca.crt",
	        Cert:           "./.daos/daos.crt",
		Key:            "./.daos/daos.key",
		Provider:	"ofi+socket",
		FabricIface:	"qib0",
		FabricIfacePort: 20000,
		Path:           "etc/daos.yml",
		DAOS_AGENT_DRPC_SOCK: "",
		ext:            ext,
	}
}

// newConfiguration creates a new instance of configuration struct
// populated with defaults and default external interface.
func NewConfiguration() ClientConfiguration {
	return newDefaultConfiguration(&ext{})
}
