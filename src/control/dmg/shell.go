//
// (C) Copyright 2019 Intel Corporation.
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
	"fmt"
	"strconv"
	"strings"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/ishell"
	"github.com/pkg/errors"
)

func getUpdateStorageReq(c *ishell.Context) (*pb.UpdateStorageReq, error) {
	// disable the '>>>' for cleaner same line input.
	c.ShowPrompt(false)
	defer c.ShowPrompt(true) // revert after user input.

	c.Print("Please enter NVMe controller model name to be updated: ")
	model := c.ReadLine()

	c.Print("Please enter starting firmware revision, only controllers " +
		"currently at this revision will be updated: ")
	startRev := c.ReadLine()

	c.Print("Please enter firmware image file-path: ")
	path := c.ReadLine()

	c.Print("Please enter slot you would like to update [default 0]: ")
	slotRaw := c.ReadLine()

	var slot int32

	if slotRaw != "" {
		slot, err := strconv.Atoi(slotRaw)
		if err != nil {
			return nil, fmt.Errorf("%s is not a number", slotRaw)
		}

		if slot < 0 || slot > 7 {
			return nil, fmt.Errorf(
				"%d needs to be a number between 0 and 7", slot)
		}
	}

	// only populate nvme fwupdate params for the moment
	return &pb.UpdateStorageReq{
		Nvme: &pb.UpdateNvmeReq{
			Model:    strings.TrimSpace(model),
			Startrev: strings.TrimSpace(startRev),
			Path:     strings.TrimSpace(path), Slot: slot,
		},
	}, nil
}

func getKillRankParams(c *ishell.Context) (pool string, rank int, err error) {
	// disable the '>>>' for cleaner same line input.
	c.ShowPrompt(false)
	defer c.ShowPrompt(true) // revert after command.

	c.Print("Pool uuid: ")
	pool = c.ReadLine()

	c.Print("Rank: ")
	rankIn := c.ReadLine()

	rank, err = strconv.Atoi(rankIn)
	if err != nil {
		err = errors.New("bad rank")
	}

	return
}

func setupShell() *ishell.Shell {
	shell := ishell.New()

	shell.AddCmd(&ishell.Cmd{
		Name: "addconns",
		Help: "Create connections to servers by supplying a space " +
			"separated list of addresses <ipv4addr/hostname:port>",
		Func: func(c *ishell.Context) {
			if len(c.Args) < 1 {
				c.Println(c.HelpText())
				return
			}
			c.Println(sprintConns(conns.ConnectClients(c.Args)))
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "clearconns",
		Help: "Command to clear stored server connections",
		Func: func(c *ishell.Context) {
			conns.ClearConns()
			c.Println(sprintConns(conns.GetActiveConns(nil)))
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "getconns",
		Help: "Command to list active server connections",
		Func: func(c *ishell.Context) {
			c.Println(sprintConns(conns.GetActiveConns(nil)))
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "listfeatures",
		Help: "Command to retrieve supported management features on " +
			"connected servers",
		Func: func(c *ishell.Context) {
			_, out := hasConns(conns.GetActiveConns(nil))
			c.Println(out)
			c.Printf("management features: %s", conns.ListFeatures())
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "scanstorage",
		Help: "Command to scan NVMe SSD controllers " +
			"and SCM modules on DAOS storage servers",
		Func: func(c *ishell.Context) {
			_, out := hasConns(conns.GetActiveConns(nil))
			c.Println(out)

			scanStor()
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "formatstorage",
		Help: "Command to format NVMe SSD controllers " +
			"and SCM modules on DAOS storage servers",
		Func: func(c *ishell.Context) {
			_, out := hasConns(conns.GetActiveConns(nil))
			c.Println(out)

			formatStor(false)
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "updatestorage",
		Help: "Command to update firmware on NVMe SSD controllers " +
			"and SCM modules on DAOS storage servers",
		Func: func(c *ishell.Context) {
			_, out := hasConns(conns.GetActiveConns(nil))
			c.Println(out)

			req, err := getUpdateStorageReq(c)
			if err != nil {
				c.Println(err)
			}

			updateStor(req, false)
		},
	})

	// TODO: operations requiring access to management service will
	//       need to connect to the first access point rather than
	//       host list, perhaps clear then ConnectClients ap[0]
	shell.AddCmd(&ishell.Cmd{
		Name: "killrank",
		Help: "Command to terminate server running as specific rank " +
			"on a DAOS pool",
		Func: func(c *ishell.Context) {
			_, out := hasConns(conns.GetActiveConns(nil))
			c.Println(out)

			poolUUID, rank, err := getKillRankParams(c)
			if err != nil {
				c.Println(err)
			}

			killRankSvc(poolUUID, uint32(rank))
		},
	})

	return shell
}
