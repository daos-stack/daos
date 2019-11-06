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
	"sort"
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

// storageScanRequest returns all discovered SCM and NVMe storage devices
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

func (c *connList) getNvmeResult(resp *ctlpb.ScanNvmeResp) *NvmeScanResult {
	nvmeResult := &NvmeScanResult{}

	nState := resp.GetState()
	if nState.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
		msg := nState.GetError()
		if msg == "" {
			msg = fmt.Sprintf("nvme %+v", nState.GetStatus())
		}
		nvmeResult.Err = errors.Errorf(msg)
		return nvmeResult
	}

	nvmeResult.Ctrlrs = resp.GetCtrlrs()
	return nvmeResult
}

func (c *connList) getScmResult(resp *ctlpb.ScanScmResp) *ScmScanResult {
	scmResult := &ScmScanResult{}

	sState := resp.GetState()
	if sState.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
		msg := sState.GetError()
		if msg == "" {
			msg = fmt.Sprintf("scm %+v", sState.GetStatus())
		}
		scmResult.Err = errors.Errorf(msg)
		return scmResult
	}

	scmResult.Modules = scmModulesFromPB(resp.GetModules())
	scmResult.Namespaces = scmNamespacesFromPB(resp.GetPmems())
	return scmResult
}

// StorageScan returns details of nonvolatile storage devices attached to each
// remote server. Critical storage device health information is also returned
// for all NVMe SSDs discovered. Data received over channel from requests
// in parallel. If health param is true, stringer repr will include stats.
func (c *connList) StorageScan(p *StorageScanReq) *StorageScanResp {
	cResults := c.makeRequests(nil, storageScanRequest)
	cNvmeScan := make(NvmeScanResults) // mapping of server address to NVMe SSDs
	cScmScan := make(ScmScanResults)   // mapping of server address to SCM modules/namespaces
	servers := make([]string, 0, len(cResults))

	for addr, res := range cResults {
		if res.Err != nil { // likely to be comms err
			c.setScanErr(cNvmeScan, cScmScan, addr, res.Err)
			continue
		}

		resp, ok := res.Value.(*ctlpb.StorageScanResp)
		if !ok {
			c.setScanErr(cNvmeScan, cScmScan, addr,
				fmt.Errorf(msgBadType, &ctlpb.StorageScanResp{}, res.Value))
			continue
		}

		if resp.GetNvme() == nil || resp.GetScm() == nil {
			c.setScanErr(cNvmeScan, cScmScan, addr,
				fmt.Errorf("malformed response, missing submessage %+v", resp))
			continue
		}

		cNvmeScan[addr] = c.getNvmeResult(resp.Nvme)
		cScmScan[addr] = c.getScmResult(resp.Scm)
		servers = append(servers, addr)
	}

	sort.Strings(servers)

	return &StorageScanResp{
		summary: p.Summary, Servers: servers, Nvme: cNvmeScan, Scm: cScmScan,
	}
}

// StorageFormatRequest attempts to format nonvolatile storage devices on a
// remote server over gRPC.
//
// Calls control StorageFormat routine which activates StorageFormat service rpc
// and returns an open stream handle. Receive on stream and send ClientResult
// over channel for each.
func StorageFormatRequest(mc Control, parms interface{}, ch chan ClientResult) {
	sRes := StorageFormatResult{}

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

		storageRes, ok := res.Value.(StorageFormatResult)
		if !ok {
			err := fmt.Errorf(msgBadType, StorageFormatResult{}, res.Value)

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
