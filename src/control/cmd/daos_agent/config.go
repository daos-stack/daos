//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"io/ioutil"

	"github.com/pkg/errors"
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
	SystemName       string                    `yaml:"name"`
	AccessPoints     []string                  `yaml:"access_points"`
	ControlPort      int                       `yaml:"port"`
	RuntimeDir       string                    `yaml:"runtime_dir"`
	LogFile          string                    `yaml:"log_file"`
	TransportConfig  *security.TransportConfig `yaml:"transport_config"`
	FabricInterfaces []*NUMAFabricConfig       `yaml:"fabric_ifaces,omitempty"`
}

// NUMAFabricConfig defines a list of fabric interfaces that belong to a NUMA
// node.
type NUMAFabricConfig struct {
	NUMANode   int                      `yaml:"numa_node"`
	Interfaces []*FabricInterfaceConfig `yaml:"devices"`
}

// FabricInterfaceConfig defines a specific fabric interface device.
type FabricInterfaceConfig struct {
	Interface string `yaml:"iface"`
	Domain    string `yaml:"domain"`
}

// LoadConfig reads a config file and uses it to populate a Config.
func LoadConfig(cfgPath string) (*Config, error) {
	if cfgPath == "" {
		return nil, errors.New("no path supplied")
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

// DefaultConfig creates a basic default configuration.
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
