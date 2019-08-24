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

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
)

// addState creates, populates and returns ResponseState in addition
// to logging any err.
func addState(
	log logging.Logger,
	status pb.ResponseStatus, errMsg string, infoMsg string, logDepth int,
	contextMsg string) *pb.ResponseState {

	state := &pb.ResponseState{
		Status: status, Error: errMsg, Info: infoMsg,
	}

	if errMsg != "" {
		log.Error(contextMsg + ": " + errMsg)
	}

	return state
}

type StorageControlService struct {
	log  logging.Logger
	nvme *nvmeStorage
	scm  *scmStorage
	drpc drpc.DomainSocketClient
}

func NewStorageControlService(log logging.Logger, cfg *Configuration) (*StorageControlService, error) {
	scriptPath, err := cfg.ext.GetAbsInstallPath(spdkSetupPath)
	if err != nil {
		return nil, err
	}

	spdkScript := &spdkSetup{
		log:         log,
		scriptPath:  scriptPath,
		nrHugePages: cfg.NrHugepages,
	}

	return &StorageControlService{
		log:  log,
		nvme: newNvmeStorage(log, cfg.NvmeShmID, spdkScript, cfg.ext),
		scm:  newScmStorage(log, cfg.ext),
		drpc: getDrpcClientConnection(cfg.SocketDir),
	}, nil
}

// ScanStorage discovers non-volatile storage hardware on node.
func (scs *StorageControlService) ScanStorage(ctx context.Context, req *pb.ScanStorageReq) (*pb.ScanStorageResp, error) {
	resp := new(pb.ScanStorageResp)

	scs.nvme.Discover(resp)
	scs.scm.Discover(resp)

	return resp, nil
}

// doFormat performs format on storage subsystems, populates response results
// in storage subsystem routines and broadcasts (closes channel) if successful.
func (c *ControlService) doFormat(i *IOServerInstance, resp *pb.FormatStorageResp) error {
	hasSuperblock := false

	needsSuperblock, err := i.NeedsSuperblock()
	if err != nil {
		return err
	}

	if !needsSuperblock {
		// server already formatted, populate response appropriately
		c.nvme.formatted = true
		c.scm.formatted = true
		hasSuperblock = true
	}

	// scaffolding
	ctrlrResults := common.NvmeControllerResults{}
	c.nvme.Format(i.runner.Config.Storage.Bdev, &ctrlrResults)
	resp.Crets = ctrlrResults

	mountResults := common.ScmMountResults{}
	c.scm.Format(i.runner.Config.Storage.SCM, &mountResults)
	resp.Mrets = mountResults

	if !hasSuperblock && c.nvme.formatted && c.scm.formatted {
		c.log.Debugf("storage format successful on server %d\n", i.runner.Config.Index)
	}
	i.NotifyStorageReady()

	return nil
}

// Format delegates to Storage implementation's Format methods to prepare
// storage for use by DAOS data plane.
//
// Errors returned will stop other servers from formatting, non-fatal errors
// specific to particular device should be reported within resp results instead.
//
// Send response containing multiple results of format operations on scm Mounts
// and nvme controllers.
func (c *ControlService) FormatStorage(
	req *pb.FormatStorageReq,
	stream pb.MgmtCtl_FormatStorageServer) error {

	resp := new(pb.FormatStorageResp)

	// temporary scaffolding
	for _, i := range c.harness.instances {
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
// Send response containing multiple results of update operations on scm Mounts
// and nvme controllers.
func (c *ControlService) UpdateStorage(req *pb.UpdateStorageReq, stream pb.MgmtCtl_UpdateStorageServer) error {
	return errors.New("UpdateStorage not implemented")
	/*resp := new(pb.UpdateStorageResp)

	for i := range c.config.Servers {
		ctrlrResults := common.NvmeControllerResults{}
		c.nvme.Update(i, req.Nvme, &ctrlrResults)
		resp.Crets = ctrlrResults

		moduleResults := common.ScmModuleResults{}
		c.scm.Update(i, req.Scm, &moduleResults)
		resp.Mrets = moduleResults
	}

	if err := stream.Send(resp); err != nil {
		return errors.WithMessagef(err, "sending response (%+v)", resp)
	}

	return nil*/
}

// TODO: implement gRPC burn-in feature in nvme and scm subsystems
// Burnin delegates to Storage implementation's Burnin methods to prepare
// storage for use by DAOS data plane.
//
// Send response containing multiple results of burn-in operations on scm Mounts
// and nvme controllers.
func (c *ControlService) BurninStorage(req *pb.BurninStorageReq, stream pb.MgmtCtl_BurninStorageServer) error {
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
func (c *ControlService) FetchFioConfigPaths(empty *pb.EmptyReq, stream pb.MgmtCtl_FetchFioConfigPathsServer) error {
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

// Setup delegates to Storage implementation's Setup methods.
func (c *StorageControlService) Setup() {
	if err := c.nvme.Setup(); err != nil {
		c.log.Debugf(
			"%s\n", errors.Wrap(err, "Warning, NVMe Setup"))
	}

	if err := c.scm.Setup(); err != nil {
		c.log.Debugf(
			"%s\n", errors.Wrap(err, "Warning, SCM Setup"))
	}
}

// Teardown delegates to Storage implementation's Teardown methods.
func (c *StorageControlService) Teardown() {
	if err := c.nvme.Teardown(); err != nil {
		c.log.Debugf(
			"%s\n", errors.Wrap(err, "Warning, NVMe Teardown"))
	}

	if err := c.scm.Teardown(); err != nil {
		c.log.Debugf(
			"%s\n", errors.Wrap(err, "Warning, SCM Teardown"))
	}
}

func (c *StorageControlService) ScanNVMe() (common.NvmeControllers, error) {
	resp := new(pb.ScanStorageResp)

	c.nvme.Discover(resp)
	if resp.Nvmestate.Status != pb.ResponseStatus_CTRL_SUCCESS {
		return nil, fmt.Errorf("nvme scan: %s", resp.Nvmestate.Error)
	}
	return c.nvme.controllers, nil
}

func (c *StorageControlService) ScanSCM() (common.ScmModules, error) {
	resp := new(pb.ScanStorageResp)

	c.scm.Discover(resp)
	if resp.Scmstate.Status != pb.ResponseStatus_CTRL_SUCCESS {
		return nil, fmt.Errorf("scm scan: %s", resp.Scmstate.Error)
	}
	return c.scm.modules, nil
}

type PrepNvmeRequest struct {
	HugePageCount int
	TargetUser    string
	PCIWhitelist  string
	ResetOnly     bool
}

func (c *StorageControlService) PrepNvme(req PrepNvmeRequest) error {
	// run reset first to ensure reallocation of hugepages
	if err := c.nvme.spdk.reset(); err != nil {
		return errors.WithMessage(err, "SPDK setup reset")
	}

	// if we're only resetting, just return before prep
	if req.ResetOnly {
		return nil
	}

	return errors.WithMessage(
		c.nvme.spdk.prep(req.HugePageCount, req.TargetUser, req.PCIWhitelist),
		"SPDK setup",
	)
}

type PrepScmRequest struct {
	Reset bool
}

func (c *StorageControlService) PrepScm(req PrepScmRequest) (rebootStr string, pmemDevs []pmemDev, err error) {
	if err = c.scm.Setup(); err != nil {
		err = errors.WithMessage(err, "SCM setup")
		return
	}

	if !c.scm.initialized {
		err = errors.New(msgScmNotInited)
		return
	}

	if len(c.scm.modules) == 0 {
		err = errors.New(msgScmNoModules)
		return
	}

	var needsReboot bool
	if req.Reset {
		// run reset to Remove namespaces and clear regions
		needsReboot, err = c.scm.PrepReset()
		if err != nil {
			err = errors.WithMessage(err, "SCM prep reset")
			return
		}
	} else {
		// transition to the next state in SCM preparation
		needsReboot, pmemDevs, err = c.scm.Prep()
		if err != nil {
			err = errors.WithMessage(err, "SCM prep reset")
			return
		}
	}

	if needsReboot {
		rebootStr = msgScmRebootRequired
	}

	return
}
