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
		Name: "listmgmtfeatures",
		Help: "Command to retrieve all supported management features from any client connections",
		Func: func(c *ishell.Context) {
			c.Println(hasConnections(conns.GetActiveConns(nil)))
			c.Printf(checkAndFormat(conns.ListFeatures()), "management feature")
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "listnvmecontrollers",
		Help: "Command to list NVMe SSD controllers",
		Func: func(c *ishell.Context) {
			c.Println(hasConnections(conns.GetActiveConns(nil)))
			c.Printf(
				checkAndFormat(conns.ListNvme()),
				"NVMe SSD controller and constituent namespace")
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "listscmmodules",
		Help: "Command to list installed SCM modules",
		Func: func(c *ishell.Context) {
			c.Println(hasConnections(conns.GetActiveConns(nil)))
			c.Printf(checkAndFormat(conns.ListScm()), "SCM module")
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "killrank",
		Help: "Command to terminate server running as specific rank on a DAOS pool",
		Func: func(c *ishell.Context) {
			if len(c.Args) != 2 {
				c.Println(c.HelpText())
				return
			}
			c.Println(hasConnections(conns.GetActiveConns(nil)))
			rank, err := strconv.Atoi(c.Args[1])
			if err != nil {
				c.Println("bad rank")
				return
			}
			c.Printf(
				"Kill Rank returned: %s\n",
				conns.KillRank(c.Args[0], uint32(rank)))
		},
	})

	// todo: implement shell commands for feature other than discovery on
	// multiple nodes

	//			// record strings that make up the option list
	//			cStrs := make([]string, len(cs))
	//			for i, v := range cs {
	//				cStrs[i] = fmt.Sprintf("[%d] %+v", i, v)
	//			}
	//
	//			ctrlrIdxs := c.Checklist(
	//				cStrs,
	//				"Select the controllers you want to run tasks on.",
	//				nil)
	//			if len(ctrlrIdxs) == 0 {
	//				c.Println("No controllers selected!")
	//				return
	//			}
	//
	//			// filter list of selected controllers to act on
	//			var ctrlrs []*pb.NvmeController
	//			for i, ctrlr := range cs {
	//				for j := range ctrlrIdxs {
	//					if i == j {
	//						ctrlrs = append(ctrlrs, ctrlr)
	//					}
	//				}
	//			}
	//
	//			featureMap, err := mgmtClient.ListFeatures("nvme")
	//			if err != nil {
	//				c.Println("Unable to retrieve nvme features", err)
	//				return
	//			}
	//
	//			taskStrs := make([]string, len(featureMap))
	//			taskHandlers := make([]string, len(featureMap))
	//			i := 0
	//			for k, v := range featureMap {
	//				taskStrs[i] = fmt.Sprintf("[%d] %s - %s", i, k, v)
	//				taskHandlers[i] = k
	//				i++
	//			}
	//
	//			taskIdx := c.MultiChoice(
	//				taskStrs,
	//				"Select the task you would like to run on the selected controllers.")
	//
	//			c.Printf(
	//				"\nRunning task %s on the following controllers:\n",
	//				taskHandlers[taskIdx])
	//			for _, ctrlr := range ctrlrs {
	//				c.Printf("\t- %+v\n", ctrlr)
	//			}
	//			c.Println("")
	//
	//			if err := nvmeTaskLookup(c, ctrlrs, taskHandlers[taskIdx]); err != nil {
	//				c.Println("Problem running task: ", err)
	//			}
	//		},
	//	})

	return shell
}
