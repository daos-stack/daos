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

package server

import (
	"fmt"
	"os"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// cliOptions struct defined flags that can be used when invoking daos_server.
type cliOptions struct {
	Port        uint16  `short:"p" long:"port" description:"Port for the gRPC management interfect to listen on"`
	MountPath   string  `short:"s" long:"storage" description:"Storage path"`
	ConfigPath  string  `short:"o" long:"config_path" description:"Server config file path"`
	Modules     *string `short:"m" long:"modules" description:"List of server modules to load"`
	Cores       uint16  `short:"c" long:"cores" default:"0" description:"option deprecated, please use targets instead"`
	Targets     uint16  `short:"t" long:"targets" default:"0" description:"number of targets to use (default use all cores)"`
	NrXsHelpers *uint16 `short:"x" long:"xshelpernr" description:"number of helper XS per VOS target (default 2)"`
	FirstCore   uint16  `short:"f" long:"firstcore" default:"0" description:"index of first core for service thread (default 0)"`
	Group       string  `short:"g" long:"group" description:"Server group name"`
	Attach      *string `short:"a" long:"attach_info" description:"Attach info patch (to support non-PMIx client, default /tmp)"`
	Map         *string `short:"y" long:"map" description:"[Temporary] System map file"`
	Rank        *rank   `short:"r" long:"rank" description:"[Temporary] Self rank"`
	SocketDir   string  `short:"d" long:"socket_dir" description:"Location for all daos_server & daos_io_server sockets"`
	Storage     StorCmd `command:"storage" alias:"st" description:"Perform tasks related to locally-attached storage"`
	Insecure    bool    `short:"i" long:"insecure" description:"allow for insecure connections"`
}

// StorCmd is the struct representing the top-level storage subcommand.
type StorCmd struct {
	Scan     ScanStorCmd `command:"scan" alias:"l" description:"Scan SCM and NVMe storage attached to local server"`
	PrepNvme PrepNvmeCmd `command:"prep-nvme" alias:"pn" description:"Prep NVMe devices for use with SPDK as current user"`
}

// ScanStorCmd is the struct representing the command to scan storage.
// Retrieves and prints details of locally attached SCM and NVMe storage.
type ScanStorCmd struct{}

// Execute is run when ScanStorCmd activates
//
// Perform task then exit immediately. No config parsing performed.
func (s *ScanStorCmd) Execute(args []string) (errs error) {
	var isErrored bool
	config := newConfiguration()
	resp := new(pb.ScanStorageResp)

	srv, err := newControlService(
		&config, getDrpcClientConnection(config.SocketDir))
	if err != nil {
		return errors.WithMessage(err, "failed to init ControlService")
	}

	fmt.Println("Scanning locally-attached storage...")
	srv.nvme.Discover(resp)
	srv.scm.Discover(resp)

	if resp.Nvmestate.Status != pb.ResponseStatus_CTRL_SUCCESS {
		fmt.Fprintln(os.Stderr, "nvme scan: "+resp.Nvmestate.Error)
		isErrored = true
	} else {
		common.PrintStructs("NVMe", srv.nvme.controllers)
	}
	if resp.Scmstate.Status != pb.ResponseStatus_CTRL_SUCCESS {
		fmt.Fprintln(os.Stderr, "scm scan: "+resp.Scmstate.Error)
		isErrored = true
	} else {
		common.PrintStructs("SCM", srv.scm.modules)
	}

	if isErrored {
		os.Exit(1)
	}
	// exit immediately to avoid continuation of main
	os.Exit(0)
	// never reached
	return
}

// PrepNvmeCmd is the struct representing the command to prep NVMe SSDs
// for use with the SPDK as an unprivileged user.
type PrepNvmeCmd struct {
	PCIWhiteList string `short:"w" long:"pci-whitelist" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)."`
	NrHugepages  int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate (in MB) for use by SPDK (default 1024)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	Reset        bool   `short:"r" long:"reset" description:"Reset SPDK returning devices to kernel modules"`
}

// Execute is run when PrepNvmeCmd activates
//
// Perform task then exit immediately. No config parsing performed.
func (p *PrepNvmeCmd) Execute(args []string) error {
	ok, usr := common.CheckSudo()
	if !ok {
		return errors.New("subcommand must be run as root or sudo")
	}

	// falls back to sudoer or root if TargetUser is unspecified
	tUsr := usr
	if p.TargetUser != "" {
		tUsr = p.TargetUser
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
		if err := server.nvme.spdk.prep(p.NrHugepages, tUsr, p.PCIWhiteList); err != nil {
			return errors.WithMessage(err, "SPDK setup")
		}
	}

	// exit immediately to avoid continuation of main
	os.Exit(0)
	// never reached
	return nil
}
