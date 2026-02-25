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
	"path"
	"path/filepath"
	"runtime/debug"
	"strings"
	"unsafe"

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

/*
 #include <stdlib.h>
*/
import "C"

func exitWithError(err error) {
	cmdName := path.Base(os.Args[0])
	fmt.Fprintf(os.Stderr, "ERROR: %s: %v\n", cmdName, err)
	if fault.HasResolution(err) {
		fmt.Fprintf(os.Stderr, "ERROR: %s: %s", cmdName, fault.ShowResolutionFor(err))
	}
	os.Exit(1)
}

type cliOptions struct {
	WriteMode bool   `long:"write_mode" short:"w" description:"Open the vos file in write mode."`
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
Available commands:

`

const helpTreePath = `
Path

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
the command‑specific help for details.`

const grumbleUnknownCmdErr = "unknown command, try 'help'"

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
// Returns false in case the opts.Args.RunCmd is unknown.
func printCmdHelp(app *grumble.App, opts *cliOptions) bool {
	err := runCmdStr(app, nil, string(opts.Args.RunCmd), "--help")
	if err != nil {
		if err.Error() == grumbleUnknownCmdErr {
			fmt.Fprintf(os.Stderr, "ERROR: Unknown command '%s'", string(opts.Args.RunCmd))
			printCommands(os.Stderr, app)
		} else {
			fmt.Fprintf(os.Stderr, "ERROR: %s", err.Error())
		}
		return false
	}
	return true
}

// Prints either general or command-specific help message.
// Returns a reasonable return code in case the caller chooses to terminate the process.
func printHelp(generalMsg string, opts *cliOptions) int {
	// ctx is not necessary since this instance of the app is not intended to run any of the commands
	app := createGrumbleApp(nil)

	if string(opts.Args.RunCmd) == "" {
		printGeneralHelp(app, generalMsg)
		return 0
	}

	if printCmdHelp(app, opts) {
		return 0
	} else {
		return 1
	}
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

func newLogger(opts *cliOptions) (*logging.LeveledLogger, error) {
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

func parseOpts(args []string, opts *cliOptions) error {
	p := flags.NewParser(opts, flags.HelpFlag|flags.IgnoreUnknown)
	p.Name = "ddb"
	p.Usage = "[OPTIONS]"
	p.ShortDescription = "daos debug tool"
	p.LongDescription = ddbLongDescription

	// Set the traceback level such that a crash results in
	// a coredump (when ulimit -c is set appropriately).
	debug.SetTraceback("crash")

	if _, err := p.ParseArgs(args); err != nil {
		if fe, ok := errors.Cause(err).(*flags.Error); ok && fe.Type == flags.ErrHelp {
			os.Exit(printHelp(fe.Error(), opts))
		}

		return err
	}

	if opts.Version {
		opts.Args.RunCmd = "version"
		opts.Args.RunCmdArgs = []string{}
		opts.CmdFile = ""
	}

	if opts.Args.RunCmd != "" && opts.CmdFile != "" {
		return errors.New("Cannot use both command file and a command string")
	}

	log, err := newLogger(opts)
	if err != nil {
		return errors.Wrap(err, "Error configuring logging")
	}
	log.Debug("Logging facilities initialized")

	var (
		ctx     *DdbContext
		cleanup func()
	)
	if ctx, cleanup, err = InitDdb(log); err != nil {
		return errors.Wrap(err, "Error initializing the DDB Context")
	}
	defer cleanup()
	app := createGrumbleApp(ctx)

	if opts.SysdbPath != "" {
		ctx.ctx.dc_db_path = C.CString(string(opts.SysdbPath))
		defer C.free(unsafe.Pointer(ctx.ctx.dc_db_path))
	}

	if opts.VosPath != "" {
		ctx.ctx.dc_pool_path = C.CString(string(opts.VosPath))
		defer C.free(unsafe.Pointer(ctx.ctx.dc_pool_path))

		if !strings.HasPrefix(string(opts.Args.RunCmd), "feature") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "open") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "close") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "prov_mem") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "smd_sync") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "rm_pool") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "dev_list") &&
			!strings.HasPrefix(string(opts.Args.RunCmd), "dev_replace") {
			log.Debugf("Connect to path: %s\n", opts.VosPath)
			if err := ddbOpen(ctx, string(opts.VosPath), bool(opts.WriteMode)); err != nil {
				return errors.Wrapf(err, "Error opening path: %s", opts.VosPath)
			}
		}
	}

	if opts.Args.RunCmd != "" || opts.CmdFile != "" {
		// Non-interactive mode
		if opts.Args.RunCmd != "" {
			err := runCmdStr(app, p, string(opts.Args.RunCmd), opts.Args.RunCmdArgs...)
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

	if err := parseOpts(os.Args[1:], &opts); err != nil {
		exitWithError(err)
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

const manMacroSection = `.\" Miscellaneous Helper macros
.de Sp \" Vertical space (when we can't use .PP)
.if t .sp .5v
.if n .sp
..
.de Vb \" Begin verbatim text
.ft CW
.nf
.ne \\$1
..
.de Ve \" End verbatim text
.ft R
.fi
..
.\" ========================================================================
.\"`

const manArgsHeader = `.SH ARGUMENTS
.SS Application Arguments`

const manCmdsHeader = `.SH COMMANDS
.SS Available Commands`

const manPathSection = `.SH PATH
.SS VOS Tree Path
Many of the commands take a VOS tree path. The format for this path is [cont]/[obj]/[dkey]/[akey]/[extent].
.TP
.B cont
The full container uuid.
.TP
.B obj
The object id.
.TP
.B keys (akey, dkey)
There are multiple types of keys:
.RS
.IP "*" 4
.B string keys
are simply the string value. If the size of the key is greater than strlen(key), then
the size is included at the end of the string value. Example: 'akey{5}' is the key: akey with a null
terminator at the end.
.IP "*" 4
.B number keys
are formatted as '{[type]: NNN}' where type is 'uint8, uint16, uint32, or uint64'. NNN
can be a decimal or hex number. Example: '{uint32: 123456}'
.IP "*" 4
.B binary keys
are formatted as '{bin: 0xHHH}' where HHH is the hex representation of the binary key.
Example: '{bin: 0x1a2b}'
.RE
.TP
.B extent
For array values in the format {lo-hi}.
.SS Index Tree Path
.RE
To make it easier to navigate the tree, indexes can be used instead of the path part. The index is
in the format [i]. Indexes and actual path values can be used together.
.SS Path Examples
VOS tree path examples:
.Sp
.Vb 1
\&        /3550f5df-e6b1-4415-947e-82e15cf769af/939000573846355970.0.13.1/dkey/akey/[0-1023]
.Ve
.Sp
Index tree path examples:
.Sp
.Vb 1
\&        [0]/[1]/[2]/[1]/[9]
.Ve
.Sp
Mixed tree path examples:
.Sp
.Vb 1
\&        /[0]/939000573846355970.0.13.1/[2]/akey{5}/[0-1023]
.Ve
.Sp`

const manLoggingSection = `.SH LOGGING
The golang cli and the C engine use separate logging systems with different log levels.
The \fI--debug=<log level>\fR option sets the log level for both systems to the closest matching
levels.  The available log levels supported by this option are: \fBTRACE\fR, \fBDEBUG\fR (or
\fBDBG\fR), \fBINFO\fR, \fBNOTICE\fR (or \fBNOTE\fR), \fBWARN\fR, \fBERROR\fR (or \fBERR\fR),
\fBCRIT\fR, \fBALRT\fR, \fBFATAL\fR (or \fBEMRG\fr), and \fBEMIT\fR.  The default log level is
\fBERROR\fR.

To not pollute the console output, the logs can be redirected to a file using the
\fI--log_dir=<path>\fR option.  However, \fBERROR\fR log messages or above will still be printed to
the console regardless if the \fI--log_dir=<path>\fR option is used or not.`

func fprintManPage(dest io.Writer, app *grumble.App, parser *flags.Parser) {
	fmt.Fprintln(dest, manMacroSection)

	parser.WriteManPage(dest)

	fmt.Fprintln(dest, manArgsHeader)
	for _, arg := range parser.Args() {
		fmt.Fprintf(dest, ".TP\n.B %s\n%s\n", arg.Name, arg.Description)
	}

	fmt.Fprintln(dest, manCmdsHeader)
	for _, cmd := range app.Commands().All() {
		if cmd.Name == "manpage" {
			continue
		}

		var cmdHelp string
		if cmd.LongHelp != "" {
			cmdHelp = cmd.LongHelp
		} else {
			cmdHelp = cmd.Help
		}
		fmt.Fprintf(dest, ".TP\n.B %s\n%s\n", cmd.Name, cmdHelp)
	}

	fmt.Fprintln(dest, manPathSection)

	fmt.Fprint(dest, manLoggingSection)
}

// Run the command in 'run' using the grumble app. shlex is used to parse the string into an argv/c format
func runCmdStr(app *grumble.App, p *flags.Parser, cmd string, args ...string) error {
	if p != nil {
		app.AddCommand(&grumble.Command{
			Name:      "manpage",
			Help:      "Generate an application man page in groff format.",
			LongHelp:  "Generate an application man page in groff format. This command is used internally to generate the man page for the application and is not intended for general use.",
			HelpGroup: "",
			Flags: func(a *grumble.Flags) {
				a.String("o", "output", "", "Output file for the man page. If not provided, the man page will be printed to stdout.")
			},
			Run: func(c *grumble.Context) error {
				dest := os.Stdout
				if c.Flags.String("output") != "" {
					fd, err := os.Create(c.Flags.String("output"))
					if err != nil {
						return errors.Wrapf(err, "Error creating file %q", c.Flags.String("output"))
					}
					defer func() {
						err = fd.Close()
						if err != nil {
							fmt.Fprintf(os.Stderr, "Error closing file %q: %s\n", c.Flags.String("output"), err)
						}
					}()
					dest = fd
				}

				fprintManPage(dest, app, p)
				return nil
			},
			Completer: nil,
		})
	}

	return app.RunCommand(append([]string{cmd}, args...))
}
