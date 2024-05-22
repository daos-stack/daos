//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"io/ioutil"
	"time"

	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/security"
)

const (
	defaultConfigFile = "daos_agent.yml"
	defaultRuntimeDir = "/var/run/daos_agent"
	defaultLogFile    = "/tmp/daos_agent.log"
)

type refreshMinutes time.Duration

func (rm *refreshMinutes) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var mins uint
	if err := unmarshal(&mins); err != nil {
		return err
	}
	*rm = refreshMinutes(time.Duration(mins) * time.Minute)
	return nil
}

func (rm refreshMinutes) Duration() time.Duration {
	return time.Duration(rm)
}

// Config defines the agent configuration.
type Config struct {
	SystemName          string                     `yaml:"name"`
	AccessPoints        []string                   `yaml:"access_points"`
	ControlPort         int                        `yaml:"port"`
	RuntimeDir          string                     `yaml:"runtime_dir"`
	LogFile             string                     `yaml:"log_file"`
	LogLevel            common.ControlLogLevel     `yaml:"control_log_mask,omitempty"`
	CredentialConfig    *security.CredentialConfig `yaml:"credential_config"`
	TransportConfig     *security.TransportConfig  `yaml:"transport_config"`
	DisableCache        bool                       `yaml:"disable_caching,omitempty"`
	CacheExpiration     refreshMinutes             `yaml:"cache_expiration,omitempty"`
	DisableAutoEvict    bool                       `yaml:"disable_auto_evict,omitempty"`
	ExcludeFabricIfaces common.StringSet           `yaml:"exclude_fabric_ifaces,omitempty"`
	FabricInterfaces    []*NUMAFabricConfig        `yaml:"fabric_ifaces,omitempty"`
	TelemetryPort       int                        `yaml:"telemetry_port,omitempty"`
	TelemetryEnabled    bool                       `yaml:"telemetry_enabled,omitempty"`
	TelemetryRetain     time.Duration              `yaml:"telemetry_retain,omitempty"`
}

// TelemetryExportEnabled returns true if client telemetry export is enabled.
func (c *Config) TelemetryExportEnabled() bool {
	return c.TelemetryPort > 0
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

	if !daos.SystemNameIsValid(cfg.SystemName) {
		return nil, fmt.Errorf("invalid system name: %q", cfg.SystemName)
	}

	if cfg.TelemetryRetain > 0 && cfg.TelemetryPort == 0 {
		return nil, errors.New("telemetry_retain requires telemetry_port")
	}

	if cfg.TelemetryEnabled && cfg.TelemetryPort == 0 {
		return nil, errors.New("telemetry_enabled requires telemetry_port")
	}

	return cfg, nil
}

// DefaultConfig creates a basic default configuration.
func DefaultConfig() *Config {
	localServer := fmt.Sprintf("localhost:%d", build.DefaultControlPort)
	return &Config{
		SystemName:       build.DefaultSystemName,
		ControlPort:      build.DefaultControlPort,
		AccessPoints:     []string{localServer},
		RuntimeDir:       defaultRuntimeDir,
		LogFile:          defaultLogFile,
		LogLevel:         common.DefaultControlLogLevel,
		TransportConfig:  security.DefaultAgentTransportConfig(),
		CredentialConfig: &security.CredentialConfig{},
	}
}
