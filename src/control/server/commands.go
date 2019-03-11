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
	"os"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/pkg/errors"
)

// ShowStorageCommand is the struct representing the command to list storage.
// Retrieves and prints details of locally attached SCM and NVMe storage.
type ShowStorageCommand struct{}

// Execute is run when ShowStorageCommand activates
//
// Perform task then exit immediately. No config parsing performed.
func (s *ShowStorageCommand) Execute(args []string) (errs error) {
	config := newConfiguration()

	server, err := newControlService(
		&config, getDrpcClientConnection(config.SocketDir))
	if err != nil {
		return errors.WithMessage(err, "failed to init ControlService")
	}

	server.Setup()

	// continue on failure for a single subsystem
	fmt.Println("Listing attached storage...")
	if err := server.nvme.Discover(); err != nil {
		errs = errors.WithMessage(err, "nvme discover")
		fmt.Fprintln(os.Stderr, errs)
	} else {
		common.PrintStructs("NVMe", server.nvme.controllers)
	}
	if err := server.scm.Discover(); err != nil {
		errs = errors.WithMessage(err, "scm discover")
		fmt.Fprintln(os.Stderr, errs)
	} else {
		common.PrintStructs("SCM", server.scm.modules)
	}

	server.Teardown()

	if errs != nil {
		os.Exit(1)
	}
	// exit immediately to avoid continuation of main
	os.Exit(0)
	// never reached
	return
}

// PrepNvmeCommand is the struct representing the command to prep NVMe SSDs
// for use with the SPDK as an unprivileged user.
type PrepNvmeCommand struct {
	NrHugepages int  `short:"p" long:"hugepages" description:"Number of hugepages to allocate for use by SPDK (default 1024)"`
	Reset       bool `short:"r" long:"reset" description:"Reset SPDK returning devices to kernel modules"`
}

// Execute is run when PrepNvmeCommand activates
//
// Perform task then exit immediately. No config parsing performed.
func (p *PrepNvmeCommand) Execute(args []string) error {
	ok, usr := common.CheckSudo()
	if !ok {
		return errors.New("This subcommand must be run with sudo!")
	}

	config := newConfiguration()

	server, err := newControlService(
		&config, getDrpcClientConnection(config.SocketDir))
	if err != nil {
		return errors.WithMessage(err, "initialising ControlService")
	}

	// run reset first to ensure reallocation of hugepages
	if err := server.nvme.spdk.reset(); err != nil {
		return errors.WithMessage(err, "SPDK setup reset")
	}
	if !p.Reset {
		if err := server.nvme.spdk.prep(p.NrHugepages, usr); err != nil {
			return errors.WithMessage(err, "SPDK setup")
		}
	}

	// exit immediately to avoid continuation of main
	os.Exit(0)
	// never reached
	return nil
}
