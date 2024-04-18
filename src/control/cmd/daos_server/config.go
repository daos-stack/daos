//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"

	"github.com/daos-stack/daos/src/control/server/config"
)

type cfgLoader interface {
	loadConfig(cfgPath string) error
	configPath() string
}

type cliOverrider interface {
	setCLIOverrides() error
}

type cfgCmd struct {
	config       *config.Server
	IgnoreConfig bool `long:"ignore-config" description:"Ignore parameters set in config file when running command"`
}

type optCfgCmd struct {
	cfgCmd
}

func (c *cfgCmd) configPath() string {
	if c.config == nil {
		return ""
	}
	return c.config.Path
}

func (c *cfgCmd) loadConfig(cfgPath string) error {
	if c.IgnoreConfig {
		c.config = nil
		return nil
	}

	// Don't load a new config if there's already
	// one present. If the caller really wants to
	// reload, it can do that explicitly.
	if c.config != nil {
		return nil
	}

	c.config = config.DefaultServer()
	if err := c.config.SetPath(cfgPath); err != nil {
		return err
	}

	return c.config.Load()
}

func (c *optCfgCmd) loadConfig(cfgPath string) error {
	if c.IgnoreConfig {
		c.config = nil
		return nil
	}

	if err := c.cfgCmd.loadConfig(cfgPath); err != nil && !os.IsNotExist(err) {
		return err
	}
	return nil
}
