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
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
	"gopkg.in/yaml.v2"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
)

const (
	providerEnvKey       = "CRT_PHY_ADDR_STR"
	ofiInterfaceEnv      = "OFI_INTERFACE"
	ofiPortEnv           = "OFI_PORT"
	daosAgentDrpcSockEnv = "DAOS_AGENT_DRPC_SOCK"
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

// Configuration contains all known configuration variables available to the client
type Configuration struct {
	SystemName      string   `yaml:"name"`
	SocketDir       string   `yaml:"socket_dir"`
	AccessPoints    []string `yaml:"access_points"`
	Port            int      `yaml:"port"`
	CaCert          string   `yaml:"ca_cert"`
	Cert            string   `yaml:"cert"`
	Key             string   `yaml:"key"`
	Provider        string   `yaml:"provider"`
	FabricIface     string   `yaml:"fabric_iface"`
	FabricIfacePort int      `yaml:"fabric_iface_port"`
	LogFile         string   `yaml:"log_file"`
	Path            string
	ext             External
}

// newDefaultConfiguration creates a new instance of configuration struct
// populated with defaults.
func newDefaultConfiguration(ext External) Configuration {
	return Configuration{
		SystemName:      "daos",
		SocketDir:       "/var/run/daos_agent",
		AccessPoints:    []string{"localhost"},
		Port:            10000,
		CaCert:          "./.daos/ca.crt",
		Cert:            "./.daos/daos.crt",
		Key:             "./.daos/daos.key",
		Provider:        "ofi+socket",
		FabricIface:     "qib0",
		FabricIfacePort: 20000,
		LogFile:         "/tmp/daos_agent.log",
		Path:            "etc/daos.yml",
		ext:             ext,
	}
}

// ProcessConfigFile loads a configuration file from the path given,
// or from the default location if none is provided.  It returns a populated
// Configuration struct based upon the default values and any config file overrides.
func ProcessConfigFile(ConfigPath string) (Configuration, error) {
	config := NewConfiguration()
	if ConfigPath != "" {
		log.Debugf("Overriding default config path with: %s", ConfigPath)
		config.Path = ConfigPath
	}

	if !filepath.IsAbs(config.Path) {
		newPath, err := common.GetAbsInstallPath(config.Path)
		if err != nil {
			return config, err
		}
		config.Path = newPath
	}

	err := config.LoadConfig()
	if err != nil {
		return config, errors.Wrap(err, "failed to read config file")
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

// ApplyCmdLineOverrides will overwrite Configuration values with any non empty
// data provided, usually from the commandline.
func (c *Configuration) ApplyCmdLineOverrides(SocketDir string, LogFile string) error {

	if SocketDir != "" {
		log.Debugf("Overriding socket path from config file with %s", SocketDir)
		c.SocketDir = SocketDir
	}

	if LogFile != "" {
		log.Debugf("Overriding LogFile path from config file with %s", LogFile)
		c.LogFile = LogFile
	}

	return nil
}

// ProcessEnvOverrides examines environment variables and overrides the client
// configuration as appropriate.
// To enable environment variable overrides, the 'providerEnvKey' environment
// variable must be set.  If it is set, then the other known environment variables
// are examined.  If overrides are enabled and a known environment variable is found,
// the corresponding Configuration variable is overwritten.
func (c *Configuration) ProcessEnvOverrides() int {
	var envVarsFound = 0
	if c.ext.getenv(providerEnvKey) != "" {
		log.Debugf("Provider key found .. reading environment vars")
		ofiInterface := c.ext.getenv(ofiInterfaceEnv)
		if ofiInterface != "" {
			log.Debugf("OFI_INTERFACE found: %s", ofiInterface)
			c.FabricIface = ofiInterface
			envVarsFound++
		}
		ofiPort := c.ext.getenv(ofiPortEnv)
		if ofiPort != "" {
			log.Debugf("OFI_PORT found: %s", ofiPort)
			port, err := strconv.Atoi(ofiPort)
			if err != nil {
				log.Debugf("Error while converting OFI_PORT to integer.  Not using OFI_PORT")
			}
			if err == nil {
				c.FabricIfacePort = port
				envVarsFound++
			}
		}
		daosAgentDrpcSock := c.ext.getenv(daosAgentDrpcSockEnv)
		if daosAgentDrpcSock != "" {
			log.Debugf("DAOS_AGENT_DRPC_SOCK found: %s", daosAgentDrpcSock)
			c.SocketDir = daosAgentDrpcSock
			envVarsFound++
		}
	}

	return envVarsFound
}

// decodes YAML representation of Configuration struct
func (c *Configuration) parse(data []byte) error {
	return yaml.Unmarshal(data, c)
}

// NewConfiguration creates a new instance of the Configuration struct
// populated with defaults and default external interface.
func NewConfiguration() Configuration {
	return newDefaultConfiguration(&ext{})
}
