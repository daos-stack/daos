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
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	types "github.com/daos-stack/daos/src/control/common/storage"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/scm"
)

// newState creates, populates and returns ResponseState in addition
// to logging any err.
func newState(log logging.Logger, status ctlpb.ResponseStatus, errMsg string, infoMsg string,
	contextMsg string) *ctlpb.ResponseState {

	state := &ctlpb.ResponseState{
		Status: status, Error: errMsg, Info: infoMsg,
	}

	if errMsg != "" {
		log.Error(contextMsg + ": " + errMsg)
	}

	return state
}

func modulesToPB(mms []scm.Module) (pbMms types.ScmModules) {
	for _, c := range mms {
		pbMms = append(
			pbMms,
			&ctlpb.ScmModule{
				Loc: &ctlpb.ScmModule_Location{
					Channel:    c.ChannelID,
					Channelpos: c.ChannelPosition,
					Memctrlr:   c.ControllerID,
					Socket:     c.SocketID,
				},
				Physicalid: c.PhysicalID,
				Capacity:   c.Capacity,
			})
	}
	return
}

func namespacesToPB(nss []scm.Namespace) (pbNss types.PmemDevices) {
	for _, ns := range nss {
		pbNss = append(pbNss,
			&ctlpb.PmemDevice{
				Uuid:     ns.UUID,
				Blockdev: ns.BlockDevice,
				Dev:      ns.Name,
				Numanode: ns.NumaNode,
			})
	}

	return
}

func (c *StorageControlService) doNvmePrepare(req *ctlpb.PrepareNvmeReq) (resp *ctlpb.PrepareNvmeResp) {
	resp = &ctlpb.PrepareNvmeResp{}
	msg := "Storage Prepare NVMe"
	err := c.NvmePrepare(NvmePrepareRequest{
		HugePageCount: int(req.GetNrhugepages()),
		TargetUser:    req.GetTargetuser(),
		PCIWhitelist:  req.GetPciwhitelist(),
		ResetOnly:     req.GetReset_(),
	})

	if err != nil {
		resp.State = newState(c.log, ctlpb.ResponseStatus_CTRL_ERR_NVME, err.Error(), "", msg)
		return
	}

	resp.State = newState(c.log, ctlpb.ResponseStatus_CTRL_SUCCESS, "", "", msg)
	return
}

func (c *StorageControlService) doScmPrepare(pbReq *ctlpb.PrepareScmReq) (pbResp *ctlpb.PrepareScmResp) {
	pbResp = &ctlpb.PrepareScmResp{}
	msg := "Storage Prepare SCM"

	scmState, err := c.GetScmState()
	if err != nil {
		pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTRL_ERR_SCM, err.Error(), "", msg)
		return
	}
	c.log.Debugf("SCM state before prep: %s", scmState)

	resp, err := c.ScmPrepare(scm.PrepareRequest{Reset: pbReq.Reset_})
	if err != nil {
		pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTRL_ERR_SCM, err.Error(), "", msg)
		return
	}

	info := ""
	if resp.RebootRequired {
		info = scm.MsgScmRebootRequired
	}

	pbResp.State = newState(c.log, ctlpb.ResponseStatus_CTRL_SUCCESS, "", info, msg)
	pbResp.Pmems = namespacesToPB(resp.Namespaces)

	return
}

// StoragePrepare configures SSDs for user specific access with SPDK and
// groups SCM modules in AppDirect/interleaved mode as kernel "pmem" devices.
func (c *StorageControlService) StoragePrepare(ctx context.Context, req *ctlpb.StoragePrepareReq) (
	*ctlpb.StoragePrepareResp, error) {

	c.log.Debug("received StoragePrepare RPC; proceeding to instance storage preparation")

	resp := &ctlpb.StoragePrepareResp{}

	if req.Nvme != nil {
		resp.Nvme = c.doNvmePrepare(req.Nvme)
	}
	if req.Scm != nil {
		resp.Scm = c.doScmPrepare(req.Scm)
	}

	return resp, nil
}

// StorageScan discovers non-volatile storage hardware on node.
func (c *StorageControlService) StorageScan(ctx context.Context, req *ctlpb.StorageScanReq) (*ctlpb.StorageScanResp, error) {
	c.log.Debug("received StorageScan RPC")

	msg := "Storage Scan "
	resp := new(ctlpb.StorageScanResp)

	controllers, err := c.NvmeScan()
	if err != nil {
		resp.Nvme = &ctlpb.ScanNvmeResp{
			State: newState(c.log, ctlpb.ResponseStatus_CTRL_ERR_NVME, err.Error(), "", msg+"NVMe"),
		}
	} else {
		resp.Nvme = &ctlpb.ScanNvmeResp{
			State:  newState(c.log, ctlpb.ResponseStatus_CTRL_SUCCESS, "", "", msg+"NVMe"),
			Ctrlrs: controllers,
		}
	}

	result, err := c.scm.Scan(scm.ScanRequest{})
	if err != nil {
		resp.Scm = &ctlpb.ScanScmResp{
			State: newState(c.log, ctlpb.ResponseStatus_CTRL_ERR_SCM, err.Error(), "", msg+"SCM"),
		}
	} else {
		msg += fmt.Sprintf("SCM (%s)", result.State)
		resp.Scm = &ctlpb.ScanScmResp{
			State: newState(c.log, ctlpb.ResponseStatus_CTRL_SUCCESS, "", "", msg),
		}
		if len(result.Namespaces) > 0 {
			resp.Scm.Pmems = namespacesToPB(result.Namespaces)
		} else {
			resp.Scm.Modules = modulesToPB(result.Modules)
		}
	}

	return resp, nil
}

// newMntRet creates and populates NVMe ctrlr result and logs error through newState.
func newMntRet(log logging.Logger, op string, mntPoint string, status ctlpb.ResponseStatus, errMsg string, infoMsg string) *ctlpb.ScmMountResult {
	return &ctlpb.ScmMountResult{
		Mntpoint: mntPoint,
		State:    newState(log, status, errMsg, infoMsg, "scm mount "+op),
	}
}

func (c *ControlService) scmFormat(mountpoint string) *ctlpb.ScmMountResult {
	ret := newMntRet(c.log, "format", mountpoint, ctlpb.ResponseStatus_CTRL_SUCCESS, "", "")

	res, err := c.scm.Format(scm.FormatRequest{Mountpoint: mountpoint})
	if err != nil {
		ret.State.Error = err.Error()
		ret.State.Status = ctlpb.ResponseStatus_CTRL_ERR_APP
	} else if !res.Formatted {
		ret.State.Error = fmt.Sprintf("%s didn't get formatted", res.Mountpoint)
		ret.State.Status = ctlpb.ResponseStatus_CTRL_ERR_APP
	}

	return ret
}

// in storage subsystem routines and broadcasts (closes channel) if successful.
func (c *ControlService) doFormat(i *IOServerInstance, reformat bool, resp *ctlpb.StorageFormatResp) (err error) {
	var formatFailed bool
	needsSuperblock := true
	needsScmFormat := reformat

	c.log.Infof("formatting storage for I/O server instance %d (reformat: %t)", i.Index, reformat)

	// If not reformatting, check if we need to format SCM.
	if !reformat {
		needsScmFormat, err = i.NeedsScmFormat()
		if err != nil {
			return errors.Wrap(err, "unable to check storage formatting")
		}
		if !needsScmFormat {
			resp.Mrets = append(resp.Mrets, newMntRet(c.log, "format", "",
				ctlpb.ResponseStatus_CTRL_ERR_APP, scm.MsgScmAlreadyFormatted, ""))
		}
	}

	// When SCM format is required, populate mount response with the result.
	if needsScmFormat {
		results := types.ScmMountResults{}
		scmConfig, err := i.scmConfig()
		if err != nil {
			return err
		}
		results = append(results, c.scmFormat(scmConfig.MountPoint))
		formatFailed = results.HasErrors()
		resp.Mrets = results
	} else {
		// If SCM was already formatted, verify if superblock exists.
		needsSuperblock, err = i.NeedsSuperblock()
		if err != nil {
			return errors.Wrap(err, "unable to check instance superblock")
		}
	}

	// If no superblock exists, populate NVMe response with format results.
	if needsSuperblock {
		results := types.NvmeControllerResults{}
		bdevConfig, err := i.bdevConfig()
		if err != nil {
			return err
		}
		// A config with SCM and no block devices is valid.
		// TODO: pull protobuf specifics out of c.nvme into this file.
		if len(bdevConfig.DeviceList) > 0 {
			c.nvme.Format(bdevConfig, &results)
			formatFailed = formatFailed || results.HasErrors()
		}
		resp.Crets = results
	}

	if formatFailed {
		c.log.Error("failure when formatting storage, check RPC response for details")
		return nil
	}

	// TODO: remove use of nvme formatted flag to be consistent with scm
	c.nvme.formatted = true
	i.NotifyStorageReady()

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
func (c *ControlService) StorageFormat(req *ctlpb.StorageFormatReq, stream ctlpb.MgmtCtl_StorageFormatServer) error {
	resp := new(ctlpb.StorageFormatResp)

	c.log.Debugf("received StorageFormat RPC %v; proceeding to instance storage format", req)

	// TODO: We may want to ease this restriction at some point, but having this
	// here for now should help to cut down on shenanigans which might result
	// in data loss.
	if c.harness.IsStarted() {
		return errors.New("cannot format storage with running I/O server instances")
	}

	// temporary scaffolding
	for _, i := range c.harness.Instances() {
		if err := c.doFormat(i, req.Reformat, resp); err != nil {
			return errors.WithMessage(err, "formatting storage")
		}
	}

	if err := stream.Send(resp); err != nil {
		return errors.WithMessagef(err, "sending response (%+v)", resp)
	}

	return nil
}

// Update is currently a placeholder method stubbing SCM module fw update.
func (c *ControlService) ScmUpdate(cfg storage.ScmConfig, req *ctlpb.UpdateScmReq, results *(types.ScmModuleResults)) {
	// respond with single result indicating no implementation
	*results = append(
		*results,
		&ctlpb.ScmModuleResult{
			Loc: &ctlpb.ScmModule_Location{},
			State: newState(c.log, ctlpb.ResponseStatus_CTRL_NO_IMPL,
				scm.MsgScmUpdateNotImpl, "", "scm module update"),
		})
}

// TODO: implement gRPC fw update feature in scm subsystem
// Update delegates to Storage implementation's fw update methods to prepare
// storage for use by DAOS data plane.
//
// Send response containing multiple results of update operations on scm mounts
// and nvme controllers.
func (c *ControlService) StorageUpdate(req *ctlpb.StorageUpdateReq, stream ctlpb.MgmtCtl_StorageUpdateServer) error {
	resp := new(ctlpb.StorageUpdateResp)

	c.log.Debug("received StorageUpdate RPC; proceeding to instance storage update")

	// TODO: We may want to ease this restriction at some point, but having this
	// here for now should help to cut down on shenanigans which might result
	// in data loss.
	if c.harness.IsStarted() {
		return errors.New("cannot update storage with running I/O server instances")
	}

	// temporary scaffolding
	for _, i := range c.harness.Instances() {
		stCfg := i.runner.Config.Storage
		ctrlrResults := types.NvmeControllerResults{}
		c.nvme.Update(stCfg.Bdev, req.Nvme, &ctrlrResults)
		resp.Crets = ctrlrResults

		moduleResults := types.ScmModuleResults{}
		c.ScmUpdate(stCfg.SCM, req.Scm, &moduleResults)
		resp.Mrets = moduleResults
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
func (c *ControlService) StorageBurnIn(req *ctlpb.StorageBurnInReq, stream ctlpb.MgmtCtl_StorageBurnInServer) error {

	c.log.Debug("received StorageBurnIn RPC; proceeding to instance storage burnin")

	return errors.New("StorageBurnIn not implemented")
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
func (c *ControlService) FetchFioConfigPaths(
	empty *ctlpb.EmptyReq, stream ctlpb.MgmtCtl_FetchFioConfigPathsServer) error {

	pluginDir, err := common.GetAbsInstallPath(spdkFioPluginDir)
	if err != nil {
		return err
	}

	paths, err := common.GetFilePaths(pluginDir, "fio")
	if err != nil {
		return err
	}

	for _, path := range paths {
		if err := stream.Send(&ctlpb.FilePath{Path: path}); err != nil {
			return err
		}
	}

	return nil
}

// TODO: to be used during the limitation of burnin feature
//// BurnInNvme runs burn-in validation on NVMe Namespace and returns cmd output
//// in a stream to the gRPC consumer.
//func (c *controlService) BurnInNvme(
//	req *ctlpb.BurnInNvmeReq, stream ctlpb.MgmtCtl_BurnInNvmeServer) error {
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
//			stream.Send(&ctlpb.BurnInNvmeReport{Report: scanner.Text()})
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
