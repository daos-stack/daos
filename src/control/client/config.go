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

package client

import (
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"strconv"

	"github.com/pkg/errors"
	yaml "gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

const (
	defaultRuntimeDir      = "/var/run/daos_agent"
	defaultLogFile         = "/tmp/daos_agent.log"
	defaultAgentConfigPath = "../etc/daos_agent.yml"
	defaultAdminConfigPath = "../etc/daos.yml"
	defaultSystemName      = "daos_server"
	defaultControlPort     = 10001
)

// External interface provides methods to support various os operations.
type External interface {
	Getenv(string) string
	RunCommand(string) error
}

type ext struct{}

// runCommand executes command in subshell (to allow redirection) and returns
// error result.
func (e *ext) RunCommand(cmd string) error {
	return exec.Command("sh", "-c", cmd).Run()
}

// getEnv wraps around os.GetEnv and implements External.getEnv().
func (e *ext) Getenv(key string) string {
	return os.Getenv(key)
}

// Configuration contains all known configuration variables available to the client
type Configuration struct {
	SystemName      string   `yaml:"name"`
	AccessPoints    []string `yaml:"access_points"`
	ControlPort     int      `yaml:"port"`
	HostList        []string `yaml:"hostlist"`
	RuntimeDir      string   `yaml:"runtime_dir"`
	HostFile        string   `yaml:"host_file"`
	LogFile         string   `yaml:"log_file"`
	LogFileFormat   string   `yaml:"log_file_format"`
	Path            string
	TransportConfig *security.TransportConfig `yaml:"transport_config"`
	Ext             External
}

// newDefaultAgentConfiguration creates a new instance of configuration struct
// populated with defaults.
func newDefaultAgentConfiguration(ext External) *Configuration {
	return &Configuration{
		SystemName:      defaultSystemName,
		AccessPoints:    []string{fmt.Sprintf("localhost:%d", defaultControlPort)},
		ControlPort:     defaultControlPort,
		HostList:        []string{fmt.Sprintf("localhost:%d", defaultControlPort)},
		RuntimeDir:      defaultRuntimeDir,
		LogFile:         defaultLogFile,
		Path:            defaultAgentConfigPath,
		TransportConfig: security.DefaultAgentTransportConfig(),
		Ext:             ext,
	}
}

// newDefaultAdminConfiguration creates a new instance of configuration struct
// populated with defaults.
func newDefaultAdminConfiguration(ext External) *Configuration {
	return &Configuration{
		SystemName:      defaultSystemName,
		AccessPoints:    []string{fmt.Sprintf("localhost:%d", defaultControlPort)},
		ControlPort:     defaultControlPort,
		HostList:        []string{fmt.Sprintf("localhost:%d", defaultControlPort)},
		RuntimeDir:      defaultRuntimeDir,
		LogFile:         defaultLogFile,
		Path:            defaultAdminConfigPath,
		TransportConfig: security.DefaultAdminTransportConfig(),
		Ext:             ext,
	}
}

// GetAdminConfig loads a configuration file from the path given,
// or from the default location if none is provided.  It returns a populated
// Configuration struct based upon the default values and any config file overrides.
func GetAdminConfig(log logging.Logger, inPath string) (*Configuration, error) {
	c := NewAdminConfiguration()
	if err := getConfig(log, inPath, c); err != nil {
		return nil, errors.Wrapf(err, "Unable to load Admin configuration")
	}
	return c, nil
}

// GetAgentConfig loads a configuration file from the path given,
// or from the default location if none is provided.  It returns a populated
// Configuration struct based upon the default values and any config file overrides.
func GetAgentConfig(log logging.Logger, inPath string) (*Configuration, error) {
	c := NewAgentConfiguration()
	if err := getConfig(log, inPath, c); err != nil {
		return nil, errors.Wrapf(err, "Unable to load Agent configuration")
	}
	return c, nil
}

func getConfig(log logging.Logger, inPath string, c *Configuration) error {
	if err := c.SetPath(inPath); err != nil {
		return err
	}

	if _, err := os.Stat(c.Path); err != nil {
		if inPath == "" && os.IsNotExist(err) {
			log.Debugf("No configuration file found; using default values")
			c.Path = ""
			return nil
		}
		return err
	}

	if err := c.Load(); err != nil {
		return errors.Wrapf(err, "parsing config file %s", c.Path)
	}
	log.Debugf("DAOS Client config read from %s", c.Path)

	if err := c.Validate(log); err != nil {
		return errors.Wrapf(err, "validating config file %s", c.Path)
	}

	return nil
}

// SetPath validates and stores the given string as the path in the config file.
func (c *Configuration) SetPath(inPath string) error {
	newPath, err := common.ResolvePath(inPath, c.Path)
	if err != nil {
		return err
	}
	c.Path = newPath

	return nil
}

// Load reads the configuration file specified by Configuration.Path
// and parses it.  Parsed values override any default values.
func (c *Configuration) Load() error {
	bytes, err := ioutil.ReadFile(c.Path)
	if err != nil {
		return err
	}

	if err = c.parse(bytes); err != nil {
		return err
	}

	return nil
}

// Validate asserts that config meets minimum requirements.
func (c *Configuration) Validate(log logging.Logger) (err error) {
	// only single access point valid for now
	if len(c.AccessPoints) > 1 {
		return FaultConfigBadAccessPoints
	}
	// apply configured control port if not supplied
	for i := range c.AccessPoints {
		// apply configured control port if not supplied
		host, port, err := common.SplitPort(c.AccessPoints[i], c.ControlPort)
		if err != nil {
			return errors.Wrap(FaultConfigBadAccessPoints, err.Error())
		}

		// warn if access point port differs from config control port
		if strconv.Itoa(c.ControlPort) != port {
			log.Debugf("access point (%s) port (%s) differs from control port (%d)",
				host, port, c.ControlPort)
		}

		if port == "0" {
			return FaultConfigBadControlPort
		}

		c.AccessPoints[i] = fmt.Sprintf("%s:%s", host, port)
	}

	return nil
}

// decodes YAML representation of Configuration struct
func (c *Configuration) parse(data []byte) error {
	return yaml.Unmarshal(data, c)
}

// NewAgentConfiguration creates a new instance of the Configuration struct
// populated with defaults and default external interface.
func NewAgentConfiguration() *Configuration {
	return newDefaultAgentConfiguration(&ext{})
}

// NewAdminConfiguration creates a new instance of the Configuration struct
// populated with defaults and default external interface.
func NewAdminConfiguration() *Configuration {
	return newDefaultAdminConfiguration(&ext{})
}
