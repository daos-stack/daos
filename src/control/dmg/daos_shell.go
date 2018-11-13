//
// (C) Copyright 2018 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/client/mgmt"

	"github.com/daos-stack/ishell"
)

var conns mgmtclient.Connections

// todo: implement shell commands for features other than discovery on
// multiple nodes

//func getUpdateParams(c *ishell.Context) (*pb.UpdateNvmeCtrlrParams, error) {
//	// disable the '>>>' for cleaner same line input.
//	c.ShowPrompt(false)
//	defer c.ShowPrompt(true) // revert after user input.
//
//	c.Print("Please enter firmware image file-path: ")
//	path := c.ReadLine()
//
//	c.Print("Please enter slot you would like to update [default 0]: ")
//	slotRaw := c.ReadLine()
//
//	var slot int32
//
//	if slotRaw != "" {
//		slot, err := strconv.Atoi(slotRaw)
//		if err != nil {
//			return nil, fmt.Errorf("%s is not a number", slotRaw)
//		}
//
//		if slot >= 0 && slot < 7 {
//			return nil, fmt.Errorf("%d needs to be a number between 0 and 7", slot)
//		}
//	}
//
//	return &pb.UpdateNvmeCtrlrParams{
//		Ctrlr: nil, Path: strings.TrimSpace(path), Slot: slot}, nil
//}

//func getFioConfig(c *ishell.Context) (configPath string, err error) {
//	// fetch existing configuration files
//	paths, err := mgmtClient.FetchFioConfigPaths()
//	if err != nil {
//		return
//	}
//	// cut prefix to display filenames not full path
//	configChoices := functional.Map(
//		paths, func(p string) string { return filepath.Base(p) })
//	// add custom path option
//	configChoices = append(configChoices, "custom path")
//
//	choiceIdx := c.MultiChoice(
//		configChoices,
//		"Select the fio configuration you would like to run on the selected controllers.")
//
//	// if custom path selected (last index), process input
//	if choiceIdx == len(configChoices)-1 {
//		// disable the '>>>' for cleaner same line input.
//		c.ShowPrompt(false)
//		defer c.ShowPrompt(true) // revert after user input.
//
//		c.Print("Please enter fio configuration file-path [has file extension .fio]: ")
//		path := c.ReadLine()
//
//		if path == "" {
//			return "", fmt.Errorf("no filepath provided")
//		}
//		if filepath.Ext(path) != ".fio" {
//			return "", fmt.Errorf("provided filepath does not end in .fio")
//		}
//		if !filepath.IsAbs(path) {
//			return "", fmt.Errorf("provided filepath is not absolute")
//		}
//
//		configPath = path
//	} else {
//		configPath = paths[choiceIdx]
//	}
//
//	return
//}

//func nvmeTaskLookup(
//	c *ishell.Context, ctrlrs []*pb.NvmeController, feature string) error {
//
//	switch feature {
//	case "nvme-fw-update":
//		params, err := getUpdateParams(c)
//		if err != nil {
//			c.Println("Problem reading user inputs: ", err.Error())
//			return err
//		}
//
//		for _, ctrlr := range ctrlrs {
//			c.Printf("\nController: %+v\n", ctrlr)
//			c.Printf(
//				"\t- Updating firmware on slot %d with image %s.\n",
//				params.Slot, params.Path)
//
//			params.Ctrlr = ctrlr
//
//			newFwrev, err := mgmtClient.UpdateNvmeCtrlr(params)
//			if err != nil {
//				c.Println("\nProblem updating firmware: ", err)
//				return err
//			}
//			c.Printf(
//				"\nSuccessfully updated firmware from revision %s to %s!\n",
//				params.Ctrlr.Fwrev, newFwrev)
//		}
//	case "nvme-burn-in":
//		configPath, err := getFioConfig(c)
//		if err != nil {
//			c.Println("Problem reading user inputs: ", err.Error())
//			return err
//		}
//
//		for _, ctrlr := range ctrlrs {
//			c.Printf("\nController: %+v\n", ctrlr)
//			c.Printf(
//				"\t- Running burn-in validation with spdk fio plugin using job file %s.\n\n",
//				filepath.Base(configPath))
//			_, err := mgmtClient.BurnInNvme(ctrlr.Id, configPath)
//			if err != nil {
//				return err
//			}
//		}
//	default:
//		c.Printf("Sorry, task '%s' has not been implemented.\n", feature)
//	}
//
//	return nil
//

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

	// todo: implement shell commands for features other than discovery on
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
	//				c.Println("Unable to retrieve nvme features", err.Error())
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
	//				c.Println("Problem running task: ", err.Error())
	//			}
	//		},
	//	})

	return shell
}

func main() {
	// by default, shell includes 'exit', 'help' and 'clear' commands.
	conns = mgmtclient.NewConnections()
	shell := setupShell()

	shell.Println("DAOS Management Shell")

	shell.Run()
}
