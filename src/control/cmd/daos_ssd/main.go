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
	"os"
	"path"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	defaultConfigFile = "daos_server.yml"
)

type mainOpts struct {
	// Minimal set of top-level options
	ConfigPath string `short:"o" long:"config" description:"Server config file path"`
	Debug      bool   `short:"d" long:"debug" description:"Enable debug output"`

	// Define subcommands
	FormatSSD formatSSDCmd `command:"format" alias:"f" description:"Format NVMe storage"`
}

type cmdLogger interface {
	setLog(*logging.LeveledLogger)
}

type logCmd struct {
	log *logging.LeveledLogger
}

func (c *logCmd) setLog(log *logging.LeveledLogger) {
	c.log = log
	// Initialize the netdetect logger
	netdetect.SetLogger(log)
}

func exitWithError(log *logging.LeveledLogger, err error) {
	log.Debugf("%+v", err)
	log.Errorf("%v", err)
	if fault.HasResolution(err) {
		log.Error(fault.ShowResolutionFor(err))
	}
	os.Exit(1)
}

func parseOpts(args []string, opts *mainOpts, log *logging.LeveledLogger) error {
	p := flags.NewParser(opts, flags.HelpFlag|flags.PassDoubleDash)
	p.SubcommandsOptional = false
	p.CommandHandler = func(cmd flags.Commander, cmdArgs []string) error {
		if len(cmdArgs) > 0 {
			// don't support positional arguments, extra cmdArgs are unexpected
			return errors.Errorf("unexpected commandline arguments: %v", cmdArgs)
		}

		if opts.Debug {
			log.SetLevel(logging.LogLevelDebug)
		}
		if logCmd, ok := cmd.(cmdLogger); ok {
			logCmd.setLog(log)
		}

		if opts.ConfigPath == "" {
			defaultConfigPath := path.Join(build.ConfigDir, defaultConfigFile)
			if _, err := os.Stat(defaultConfigPath); err == nil {
				opts.ConfigPath = defaultConfigPath
			}
		}
		if cfgCmd, ok := (cmd.(cfgLoader)); ok {
			cfgCmd.setPath(opts.ConfigPath)
		}

		if err := cmd.Execute(cmdArgs); err != nil {
			return err
		}

		return nil
	}

	// Parse commandline flags which override options loaded from config.
	_, err := p.ParseArgs(args)
	if err != nil {
		return err
	}

	return nil
}

func main() {
	log := logging.NewCommandLineLogger()
	var opts mainOpts

	if err := parseOpts(os.Args[1:], &opts, log); err != nil {
		exitWithError(log, err)
	}
}
