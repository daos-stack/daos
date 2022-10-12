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
	Run       string    `long:"run_cmd" short:"R" description:"Execute the single command <cmd>, then exit"`
	File      string    `long:"file_cmd" short:"f" description:"Path to a file container a list of ddb commands, one command per line, then exit."`
	WriteMode bool      `long:"write_mode" short:"w" description:"Open the vos file in write mode."`
}

func main() {
	// Must lock to OS thread because vos init/fini uses ABT init and finalize which must be called on the same thread
	runtime.LockOSThread()
	C.ddb_init()

	ctx := C.struct_ddb_ctx{}
	C.ddb_ctx_init(&ctx) // Initialize with ctx default values

	app := buildApp(&ctx)

	var opts cliOptions
	p := flags.NewParser(&opts, flags.Default)
	p.Name = "ddb"
	p.Usage = "[OPTIONS] [<vos_file_path>]"
	p.ShortDescription = "daos debug tool"
	p.LongDescription = `The DAOS Debug Tool (ddb) allows a user to navigate through and modify
a file in the VOS format. It offers both a command line and interactive
shell mode. If the '-R' or '-f' options are not provided, then it will
run in interactive mode. In order to modify the file, the '-w' option
must be included. The optional <vos_file_path> will be opened before running
commands supplied by '-R' or '-f' or entering interactive mode.`
	rest, err := p.ParseArgs(os.Args[1:])
	if err != nil {
		return
	}
	ctx.dc_write_mode = C.bool(opts.WriteMode)

	if len(rest) > 1 {
		fmt.Printf("Unknown argument: %s\n", rest[1])
		return
	}

	if len(opts.Run) > 0 && len(opts.File) > 0 {
		print("Cannot use both '-R' and '-f'.\n")
		return
	}

	if len(rest) == 1 {
		// Path to vos file was supplied. Open before moving on
		fmt.Printf("Connect to path: %s\n", rest[0])
		err := open_wrapper(&ctx, rest[0], false)
		if err != nil {
			fmt.Printf("Error opening file '%s': %s\n", rest[0], err)
			return
		}
	}

	if len(opts.Run) > 0 {
		err := runCmdStr(app, opts.Run)
		if err != nil {
			fmt.Printf("%s\n", err)
		}
		return
	} else if len(opts.File) > 0 {
		file, err := os.Open(opts.File)
		if err != nil {
			fmt.Printf("Error opening file. Error: %s\n", err)
			return
		}
		fmt.Printf("Running commands in: %s\n", opts.File)
		scanner := bufio.NewScanner(file)
		for scanner.Scan() {
			cmd := scanner.Text()
			fmt.Printf("Running Command: %s\n", cmd)
			err := runCmdStr(app, cmd)
			if err != nil {
				fmt.Printf("Failed running command '%s'. Error: %s\n", cmd, err)
				return // don't continue if a command fails
			}
		}

		err = file.Close()
		if err != nil {
			fmt.Printf("Error closing file. Error: %s\n", err)
		}
		return
	}

	/* Run in interactive mode */
	/* app.Run() uses the os.Args so need to clear them before running */
	os.Args = append(os.Args[:1])
	err = app.Run()

	if err != nil {
		fmt.Printf("error: %s\n", err)
	}

	C.ddb_fini()
	runtime.UnlockOSThread()
}

func runCmdStr(app *grumble.App, run string) error {
	args, err := shlex.Split(run, true)

	if err != nil {
		fmt.Printf("Error parsing run command '%s'\n", run)
		return err
	}
	return app.RunCommand(args)
}