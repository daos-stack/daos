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

package main

import (
	"fmt"
	"hash/fnv"
	"io/ioutil"
	"os"
	"path/filepath"
	"strconv"
	"strings"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
	yaml "gopkg.in/yaml.v2"
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

func (c *configuration) loadConfig() error {
	if c.Path == "" {
		return errors.New(msgConfigNoPath)
	}

	bytes, err := ioutil.ReadFile(c.Path)
	if err != nil {
		return err
	}

	if err = c.parse(bytes); err != nil {
		return err
	}

	return nil
}

func (c *configuration) saveConfig(filename string) error {
	bytes, err := yaml.Marshal(c)

	if err != nil {
		return err
	}

	return ioutil.WriteFile(filename, bytes, 0644)
}

func (c *configuration) setPath(path string) error {
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

// loadConfigOpts derives file location and parses configuration options
// from both config file and commandline flags.
func loadConfigOpts(cliOpts *cliOptions, host string) (
	config configuration, err error) {

	config = newConfiguration()

	if err := config.setPath(cliOpts.ConfigPath); err != nil {
		return config, errors.WithMessage(err, "set path")
	}

	if err := config.loadConfig(); err != nil {
		return config, errors.Wrap(err, "read config file")
	}
	log.Debugf("DAOS config read from %s", config.Path)

	// get unique identifier to activate SPDK multiprocess mode
	config.NvmeShmID = hash(host + strconv.Itoa(os.Getpid()))

	if err = config.getIOParams(cliOpts); err != nil {
		return config, errors.Wrap(
			err, "failed to retrieve I/O service params")
	}

	if len(config.Servers) == 0 {
		return config, errors.New("missing I/O service params")
	}

	for idx := range config.Servers {
		config.Servers[idx].Hostname = host
	}

	return config, nil
}

// saveActiveConfig saves read-only active config, tries config dir then /tmp/
func saveActiveConfig(config *configuration) {
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
	h.Write([]byte(s))
	// mask MSB of uint32 as this will be sign bit
	return int(h.Sum32() & 0x7FFFFFFF)
}

// populateCliOpts populates options string slice for single I/O service
func (c *configuration) populateCliOpts(i int) error {
	// avoid mutating subject during iteration, instead access through
	// config/parent object
	srv := &c.Servers[i]

	srv.CliOpts = append(
		srv.CliOpts,
		"-t", strconv.Itoa(srv.Targets),
		"-g", c.SystemName,
		"-s", srv.ScmMount)

	if c.Modules != "" {
		srv.CliOpts = append(srv.CliOpts, "-m", c.Modules)
	}
	if c.Attach != "" {
		srv.CliOpts = append(srv.CliOpts, "-a", c.Attach)
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
	if c.SystemMap != "" {
		srv.CliOpts = append(srv.CliOpts, "-y", c.SystemMap)
	}
	if srv.Rank != nil {
		srv.CliOpts = append(
			srv.CliOpts, "-r", srv.Rank.String())
	}
	if c.SocketDir != "" {
		srv.CliOpts = append(srv.CliOpts, "-d", c.SocketDir)
	}
	if c.NvmeShmID > 0 {
		// Add shm_id so I/O service can share spdk access to controllers
		// with mgmtControlServer process. Currently not user
		// configurable when starting daos_server, use default.
		srv.CliOpts = append(
			srv.CliOpts, "-i", strconv.Itoa(c.NvmeShmID))
	}

	return nil
}

// cmdlineOverride mutates configuration options based on commandline
// options overriding those loaded from configuration file.
//
// Current cli opts for daos_server also specified in config:
//   port, mount path, targets, group, rank, socket dir
// Current cli opts to be passed to be stored by daos_server:
//   modules, attach, map
func (c *configuration) cmdlineOverride(opts *cliOptions) {
	// Populate options that can be provided on both the commandline and config.
	if opts.Port > 0 {
		c.Port = int(opts.Port)
	}
	if opts.Rank != nil {
		// global rank parameter should only apply to first I/O service
		c.Servers[0].Rank = opts.Rank
	}

	// override each per-server config
	for i := range c.Servers {
		srv := &c.Servers[i]

		if opts.MountPath != "" {
			// override each per-server config in addition to global value
			c.ScmMountPath = opts.MountPath
			srv.ScmMount = opts.MountPath
		} else if srv.ScmMount == "" {
			// if scm not specified for server, apply global
			srv.ScmMount = c.ScmMountPath
		}
		if opts.Cores > 0 {
			fmt.Println("-c option deprecated, please use -t instead")
			srv.Targets = int(opts.Cores)
		}
		// Targets should override Cores if specified in cmdline or
		// config file.
		if opts.Targets > 0 {
			srv.Targets = int(opts.Targets)
		}
		if opts.NrXsHelpers != nil {
			srv.NrXsHelpers = int(*opts.NrXsHelpers)
		}
		if opts.FirstCore > 0 {
			srv.FirstCore = int(opts.FirstCore)
		}
	}

	if opts.Group != "" {
		c.SystemName = opts.Group
	}
	if opts.SocketDir != "" {
		c.SocketDir = opts.SocketDir
	}
	if opts.Modules != nil {
		c.Modules = *opts.Modules
	}
	if opts.Attach != nil {
		c.Attach = *opts.Attach
	}
	if opts.Map != nil {
		c.SystemMap = *opts.Map
	}

	return
}

// validateConfig asserts that config meets minimum requirements
func (c *configuration) validateConfig() error {
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
	}

	return nil
}

// getIOParams builds commandline options and environment variables to provide
// to forked I/O service
func (c *configuration) getIOParams(cliOpts *cliOptions) error {
	if err := c.validateConfig(); err != nil {
		examplesPath, _ := c.ext.getAbsInstallPath(relConfExamplesPath)

		return errors.WithMessagef(err, msgBadConfig+examplesPath)
	}

	// override config with commandline supplied options
	c.cmdlineOverride(cliOpts)

	for i := range c.Servers {
		srv := &c.Servers[i]

		if err := c.populateCliOpts(i); err != nil {
			return errors.WithMessagef(
				err,
				"populating I/O service options")
		}

		// add to existing config file EnvVars
		srv.EnvVars = append(
			srv.EnvVars,
			"CRT_PHY_ADDR_STR="+c.Provider,
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

	return nil
}

// populateEnv adds envs from config options to existing envs from user's shell
// overwriting any existing values for given key
func (c *configuration) populateEnv(i int, envs *[]string) {
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

func (c *configuration) setLogging(name string) (*os.File, error) {
	// Set log level mask for default logger from config.
	switch c.ControlLogMask {
	case cLogDebug:
		log.Debugf("Switching control log level to DEBUG")
		log.SetLevel(log.Debug)
	case cLogError:
		log.Debugf("Switching control log level to ERROR")
		log.SetLevel(log.Error)
	}

	// Set log file for default logger if specified in config.
	if c.ControlLogFile != "" {
		f, err := common.AppendFile(c.ControlLogFile)
		if err != nil {
			return nil, errors.WithMessage(
				err, "create log file")
		}

		log.Debugf(
			"%s logging to file %s",
			os.Args[0], c.ControlLogFile)

		log.SetOutput(f)

		return f, nil
	}

	// if no logfile specified, output from multiple hosts
	// may get aggregated, prefix entries with hostname
	log.NewDefaultLogger(log.Debug, name+" ", os.Stderr)

	return nil, nil
}
