//
// (C) Copyright 2022-2024 Intel Corporation.
// (C) Copyright 2025-2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"bufio"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"runtime/debug"
	"slices"
	"strings"

	"github.com/desertbit/columnize"
	"github.com/desertbit/go-shlex"
	"github.com/desertbit/grumble"
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
)

var errHelpRequested = errors.New("help requested")

type unknownCmdError struct {
	cmd string
}

func (e *unknownCmdError) Error() string {
	return fmt.Sprintf("Error running command '%s' unknown command, try 'help'", e.cmd)
}

func exitWithError(err error) {
	cmdName := filepath.Base(os.Args[0])
	msg := fmt.Sprintf("ERROR: %s: %v", cmdName, err)
	if fault.HasResolution(err) {
		msg = fmt.Sprintf("%s (%s)", msg, fault.ShowResolutionFor(err))
	}
	fmt.Fprintln(os.Stderr, msg)

	if _, ok := err.(*unknownCmdError); ok {
		app := createGrumbleApp(nil)
		printCommands(os.Stderr, app)
	}
	os.Exit(1)
}

type cliOptions struct {
	WriteMode bool   `long:"write_mode" short:"w" description:"Open the VOS file in write mode."`
	CmdFile   string `long:"cmd_file" short:"f" description:"Path to a file containing a sequence of ddb commands to execute."`
	SysdbPath string `long:"db_path" short:"p" description:"Path to the sys db."`
	VosPath   string `long:"vos_path" short:"s" description:"Path to the VOS file to open."`
	Version   bool   `short:"v" long:"version" description:"Show version"`
	Debug     string `long:"debug" description:"Logging log level (default to ERROR).  More details can be found in the ddb man page."`
	LogDir    string `long:"log_dir" description:"Directory to write log files to. If not provided, logs will only be written to the console."`
	Args      struct {
		RunCmd     string   `positional-arg-name:"ddb_command" description:"Optional ddb command to run. If not provided, the tool will run in interactive mode."`
		RunCmdArgs []string `positional-arg-name:"ddb_command_args" description:"Arguments for the ddb command to run. If not provided, the command will be run without any arguments."`
	} `positional-args:"yes"`
}

const helpCommandsHeader = `
Available Commands:

`

const helpTreePath = `
VOS Paths:

Many of the commands take a VOS tree path. The format for this path is
[cont]/[obj]/[dkey]/[akey]/[extent].  To make it easier to navigate the tree, indexes can be used
instead of the path part. The index is in the format [i]. Indexes and actual path values can be used
together.

More details on the path format can be found in the ddb man page.

`

const ddbLongDescription = `The DAOS Debug Tool (ddb) allows a user to navigate through and modify
a file in the VOS format. It offers both a command line and interactive
shell mode. If neither a single command or '-f' option is provided, then
the tool will run in interactive mode. In order to modify the VOS file,
the '-w' option must be included.

If the command requires it, the VOS file must be provided with the parameter 
--vos-path. The VOS file will be opened before any commands are executed. See
the command‑specific help for details.

A DAOS file system can operate in different modes depending on the available hardware resources.
The two primary modes are MD-on-SSD and PMEM. In MD-on-SSD mode (the default), metadata is stored
on NVMe devices, which requires additional preliminary steps before using ddb. See the MD-on-SSD
MODE section of the manpage for details.
`

const grumbleUnknownCmdErr = "unknown command, try 'help'"
const runCmdArgsErr = "Cannot use both command file and a command string"
const vosPathMissErr = "Cannot use sys db path without a VOS path"
const loggerInitErr = "Logging facilities cannot be initialized"
const ctxInitErr = "DDB Context cannot be initialized"
const vosPathOpenErr = "Error opening VOS path '%s'"

func runFileCmds(app *grumble.App, log logging.Logger, fileName string) error {
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
		err = runCmdStr(app, nil, lineCmd[0], lineCmd[1:]...)
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
func printCommands(fd io.Writer, app *grumble.App) {
	var output []string
	for _, c := range app.Commands().All() {
		if c.Name == "quit" {
			continue
		}
		row := c.Name + columnize.DefaultConfig().Delim + c.Help
		output = append(output, row)
	}
	fmt.Fprintf(fd, helpCommandsHeader+columnize.SimpleFormat(output)+"\n\n")
}

func printGeneralHelp(app *grumble.App, generalMsg string) {
	fmt.Println(generalMsg)       // standard help from go-flags
	printCommands(os.Stdout, app) // list of commands
	fmt.Printf(helpTreePath)      // extra info on VOS Tree Path syntax
}

// Ask grumble to generate a help message for the requested command.
// Caveat: There is no known easy way of forcing grumble to use log to print the generated message
// so the output goes directly to stdout.
func printCmdHelp(app *grumble.App, opts cliOptions) error {
	if err := runCmdStr(app, nil, string(opts.Args.RunCmd), "--help"); err != nil {
		return &unknownCmdError{cmd: opts.Args.RunCmd}
	}
	return errHelpRequested
}

// Prints either general or command-specific help message.
// Returns a reasonable return code in case the caller chooses to terminate the process.
func printHelp(opts cliOptions, generalMsg string) error {
	// ctx is not necessary since this instance of the app is not intended to run any of the commands
	app := createGrumbleApp(nil)

	if string(opts.Args.RunCmd) == "" {
		printGeneralHelp(app, generalMsg)
		return errHelpRequested
	}

	return printCmdHelp(app, opts)
}

func setenvIfNotSet(key, value string) {
	if os.Getenv(key) == "" {
		os.Setenv(key, value)
	}
}

// The golang cli and the C engine use separate logging systems with different log levels.
// This function maps a string log level to the closest matching levels for both systems.
// More details on the log levels can be found in the LOGGING section of the ddb man page.
func strToLogLevels(level string) (logging.LogLevel, engine.LogLevel, error) {
	switch strings.ToUpper(level) {
	case "TRACE":
		return logging.LogLevelTrace, engine.LogLevelDbug, nil
	case "DEBUG", "DBUG":
		return logging.LogLevelDebug, engine.LogLevelDbug, nil
	case "INFO":
		return logging.LogLevelInfo, engine.LogLevelInfo, nil
	case "NOTE", "NOTICE":
		return logging.LogLevelNotice, engine.LogLevelNote, nil
	case "WARN":
		return logging.LogLevelNotice, engine.LogLevelWarn, nil
	case "ERROR", "ERR":
		return logging.LogLevelError, engine.LogLevelErr, nil
	case "CRIT":
		return logging.LogLevelError, engine.LogLevelCrit, nil
	case "ALRT":
		return logging.LogLevelError, engine.LogLevelAlrt, nil
	case "FATAL", "EMRG":
		return logging.LogLevelError, engine.LogLevelEmrg, nil
	case "EMIT":
		return logging.LogLevelError, engine.LogLevelEmit, nil
	default:
		return logging.LogLevelDisabled, engine.LogLevelUndefined, errors.Errorf("invalid log level %q", level)
	}
}

func newLogger(opts cliOptions) (*logging.LeveledLogger, error) {
	level := "ERR"
	if opts.Debug != "" {
		level = opts.Debug
	}
	cliLogLevel, engineLogLevel, err := strToLogLevels(level)
	if err != nil {
		return nil, errors.Wrap(err, "Error parsing log level")
	}

	consoleLog := logging.NewCommandLineLogger()
	consoleLog.WithLogLevel(cliLogLevel)

	setenvIfNotSet("D_LOG_MASK", engineLogLevel.String())
	setenvIfNotSet("DD_STDERR", "ERR")

	if opts.LogDir == "" {
		return consoleLog, nil
	}

	path := filepath.Clean(opts.LogDir)
	fi, err := os.Stat(path)
	if err != nil {
		return nil, errors.Wrapf(err, "Error accessing debug directory %q", path)
	}
	if !fi.IsDir() {
		return nil, errors.Errorf("Debug path %q is not a directory", path)
	}

	setenvIfNotSet("D_LOG_FILE", filepath.Join(path, "ddb-engine.log"))

	var fd *os.File
	fd, err = common.AppendFile(filepath.Join(path, "ddb-cli.log"))
	if err != nil {
		return nil, errors.Wrapf(err, "Error opening debug log file 'ddb-cli.log' in %q", path)
	}

	consoleLog.WithLogLevel(logging.LogLevelError)
	fileLog := logging.NewCombinedLogger("DDB", fd)
	fileLog.WithLogLevel(cliLogLevel)
	fileLog.WithErrorLogger(consoleLog)

	return fileLog, nil
}

func closePoolIfOpen(api DdbAPI, log *logging.LeveledLogger) {
	if !api.PoolIsOpen() {
		return
	}

	log.Info("Closing pool...\n")
	if err := api.Close(); err != nil {
		log.Errorf("Error closing pool: %s\n", err)
	}
}

func parseOpts(args []string, api DdbAPI) (cliOptions, *flags.Parser, error) {
	var opts cliOptions
	parser := flags.NewParser(&opts, flags.HelpFlag|flags.IgnoreUnknown)
	parser.Name = "ddb"
	parser.Usage = "[OPTIONS]"
	parser.ShortDescription = "daos debug tool"
	parser.LongDescription = ddbLongDescription

	if _, err := parser.ParseArgs(args); err != nil {
		if fe, ok := errors.Cause(err).(*flags.Error); ok && fe.Type == flags.ErrHelp {
			return opts, nil, printHelp(opts, fe.Error())
		}

		return opts, nil, err
	}

	if opts.Args.RunCmd != "" && opts.CmdFile != "" {
		return opts, nil, errors.New(runCmdArgsErr)
	}

	if opts.VosPath == "" && opts.SysdbPath != "" {
		return opts, nil, errors.New(vosPathMissErr)
	}

	return opts, parser, nil
}

func run(api DdbAPI, log *logging.LeveledLogger, opts cliOptions, parser *flags.Parser) error {
	cleanup, err := api.Init(log)
	if err != nil {
		return errors.Wrap(err, ctxInitErr)
	}
	defer cleanup()
	app := createGrumbleApp(api)

	if opts.VosPath != "" {
		// Commands that manage the pool open/close lifecycle themselves and must
		// not have the pool pre-opened by the CLI layer.
		noAutoOpen := []string{
			"feature",
			"open",
			"close",
			"prov_mem",
			"smd_sync",
			"rm_pool",
			"dev_list",
			"dev_replace",
		}
		if !slices.Contains(noAutoOpen, opts.Args.RunCmd) {
			log.Debugf("Connect to path: %s\n", opts.VosPath)
			if err := api.Open(string(opts.VosPath), string(opts.SysdbPath), opts.WriteMode); err != nil {
				return errors.Wrapf(err, vosPathOpenErr, opts.VosPath)
			}
			defer closePoolIfOpen(api, log)
		}
	}

	if opts.Args.RunCmd != "" || opts.CmdFile != "" {
		// Non-interactive mode
		if opts.Args.RunCmd != "" {
			err = runCmdStr(app, parser, string(opts.Args.RunCmd), opts.Args.RunCmdArgs...)
		} else {
			err = runFileCmds(app, log, opts.CmdFile)
		}
		if err != nil && err.Error() == grumbleUnknownCmdErr {
			return &unknownCmdError{cmd: opts.Args.RunCmd}
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
	closePoolIfOpen(api, log)
	return result
}

func main() {
	// Set the traceback level such that a crash results in
	// a coredump (when ulimit -c is set appropriately).
	debug.SetTraceback("crash")

	// Must be called before any write to stdout.
	if err := logging.DisableCStdoutBuffering(); err != nil {
		exitWithError(err)
	}

	ctx := &DdbContext{}
	opts, parser, err := parseOpts(os.Args[1:], ctx)
	if errors.Is(err, errHelpRequested) {
		return
	}
	if err != nil {
		exitWithError(err)
	}

	if opts.Version {
		fmt.Printf("ddb version %s\n", build.DaosVersion)
		return
	}

	log, err := newLogger(opts)
	if err != nil {
		exitWithError(errors.Wrap(err, loggerInitErr))
	}
	log.Debug("Logging facilities initialized")

	if err = run(ctx, log, opts, parser); err != nil {
		exitWithError(err)
	}
}

func createGrumbleApp(api DdbAPI) *grumble.App {
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

	addAppCommands(app, api)

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
func runCmdStr(app *grumble.App, p *flags.Parser, cmd string, args ...string) error {
	if p != nil {
		addManPageCommand(app, p)
	}

	return app.RunCommand(append([]string{cmd}, args...))
}
