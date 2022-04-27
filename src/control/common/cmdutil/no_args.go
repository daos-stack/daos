//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cmdutil

import (
	"strings"

	"github.com/pkg/errors"
)

type ArgsHandler interface {
	CheckArgs([]string) error
	HandleArgs([]string) error
}

var _ ArgsHandler = (*NoArgsCmd)(nil)

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
