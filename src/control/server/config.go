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

package server

import (
	"hash/fnv"
	"io/ioutil"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/pkg/errors"
	yaml "gopkg.in/yaml.v2"

	"github.com/daos-stack/daos/src/control/common"
	log "github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

const (
	configOut              = ".daos_server.active.yml"
	relConfExamplesPath    = "utils/config/examples/"
	msgBadConfig           = "insufficient config file, see examples in "
	msgConfigNoProvider    = "provider not specified in config"
	msgConfigNoPath        = "no config path set"
	msgConfigNoServers     = "no servers specified in config"
	msgConfigServerNoIface = "fabric interface not specified in config"
)

func (c *Configuration) Load() error {
	if c.Path == "" {
		return errors.New(msgConfigNoPath)
	}

	bytes, err := ioutil.ReadFile(c.Path)
	if err != nil {
		return errors.WithMessage(err, "reading file")
	}

	if err = c.parse(bytes); err != nil {
		return errors.WithMessage(err, "parse failed; config contains invalid "+
			"parameters and may be out of date, see server config examples")
	}

	return nil
}

func (c *Configuration) saveConfig(filename string) error {
	bytes, err := yaml.Marshal(c)

	if err != nil {
		return err
	}

	return ioutil.WriteFile(filename, bytes, 0644)
}

func (c *Configuration) SetNvmeShmID(base string) {
	c.NvmeShmID = hash(base + strconv.Itoa(os.Getpid()))
}

func (c *Configuration) SetPath(path string) error {
	if path != "" {
		c.Path = path
	}

	if !filepath.IsAbs(c.Path) {
		newPath, err := c.ext.getAbsInstallPath(c.Path)
		if err != nil {
			return err
		}
		c.Path = newPath
	}

	return nil
}

// saveActiveConfig saves read-only active config, tries config dir then /tmp/
func saveActiveConfig(config *Configuration) {
	activeConfig := filepath.Join(filepath.Dir(config.Path), configOut)
	eMsg := "Warning: active config could not be saved (%s)"
	err := config.saveConfig(activeConfig)
	if err != nil {
		log.Debugf(eMsg, err)

		activeConfig = filepath.Join("/tmp", configOut)
		err = config.saveConfig(activeConfig)
		if err != nil {
			log.Debugf(eMsg, err)
		}
	}
	if err == nil {
		log.Debugf("Active config saved to %s (read-only)", activeConfig)
	}
}

// hash produces unique int from string, mask MSB on conversion to signed int
func hash(s string) int {
	h := fnv.New32a()
	if _, err := h.Write([]byte(s)); err != nil {
		panic(err) // should never happen
	}

	return int(h.Sum32() & 0x7FFFFFFF) // mask MSB of uint32 as this will be sign bit
}

func (srv *IOServerConfig) SetFlags(cfg *Configuration) {
	srv.CliOpts = append(
		srv.CliOpts,
		"-t", strconv.Itoa(srv.Targets),
		"-g", cfg.SystemName,
		"-s", srv.ScmMount,
		"-p", strconv.Itoa(srv.PinnedNumaNode))

	if cfg.Modules != "" {
		srv.CliOpts = append(srv.CliOpts, "-m", cfg.Modules)
	}
	if cfg.Attach != "" {
		srv.CliOpts = append(srv.CliOpts, "-a", cfg.Attach)
	}
	if srv.NrXsHelpers > 2 {
		log.Errorf(
			"invalid NrXsHelpers %d exceed [0, 2], "+
				"using default value of 2", srv.NrXsHelpers)
		srv.NrXsHelpers = 2
	} else if srv.NrXsHelpers != 2 {
		srv.CliOpts = append(
			srv.CliOpts, "-x", strconv.Itoa(srv.NrXsHelpers))
	}
	if srv.FirstCore > 0 {
		srv.CliOpts = append(
			srv.CliOpts, "-f", strconv.Itoa(srv.FirstCore))
	}
	if cfg.SocketDir != "" {
		srv.CliOpts = append(srv.CliOpts, "-d", cfg.SocketDir)
	}
	if cfg.NvmeShmID > 0 {
		// Add shm_id so I/O service can share spdk access to controllers
		// with mgmtControlServer process. Currently not user
		// configurable when starting daos_server, use default.
		srv.CliOpts = append(
			srv.CliOpts, "-i", strconv.Itoa(cfg.NvmeShmID))
	}

	srv.EnvVars = append(
		srv.EnvVars,
		"CRT_PHY_ADDR_STR="+cfg.Provider,
		"OFI_INTERFACE="+srv.FabricIface,
		"D_LOG_MASK="+srv.LogMask,
		"D_LOG_FILE="+srv.LogFile)

	// populate only if non-zero
	if srv.FabricIfacePort != 0 {
		srv.EnvVars = append(
			srv.EnvVars,
			"OFI_PORT="+strconv.Itoa(srv.FabricIfacePort))
	}
}

func (c *Configuration) SetIOServerFlags() error {
	if err := c.Validate(); err != nil {
		examplesPath, _ := c.ext.getAbsInstallPath(relConfExamplesPath)

		return errors.WithMessagef(err, msgBadConfig+examplesPath)
	}

	for _, srv := range c.Servers {
		srv.SetFlags(c)
	}
	return nil
}

// Validate asserts that config meets minimum requirements
func (c *Configuration) Validate() error {
	if c.Provider == "" {
		return errors.New(msgConfigNoProvider)
	}

	if len(c.Servers) == 0 {
		return errors.New(msgConfigNoServers)
	}

	for i, srv := range c.Servers {
		if srv.FabricIface == "" {
			return errors.Errorf(
				msgConfigServerNoIface+" for I/O service %d", i)
		}

		validConfig, err := netdetect.ValidateNetworkConfig(c.Provider, srv.FabricIface, uint(srv.PinnedNumaNode))
		if err != nil {
			return errors.Errorf("Unable to validate the network configuration for provider: %s, with device: %s on NUMA node %d.  Error: %v", c.Provider, srv.FabricIface, srv.PinnedNumaNode, err)
		}

		if !validConfig {
			return errors.Errorf("Network device configuration for Provider: %s, with device: %s on NUMA node %d is invalid.", c.Provider, srv.FabricIface, srv.PinnedNumaNode)
		}

		log.Debugf("Network device configuration for Provider: %s, with device: %s on NUMA node %d is valid.", c.Provider, srv.FabricIface, srv.PinnedNumaNode)
	}

	return nil
}

// populateEnv adds envs from config options to existing envs from user's shell
// overwriting any existing values for given key
func (c *Configuration) populateEnv(i int, envs *[]string) {
	for _, newEnv := range c.Servers[i].EnvVars {
		key := strings.Split(newEnv, "=")[0]

		// filter out any matching keys in envs then adds new value
		*envs = common.Filter(
			*envs,
			func(s string) bool {
				return key != strings.Split(s, "=")[0]
			})
		*envs = append(*envs, newEnv)
	}
}
