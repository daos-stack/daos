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
// provided in Contract No. B609815.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"common/agent"
	"modules/security"

	"github.com/abiosoft/ishell"
	"github.com/jessevdk/go-flags"
	"github.com/satori/go.uuid"
)

var (
	client *agent.DAOSAgentClient
)

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
	// register a function for "connect" command.
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
				if client.Connected() {
					c.Println("Already connected to local agent")
					return
				}

				err = client.Connect(ConnectOpts.Host)
				if err != nil {
					c.Println("Unable to connect to ", ConnectOpts.Host, err.Error())
				}
			} else {
				c.Println("Connecting to Management infrastructure not yet implemented.")
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
			if client.Connected() == false {
				c.Println("Connection to local agent required")
				return
			}
			token, err := client.RequestSecurityContext()
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
			if client.Connected() == false {
				c.Println("Connection to local agent required")
				return
			}
			if len(c.Args) < 1 {
				c.Println(c.HelpText())
				return
			}
			ctx, err := client.VerifySecurityHandle(c.Args[0])
			if err != nil {
				c.Println("Unable to validate security handle: ", err.Error())
				return
			}

			context, err := security.AuthSysFromAuthToken(ctx)
			c.Printf("Handle: %s\nContext: %s\n", c.Args[0], context.String())

		},
	})

	return shell
}
func main() {
	// create new shell.
	// by default, new shell includes 'exit', 'help' and 'clear' commands.
	client = agent.NewDAOSAgentClient()
	shell := setupShell()

	// display welcome info.
	shell.Println("DAOS Management Shell")

	// run shell
	shell.Run()
}
