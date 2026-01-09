//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
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
	"unsafe"

	"github.com/desertbit/columnize"
	"github.com/desertbit/go-shlex"
	"github.com/desertbit/grumble"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
)

/*
 #include <stdlib.h>
*/
import "C"

func exitWithError(log logging.Logger, err error) {
	cmdName := path.Base(os.Args[0])
	log.Errorf("%s: %v", cmdName, err)
	if fault.HasResolution(err) {
		log.Errorf("%s: %s", cmdName, fault.ShowResolutionFor(err))
	}
	os.Exit(1)
}

type cliOptions struct {
	Debug     bool       `long:"debug" description:"enable debug output"`
	WriteMode bool       `long:"write_mode" short:"w" description:"Open the vos file in write mode."`
	CmdFile   string     `long:"cmd_file" short:"f" description:"Path to a file containing a sequence of ddb commands to execute."`
	SysdbPath string     `long:"db_path" short:"p" description:"Path to the sys db."`
	VosPath   vosPathStr `long:"vos_path" short:"s" description:"Path to the VOS file to open."`
	Version   bool       `short:"v" long:"version" description:"Show version"`
	Args      struct {
		RunCmd     ddbCmdStr `positional-arg-name:"ddb_command"`
		RunCmdArgs []string  `positional-arg-name:"ddb_command_args"`
	} `positional-args:"yes"`
}

const helpCommandsHeader = `
Available commands:

`

const helpVosTreePath = `
Path

Many of the commands take a VOS tree path. The format for this path
is [cont]/[obj]/[dkey]/[akey]/[extent].
- cont - the full container uuid.
- obj - the object id.
- keys (akey, dkey) - there are multiple types of keys
   -- string keys are simply the string value. If the size of the
      key is greater than strlen(key), then the size is included at
      the end of the string value. Example: 'akey{5}' is the key: akey
      with a null terminator at the end.
   -- number keys are formatted as '{[type]: NNN}' where type is
      'uint8, uint16, uint32, or uint64'. NNN can be a decimal or
      hex number. Example: '{uint32: 123456}'
   -- binary keys are formatted as '{bin: 0xHHH}' where HHH is the hex
      representation of the binary key. Example: '{bin: 0x1a2b}'
- extent for array values - in the format {lo-hi}.

To make it easier to navigate the tree, indexes can be
used instead of the path part. The index is in the format [i]. Indexes
and actual path values can be used together

Example Paths:
/3550f5df-e6b1-4415-947e-82e15cf769af/939000573846355970.0.13.1/dkey/akey/[0-1023]
[0]/[1]/[2]/[1]/[9]
/[0]/939000573846355970.0.13.1/[2]/akey{5}/[0-1023]

`

const grumbleUnknownCmdErr = "unknown command, try 'help'"

type vosPathStr string

func (pathStr vosPathStr) Complete(match string) (comps []flags.Completion) {
	if match == "" || match == "/" {
		match = defMntPrefix
	}
	for _, comp := range listDirVos(match) {
		comps = append(comps, flags.Completion{Item: comp})
	}
	sort.Slice(comps, func(i, j int) bool { return comps[i].Item < comps[j].Item })

	return
}

type ddbCmdStr string

func (cmdStr ddbCmdStr) Complete(match string) (comps []flags.Completion) {
	// hack to get at command names
	ctx, cleanup, err := InitDdb(nil)
	if err != nil {
		return
	}
	defer cleanup()

	app := createGrumbleApp(ctx)
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
		return errors.Wrapf(err, "Error opening file %q", fileName)

	}
	defer func() {
		err = file.Close()
		if err != nil {
			log.Errorf("Error closing %q: %s\n", fileName, err)
		}
	}()

	log.Debugf("Running commands in %q\n", fileName)
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		lineStr := scanner.Text()
		lineCmd, err := shlex.Split(lineStr, true)
		if err != nil {
			return errors.Wrapf(err, "Failed running command %q", lineStr)
		}
		if len(lineCmd) == 0 || strings.HasPrefix(lineCmd[0], "#") {
			continue
		}
		log.Debugf("Running Command %q\n", lineStr)
		err = runCmdStr(app, lineCmd[0], lineCmd[1:]...)
		if err != nil {
			return errors.Wrapf(err, "Failed running command %q", lineStr)
		}
	}

	return nil
}

// One cannot relay on grumble to print the list of commands since app does not allow executing
// the help command from the outside of the interactive mode.
// This method extracts commands and their respective help (short) messages in the simplest possible way,
// put them in columns and print them using the provided log.
func printCommands(app *grumble.App, log *logging.LeveledLogger) {
	var output []string
	for _, c := range app.Commands().All() {
		if c.Name == "quit" {
			continue
		}
		row := c.Name + columnize.DefaultConfig().Delim + c.Help
		output = append(output, row)
	}
	log.Info(helpCommandsHeader + columnize.SimpleFormat(output) + "\n\n")
}

func printGeneralHelp(app *grumble.App, generalMsg string, log *logging.LeveledLogger) {
	log.Info(generalMsg + "\n") // standard help from go-flags
	printCommands(app, log)     // list of commands
	log.Info(helpVosTreePath)   // extra info on VOS Tree Path syntax
}

// Ask grumble to generate a help message for the requested command.
// Caveat: There is no known easy way of forcing grumble to use log to print the generated message
// so the output goes directly to stdout.
// Returns false in case the opts.Args.RunCmd is unknown.
func printCmdHelp(app *grumble.App, opts *cliOptions, log *logging.LeveledLogger) bool {
	err := runCmdStr(app, string(opts.Args.RunCmd), "--help")
	if err != nil {
		if err.Error() == grumbleUnknownCmdErr {
			log.Errorf("unknown command '%s'", string(opts.Args.RunCmd))
			printCommands(app, log)
		} else {
			log.Error(err.Error())
		}
		return false
	}
	return true
}

// Prints either general or command-specific help message.
// Returns a reasonable return code in case the caller chooses to terminate the process.
func printHelp(generalMsg string, opts *cliOptions, log *logging.LeveledLogger) int {
	// ctx is not necessary since this instance of the app is not intended to run any of the commands
	app := createGrumbleApp(nil)

	if string(opts.Args.RunCmd) == "" {
		printGeneralHelp(app, generalMsg, log)
		return 0
	}

	if printCmdHelp(app, opts, log) {
		return 0
	} else {
		return 1
	}
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
the '-w' option must be included.

If the command requires it, the VOS file provided as the first positional
parameter will be opened before any commands are executed. See the
command‑specific help for details. When the VOS file is not required, it is
ignored; however, it must still be supplied, and it may be empty (""), e.g.

ddb "" ls --help
`

	// Set the traceback level such that a crash results in
	// a coredump (when ulimit -c is set appropriately).
	debug.SetTraceback("crash")

	if _, err := p.ParseArgs(args); err != nil {
		if fe, ok := errors.Cause(err).(*flags.Error); ok && fe.Type == flags.ErrHelp {
			os.Exit(printHelp(fe.Error(), opts, log))
		}

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

	ctx, cleanup, err := InitDdb(log)
	if err != nil {
		return errors.Wrap(err, "Error initializing the DDB Context")
	}
	defer cleanup()
	app := createGrumbleApp(ctx)

	if opts.VosPath != "" {
		if !strings.HasPrefix(string(opts.Args.RunCmd), "feature") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "rm_pool") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "dev_list") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "dev_replace") {
			log.Debugf("Connect to path: %s\n", opts.VosPath)
			if err := ddbOpen(ctx, string(opts.VosPath), string(opts.SysdbPath), opts.WriteMode); err != nil {
				return errors.Wrapf(err, "Error opening path: %s", opts.VosPath)
			}
		}
	}

	if opts.Args.RunCmd != "" && opts.CmdFile != "" {
		return errors.New("Cannot use both command file and a command string")
	}

	if opts.VosPath != "" {
		ctx.ctx.dc_pool_path = C.CString(string(opts.VosPath))
		defer C.free(unsafe.Pointer(ctx.ctx.dc_pool_path))
	}
	if opts.Args.RunCmd != "" || opts.CmdFile != "" {
		// Non-interactive mode
		if opts.Args.RunCmd != "" {
			err := runCmdStr(app, string(opts.Args.RunCmd), opts.Args.RunCmdArgs...)
			if err != nil {
				log.Errorf("Error running command %q %s\n", string(opts.Args.RunCmd), err)
			}
		} else {
			err := runFileCmds(log, app, opts.CmdFile)
			if err != nil {
				log.Errorf("Error running command file: %s\n", err)
			}
		}

		if ddbPoolIsOpen(ctx) {
			err := ddbClose(ctx)
			if err != nil {
				log.Error("Error closing pool\n")
			}
		}
		return err
	}

	// Interactive mode
	// Print the version upon entry
	log.Infof("ddb version %s", build.DaosVersion)
	// app.Run() uses the os.Args so need to clear them before running
	os.Args = []string{}
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
		exitWithError(log, err)
	}
}

func createGrumbleApp(ctx *DdbContext) *grumble.App {
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
