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
	providerEnvKey = "CRT_PHY_ADDR_STR"
	configOut      = ".daos_server.active.yml"
)

func (c *configuration) loadConfig() error {
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

// loadConfigOpts derives file location and parses configuration options
// from both config file and commandline flags.
func loadConfigOpts(cliOpts *cliOptions) (configuration, error) {
	config := newConfiguration()

	if cliOpts.ConfigPath != "" {
		config.Path = cliOpts.ConfigPath
	}
	if !filepath.IsAbs(config.Path) {
		newPath, err := common.GetAbsInstallPath(config.Path)
		if err != nil {
			return config, err
		}
		config.Path = newPath
	}

	err := config.loadConfig()
	if err != nil {
		return config, errors.Wrap(err, "failed to read config file")
	}
	log.Debugf("DAOS config read from %s", config.Path)

	host, err := os.Hostname()
	if err != nil {
		return config, errors.Wrap(err, "failed to get hostname")
	}

	// get unique identifier to activate SPDK multiprocess mode
	config.NvmeShmID = hash(host + strconv.Itoa(os.Getpid()))

	if err = config.getIOParams(cliOpts); err != nil {
		return config, errors.Wrap(
			err, "failed to retrieve I/O server params")
	}
	if len(config.Servers) == 0 {
		return config, errors.New("missing I/O server params")
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

// setNumCores takes number of cores and converts to list of ranges
func setNumCores(num int) (rs []string, err error) {
	if num < 1 {
		return rs, fmt.Errorf(
			"invalid number of cpus (cores) specified: %d", num)
	}
	if num == 1 {
		return append(rs, "0"), err
	}
	return append(rs, "0-"+strconv.Itoa(num-1)), err
}

// getNumCores takes list of ranges specified by strings and returns number of
// contiguous cores represented
func getNumCores(rs []string) (num int, err error) {
	// check list is nil or empty as in that case we want to pass 0 to maintain
	// functional parity if core/cpu count is unspecified on cli and config
	if (rs == nil) || (len(rs) == 0) {
		return
	}
	var lower, upper int
	for _, s := range rs {
		limits := strings.Split(s, "-")
		if len(limits) == 1 {
			if _, err = strconv.Atoi(limits[0]); err != nil {
				return
			}
			num++
			continue
		}
		if len(limits) == 2 {
			lower, err = strconv.Atoi(limits[0])
			if err != nil {
				return
			}
			upper, err = strconv.Atoi(limits[1])
			if err != nil {
				return
			}
			if upper > lower {
				num += (upper - lower) + 1
				continue
			}
		}
		return num, fmt.Errorf(
			"unsupported range format %s, need <int>-<int> e.g. 1-10", s)
	}
	return
}

// populateCliOpts populates options string slice for single server
//
// Current mandatory cli opts for daos_io_server:
//   group/system name (-g), number cores (-c), scm mount point (-s)
// Current optional cli opts for daos_io_server:
//  rank (-r), modules (-m), attach (-a), system map (-y), ??socket dir??
func (c *configuration) populateCliOpts(i int) error {
	server := &c.Servers[i]
	// calculate number of cores to use from supplied cpu ranges
	var numCores int
	numCores, err := getNumCores(server.Cpus)
	if err != nil {
		return fmt.Errorf("server%d cpus invalid: %s", i, err)
	}
	server.CliOpts = append(
		server.CliOpts,
		"-c", strconv.Itoa(numCores),
		"-g", c.SystemName,
		"-s", server.ScmMount)
	if c.Modules != "" {
		server.CliOpts = append(server.CliOpts, "-m", c.Modules)
	}
	if c.Attach != "" {
		server.CliOpts = append(server.CliOpts, "-a", c.Attach)
	}
	server.CliOpts = append(server.CliOpts, "-x", strconv.Itoa(c.XShelpernr))
	if c.Firstcore > 0 {
		server.CliOpts = append(server.CliOpts, "-f", strconv.Itoa(c.Firstcore))
	}
	if c.SystemMap != "" {
		server.CliOpts = append(server.CliOpts, "-y", c.SystemMap)
	}
	if server.Rank != nil {
		server.CliOpts = append(server.CliOpts, "-r", server.Rank.String())
	}
	if c.SocketDir != "" {
		server.CliOpts = append(server.CliOpts, "-d", c.SocketDir)
	}
	if c.NvmeShmID > 0 {
		// Add shm_id so io_server can share spdk access to controllers
		// with mgmtControlServer process. Currently not user
		// configurable when starting daos_server, use default.
		server.CliOpts = append(
			server.CliOpts, "-i", strconv.Itoa(c.NvmeShmID))
	}

	return nil
}

// cmdlineOverride mutates configuration options based on commandline
// options overriding those loaded from configuration file.
//
// Current cli opts for daos_server also specified in config:
//   port, mount path, cores, group, rank, socket dir
// Current cli opts to be passed to be stored by daos_server:
//   modules, attach, map
func (c *configuration) cmdlineOverride(opts *cliOptions) {
	// Populate options that can be provided on both the commandline and config.
	if opts.Port > 0 {
		c.Port = int(opts.Port)
	}
	// override each per-server config
	for i := range c.Servers {
		if opts.MountPath != "" {
			// override each per-server config in addition to global value
			c.ScmMountPath = opts.MountPath
			c.Servers[i].ScmMount = opts.MountPath
		} else if c.Servers[i].ScmMount == "" {
			// if scm not specified for server, apply global
			c.Servers[i].ScmMount = c.ScmMountPath
		}
		if opts.Cores > 0 {
			c.Servers[i].Cpus, _ = setNumCores(int(opts.Cores))
		}
		if opts.Rank != nil {
			// override first per-server config (doesn't make sense
			// to reply to more than one server)
			c.Servers[0].Rank = opts.Rank
		}
	}
	if opts.XShelpernr > 2 {
		log.Errorf("invalid XShelpernr %d exceed [0, 2], use default value of 1",
			   opts.XShelpernr)
		c.XShelpernr = 1
	} else {
		c.XShelpernr = opts.XShelpernr
	}
	if opts.Firstcore > 0 {
		c.Firstcore = opts.Firstcore
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

// validateConfig asserts that config meets minimum requirements and
// in the case of missing config file info attempts to detect external
// os environment variables (returns true to skip following env creation)
func (c *configuration) validateConfig() (bool, error) {
	if c.ext.getenv(providerEnvKey) != "" {
		if len(c.Servers) == 0 {
			c.Servers = append(c.Servers, newDefaultServer())
		}
		return true, nil
	}
	// if provider or Servers are missing and we can't detect os envs, we don't
	// have sufficient info to start io servers
	if (c.Provider == "") || (len(c.Servers) == 0) {
		return false, fmt.Errorf(
			"required parameters missing from config and os environment (%s)",
			providerEnvKey)
	}
	return false, nil
}

// getIOParams builds lists of commandline options and environment variables
// to pass when invoking I/O server instances.
func (c *configuration) getIOParams(cliOpts *cliOptions) error {
	// if config doesn't specify server and/or provider we need to
	// attempt to check if at least some of the envs exist and
	// notify that IO server will run with user set env vars
	//
	// if provider not specified in config, make sure it is already
	// set in os. If it is create default Server (envs will not be set)
	skipEnv, err := c.validateConfig()
	if err != nil {
		return err
	}

	// override config with commandline supplied options and compute io server
	// paramaters, perform this after initial validation
	c.cmdlineOverride(cliOpts)

	for i := range c.Servers {
		// avoid mutating subject during iteration, instead access through
		// config/parent object
		server := &c.Servers[i]
		// verify scm mount path is valid
		mntpt := server.ScmMount
		if err = c.checkMount(mntpt); err != nil {
			return fmt.Errorf(
				"server%d scm mount path (%s) not mounted: %s",
				i, mntpt, err)
		}
		if err = c.populateCliOpts(i); err != nil {
			return err
		}
		if !skipEnv {
			// add to existing config file EnvVars
			server.EnvVars = append(
				server.EnvVars,
				providerEnvKey+"="+c.Provider,
				"OFI_INTERFACE="+server.FabricIface,
				"OFI_PORT="+strconv.Itoa(server.FabricIfacePort),
				"D_LOG_MASK="+server.LogMask,
				"D_LOG_FILE="+server.LogFile)
			continue
		}
		examplesPath, _ := common.GetAbsInstallPath("utils/config/examples/")
		// user environment variable detected for provider, assume all
		// necessary environment already exists and clear server config EnvVars
		log.Errorf(
			"using os env vars, specify params in config instead: %s",
			examplesPath)
		server.EnvVars = []string{}
	}
	return nil
}

// PopulateEnv adds envs from config options
func (c *configuration) populateEnv(ioIdx int, envs *[]string) {
	for _, env := range c.Servers[ioIdx].EnvVars {
		kv := strings.Split(env, "=")
		if kv[1] == "" {
			log.Debugf("empty value for env %s detected", kv[0])
		}
		*envs = append(*envs, env)
	}
}
