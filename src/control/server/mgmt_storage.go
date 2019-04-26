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
	"os"

	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
)

// addState creates, populates and returns ResponseState in addition
// to logging any err.
func addState(
	status pb.ResponseStatus, errMsg string, infoMsg string, logDepth int,
	contextMsg string) *pb.ResponseState {

	state := new(pb.ResponseState)

	state.Status = status
	state.Error = errMsg
	state.Info = infoMsg

	if errMsg != "" {
		log.Errordf(logDepth, contextMsg+": "+errMsg)
	}

	return state
}

// ScanStorage discovers non-volatile storage hardware on node.
func (c *controlService) ScanStorage(
	ctx context.Context, params *pb.ScanStorageParams) (
	*pb.ScanStorageResp, error) {

	resp := new(pb.ScanStorageResp)

	log.Debugf("nvme scan, should be quick!\n")
	c.nvme.Discover(resp)

	log.Debugf("scm scan, should be quick!\n")
	c.scm.Discover(resp)

	return resp, nil
}

// doFormat locks format condition variable and performs format on storage
// subsystems. Response results will be populated by the subsystem routines.
func (c *controlService) doFormat(i int, resp *pb.FormatStorageResp) {
	cond := c.config.Servers[i].FormatCond
	// wait for lock to be released when main is ready
	cond.L.Lock()
	defer cond.L.Unlock()

	log.Debugf("performing nvme format, may take several minutes!\n")
	c.nvme.Format(i, resp)

	log.Debugf("performing scm format, should be quick!\n")
	c.scm.Format(i, resp)

	// storage subsystem format successful, signal to alert main.
	cond.Signal()
}

// Format delegates to Storage implementation's Format methods to prepare
// storage for use by DAOS data plane.
//
// Errors returned will stop other servers from formatting, non-fatal errors
// specific to a particular device should be reported within resp results.
//
// Send response containing multiple results of format operations on scm mounts
// and nvme controllers.
func (c *controlService) FormatStorage(
	params *pb.FormatStorageParams,
	stream pb.MgmtControl_FormatStorageServer) error {

	resp := new(pb.FormatStorageResp)

	if c.config.FormatOverride {
		return errors.New(
			"FormatStorage call unsupported when " +
				"format_override == true in server config file")
	}

	// TODO: execute in parallel across servers
	for i := range c.config.Servers {
		// verify superblock doesn't exist
		if _, err := os.Stat(
			iosrvSuperPath(c.config.Servers[i].ScmMount)); err == nil {

			log.Debugf("FormatStorage: server %d already formatted", i)
			continue
		} else if !os.IsNotExist(err) {
			return err
		}

		c.doFormat(i, resp)

		log.Debugf(
			"FormatStorage: format successful on server %d\n", i)
	}

	if err := stream.Send(resp); err != nil {
		return err
	}

	return nil
}

// Burnin delegates to Storage implementation's Burnin methods to prepare
// storage for use by DAOS data plane.
func (c *controlService) BurninStorage(
	params *pb.BurninStorageParams,
	stream pb.MgmtControl_BurninStorageServer) error {

	// TODO: return something useful like ack in response
	if err := stream.Send(&pb.BurninStorageResp{}); err != nil {
		return err
	}

	return nil
}

// Update delegates to Storage implementation's Burnin methods to prepare
// storage for use by DAOS data plane.
func (c *controlService) UpdateStorage(
	params *pb.UpdateStorageParams,
	stream pb.MgmtControl_UpdateStorageServer) error {

	// TODO: return something useful like ack in response
	if err := stream.Send(&pb.UpdateStorageResp{}); err != nil {
		return err
	}

	return nil
}

//// UpdateNvmeCtrlr updates the firmware on a NVMe controller, verifying that the
//// fwrev reported changes after update.
////
//// Todo: in real life Ctrlr.Id is not guaranteed to be unique, use pciaddr instead
//func (c *controlService) UpdateNvmeCtrlr(
//	ctx context.Context, params *pb.UpdateNvmeParams) (*pb.NvmeController, error) {
//	pciAddr := params.GetPciaddr()
//	//fwRev := params.GetFwrev()
//	if err := c.nvme.Update(pciAddr, params.Path, params.Slot); err != nil {
//		return nil, err
//	}
//	for _, ctrlr := range c.nvme.controllers {
//		if ctrlr.Pciaddr == pciAddr {
//			// TODO: verify at caller
//			//			if ctrlr.Fwrev == fwRev {
//			//				return nil, errors.Errorf("update failed, firmware revision unchanged")
//			//			}
//			return ctrlr, nil
//		}
//	}
//	return nil, errors.Errorf("update failed, no matching controller found")
//}

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
		if err := stream.Send(&pb.FilePath{Path: path}); err != nil {
			return err
		}
	}

	return nil
}

//// BurnInNvme runs burn-in validation on NVMe Namespace and returns cmd output
//// in a stream to the gRPC consumer.
//func (c *controlService) BurnInNvme(
//	params *pb.BurnInNvmeParams, stream pb.MgmtControl_BurnInNvmeServer) error {
//	// retrieve command components
//	cmdName, args, env, err := c.nvme.BurnIn(
//		params.GetPciaddr(),
//		// hardcode first Namespace on controller for the moment
//		1,
//		params.Path.Path)
//	if err != nil {
//		return err
//	}
//	// construct command executer and init env/reader
//	cmd := exec.Command(cmdName, args...)
//	cmd.Env = os.Environ()
//	cmd.Env = append(cmd.Env, env)
//	var stderr bytes.Buffer
//	cmd.Stderr = &stderr
//	cmdReader, err := cmd.StdoutPipe()
//	if err != nil {
//		return errors.Errorf("Error creating StdoutPipe for Cmd %v", err)
//	}
//	// run text scanner as goroutine
//	scanner := bufio.NewScanner(cmdReader)
//	go func() {
//		for scanner.Scan() {
//			stream.Send(&pb.BurnInNvmeReport{Report: scanner.Text()})
//		}
//	}()
//	// start command and wait for finish
//	err = cmd.Start()
//	if err != nil {
//		return errors.Errorf(
//			"Error starting Cmd: %s, Args: %v, Env: %s (%v)",
//			cmdName, args, env, err)
//	}
//	err = cmd.Wait()
//	if err != nil {
//		return errors.Errorf(
//			"Error waiting for completion of Cmd: %s, Args: %v, Env: %s (%v, %q)",
//			cmdName, args, env, err, stderr.String())
//	}
//	return nil
//}
