//
// (C) Copyright 2018-2019 Intel Corporation.
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
	"bufio"
	"bytes"
	"os"
	"os/exec"

	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
)

// ListNvmeCtrlrs lists all NVMe controllers.
func (c *controlService) ListNvmeCtrlrs(
	empty *pb.EmptyParams, stream pb.MgmtControl_ListNvmeCtrlrsServer) error {

	log.Debugf("ControlService.ListNvmeCtrlrs dispatch")

	if err := c.nvme.Discover(); err != nil {
		return err
	}

	for _, ctrlr := range c.nvme.controllers {
		if err := stream.Send(ctrlr); err != nil {
			return err
		}
	}

	return nil
}

// UpdateNvmeCtrlr updates the firmware on a NVMe controller, verifying that the
// fwrev reported changes after update.
//
// Todo: in real life Ctrlr.Id is not guaranteed to be unique, use pciaddr instead
func (c *controlService) UpdateNvmeCtrlr(
	ctx context.Context, params *pb.UpdateNvmeParams) (*pb.NvmeController, error) {
	pciAddr := params.GetPciaddr()
	//fwRev := params.GetFwrev()
	if err := c.nvme.Update(pciAddr, params.Path, params.Slot); err != nil {
		return nil, err
	}
	for _, ctrlr := range c.nvme.controllers {
		if ctrlr.Pciaddr == pciAddr {
			// TODO: verify at caller
			//			if ctrlr.Fwrev == fwRev {
			//				return nil, errors.Errorf("update failed, firmware revision unchanged")
			//			}
			return ctrlr, nil
		}
	}
	return nil, errors.Errorf("update failed, no matching controller found")
}

// FetchFioConfigPaths retrieves any configuration files in fio_plugin directory
func (c *controlService) FetchFioConfigPaths(
	empty *pb.EmptyParams, stream pb.MgmtControl_FetchFioConfigPathsServer) error {
	pluginDir, err := common.GetAbsInstallPath(spdkFioPluginDir)
	if err != nil {
		return err
	}
	paths, err := common.GetFilePaths(pluginDir, "fio")
	if err != nil {
		return err
	}
	for _, path := range paths {
		if err := stream.Send(&pb.FioConfigPath{Path: path}); err != nil {
			return err
		}
	}
	return nil
}

// BurnInNvme runs burn-in validation on NVMe Namespace and returns cmd output
// in a stream to the gRPC consumer.
func (c *controlService) BurnInNvme(
	params *pb.BurnInNvmeParams, stream pb.MgmtControl_BurnInNvmeServer) error {
	// retrieve command components
	cmdName, args, env, err := c.nvme.BurnIn(
		params.GetPciaddr(),
		// hardcode first Namespace on controller for the moment
		1,
		params.Path.Path)
	if err != nil {
		return err
	}
	// construct command executer and init env/reader
	cmd := exec.Command(cmdName, args...)
	cmd.Env = os.Environ()
	cmd.Env = append(cmd.Env, env)
	var stderr bytes.Buffer
	cmd.Stderr = &stderr
	cmdReader, err := cmd.StdoutPipe()
	if err != nil {
		return errors.Errorf("Error creating StdoutPipe for Cmd %v", err)
	}
	// run text scanner as goroutine
	scanner := bufio.NewScanner(cmdReader)
	go func() {
		for scanner.Scan() {
			stream.Send(&pb.BurnInNvmeReport{Report: scanner.Text()})
		}
	}()
	// start command and wait for finish
	err = cmd.Start()
	if err != nil {
		return errors.Errorf(
			"Error starting Cmd: %s, Args: %v, Env: %s (%v)",
			cmdName, args, env, err)
	}
	err = cmd.Wait()
	if err != nil {
		return errors.Errorf(
			"Error waiting for completion of Cmd: %s, Args: %v, Env: %s (%v, %q)",
			cmdName, args, env, err, stderr.String())
	}
	return nil
}
