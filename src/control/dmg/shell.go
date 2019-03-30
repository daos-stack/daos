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
	"strconv"

	"github.com/daos-stack/ishell"
)

func setupShell() *ishell.Shell {
	shell := ishell.New()

	shell.AddCmd(&ishell.Cmd{
		Name: "addconns",
		Help: "Command to create connections to servers by supplying a space separated list of addresses <ipv4addr/hostname:port>",
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
		Help: "Command to retrieve supported management features on connected servers",
		Func: func(c *ishell.Context) {
			c.Println(hasConns(conns.GetActiveConns(nil)))
			c.Printf(unpackFormat(conns.ListFeatures()), "management feature")
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "liststorage",
		Help: "Command to list locally-attached NVMe SSD controllers and SCM modules",
		Func: func(c *ishell.Context) {
			c.Println(hasConns(conns.GetActiveConns(nil)))

			listStor()
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "killrank",
		Help: "Command to terminate server running as specific rank on a DAOS pool",
		Func: func(c *ishell.Context) {
			// disable the '>>>' for cleaner same line input.
			c.ShowPrompt(false)
			defer c.ShowPrompt(true) // revert after command.

			c.Print("Pool uuid: ")
			poolUUID := c.ReadLine()

			c.Print("Rank: ")
			rankIn := c.ReadLine()

			c.Println(hasConns(conns.GetActiveConns(nil)))

			rank, err := strconv.Atoi(rankIn)
			if err != nil {
				c.Println("bad rank")
				return
			}

			killRankSvc(poolUUID, uint32(rank))
		},
	})

	return shell
}
