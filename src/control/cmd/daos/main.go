//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path"
	"runtime/debug"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
)

type cliOptions struct {
	Debug      bool           `long:"debug" description:"enable debug output"`
	Verbose    bool           `long:"verbose" description:"enable verbose output (when applicable)"`
	JSON       bool           `long:"json" short:"j" description:"enable JSON output"`
	Container  containerCmd   `command:"container" alias:"cont" description:"perform tasks related to DAOS containers"`
	Pool       poolCmd        `command:"pool" description:"perform tasks related to DAOS pools"`
	Filesystem fsCmd          `command:"filesystem" alias:"fs" description:"POSIX filesystem operations"`
	Object     objectCmd      `command:"object" alias:"obj" description:"DAOS object operations"`
	System     systemCmd      `command:"system" alias:"sys" description:"DAOS system operations"`
	Version    versionCmd     `command:"version" description:"print daos version"`
	ManPage    cmdutil.ManCmd `command:"manpage" hidden:"true"`
}

type versionCmd struct {
	cmdutil.JSONOutputCmd
}

func (cmd *versionCmd) Execute(_ []string) error {
	if cmd.JSONOutputEnabled() {
		buf, err := build.MarshalJSON(build.CLIUtilName)
		if err != nil {
			return err
		}
		return cmd.OutputJSON(json.RawMessage(buf), nil)
	}

	fmt.Printf("%s, libdaos v%s\n", build.String(build.CLIUtilName), apiVersion())
	os.Exit(0)
	return nil
}

func exitWithError(log logging.Logger, err error) {
	cmdName := path.Base(os.Args[0])
	log.Errorf("%s: %v", cmdName, err)
	if fault.HasResolution(err) {
		log.Errorf("%s: %s", cmdName, fault.ShowResolutionFor(err))
	}
	os.Exit(1)
}

func parseOpts(args []string, opts *cliOptions, log *logging.LeveledLogger) error {
	var wroteJSON atm.Bool
	p := flags.NewParser(opts, flags.Default)
	p.Name = "daos"
	p.ShortDescription = "Command to manage DAOS pool/container/object"
	p.LongDescription = `daos is a tool that can be used to manage/query pool content,
create/query/manage/destroy a container inside a pool, copy data
between a POSIX container and a POSIX filesystem, clone a DAOS container,
or query/manage an object inside a container.`
	p.Options ^= flags.PrintErrors // Don't allow the library to print errors
	p.CommandHandler = func(cmd flags.Commander, args []string) error {
		if cmd == nil {
			return nil
		}

		if manCmd, ok := cmd.(cmdutil.ManPageWriter); ok {
			manCmd.SetWriteFunc(p.WriteManPage)
			return cmd.Execute(args)
		}

		if opts.Debug {
			log.SetLevel(logging.LogLevelTrace)
			if os.Getenv("D_LOG_MASK") == "" {
				os.Setenv("D_LOG_MASK", "DEBUG,OBJECT=ERR,PLACEMENT=ERR")
			}
			if os.Getenv("DD_MASK") == "" {
				os.Setenv("DD_MASK", "mgmt")
			}
			log.Debug("debug output enabled")
		}

		if jsonCmd, ok := cmd.(cmdutil.JSONOutputter); ok && opts.JSON {
			jsonCmd.EnableJSONOutput(os.Stdout, &wroteJSON)
			// disable output on stdout other than JSON
			log.ClearLevel(logging.LogLevelInfo)
		}

		if logCmd, ok := cmd.(cmdutil.LogSetter); ok {
			logCmd.SetLog(log)
		}

		if daosCmd, ok := cmd.(daosCaller); ok {
			fini, err := daosCmd.initDAOS()
			if err != nil {
				return err
			}
			defer fini()
		}

		if argsCmd, ok := cmd.(cmdutil.ArgsHandler); ok {
			if err := argsCmd.CheckArgs(args); err != nil {
				return err
			}
		}

		// fixup args for commands that can use --path and
		// positional arguments
		if contPathCmd, ok := cmd.(interface {
			parseContPathArgs([]string) ([]string, error)
		}); ok {
			var err error
			args, err = contPathCmd.parseContPathArgs(args)
			if err != nil {
				return err
			}
		}

		if err := cmd.Execute(args); err != nil {
			return err
		}

		return nil
	}

	// Configure DAOS client logging to stderr if no log file
	// is specified. This is to avoid polluting the JSON output.
	if os.Getenv("D_LOG_FILE") == "" {
		os.Setenv("D_LOG_FILE", "/dev/null")
		if os.Getenv("DD_STDERR") == "" {
			os.Setenv("DD_STDERR", "debug")
		}
	} else if os.Getenv("DD_STDERR") == "" {
		os.Setenv("DD_STDERR", "err")
	}

	// Initialize the daos debug system first so that
	// any allocations made as part of argument parsing
	// are logged when running under NLT.
	debugFini, err := initDaosDebug()
	if err != nil {
		exitWithError(log, err)
	}
	defer debugFini()

	// Set the traceback level such that a crash results in
	// a coredump (when ulimit -c is set appropriately).
	debug.SetTraceback("crash")

	_, err = p.ParseArgs(args)
	if opts.JSON && wroteJSON.IsFalse() {
		return cmdutil.OutputJSON(os.Stdout, nil, err)
	}
	return err
}

func main() {
	var opts cliOptions
	log := logging.NewCommandLineLogger()

	if err := parseOpts(os.Args[1:], &opts, log); err != nil {
		if fe, ok := errors.Cause(err).(*flags.Error); ok && fe.Type == flags.ErrHelp {
			log.Info(fe.Error())
			os.Exit(0)
		}
		exitWithError(log, err)
	}
}
