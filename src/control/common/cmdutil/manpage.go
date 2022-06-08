//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cmdutil

import (
	"io"
	"os"
)

// ManPageWriter defines an interface to be implemented
// by commands that write a man page.
type ManPageWriter interface {
	SetWriteFunc(func(io.Writer))
}

// ManCmd defines a go-flags subcommand handler for generating
// a manpage.
type ManCmd struct {
	writeFn func(io.Writer)
	Output  string `long:"output" short:"o" description:"output file"`
}

func (cmd *ManCmd) SetWriteFunc(fn func(io.Writer)) {
	cmd.writeFn = fn
}

func (cmd *ManCmd) Execute(_ []string) error {
	output := os.Stdout
	if cmd.Output != "" {
		var err error
		output, err = os.Create(cmd.Output)
		if err != nil {
			return err
		}
	}

	cmd.writeFn(output)
	return nil
}
