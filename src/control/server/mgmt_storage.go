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

	state := &pb.ResponseState{
		Status: status, Error: errMsg, Info: infoMsg,
	}

	if errMsg != "" {
		log.Errordf(logDepth, contextMsg+": "+errMsg)
	}

	return state
}

// ScanStorage discovers non-volatile storage hardware on node.
func (c *controlService) ScanStorage(
	ctx context.Context, req *pb.ScanStorageReq) (
	*pb.ScanStorageResp, error) {

	resp := new(pb.ScanStorageResp)

	c.nvme.Discover(resp)
	c.scm.Discover(resp)

	return resp, nil
}

// doFormat performs format on storage subsystems, populates response results
// in storage subsystem routines and broadcasts (closes channel) if successful.
func (c *controlService) doFormat(i int, resp *pb.FormatStorageResp) error {
	srv := c.config.Servers[i]
	serverFormatted := false

	if ok, err := c.config.ext.exists(
		iosrvSuperPath(srv.ScmMount)); err != nil {

		return errors.Wrap(err, "locating superblock")
	} else if ok {
		// server already formatted, populate response appropriately
		c.nvme.formatted = true
		c.scm.formatted = true
		serverFormatted = true
	}

	c.nvme.Format(i, &resp.Crets)
	c.scm.Format(i, &resp.Mrets)

	if !serverFormatted && c.nvme.formatted && c.scm.formatted {
		// storage subsystem format successful, broadcast formatted
		close(srv.formatted)
		log.Debugf("storage format successful on server %d\n", i)
	}

	return nil
}

// Format delegates to Storage implementation's Format methods to prepare
// storage for use by DAOS data plane.
//
// Errors returned will stop other servers from formatting, non-fatal errors
// specific to particular device should be reported within resp results instead.
//
// Send response containing multiple results of format operations on scm mounts
// and nvme controllers.
func (c *controlService) FormatStorage(
	req *pb.FormatStorageReq,
	stream pb.MgmtCtl_FormatStorageServer) error {

	resp := new(pb.FormatStorageResp)

	for i := range c.config.Servers {
		if err := c.doFormat(i, resp); err != nil {
			return errors.WithMessage(err, "formatting storage")
		}
	}

	if err := stream.Send(resp); err != nil {
		return errors.WithMessagef(err, "sending response (%+v)", resp)
	}

	return nil
}

// TODO: implement gRPC fw update feature in scm subsystem
// Update delegates to Storage implementation's fw update methods to prepare
// storage for use by DAOS data plane.
//
// Send response containing multiple results of update operations on scm mounts
// and nvme controllers.
func (c *controlService) UpdateStorage(
	req *pb.UpdateStorageReq,
	stream pb.MgmtCtl_UpdateStorageServer) error {

	resp := new(pb.UpdateStorageResp)

	for i := range c.config.Servers {
		c.nvme.Update(i, req.Nvme, &resp.Crets)
		c.scm.Update(i, req.Scm, &resp.Mrets)
	}

	if err := stream.Send(resp); err != nil {
		return errors.WithMessagef(err, "sending response (%+v)", resp)
	}

	return nil
}

// TODO: implement gRPC burn-in feature in nvme and scm subsystems
// Burnin delegates to Storage implementation's Burnin methods to prepare
// storage for use by DAOS data plane.
//
// Send response containing multiple results of burn-in operations on scm mounts
// and nvme controllers.
func (c *controlService) BurninStorage(
	req *pb.BurninStorageReq,
	stream pb.MgmtCtl_BurninStorageServer) error {

	return errors.New("BurninStorage not implemented")
	//	for i := range c.config.Servers {
	//		c.nvme.BurnIn(i, req.Nvme, resp)
	//		c.scm.BurnIn(i, req.Scm, resp)
	//	}

	//	if err := stream.Send(resp); err != nil {
	//		return errors.WithMessagef(err, "sending response (%+v)", resp)
	//	}

	//	return nil
}

// FetchFioConfigPaths retrieves any configuration files in fio_plugin directory
func (c *controlService) FetchFioConfigPaths(
	empty *pb.EmptyReq, stream pb.MgmtCtl_FetchFioConfigPathsServer) error {

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

// TODO: to be used during the limitation of burnin feature
//// BurnInNvme runs burn-in validation on NVMe Namespace and returns cmd output
//// in a stream to the gRPC consumer.
//func (c *controlService) BurnInNvme(
//	req *pb.BurnInNvmeReq, stream pb.MgmtCtl_BurnInNvmeServer) error {
//	// retrieve command components
//	cmdName, args, env, err := c.nvme.BurnIn(
//		req.GetPciaddr(),
//		// hardcode first Namespace on controller for the moment
//		1,
//		req.Path.Path)
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
