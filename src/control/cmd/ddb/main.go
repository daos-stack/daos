//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bufio"
	"os"
	"path"
	"path/filepath"
	"runtime/debug"
	"sort"
	"strings"

	"github.com/desertbit/grumble"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
)

func exitWithError(log logging.Logger, err error) {
	cmdName := path.Base(os.Args[0])
	log.Errorf("%s: %v", cmdName, err)
	if fault.HasResolution(err) {
		log.Errorf("%s: %s", cmdName, fault.ShowResolutionFor(err))
	}
	os.Exit(1)
}

type cliOptions struct {
	Debug     bool   `long:"debug" description:"enable debug output"`
	WriteMode bool   `long:"write_mode" short:"w" description:"Open the vos file in write mode."`
	CmdFile   string `long:"cmd_file" short:"f" description:"Path to a file containing a sequence of ddb commands to execute."`
	Version   bool   `short:"v" long:"version" description:"Show version"`
	JSON      bool   `short:"j" long:"json" description:"JSON output for not interactive mode"`
	Args      struct {
		VosPath    vosPathStr `positional-arg-name:"vos_file_path"`
		RunCmd     ddbCmdStr  `positional-arg-name:"ddb_command"`
		RunCmdArgs []string   `positional-arg-name:"ddb_command_args"`
	} `positional-args:"yes"`
}

type vosPathStr string

func (pathStr vosPathStr) Complete(match string) (comps []flags.Completion) {
	if match == "" || match == "/" {
		match = defMntPrefix
	}
	for _, comp := range listDir(match) {
		comps = append(comps, flags.Completion{Item: comp})
	}
	sort.Slice(comps, func(i, j int) bool { return comps[i].Item < comps[j].Item })

	return
}

type ddbCmdStr string

func (cmdStr ddbCmdStr) Complete(match string) (comps []flags.Completion) {
	// hack to get at command names
	ctx, cleanup, err := InitDdb()
	if err != nil {
		return
	}
	defer cleanup()
	cmdCtx := CommandContext{ddbContext: ctx, jsonOutput: false, jsonOutputHandled: false}
	app := createGrumbleApp(&cmdCtx)
	for _, cmd := range app.Commands().All() {
		if match == "" || strings.HasPrefix(cmd.Name, match) {
			comps = append(comps, flags.Completion{Item: cmd.Name})
		}
	}
	sort.Slice(comps, func(i, j int) bool { return comps[i].Item < comps[j].Item })

	return
}

func (cmdStr *ddbCmdStr) UnmarshalFlag(fv string) error {
	*cmdStr = ddbCmdStr(fv)
	return nil
}

func runFileCmds(log logging.Logger, app *grumble.App, fileName string) error {
	file, err := os.Open(fileName)
	if err != nil {
		return errors.Wrapf(err, "Error opening file: %s", fileName)

	}
	defer func() {
		err = file.Close()
		if err != nil {
			log.Errorf("Error closing %s: %s\n", fileName, err)
		}
	}()

	log.Debugf("Running commands in: %s\n", fileName)
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		fileCmd := scanner.Text()
		log.Debugf("Running Command: %s\n", fileCmd)
		err := runCmdStr(app, fileCmd)
		if err != nil {
			return errors.Wrapf(err, "Failed running command %q", fileCmd)
		}
	}

	return nil
}

func parseOpts(args []string, opts *cliOptions, log *logging.LeveledLogger) error {
	p := flags.NewParser(opts, flags.HelpFlag|flags.IgnoreUnknown)
	p.Name = "ddb"
	p.Usage = "[OPTIONS]"
	p.ShortDescription = "daos debug tool"
	p.LongDescription = `The DAOS Debug Tool (ddb) allows a user to navigate through and modify
a file in the VOS format. It offers both a command line and interactive
shell mode. If neither a single command or '-f' option is provided, then
the tool will run in interactive mode. In order to modify the VOS file,
the '-w' option must be included. If supplied, the VOS file supplied in
the first positional parameter will be opened before commands are executed.`

	// Set the traceback level such that a crash results in
	// a coredump (when ulimit -c is set appropriately).
	debug.SetTraceback("crash")

	if _, err := p.ParseArgs(args); err != nil {
		return err
	}

	if opts.Version {
		log.Infof("ddb version %s", build.DaosVersion)
		return nil
	}

	if opts.Debug {
		log.WithLogLevel(logging.LogLevelDebug)
		log.Debug("debug output enabled")
	}

	ctx, cleanup, err := InitDdb()

	cmdCtx := CommandContext{ddbContext: ctx, jsonOutput: opts.JSON, jsonOutputHandled: false}

	if err != nil {
		return errors.Wrap(err, "Error initializing the DDB Context")
	}
	defer cleanup()
	app := createGrumbleApp(&cmdCtx)

	if opts.Args.VosPath != "" {
		log.Debugf("Connect to path: %s\n", opts.Args.VosPath)
		if err := ddbOpen(ctx, string(opts.Args.VosPath), opts.WriteMode); err != nil {
			return errors.Wrapf(err, "Error opening path: %s", opts.Args.VosPath)
		}
	}

	if opts.Args.RunCmd != "" && opts.CmdFile != "" {
		return errors.New("Cannot use both command file and a command string")
	}

	if opts.Args.RunCmd != "" || opts.CmdFile != "" {
		// Non-interactive mode
		if opts.Args.RunCmd != "" {
			err := runCmdStr(app, string(opts.Args.RunCmd), opts.Args.RunCmdArgs...)
			if err != nil {
				log.Errorf("Error running command %s\n", string(opts.Args.RunCmd))
			}
		} else {
			err := runFileCmds(log, app, opts.CmdFile)
			if err != nil {
				log.Error("Error running command file\n")
			}
		}

		if ddbPoolIsOpen(ctx) {
			err := ddbClose(ctx)
			if err != nil {
				log.Error("Error closing pool\n")
			}
		}

		if cmdCtx.jsonOutput && !cmdCtx.jsonOutputHandled {
			log.Notice("Command does not support json output")
		}

		return err
	}

	// Interactive mode
	if opts.JSON {
		log.Notice("Interactive mode does not support json output")
	}
	// Print the version upon entry
	log.Infof("ddb version %s", build.DaosVersion)
	// app.Run() uses the os.Args so need to clear them before running
	os.Args = args
	result := app.Run()
	// make sure pool is closed
	if ddbPoolIsOpen(ctx) {
		err := ddbClose(ctx)
		if err != nil {
			log.Error("Error closing pool\n")
		}
	}
	return result
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

func createGrumbleApp(ctx *CommandContext) *grumble.App {
	homedir, err := os.UserHomeDir()
	if err != nil {
		homedir = "/tmp"
	}
	var app = grumble.New(&grumble.Config{
		Name:        "ddb",
		Flags:       nil,
		HistoryFile: filepath.Join(homedir, ".ddb_history"),
		NoColor:     false,
		Prompt:      "ddb:  ",
	})

	addAppCommands(app, ctx)

	// Add the quit command. grumble also includes a builtin exit command
	app.AddCommand(&grumble.Command{
		Name:      "quit",
		Aliases:   []string{"q"},
		Help:      "exit the shell",
		LongHelp:  "",
		HelpGroup: "",
		Run: func(c *grumble.Context) error {
			c.Stop()
			return nil
		},
		Completer: nil,
	})
	return app
}

// Run the command in 'run' using the grumble app. shlex is used to parse the string into an argv/c format
func runCmdStr(app *grumble.App, cmd string, args ...string) error {
	return app.RunCommand(append([]string{cmd}, args...))
}
