//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"

	"github.com/jessevdk/go-flags"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/cmdutil"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/ui"
)

// ContCmd is the struct representing the top-level container subcommand.
type ContCmd struct {
	SetOwner ContSetOwnerCmd `command:"set-owner" description:"Change the owner for a DAOS container"`
}

// ContSetOwnerCmd is the struct representing the command to change the owner of a DAOS container.
type ContSetOwnerCmd struct {
	baseCmd
	ctlInvokerCmd
	cmdutil.JSONOutputCmd
	GroupName ui.ACLPrincipalFlag `short:"g" long:"group" description:"New owner-group for the container, format name@domain"`
	UserName  ui.ACLPrincipalFlag `short:"u" long:"user" description:"New owner-user for the container, format name@domain"`
	ContUUID  string              `short:"c" long:"cont" required:"1" description:"UUID of the DAOS container"`
	PoolUUID  string              `short:"p" long:"pool" required:"1" description:"UUID of the DAOS pool for the container"`
}

// Execute runs the container set-owner command
func (c *ContSetOwnerCmd) Execute(args []string) error {
	if c.GroupName == "" && c.UserName == "" {
		return &flags.Error{
			Type:    flags.ErrRequired,
			Message: "at least one of `--user' or `--group' must be supplied",
		}
	}
	msg := "SUCCEEDED"
	req := &control.ContSetOwnerReq{
		ContUUID: c.ContUUID,
		PoolUUID: c.PoolUUID,
		User:     c.UserName.String(),
		Group:    c.GroupName.String(),
	}

	ctx := context.Background()
	err := control.ContSetOwner(ctx, c.ctlInvoker, req)
	if err != nil {
		msg = errors.WithMessage(err, "FAILED").Error()
	}

	if c.JSONOutputEnabled() {
		return c.OutputJSON(nil, err)
	}

	c.Infof("Container-set-owner command %s\n", msg)

	return err
}
