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

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/log"
	"github.com/pkg/errors"
	"golang.org/x/net/context"
)

const (
	msgOpenStreamFail = "client.UpdateStorage() open stream failed: "
	msgStreamRecv     = "%T recv() failed"
	msgTypeAssert     = "type assertion failed, wanted %T got %T"
)

// scanStorageRequest returns all discovered SCM and NVMe storage devices
// discovered on a remote server by calling over gRPC channel.
func scanStorageRequest(mc Control, req interface{}, ch chan ClientResult) {
	sRes := storageResult{}

	resp, err := mc.getCtlClient().ScanStorage(
		context.Background(), &pb.ScanStorageReq{})
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err} // return comms error
		return
	}

	// process storage subsystem responses
	nState := resp.GetNvmestate()
	if nState.GetStatus() != pb.ResponseStatus_CTRL_SUCCESS {
		msg := nState.GetError()
		if msg == "" {
			msg = fmt.Sprintf("nvme %+v", nState.GetStatus())
		}
		sRes.nvmeCtrlr.Err = errors.Errorf(msg)
	} else {
		sRes.nvmeCtrlr.Ctrlrs = resp.Ctrlrs
	}

	sState := resp.GetScmstate()
	if sState.GetStatus() != pb.ResponseStatus_CTRL_SUCCESS {
		msg := sState.GetError()
		if msg == "" {
			msg = fmt.Sprintf("scm %+v", sState.GetStatus())
		}
		sRes.scmModule.Err = errors.Errorf(msg)
	} else {
		sRes.scmModule.Modules = resp.Modules
	}

	ch <- ClientResult{mc.getAddress(), sRes, nil}
}

// ScanStorage returns details of nonvolatile storage devices attached to each
// remote server. Data received over channel from requests running in parallel.
func (c *connList) ScanStorage() (ClientCtrlrMap, ClientModuleMap) {
	cResults := c.makeRequests(nil, scanStorageRequest)
	cCtrlrs := make(ClientCtrlrMap)   // mapping of server address to NVMe SSDs
	cModules := make(ClientModuleMap) // mapping of server address to SCM modules

	for _, res := range cResults {
		if res.Err != nil {
			cCtrlrs[res.Address] = CtrlrResults{Err: res.Err}
			cModules[res.Address] = ModuleResults{Err: res.Err}
			continue
		}

		storageRes, ok := res.Value.(storageResult)
		if !ok {
			err := fmt.Errorf(msgBadType, storageResult{}, res.Value)

			cCtrlrs[res.Address] = CtrlrResults{Err: err}
			cModules[res.Address] = ModuleResults{Err: err}
			continue
		}

		cCtrlrs[res.Address] = storageRes.nvmeCtrlr
		cModules[res.Address] = storageRes.scmModule
		// TODO: return SCM region/mount info in storageRes.scmMount
	}

	return cCtrlrs, cModules
}

// formatStorageRequest attempts to format nonvolatile storage devices on a
// remote server over gRPC.
//
// Calls control formatStorage routine which activates FormatStorage service rpc
// and returns an open stream handle. Receive on stream and send ClientResult
// over channel for each.
func formatStorageRequest(mc Control, parms interface{}, ch chan ClientResult) {
	sRes := storageResult{}

	// Maximum time limit for format is 2hrs to account for lengthy low
	// level formatting of multiple devices sequentially.
	ctx, cancel := context.WithTimeout(context.Background(), 120*time.Minute)
	defer cancel()

	stream, err := mc.getCtlClient().FormatStorage(ctx, &pb.FormatStorageReq{})
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
			log.Errorf(err.Error())
			ch <- ClientResult{mc.getAddress(), nil, err}
			return // recv err
		}

		sRes.nvmeCtrlr.Responses = resp.Crets
		sRes.scmMount.Responses = resp.Mrets

		ch <- ClientResult{mc.getAddress(), sRes, nil}
	}
}

// FormatStorage prepares nonvolatile storage devices attached to each
// remote server in the connection list for use with DAOS.
func (c *connList) FormatStorage() (ClientCtrlrMap, ClientMountMap) {
	cResults := c.makeRequests(nil, formatStorageRequest)
	cCtrlrResults := make(ClientCtrlrMap) // srv address:NVMe SSDs
	cMountResults := make(ClientMountMap) // srv address:SCM mounts

	for _, res := range cResults {
		if res.Err != nil {
			cCtrlrResults[res.Address] = CtrlrResults{Err: res.Err}
			cMountResults[res.Address] = MountResults{Err: res.Err}
			continue
		}

		storageRes, ok := res.Value.(storageResult)
		if !ok {
			err := fmt.Errorf(msgBadType, storageResult{}, res.Value)

			cCtrlrResults[res.Address] = CtrlrResults{Err: err}
			cMountResults[res.Address] = MountResults{Err: err}
			continue
		}

		cCtrlrResults[res.Address] = storageRes.nvmeCtrlr
		cMountResults[res.Address] = storageRes.scmMount
		// storageRes.scmModule ignored for update
	}

	return cCtrlrResults, cMountResults
}

// updateStorageRequest attempts to update firmware on nonvolatile storage
// devices on a remote server by calling over gRPC channel.
//
// Calls control updateStorage routine which activates UpdateStorage service rpc
// and returns an open stream handle. Receive on stream and send ClientResult
// over channel for each.
func updateStorageRequest(
	mc Control, req interface{}, ch chan ClientResult) {

	sRes := storageResult{}

	// Maximum time limit for update is 2hrs to account for lengthy firmware
	// updates of multiple devices sequentially.
	ctx, cancel := context.WithTimeout(context.Background(), 120*time.Minute)
	defer cancel()

	var updateReq *pb.UpdateStorageReq
	switch v := req.(type) {
	case *pb.UpdateStorageReq:
		updateReq = v
	default:
		err := errors.Errorf(
			msgTypeAssert, pb.UpdateStorageReq{}, req)

		log.Errorf(err.Error())
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // type err
	}

	stream, err := mc.getCtlClient().UpdateStorage(ctx, updateReq)
	if err != nil {
		log.Errorf(err.Error())
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
			log.Errorf(err.Error())
			ch <- ClientResult{mc.getAddress(), nil, err}
			return // recv err
		}

		sRes.nvmeCtrlr.Responses = resp.Crets
		sRes.scmModule.Responses = resp.Mrets

		ch <- ClientResult{mc.getAddress(), sRes, nil}
	}
}

// UpdateStorage prepares nonvolatile storage devices attached to each
// remote server in the connection list for use with DAOS.
func (c *connList) UpdateStorage(req *pb.UpdateStorageReq) (
	ClientCtrlrMap, ClientModuleMap) {

	cResults := c.makeRequests(req, updateStorageRequest)
	cCtrlrResults := make(ClientCtrlrMap)   // srv address:NVMe SSDs
	cModuleResults := make(ClientModuleMap) // srv address:SCM modules

	for _, res := range cResults {
		if res.Err != nil {
			cCtrlrResults[res.Address] = CtrlrResults{Err: res.Err}
			cModuleResults[res.Address] = ModuleResults{Err: res.Err}
			continue
		}

		storageRes, ok := res.Value.(storageResult)
		if !ok {
			err := fmt.Errorf(
				msgTypeAssert, storageResult{}, res.Value)

			cCtrlrResults[res.Address] = CtrlrResults{Err: err}
			cModuleResults[res.Address] = ModuleResults{Err: err}
			continue
		}

		cCtrlrResults[res.Address] = storageRes.nvmeCtrlr
		cModuleResults[res.Address] = storageRes.scmModule
		// storageRes.scmMount ignored for update
	}

	return cCtrlrResults, cModuleResults
}

// TODO: implement burnin in a similar way to format

// FetchFioConfigPaths retrieves absolute file paths for fio configurations
// residing in spdk fio_plugin directory on server.
func (c *control) FetchFioConfigPaths() (paths []string, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	stream, err := c.getCtlClient().FetchFioConfigPaths(ctx, &pb.EmptyReq{})
	if err != nil {
		return
	}
	var p *pb.FilePath
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

	req := &pb.BurninNvmeReq{
		Fioconfig: &pb.FilePath{Path: configPath},
	}
	_, err = c.getCtlClient().BurninStorage(
		ctx, &pb.BurninStorageReq{Nvme: req})
	if err != nil {
		return
	}
	//	var report *pb.BurnInNvmeReport
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
