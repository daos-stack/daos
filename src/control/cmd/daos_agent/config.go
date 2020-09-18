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

package main

import (
	"fmt"
	"io/ioutil"

	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/security"
)

const (
	defaultConfigFile = "daos_agent.yml"
	defaultRuntimeDir = "/var/run/daos_agent"
	defaultLogFile    = "/tmp/daos_agent.log"
)

// Config defines the agent configuration.
type Config struct {
	SystemName      string                    `yaml:"name"`
	AccessPoints    []string                  `yaml:"access_points"`
	ControlPort     int                       `yaml:"port"`
	RuntimeDir      string                    `yaml:"runtime_dir"`
	LogFile         string                    `yaml:"log_file"`
	TransportConfig *security.TransportConfig `yaml:"transport_config"`
}

func LoadConfig(cfgPath string) (*Config, error) {
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

func DefaultConfig() *Config {
	localServer := fmt.Sprintf("localhost:%d", build.DefaultControlPort)
	return &Config{
		SystemName:      build.DefaultSystemName,
		ControlPort:     build.DefaultControlPort,
		AccessPoints:    []string{localServer},
		RuntimeDir:      defaultRuntimeDir,
		LogFile:         defaultLogFile,
		TransportConfig: security.DefaultAgentTransportConfig(),
	}
}
