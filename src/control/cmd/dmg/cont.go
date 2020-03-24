//
// (C) Copyright 2020 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/client"
)

// ContCmd is the struct representing the top-level container subcommand.
type ContCmd struct {
	SetOwner ContSetOwnerCmd `command:"set-owner" description:"Change the owner for a DAOS container"`
}

// ContSetOwnerCmd is the struct representing the command to change the owner of a DAOS container.
type ContSetOwnerCmd struct {
	logCmd
	connectedCmd
	GroupName string `short:"g" long:"group" description:"New owner-group for the container, format name@domain"`
	UserName  string `short:"u" long:"user" description:"New owner-user for the container, format name@domain"`
	ContUUID  string `short:"c" long:"cont" required:"1" description:"UUID of the DAOS container"`
	PoolUUID  string `short:"p" long:"pool" required:"1" description:"UUID of the DAOS pool for the container"`
}

// Execute runs the container set-owner command
func (c *ContSetOwnerCmd) Execute(args []string) error {
	msg := "SUCCEEDED"
	req := client.ContSetOwnerReq{
		ContUUID: c.ContUUID,
		PoolUUID: c.PoolUUID,
		User:     c.UserName,
		Group:    c.GroupName,
	}

	err := c.conns.ContSetOwner(req)
	if err != nil {
		msg = errors.WithMessage(err, "FAILED").Error()
	}

	c.log.Infof("Container-set-owner command %s\n", msg)

	return err
}
