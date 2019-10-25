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

package client

import (
	"fmt"
	"io"
	"time"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	types "github.com/daos-stack/daos/src/control/common/storage"
)

const (
	msgOpenStreamFail = "client.StorageUpdate() open stream failed: "
	msgStreamRecv     = "%T recv() failed"
	msgTypeAssert     = "type assertion failed, wanted %T got %T"
)

// storagePrepareRequest returns results of SCM and NVMe prepare actions
// on a remote server by calling over gRPC channel.
func storagePrepareRequest(mc Control, req interface{}, ch chan ClientResult) {
	prepareReq, ok := req.(*ctlpb.StoragePrepareReq)
	if !ok {
		err := errors.Errorf(msgTypeAssert, &ctlpb.StoragePrepareReq{}, req)

		mc.logger().Errorf(err.Error())
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // type err
	}

	resp, err := mc.getCtlClient().StoragePrepare(context.Background(), prepareReq)
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err} // return comms error
		return
	}

	ch <- ClientResult{mc.getAddress(), resp, nil}
}

// StoragePrepare returns details of nonvolatile storage devices attached to each
// remote server. Data received over channel from requests running in parallel.
func (c *connList) StoragePrepare(req *ctlpb.StoragePrepareReq) ResultMap {
	return c.makeRequests(req, storagePrepareRequest)
}

// storageScan/etc/equest returns all discovered SCM and NVMe storage devices
// discovered on a remote server by calling over gRPC channel.
func storageScanRequest(mc Control, req interface{}, ch chan ClientResult) {
	resp, err := mc.getCtlClient().StorageScan(context.Background(), &ctlpb.StorageScanReq{})
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err} // return comms error
		return
	}

	ch <- ClientResult{mc.getAddress(), resp, nil}
}

func (c *connList) setScanErr(cNvmeScan NvmeScanResults, cScmScan ScmScanResults,
	address string, err error) {

	cNvmeScan[address] = &NvmeScanResult{Err: err}
	cScmScan[address] = &ScmScanResult{Err: err}
}

// StorageScan returns details of nonvolatile storage devices attached to each
// remote server. Critical storage device health information is also returned
// for all NVMe SSDs discovered. Data received over channel from requests
// running in parallel.
func (c *connList) StorageScan() (NvmeScanResults, ScmScanResults) {
	cResults := c.makeRequests(nil, storageScanRequest)
	cNvmeScan := make(NvmeScanResults) // mapping of server address to NVMe SSDs
	cScmScan := make(ScmScanResults)   // mapping of server address to SCM modules/namespaces

	for _, res := range cResults {
		if res.Err != nil { // likely to be comms err
			c.setScanErr(cNvmeScan, cScmScan, res.Address, res.Err)
			continue
		}

		resp, ok := res.Value.(ctlpb.StorageScanResp)
		if !ok {
			c.setScanErr(cNvmeScan, cScmScan, res.Address,
				fmt.Errorf(msgBadType, ctlpb.StorageScanResp{}, res.Value))
			continue
		}

		if resp.GetNvme() == nil || resp.GetScm() == nil {
			c.setScanErr(cNvmeScan, cScmScan, res.Address,
				fmt.Errorf("malformed response, missing submessage %+v", resp))
			continue
		}

		// Process storage subsystem responses.
		nvmeResult := &NvmeScanResult{}
		scmResult := &ScmScanResult{}

		nState := resp.Nvme.GetState()
		if nState.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
			msg := nState.GetError()
			if msg == "" {
				msg = fmt.Sprintf("nvme %+v", nState.GetStatus())
			}
			nvmeResult.Err = errors.Errorf(msg)
		} else {
			nvmeResult.Resp = resp.Nvme.Ctrlrs
		}

		sState := resp.Scm.GetState()
		if sState.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
			msg := sState.GetError()
			if msg == "" {
				msg = fmt.Sprintf("scm %+v", sState.GetStatus())
			}
			scmResult.Err = errors.Errorf(msg)
		} else {
			// reconstruct scm_types.ScanResponse from protobuf msg
			scmResult.Resp.Modules = scmModulesFromPB(resp.Scm.GetModules())
			scmResult.Resp.Namespaces = scmNamespacesFromPB(resp.Scm.GetPmems())
			// FIXME: populate ScanResponse.ScmState
		}

		cNvmeScan[res.Address] = nvmeResult
		cScmScan[res.Address] = scmResult
	}

	return cNvmeScan, cScmScan
}

// StorageFormatRequest attempts to format nonvolatile storage devices on a
// remote server over gRPC.
//
// Calls control StorageFormat routine which activates StorageFormat service rpc
// and returns an open stream handle. Receive on stream and send ClientResult
// over channel for each.
func StorageFormatRequest(mc Control, parms interface{}, ch chan ClientResult) {
	sRes := StorageResult{}

	// Maximum time limit for format is 2hrs to account for lengthy low
	// level formatting of multiple devices sequentially.
	ctx, cancel := context.WithTimeout(context.Background(), 120*time.Minute)
	defer cancel()

	req := &ctlpb.StorageFormatReq{}
	if parms != nil {
		if preq, ok := parms.(*ctlpb.StorageFormatReq); ok {
			req = preq
		}
	}

	stream, err := mc.getCtlClient().StorageFormat(ctx, req)
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // stream err
	}

	for {
		resp, err := stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			err := errors.Wrapf(err, msgStreamRecv, stream)
			mc.logger().Errorf(err.Error())
			ch <- ClientResult{mc.getAddress(), nil, err}
			return // recv err
		}

		sRes.nvmeCtrlr.Responses = resp.Crets
		sRes.scmMount.Responses = resp.Mrets

		ch <- ClientResult{mc.getAddress(), sRes, nil}
	}
}

// StorageFormat prepares nonvolatile storage devices attached to each
// remote server in the connection list for use with DAOS.
func (c *connList) StorageFormat(reformat bool) (ClientCtrlrMap, ClientMountMap) {
	req := &ctlpb.StorageFormatReq{Reformat: reformat}
	cResults := c.makeRequests(req, StorageFormatRequest)
	cCtrlrResults := make(ClientCtrlrMap) // srv address:NVMe SSDs
	cMountResults := make(ClientMountMap) // srv address:SCM mounts

	for _, res := range cResults {
		if res.Err != nil {
			cCtrlrResults[res.Address] = types.CtrlrResults{Err: res.Err}
			cMountResults[res.Address] = types.MountResults{Err: res.Err}
			continue
		}

		storageRes, ok := res.Value.(StorageResult)
		if !ok {
			err := fmt.Errorf(msgBadType, StorageResult{}, res.Value)

			cCtrlrResults[res.Address] = types.CtrlrResults{Err: err}
			cMountResults[res.Address] = types.MountResults{Err: err}
			continue
		}

		cCtrlrResults[res.Address] = storageRes.nvmeCtrlr
		cMountResults[res.Address] = storageRes.scmMount
		// storageRes.scmModule ignored for update
	}

	return cCtrlrResults, cMountResults
}

// storageUpdateRequest attempts to update firmware on nonvolatile storage
// devices on a remote server by calling over gRPC channel.
//
// Calls control storageUpdate routine which activates StorageUpdate service rpc
// and returns an open stream handle. Receive on stream and send ClientResult
// over channel for each.
func storageUpdateRequest(
	mc Control, req interface{}, ch chan ClientResult) {

	sRes := StorageResult{}

	// Maximum time limit for update is 2hrs to account for lengthy firmware
	// updates of multiple devices sequentially.
	ctx, cancel := context.WithTimeout(context.Background(), 120*time.Minute)
	defer cancel()

	var updateReq *ctlpb.StorageUpdateReq
	switch v := req.(type) {
	case *ctlpb.StorageUpdateReq:
		updateReq = v
	default:
		err := errors.Errorf(
			msgTypeAssert, ctlpb.StorageUpdateReq{}, req)

		mc.logger().Errorf(err.Error())
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // type err
	}

	stream, err := mc.getCtlClient().StorageUpdate(ctx, updateReq)
	if err != nil {
		mc.logger().Errorf(err.Error())
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // stream err
	}

	for {
		resp, err := stream.Recv()
		if err == io.EOF {
			break
		}
		if err != nil {
			err := errors.Wrapf(err, msgStreamRecv, stream)
			mc.logger().Errorf(err.Error())
			ch <- ClientResult{mc.getAddress(), nil, err}
			return // recv err
		}

		sRes.nvmeCtrlr.Responses = resp.Crets
		sRes.scmModule.Responses = resp.Mrets

		ch <- ClientResult{mc.getAddress(), sRes, nil}
	}
}

// TODO: re-enable when feature is supported
// StorageUpdate prepares nonvolatile storage devices attached to each
// remote server in the connection list for use with DAOS.
//func (c *connList) StorageUpdate(req *ctlpb.StorageUpdateReq) (ClientCtrlrMap, ClientModuleMap) {
//
//	cResults := c.makeRequests(req, storageUpdateRequest)
//	cCtrlrResults := make(ClientCtrlrMap)   // srv address:NVMe SSDs
//	cModuleResults := make(ClientModuleMap) // srv address:SCM modules
//
//	for _, res := range cResults {
//		if res.Err != nil {
//			cCtrlrResults[res.Address] = types.CtrlrResults{Err: res.Err}
//			cModuleResults[res.Address] = types.ModuleResults{Err: res.Err}
//			continue
//		}
//
//		storageRes, ok := res.Value.(StorageResult)
//		if !ok {
//			err := fmt.Errorf(
//				msgTypeAssert, StorageResult{}, res.Value)
//
//			cCtrlrResults[res.Address] = types.CtrlrResults{Err: err}
//			cModuleResults[res.Address] = types.ModuleResults{Err: err}
//			continue
//		}
//
//		cCtrlrResults[res.Address] = storageRes.nvmeCtrlr
//		cModuleResults[res.Address] = storageRes.scmModule
//		// storageRes.scmMount ignored for update
//	}
//
//	return cCtrlrResults, cModuleResults
//}

// TODO: implement burnin in a similar way to format

// FetchFioConfigPaths retrieves absolute file paths for fio configurations
// residing in spdk fio_plugin directory on server.
func (c *control) FetchFioConfigPaths() (paths []string, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	stream, err := c.getCtlClient().FetchFioConfigPaths(ctx, &ctlpb.EmptyReq{})
	if err != nil {
		return
	}
	var p *ctlpb.FilePath
	for {
		p, err = stream.Recv()
		if err == io.EOF {
			err = nil
			break
		} else if err != nil {
			return
		}
		paths = append(paths, p.Path)
	}
	return
}

// BurnInNvme runs burn-in validation on NVMe Namespace and returns cmd output
// in a stream to the gRPC consumer.
func (c *control) BurnInNvme(pciAddr string, configPath string) (
	reports []string, err error) {

	// Maximum time limit for BurnIn is 2hrs
	ctx, cancel := context.WithTimeout(context.Background(), 120*time.Minute)
	defer cancel()

	req := &ctlpb.BurninNvmeReq{
		Fioconfig: &ctlpb.FilePath{Path: configPath},
	}
	_, err = c.getCtlClient().StorageBurnIn(
		ctx, &ctlpb.StorageBurnInReq{Nvme: req})
	if err != nil {
		return
	}
	//	var report *ctlpb.BurnInNvmeReport
	//	for {
	//		report, err = stream.Recv()
	//		if err == io.EOF {
	//			err = nil
	//			break
	//		} else if err != nil {
	//			return
	//		}
	//		fmt.Println(report.Report)
	//		reports = append(reports, report.Report)
	//	}
	return
}
