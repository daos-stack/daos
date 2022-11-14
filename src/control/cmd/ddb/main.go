//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

/*
 #cgo CFLAGS: -I${SRCDIR}/../../../ddb/
 #cgo LDFLAGS: -lddb -lgurt
 #include <ddb.h>
*/
import "C"
import (
	"bufio"
	"fmt"
	"github.com/desertbit/go-shlex"
	"github.com/desertbit/grumble"
	"github.com/jessevdk/go-flags"
	"os"
	"runtime"
)

type cliOptions struct {
	Run       string `long:"run_cmd" short:"R" description:"Execute the single command <cmd>, then exit"`
	File      string `long:"file_cmd" short:"f" description:"Path to a file container a list of ddb commands, one command per line, then exit."`
	WriteMode bool   `long:"write_mode" short:"w" description:"Open the vos file in write mode."`
}

func main() {
	// Must lock to OS thread because vos init/fini uses ABT init and finalize which must be called on the same thread
	runtime.LockOSThread()
	C.ddb_init()

	ctx := C.struct_ddb_ctx{}
	C.ddb_ctx_init(&ctx) // Initialize with ctx default values

	// There are two 'stages' to the cli interactivity. The first is the parsing of the command line arguments
	// and flags. The flags module is used to handle this stage.
	// The second 'stage' is the actual execution of the commands that ddb provides as well as the
	// interactive mode. A grumble App from the grumble package is used to help handle this stage.
	var opts cliOptions
	parser := createFlagsParser(&opts)
	app := createGrumbleApp(&ctx)

	rest, err := parser.ParseArgs(os.Args[1:])
	if err != nil {
		goto done
	}

	if len(rest) > 1 {
		fmt.Printf("Unknown argument: %s\n", rest[1])
		goto done
	}

	if len(opts.Run) > 0 && len(opts.File) > 0 {
		print("Cannot use both '-R' and '-f'.\n")
		goto done
	}

	if len(rest) == 1 {
		// Path to vos file was supplied. Open before moving on
		fmt.Printf("Connect to path: %s\n", rest[0])
		err := open_wrapper(&ctx, rest[0], opts.WriteMode)
		if err != nil {
			fmt.Printf("Error opening file '%s': %s\n", rest[0], err)
			goto done
		}
	}

	if len(opts.Run) > 0 {
		// '-R'
		err := runCmdStr(app, opts.Run)
		if err != nil {
			fmt.Printf("%s\n", err)
		}
		goto done
	} else if len(opts.File) > 0 {
		// '-f'
		file, err := os.Open(opts.File)
		if err != nil {
			fmt.Printf("Error opening file. Error: %s\n", err)
			goto done

		}
		fmt.Printf("Running commands in: %s\n", opts.File)
		scanner := bufio.NewScanner(file)
		for scanner.Scan() {
			cmd := scanner.Text()
			fmt.Printf("Running Command: %s\n", cmd)
			err := runCmdStr(app, cmd)
			if err != nil {
				fmt.Printf("Failed running command '%s'. Error: %s\n", cmd, err)
				goto done // don't continue if a command fails
			}
		}

		err = file.Close()
		if err != nil {
			fmt.Printf("Error closing file. Error: %s\n", err)
		}
		goto done
	}

	/* Run in interactive mode */
	// Print the version upon entry
	err = version_wrapper(&ctx)
	if err != nil {
		fmt.Printf("Version error: %s", err)
	}
	/* app.Run() uses the os.Args so need to clear them before running */
	os.Args = append(os.Args[:1])
	err = app.Run()

	if err != nil {
		fmt.Printf("error: %s\n", err)
	}
done:
	C.ddb_fini()
	runtime.UnlockOSThread()
}

func createGrumbleApp(ctx *C.struct_ddb_ctx) *grumble.App {
	var app = grumble.New(&grumble.Config{
		Name: "ddb",
		Description: `The DAOS Debug Tool (ddb) allows a user to navigate through and modify
a file in the VOS format. In order to modify the file, the '-w' option must
be included when opening the vos file.

Many of the commands take a vos tree path. The format for this path
is 'cont_uuid/obj_id/dkey/akey/recx'. The keys currently only support string
keys. The recx for array values is the format {lo-hi}. To make it easier to
navigate the tree, indexes can be used instead of the path part. The index
is in the format '[i]', for example '[0]/[0]/[0]'`,
		Flags:       nil,
		HistoryFile: "/tmp/ddb.hist",
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

func createFlagsParser(opts *cliOptions) *flags.Parser {
	p := flags.NewParser(opts, flags.Default)
	p.Name = "ddb"
	p.Usage = "[OPTIONS] [<vos_file_path>]"
	p.ShortDescription = "daos debug tool"
	p.LongDescription = `The DAOS Debug Tool (ddb) allows a user to navigate through and modify
a file in the VOS format. It offers both a command line and interactive
shell mode. If the '-R' or '-f' options are not provided, then it will
run in interactive mode. In order to modify the file, the '-w' option
must be included. The optional <vos_file_path> will be opened before running
commands supplied by '-R' or '-f' or entering interactive mode.`
	return p
}

// Run the command in 'run' using the grumble app. shlex is used to parse the string into an argv/c format
func runCmdStr(app *grumble.App, run string) error {
	args, err := shlex.Split(run, true)

	if err != nil {
		fmt.Printf("Error parsing run command '%s'\n", run)
		return err
	}
	return app.RunCommand(args)
}
