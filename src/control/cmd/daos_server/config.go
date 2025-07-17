//
// (C) Copyright 2019-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
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

	c.config = config.DefaultServer()
	if c.ConfigPath == "" {
		if path, err := build.FindConfigFilePath(defaultConfigFile); err == nil {
			c.ConfigPath = path
		} else if c.configOptional() {
			log.Debugf("optional config file not found: %s", err.Error())
			return nil
		} else {
			// not found and not optional
			c.config = nil
			return err
		}
	}

	if err := c.config.SetPath(c.ConfigPath); err != nil {
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
