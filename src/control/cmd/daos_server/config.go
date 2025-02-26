//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"
	"path"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/config"
)

type cfgLoader interface {
	loadConfig(logging.Logger) error
	configPath() string
	configOptional() bool
}

type optionalCfgLoader interface {
	setOptional()
}

type cliOverrider interface {
	setCLIOverrides() error
}

type cfgCmd struct {
	config           *config.Server
	configIsOptional bool
	IgnoreConfig     bool   `long:"ignore-config" description:"Ignore parameters set in config file when running command"`
	ConfigPath       string `short:"o" long:"config" description:"Server config file path"`
}

func (c *cfgCmd) configPath() string {
	if c.config == nil {
		return ""
	}

	return c.config.Path
}

func (c *cfgCmd) loadConfig(log logging.Logger) error {
	if c.IgnoreConfig {
		c.config = nil
		return nil
	}

	// Don't load a new config if there's already one present. If the caller really wants to
	// reload, it can do that explicitly.
	if c.config != nil {
		return nil
	}

	setInArgs := false
	if c.ConfigPath != "" {
		setInArgs = true
	} else {
		// Set config path to build directory if not supplied in command args.
		c.ConfigPath = path.Join(build.ConfigDir, defaultConfigFile)
	}

	c.config = config.DefaultServer()
	if err := c.config.SetPath(c.ConfigPath); err != nil {
		if os.IsNotExist(err) && c.configOptional() && !setInArgs {
			// Situation permitted where for an optCfgCmd -o has not been set and no
			// file exists at the default path.
			c.ConfigPath = ""
			return nil
		}

		return err
	}

	return c.config.Load(log)
}

func (c *cfgCmd) configOptional() bool {
	return c.configIsOptional
}

type optCfgCmd struct {
	cfgCmd
}

func (oc *optCfgCmd) setOptional() {
	oc.configIsOptional = true
}
