//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path"
	"runtime/debug"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	jsonOutputter interface {
		enableJsonOutput(bool, io.Writer, *atm.Bool)
		jsonOutputEnabled() bool
		outputJSON(interface{}, error) error
		errorJSON(error) error
	}

	jsonOutputCmd struct {
		wroteJSON      *atm.Bool
		writer         io.Writer
		shouldEmitJSON bool
	}
)

func (cmd *jsonOutputCmd) enableJsonOutput(emitJson bool, w io.Writer, wj *atm.Bool) {
	cmd.shouldEmitJSON = emitJson
	cmd.writer = w
	cmd.wroteJSON = wj
}

func (cmd *jsonOutputCmd) jsonOutputEnabled() bool {
	return cmd.shouldEmitJSON
}

func outputJSON(out io.Writer, in interface{}, cmdErr error) error {
	status := 0
	var errStr *string
	if cmdErr != nil {
		errStr = new(string)
		*errStr = cmdErr.Error()
		if s, ok := errors.Cause(cmdErr).(drpc.DaosStatus); ok {
			status = int(s)
		} else {
			status = int(drpc.DaosMiscError)
		}
	}

	data, err := json.MarshalIndent(struct {
		Response interface{} `json:"response"`
		Error    *string     `json:"error"`
		Status   int         `json:"status"`
	}{in, errStr, status}, "", "  ")
	if err != nil {
		return err
	}

	if _, err = out.Write(append(data, []byte("\n")...)); err != nil {
		return err
	}

	return cmdErr
}

func (cmd *jsonOutputCmd) outputJSON(in interface{}, cmdErr error) error {
	if cmd.wroteJSON.IsTrue() {
		return cmdErr
	}
	cmd.wroteJSON.SetTrue()
	return outputJSON(cmd.writer, in, cmdErr)
}

func errorJSON(err error) error {
	return outputJSON(os.Stdout, nil, err)
}

func (cmd *jsonOutputCmd) errorJSON(err error) error {
	return cmd.outputJSON(nil, err)
}

var _ jsonOutputter = (*jsonOutputCmd)(nil)

type cmdLogger interface {
	setLog(*logging.LeveledLogger)
}

type logCmd struct {
	log *logging.LeveledLogger
}

func (c *logCmd) setLog(log *logging.LeveledLogger) {
	c.log = log
}

type cliOptions struct {
	Debug      bool          `long:"debug" description:"enable debug output"`
	Verbose    bool          `long:"verbose" description:"enable verbose output (when applicable)"`
	JSON       bool          `long:"json" short:"j" description:"enable JSON output"`
	Container  containerCmd  `command:"container" alias:"cont" description:"perform tasks related to DAOS containers"`
	Pool       poolCmd       `command:"pool" description:"perform tasks related to DAOS pools"`
	Filesystem fsCmd         `command:"filesystem" alias:"fs" description:"POSIX filesystem operations"`
	Object     objectCmd     `command:"object" alias:"obj" description:"DAOS object operations"`
	Version    versionCmd    `command:"version" description:"print daos version"`
	ManPage    common.ManCmd `command:"manpage" hidden:"true"`
}

type versionCmd struct{}

func (cmd *versionCmd) Execute(_ []string) error {
	fmt.Printf("daos version %s, libdaos %s\n", build.DaosVersion, apiVersion())
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

		if manCmd, ok := cmd.(common.ManPageWriter); ok {
			manCmd.SetWriteFunc(p.WriteManPage)
			return cmd.Execute(args)
		}

		if opts.Debug {
			log.WithLogLevel(logging.LogLevelDebug)
			if os.Getenv("D_LOG_MASK") == "" {
				os.Setenv("D_LOG_MASK", "DEBUG,OBJECT=ERR,PLACEMENT=ERR")
			}
			log.Debug("debug output enabled")
		}

		if jsonCmd, ok := cmd.(jsonOutputter); ok {
			jsonCmd.enableJsonOutput(opts.JSON, os.Stdout, &wroteJSON)
			if opts.JSON {
				// disable output on stdout other than JSON
				log.ClearLevel(logging.LogLevelInfo)
			}
		}

		if logCmd, ok := cmd.(cmdLogger); ok {
			logCmd.setLog(log)
		}

		if daosCmd, ok := cmd.(daosCaller); ok {
			fini, err := daosCmd.initDAOS()
			if err != nil {
				return err
			}
			defer fini()
		}

		if err := cmd.Execute(args); err != nil {
			return err
		}

		return nil
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
		return errorJSON(err)
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
