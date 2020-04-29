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

package control

import (
	"fmt"
	"io/ioutil"
	"os"
	"path"

	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/security"
)

const (
	defaultConfigFile = "daos_control.yml"
)

// Config defines the parameters used to connect to a control API server.
type Config struct {
	SystemName      string                    `yaml:"name"`
	ControlPort     int                       `yaml:"port"`
	HostList        []string                  `yaml:"hostlist"`
	TransportConfig *security.TransportConfig `yaml:"transport_config"`
}

// DefaultConfig returns a Config populated with default values. Only
// suitable for single-node configurations.
func DefaultConfig() *Config {
	localServer := fmt.Sprintf("localhost:%d", build.DefaultControlPort)
	return &Config{
		SystemName:      build.DefaultSystemName,
		ControlPort:     build.DefaultControlPort,
		HostList:        []string{localServer},
		TransportConfig: security.DefaultClientTransportConfig(),
	}
}

// UserConfigPath returns the computed path to a per-user
// control configuration file, if it exists.
func UserConfigPath() string {
	// If we can't determine $HOME it's weird but not fatal.
	userHome, _ := os.UserHomeDir()
	return path.Join(userHome, "."+defaultConfigFile)
}

// SystemConfigPath returns the computed path to the system
// control configuration file, if it exists.
func SystemConfigPath() string {
	return path.Join(build.ConfigDir, defaultConfigFile)
}

// LoadConfig attempts to load a configuration by one of the following:
// 1. If the supplied path is a non-empty string, use it.
// Otherwise,
// 2. Try to load the config from the current user's home directory.
// 3. Finally, try to load the config from the system location.
func LoadConfig(cfgPath string) (*Config, error) {
	if cfgPath == "" {
		// Try to find either a per-user config file or use
		// the system config file.
		for _, cp := range []string{UserConfigPath(), SystemConfigPath()} {
			if _, err := os.Stat(cp); err == nil {
				cfgPath = cp
				break
			}
		}
	}

	data, err := ioutil.ReadFile(cfgPath)
	if err != nil {
		return nil, err
	}

	cfg := DefaultConfig()
	if err := yaml.UnmarshalStrict(data, cfg); err != nil {
		return nil, err
	}
	return cfg, nil
}
