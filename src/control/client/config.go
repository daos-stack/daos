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
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"

	"github.com/pkg/errors"
	yaml "gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

const (
	defaultRuntimeDir = "/var/run/daos_agent"
	defaultLogFile    = "/tmp/daos_agent.log"
	defaultConfigPath = "etc/daos.yml"
	defaultSystemName = "daos_server"
	defaultPort       = 10000
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
	Port            int      `yaml:"port"`
	HostList        []string `yaml:"hostlist"`
	RuntimeDir      string   `yaml:"runtime_dir"`
	HostFile        string   `yaml:"host_file"`
	LogFile         string   `yaml:"log_file"`
	LogFileFormat   string   `yaml:"log_file_format"`
	Path            string
	TransportConfig *security.TransportConfig `yaml:"transport_config"`
	Ext             External
}

// newDefaultConfiguration creates a new instance of configuration struct
// populated with defaults.
func newDefaultConfiguration(ext External) *Configuration {
	return &Configuration{
		SystemName:      defaultSystemName,
		AccessPoints:    []string{"localhost"},
		Port:            defaultPort,
		HostList:        []string{"localhost:10001"},
		RuntimeDir:      defaultRuntimeDir,
		LogFile:         defaultLogFile,
		Path:            defaultConfigPath,
		TransportConfig: security.DefaultClientTransportConfig(),
		Ext:             ext,
	}
}

// GetConfig loads a configuration file from the path given,
// or from the default location if none is provided.  It returns a populated
// Configuration struct based upon the default values and any config file overrides.
func GetConfig(log logging.Logger, ConfigPath string) (*Configuration, error) {
	config := NewConfiguration()
	if ConfigPath != "" {
		log.Debugf("Overriding default config path with: %s", ConfigPath)
		config.Path = ConfigPath
	}

	if !filepath.IsAbs(config.Path) {
		newPath, err := common.GetAbsInstallPath(config.Path)
		if err != nil {
			return nil, errors.Wrap(err, "resolving install path")
		}

		config.Path = newPath
	}

	_, err := os.Stat(config.Path)
	if err != nil {
		if os.IsNotExist(err) && ConfigPath == "" {
			log.Debugf("No configuration file found; using default values")
			return config, nil
		}
		return nil, errors.Wrapf(err, "failed to stat config file %s", config.Path)
	}

	if err := config.LoadConfig(); err != nil {
		return nil, errors.Wrapf(err, "parsing config file %s", config.Path)
	}
	log.Debugf("DAOS Client config read from %s", config.Path)

	return config, nil
}

// LoadConfig reads the configuration file specified by Configuration.Path
// and parses it.  Parsed values override any default values.
func (c *Configuration) LoadConfig() error {
	bytes, err := ioutil.ReadFile(c.Path)
	if err != nil {
		return err
	}

	if err = c.parse(bytes); err != nil {
		return err
	}

	return nil
}

// decodes YAML representation of Configuration struct
func (c *Configuration) parse(data []byte) error {
	return yaml.Unmarshal(data, c)
}

// NewConfiguration creates a new instance of the Configuration struct
// populated with defaults and default external interface.
func NewConfiguration() *Configuration {
	return newDefaultConfiguration(&ext{})
}
