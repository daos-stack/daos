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

package mgmt

import (
	"bufio"
	"bytes"
	"fmt"
	"os"
	"os/exec"

	"golang.org/x/net/context"

	pb "mgmt/proto"
	"utils/handlers"
)

// FetchNVMe populates controllers and namespaces in ControlService
// as side effect.
//
// todo: presumably we want to be able to detect namespaces added
//       during the lifetime of the daos_server process, in that case
//       will need to rerun discover here (currently SPDK throws
//		 exception if you try to probe a second time).
func (s *ControlService) FetchNVMe() error {
	if s.storageInitialised != true {
		// todo: pass shm_id to Init()
		if err := s.Storage.Init(); err != nil {
			return err
		}
		ret := s.Storage.Discover()
		if err := s.populateNVMe(ret); err != nil {
			return err
		}
	}
	return nil
}

// ListNVMeCtrlrs lists all NVMe controllers.
func (s *ControlService) ListNVMeCtrlrs(
	empty *pb.EmptyParams, stream pb.MgmtControl_ListNVMeCtrlrsServer) error {
	if err := s.FetchNVMe(); err != nil {
		return err
	}
	for _, c := range s.NvmeControllers {
		if err := stream.Send(c); err != nil {
			return err
		}
	}
	return nil
}

// ListNVMeNss lists all namespaces discovered on attached NVMe controllers.
func (s *ControlService) ListNVMeNss(
	ctrlr *pb.NVMeController, stream pb.MgmtControl_ListNVMeNssServer) error {
	if err := s.FetchNVMe(); err != nil {
		return err
	}
	for _, ns := range s.NvmeNamespaces {
		if ns.Controller.Id == ctrlr.Id {
			if err := stream.Send(ns); err != nil {
				return err
			}
		}
	}
	return nil
}

// UpdateNVMeCtrlr updates the firmware on a NVMe controller, verifying that the
// fwrev reported changes after update.
func (s *ControlService) UpdateNVMeCtrlr(
	ctx context.Context, params *pb.UpdateNVMeCtrlrParams) (*pb.NVMeController, error) {
	id := params.Ctrlr.Id
	ret := s.Storage.Update(UpdateParams{id, params.Path, params.Slot})
	if err := s.populateNVMe(ret); err != nil {
		return nil, err
	}
	c, exists := s.NvmeControllers[id]
	if !exists {
		return nil, fmt.Errorf("update failed, no matching controller found")
	}
	if c.Fwrev == params.Ctrlr.Fwrev {
		return nil, fmt.Errorf("update failed, firmware revision unchanged")
	}
	return c, nil
}

// FetchFioConfigPaths retrieves any configuration files in fio_plugin directory
func (s *ControlService) FetchFioConfigPaths(
	empty *pb.EmptyParams, stream pb.MgmtControl_FetchFioConfigPathsServer) error {
	pluginDir, err := handlers.GetAbsInstallPath(spdkFioPluginDir)
	if err != nil {
		return err
	}
	paths, err := handlers.GetFilePaths(pluginDir, "fio")
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

// BurnInNVMe runs burn-in validation on NVMe Namespace and returns cmd output
// in a stream to the gRPC consumer.
func (s *ControlService) BurnInNVMe(
	params *pb.BurnInNVMeParams, stream pb.MgmtControl_BurnInNVMeServer) error {
	// retrieve command components
	cmdName, args, env, err := s.Storage.BurnIn(
		BurnInParams{
			PciAddr: s.NvmeControllers[params.Ctrlrid].Pciaddr,
			// hardcode first Namespace on controller for the moment
			NsID:       1,
			ConfigPath: params.Path.Path,
		})
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
		return fmt.Errorf("Error creating StdoutPipe for Cmd %v", err)
	}
	// run text scanner as goroutine
	scanner := bufio.NewScanner(cmdReader)
	go func() {
		for scanner.Scan() {
			stream.Send(&pb.BurnInNVMeReport{Report: scanner.Text()})
		}
	}()
	// start command and wait for finish
	err = cmd.Start()
	if err != nil {
		return fmt.Errorf(
			"Error starting Cmd: %s, Args: %v, Env: %s (%v)",
			cmdName, args, env, err)
	}
	err = cmd.Wait()
	if err != nil {
		return fmt.Errorf(
			"Error waiting for completion of Cmd: %s, Args: %v, Env: %s (%v, %q)",
			cmdName, args, env, err, stderr.String())
	}
	return nil
}
