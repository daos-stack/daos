//
// (C) Copyright 2020-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"fmt"
	"io"
	"os"
	"regexp"
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

// ConfigRegexp is a type alias for regexp.Regexp that implements the yaml.Unmarshaler interface.
type ConfigRegexp regexp.Regexp

// FromString attempts to compile the given string as a regular expression.
func (cr *ConfigRegexp) FromString(in string) error {
	if in == "" {
		return errors.New("empty regular expression")
	}

	re, err := regexp.Compile(in)
	if err != nil {
		return errors.Wrapf(err, "invalid regular expression: %s", in)
	}
	*cr = ConfigRegexp(*re)
	return nil
}

// UnmarshalYAML implements the yaml.Unmarshaler interface.
func (cr *ConfigRegexp) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var str string
	if err := unmarshal(&str); err != nil {
		return err
	}
	return cr.FromString(str)
}

func (cr *ConfigRegexp) String() string {
	return (*regexp.Regexp)(cr).String()
}

// MatchString provides a wrapper for the underlying Regexp.MatchString method.
func (cr *ConfigRegexp) MatchString(s string) bool {
	return (*regexp.Regexp)(cr).MatchString(s)
}

// TelemetryConfig defines the agent telemetry configuration.
type TelemetryConfig struct {
	Port       int           `yaml:"telemetry_port,omitempty"`
	Enabled    bool          `yaml:"telemetry_enabled,omitempty"`
	Retain     time.Duration `yaml:"telemetry_retain,omitempty"`
	RegPattern *ConfigRegexp `yaml:"telemetry_enabled_procs,omitempty"`
	IgnPattern *ConfigRegexp `yaml:"telemetry_disabled_procs,omitempty"`
}

// Validate performs basic validation of the telemetry configuration.
func (tc *TelemetryConfig) Validate() error {
	if tc == nil {
		return errors.New("telemetry config is nil")
	}

	if tc.Retain > 0 && !tc.Enabled {
		return errors.New("telemetry_retain requires telemetry_enabled: true")
	}

	if tc.Enabled && tc.Port == 0 {
		return errors.New("telemetry_enabled requires telemetry_port")
	}

	if tc.RegPattern != nil {
		if !tc.Enabled {
			return errors.New("cannot specify telemetry_enabled_procs without telemetry_enabled")
		}
	}
	if tc.IgnPattern != nil {
		if !tc.Enabled {
			return errors.New("cannot specify telemetry_disabled_procs without telemetry_enabled")
		}
	}
	if tc.IgnPattern != nil && tc.RegPattern != nil {
		return errors.New("cannot specify both telemetry_enabled_procs and telemetry_disabled_procs")
	}

	return nil
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
	EvictOnStart        bool                       `yaml:"enable_evict_on_start,omitempty"`
	ExcludeFabricIfaces common.StringSet           `yaml:"exclude_fabric_ifaces,omitempty"`
	IncludeFabricIfaces common.StringSet           `yaml:"include_fabric_ifaces,omitempty"`
	FabricInterfaces    []*NUMAFabricConfig        `yaml:"fabric_ifaces,omitempty"`
	ProviderIdx         uint                       // TODO SRS-31: Enable with multiprovider functionality
	Telemetry           TelemetryConfig            `yaml:",inline"`
}

// Validate performs basic validation of the configuration.
func (c *Config) Validate() error {
	if c == nil {
		return errors.New("config is nil")
	}

	if !daos.SystemNameIsValid(c.SystemName) {
		return fmt.Errorf("invalid system name: %s", c.SystemName)
	}

	if len(c.ExcludeFabricIfaces) > 0 && len(c.IncludeFabricIfaces) > 0 {
		return errors.New("cannot specify both exclude_fabric_ifaces and include_fabric_ifaces")
	}

	if err := c.Telemetry.Validate(); err != nil {
		return err
	}

	return nil
}

// TelemetryExportEnabled returns true if client telemetry export is enabled.
func (c *Config) TelemetryExportEnabled() bool {
	return c.Telemetry.Port > 0
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

// ReadConfig reads a config from an io.Reader and uses it to populate a Config.
func ReadConfig(cfgReader io.Reader) (*Config, error) {
	data, err := io.ReadAll(cfgReader)
	if err != nil {
		return nil, errors.Wrap(err, "reading config")
	}

	cfg := DefaultConfig()
	if err := yaml.UnmarshalStrict(data, cfg); err != nil {
		return nil, errors.Wrap(err, "parsing config")
	}

	if err := cfg.Validate(); err != nil {
		return nil, errors.Wrap(err, "agent config validation failed")
	}

	return cfg, nil

}

// LoadConfig reads a config file and uses it to populate a Config.
func LoadConfig(cfgPath string) (*Config, error) {
	if cfgPath == "" {
		return nil, errors.New("no config path supplied")
	}
	cfgFile, err := os.Open(cfgPath)
	if err != nil {
		return nil, errors.Wrapf(err, "opening config file %q", cfgPath)
	}
	defer cfgFile.Close()

	cfg, err := ReadConfig(cfgFile)
	if err != nil {
		return nil, errors.Wrapf(err, "reading config file %q", cfgPath)
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
		LogLevel:         common.DefaultControlLogLevel,
		TransportConfig:  security.DefaultAgentTransportConfig(),
		CredentialConfig: &security.CredentialConfig{},
	}
}
