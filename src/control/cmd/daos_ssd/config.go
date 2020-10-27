//
// (C) Copyright 2020 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/server"
)

type cfgLoader interface {
	loadConfig() error
	configPath() string
	setPath(cfgPath string)
}

type cliOverrider interface {
	setCLIOverrides() error
}

type cfgCmd struct {
	cfgPath string
	config  *server.Configuration
}

func (c *cfgCmd) setPath(cp string) {
	c.cfgPath = cp
}

func (c *cfgCmd) configPath() string {
	if c.config == nil {
		return ""
	}
	return c.config.Path
}

func (c *cfgCmd) loadConfig() error {
	// Don't load a new config if there's already
	// one present. If the caller really wants to
	// reload, it can do that explicitly.
	if c.config != nil {
		return nil
	}

	c.config = server.NewConfiguration()
	if err := c.config.SetPath(c.cfgPath); err != nil {
		return err
	}

	return c.config.Load()
}
