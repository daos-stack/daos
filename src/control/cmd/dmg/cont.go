//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

// ContID is a container label or UUID.
type ContID struct {
	ui.LabelOrUUIDFlag
}

// contCmd is the base struct for all container commands.
type contCmd struct {
	poolCmd

	Args struct {
		Cont ContID `positional-arg-name:"<container label or UUID>" required:"1"`
	} `positional-args:"yes"`
}

// ContCmd is the struct representing the top-level container subcommand.
type ContCmd struct {
	SetOwner ContSetOwnerCmd `command:"set-owner" description:"Change the owner for a DAOS container"`
}

// ContSetOwnerCmd is the struct representing the command to change the owner of a DAOS container.
type ContSetOwnerCmd struct {
	contCmd
	GroupName ui.ACLPrincipalFlag `short:"g" long:"group" description:"New owner-group for the container, format name@domain"`
	UserName  ui.ACLPrincipalFlag `short:"u" long:"user" description:"New owner-user for the container, format name@domain"`
}

// Execute runs the container set-owner command
func (cmd *ContSetOwnerCmd) Execute(args []string) error {
	if cmd.GroupName == "" && cmd.UserName == "" {
		return &flags.Error{
			Type:    flags.ErrRequired,
			Message: "at least one of `--user' or `--group' must be supplied",
		}
	}
	msg := "SUCCEEDED"
	req := &control.ContSetOwnerReq{
		ContID: cmd.Args.Cont.String(),
		PoolID: cmd.poolCmd.Args.Pool.String(),
		User:   cmd.UserName.String(),
		Group:  cmd.GroupName.String(),
	}

	ctx := cmd.MustLogCtx()
	err := control.ContSetOwner(ctx, cmd.ctlInvoker, req)
	if err != nil {
		msg = errors.WithMessage(err, "FAILED").Error()
	}

	if cmd.JSONOutputEnabled() {
		return cmd.OutputJSON(nil, err)
	}

	cmd.Infof("Container-set-owner command %s\n", msg)

	return err
}
