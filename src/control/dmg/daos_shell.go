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
	"fmt"
	"path/filepath"
	"strconv"
	"strings"

	"client/agent"
	"client/mgmt"
	"utils/functional"
	"security"

	pb "mgmt/proto"

	"github.com/daos-stack/ishell"
	"github.com/jessevdk/go-flags"
	"github.com/satori/go.uuid"
)

var (
	agentClient   *agent.DAOSAgentClient
	controlClient *control.DAOSMgmtClient
)

func getUpdateParams(c *ishell.Context) (*pb.UpdateNVMeCtrlrParams, error) {
	// disable the '>>>' for cleaner same line input.
	c.ShowPrompt(false)
	defer c.ShowPrompt(true) // revert after user input.

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

		if slot >= 0 && slot < 7 {
			return nil, fmt.Errorf("%d needs to be a number between 0 and 7", slot)
		}
	}

	return &pb.UpdateNVMeCtrlrParams{
		Ctrlr: nil, Path: strings.TrimSpace(path), Slot: slot}, nil
}

func getFioConfig(c *ishell.Context) (configPath string, err error) {
	// fetch existing configuration files
	paths, err := controlClient.FetchFioConfigPaths()
	if err != nil {
		return
	}
	// cut prefix to display filenames not full path
	configChoices := functional.Map(
		paths, func(p string) string { return filepath.Base(p) })
	// add custom path option
	configChoices = append(configChoices, "custom path")

	choiceIdx := c.MultiChoice(
		configChoices,
		"Select the fio configuration you would like to run on the selected controllers.")

	// if custom path selected (last index), process input
	if choiceIdx == len(configChoices)-1 {
		// disable the '>>>' for cleaner same line input.
		c.ShowPrompt(false)
		defer c.ShowPrompt(true) // revert after user input.

		c.Print("Please enter fio configuration file-path [has file extension .fio]: ")
		path := c.ReadLine()

		if path == "" {
			return "", fmt.Errorf("no filepath provided")
		}
		if filepath.Ext(path) != ".fio" {
			return "", fmt.Errorf("provided filepath does not end in .fio")
		}
		if !filepath.IsAbs(path) {
			return "", fmt.Errorf("provided filepath is not absolute")
		}

		configPath = path
	} else {
		configPath = paths[choiceIdx]
	}

	return
}

func nvmeTaskLookup(
	c *ishell.Context, ctrlrs []*pb.NVMeController, feature string) error {

	switch feature {
	case "nvme-namespaces":
		for _, ctrlr := range ctrlrs {
			c.Printf("Controller: %+v\n", ctrlr)

			nss, err := controlClient.ListNVMeNss(ctrlr)
			if err != nil {
				c.Println("Problem retrieving namespaces: ", err.Error())
				return err
			}

			for _, ns := range nss {
				c.Printf(
					"\t- Namespace ID: %d, Capacity: %dGB\n", ns.Id, ns.Capacity)
			}
		}
	case "nvme-fw-update":
		params, err := getUpdateParams(c)
		if err != nil {
			c.Println("Problem reading user inputs: ", err.Error())
			return err
		}

		for _, ctrlr := range ctrlrs {
			c.Printf("\nController: %+v\n", ctrlr)
			c.Printf(
				"\t- Updating firmware on slot %d with image %s.\n",
				params.Slot, params.Path)

			params.Ctrlr = ctrlr

			newFwrev, err := controlClient.UpdateNVMeCtrlr(params)
			if err != nil {
				c.Println("\nProblem updating firmware: ", err)
				return err
			}
			c.Printf(
				"\nSuccessfully updated firmware from revision %s to %s!\n",
				params.Ctrlr.Fwrev, newFwrev)
		}
	case "nvme-burn-in":
		configPath, err := getFioConfig(c)
		if err != nil {
			c.Println("Problem reading user inputs: ", err.Error())
			return err
		}

		for _, ctrlr := range ctrlrs {
			c.Printf("\nController: %+v\n", ctrlr)
			c.Printf(
				"\t- Running burn-in validation with spdk fio plugin using job file %s.\n\n",
				filepath.Base(configPath))
			_, err := controlClient.BurnInNVMe(ctrlr.Id, configPath)
			if err != nil {
				return err
			}
		}
	default:
		c.Printf("Sorry, task '%s' has not been implemented.\n", feature)
	}

	return nil
}

func setupShell() *ishell.Shell {
	shell := ishell.New()

	// Struct definition for arguments processed for the connect command
	// go-flags uses struct decorators and introspection to handle this
	//	Local: determine if this is a connection to a local socket for accessing the agent.
	//	Host: The address of the target to connect to. If Local is true it is just the file path.
	var ConnectOpts struct {
		Local bool   `short:"l" long:"local" description:"Host represents a local daos_agent"`
		Host  string `short:"t" long:"host" description:"Host to connect to" required:"true"`
	}

	shell.AddCmd(&ishell.Cmd{
		Name: "connect",
		Help: "Connect to management infrastructure",
		Func: func(c *ishell.Context) {
			_, err := flags.ParseArgs(&ConnectOpts, c.Args)
			if err != nil {
				c.Println("Error parsing Connect args: ", err.Error())
				return
			}
			if ConnectOpts.Local {
				if agentClient.Connected() {
					c.Println("Already connected to local agent")
					return
				}

				err = agentClient.Connect(ConnectOpts.Host)
				if err != nil {
					c.Println("Unable to connect to ", ConnectOpts.Host, err.Error())
				}
			} else {
				if controlClient.Connected() {
					c.Println("Already connected to mgmt server")
					return
				}

				err = controlClient.Connect(ConnectOpts.Host)
				if err != nil || controlClient.Connected() == false {
					c.Println("Unable to connect to ", ConnectOpts.Host, err.Error())
				}
			}
		},
	})

	// The getHandle command requests a handle to security context and returns a UUID.
	// It does not require any arguments as the agent gatheres the necessary information.
	// Future version may take parameters as additional security mechanismss are introduced.
	shell.AddCmd(&ishell.Cmd{
		Name: "gethandle",
		Help: "Command to test requesting a security handle",
		Func: func(c *ishell.Context) {
			if agentClient.Connected() == false {
				c.Println("Connection to local agent required")
				return
			}
			token, err := agentClient.RequestSecurityContext()
			if err != nil {
				c.Println("Unable to request security context: ", err.Error())
				return
			}

			handle, err2 := uuid.FromBytes(token.GetToken())
			if err2 != nil {
				c.Println("Unable to convert handle bytes into uuid")
				return
			}
			c.Println("Security Token Flavor is: ", token.GetFlavor().String())
			c.Println("Security Token is: ", handle.String())
		},
	})

	// The getsecctx command takes in a UUID in string form and
	// returns the security context information assocuated with
	// that handle that it receives from the management server.
	shell.AddCmd(&ishell.Cmd{
		Name: "getsecctx",
		Help: "Command to test requesting a security context from a handle",
		Func: func(c *ishell.Context) {
			if agentClient.Connected() == false {
				c.Println("Connection to local agent required")
				return
			}
			if len(c.Args) < 1 {
				c.Println(c.HelpText())
				return
			}
			ctx, err := agentClient.VerifySecurityHandle(c.Args[0])
			if err != nil {
				c.Println("Unable to validate security handle: ", err.Error())
				return
			}

			context, err := security.AuthSysFromAuthToken(ctx)
			c.Printf("Handle: %s\nContext: %s\n", c.Args[0], context.String())

		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "getmgmtfeature",
		Help: "Command to retrieve the description of a given feature",
		Func: func(c *ishell.Context) {
			if controlClient.Connected() == false {
				c.Println("Connection to management server required")
				return
			}

			if len(c.Args) < 1 {
				c.Println(c.HelpText())
				return
			}

			ctx, err := controlClient.GetFeature(c.Args[0])
			if err != nil {
				c.Println("Unable to retrieve feature details", err.Error())
				return
			}
			c.Printf(
				"Feature: %s\nDescription: %s\nCategory: %s\n",
				c.Args[0], ctx.GetDescription(), ctx.Category.Category)
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "listmgmtfeatures",
		Help: "Command to retrieve all supported management features",
		Func: func(c *ishell.Context) {
			if controlClient.Connected() == false {
				c.Println("Connection to management server required")
				return
			}

			err := controlClient.ListAllFeatures()
			if err != nil {
				c.Println("Unable to retrieve features", err.Error())
				return
			}
		},
	})

	shell.AddCmd(&ishell.Cmd{
		Name: "nvme",
		Help: "Perform tasks on NVMe controllers",
		Func: func(c *ishell.Context) {
			if controlClient.Connected() == false {
				c.Println("Connection to management server required")
				return
			}

			cs, err := controlClient.ListNVMeCtrlrs()
			if err != nil {
				c.Println("Unable to retrieve controller details", err.Error())
				return
			}

			// record strings that make up the option list
			cStrs := make([]string, len(cs))
			for i, v := range cs {
				cStrs[i] = fmt.Sprintf("[%d] %+v", i, v)
			}

			ctrlrIdxs := c.Checklist(
				cStrs,
				"Select the controllers you want to run tasks on.",
				nil)
			if len(ctrlrIdxs) == 0 {
				c.Println("No controllers selected!")
				return
			}

			// filter list of selected controllers to act on
			var ctrlrs []*pb.NVMeController
			for i, ctrlr := range cs {
				for j := range ctrlrIdxs {
					if i == j {
						ctrlrs = append(ctrlrs, ctrlr)
					}
				}
			}

			featureMap, err := controlClient.ListFeatures("nvme")
			if err != nil {
				c.Println("Unable to retrieve nvme features", err.Error())
				return
			}

			taskStrs := make([]string, len(featureMap))
			taskHandlers := make([]string, len(featureMap))
			i := 0
			for k, v := range featureMap {
				taskStrs[i] = fmt.Sprintf("[%d] %s - %s", i, k, v)
				taskHandlers[i] = k
				i++
			}

			taskIdx := c.MultiChoice(
				taskStrs,
				"Select the task you would like to run on the selected controllers.")

			c.Printf(
				"\nRunning task %s on the following controllers:\n",
				taskHandlers[taskIdx])
			for _, ctrlr := range ctrlrs {
				c.Printf("\t- %+v\n", ctrlr)
			}
			c.Println("")

			if err := nvmeTaskLookup(c, ctrlrs, taskHandlers[taskIdx]); err != nil {
				c.Println("Problem running task: ", err.Error())
			}
		},
	})

	return shell
}

func main() {
	// by default, shell includes 'exit', 'help' and 'clear' commands.
	agentClient = agent.NewDAOSAgentClient()
	controlClient = control.NewDAOSMgmtClient()
	shell := setupShell()

	shell.Println("DAOS Management Shell")

	shell.Run()
}
