//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package common

import (
	"io"
	"os"
	"strings"

	"github.com/pkg/errors"
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

type ArgsHandler interface {
	CheckArgs([]string) error
	HandleArgs([]string) error
}

// NoArgsCmd defines an embeddable struct that can be used to
// implement a command that does not take any arguments.
type NoArgsCmd struct{}

func (cmd *NoArgsCmd) CheckArgs(args []string) error {
	if len(args) != 0 {
		return errors.Errorf("unexpected arguments: %s", strings.Join(args, " "))
	}
	return nil
}

func (cmd *NoArgsCmd) HandleArgs(args []string) error {
	return errors.New("this command does not accept additional arguments")
}
