//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025 Hewlett Packard Enterprise Development LP
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
	Debug     bool   `long:"debug" description:"enable debug output"`
	WriteMode bool   `long:"write_mode" short:"w" description:"Open the vos file in write mode."`
	CmdFile   string `long:"cmd_file" short:"f" description:"Path to a file containing a sequence of ddb commands to execute."`
	Version   bool   `short:"v" long:"version" description:"Show version"`
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
	for _, comp := range listDirVos(match) {
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
the first positional parameter will be opened before commands are executed.

Many of the commands take a vos tree path. The format for this path
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
	if err != nil {
		return errors.Wrap(err, "Error initializing the DDB Context")
	}
	defer cleanup()
	app := createGrumbleApp(ctx)

	if opts.Args.VosPath != "" {
		if !strings.HasPrefix(string(opts.Args.RunCmd), "feature") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "rm_pool") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "dev_list") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "dev_replace") {
			log.Debugf("Connect to path: %s\n", opts.Args.VosPath)
			if err := ddbOpen(ctx, string(opts.Args.VosPath), opts.WriteMode); err != nil {
				return errors.Wrapf(err, "Error opening path: %s", opts.Args.VosPath)
			}
		}
	}

	if opts.Args.RunCmd != "" && opts.CmdFile != "" {
		return errors.New("Cannot use both command file and a command string")
	}

	if opts.Args.VosPath != "" {
		ctx.ctx.dc_pool_path = C.CString(string(opts.Args.VosPath))
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
				log.Error("Error running command file\n")
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
		if fe, ok := errors.Cause(err).(*flags.Error); ok && fe.Type == flags.ErrHelp {
			log.Info(fe.Error())
			os.Exit(0)
		}
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
