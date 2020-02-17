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

	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
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

func (c *connList) setPrepareErr(cNvmePrepare NvmePrepareResults, cScmPrepare ScmPrepareResults,
	address string, err error) {

	cNvmePrepare[address] = &NvmePrepareResult{Err: err}
	cScmPrepare[address] = &ScmPrepareResult{Err: err}
}

func (c *connList) getNvmePrepareResult(resp *ctlpb.PrepareNvmeResp) (nvmeResult *NvmePrepareResult) {
	nvmeResult = &NvmePrepareResult{}

	nState := resp.GetState()
	if nState.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
		msg := nState.GetError()
		if msg == "" {
			msg = fmt.Sprintf("nvme %+v", nState.GetStatus())
		}
		nvmeResult.Err = errors.Errorf(msg)
		return
	}

	return
}

func (c *connList) getScmPrepareResult(resp *ctlpb.PrepareScmResp) (scmResult *ScmPrepareResult) {
	var err error
	scmResult = &ScmPrepareResult{}

	sState := resp.GetState()
	if sState.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
		msg := sState.GetError()
		if msg == "" {
			msg = fmt.Sprintf("scm %+v", sState.GetStatus())
		}
		scmResult.Err = errors.Errorf(msg)
		return
	}

	namespaces := resp.GetNamespaces()
	scmResult.Namespaces, err = (*proto.ScmNamespaces)(&namespaces).ToNative()
	if err != nil {
		scmResult.Err = errors.Wrap(err, "scm namespaces")
		return
	}

	return
}

// StoragePrepare returns details of nonvolatile storage devices attached to each
// remote server. Data received over channel from requests running in parallel.
func (c *connList) StoragePrepare(req *ctlpb.StoragePrepareReq) *StoragePrepareResp {
	cResults := c.makeRequests(req, storagePrepareRequest)
	cNvmePrepare := make(NvmePrepareResults) // mapping of server address to NVMe SSDs
	cScmPrepare := make(ScmPrepareResults)   // mapping of server address to SCM modules/namespaces
	servers := make([]string, 0, len(cResults))

	for addr, res := range cResults {
		if res.Err != nil { // likely to be comms err
			c.setPrepareErr(cNvmePrepare, cScmPrepare, addr, res.Err)
			continue
		}

		resp, ok := res.Value.(*ctlpb.StoragePrepareResp)
		if !ok {
			c.setPrepareErr(cNvmePrepare, cScmPrepare, addr,
				fmt.Errorf(msgBadType, &ctlpb.StoragePrepareResp{}, res.Value))
			continue
		}

		if resp.GetNvme() == nil || resp.GetScm() == nil {
			c.setPrepareErr(cNvmePrepare, cScmPrepare, addr,
				fmt.Errorf("malformed response, missing submessage %+v", resp))
			continue
		}

		cNvmePrepare[addr] = c.getNvmePrepareResult(resp.Nvme)
		cScmPrepare[addr] = c.getScmPrepareResult(resp.Scm)
		servers = append(servers, addr)
	}

	sort.Strings(servers)

	return &StoragePrepareResp{Servers: servers, Nvme: cNvmePrepare, Scm: cScmPrepare}
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

func (c *connList) getNvmeResult(resp *ctlpb.ScanNvmeResp) (nvmeResult *NvmeScanResult) {
	nvmeResult = &NvmeScanResult{}

	nState := resp.GetState()
	if nState.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
		msg := nState.GetError()
		if msg == "" {
			msg = fmt.Sprintf("nvme %+v", nState.GetStatus())
		}
		nvmeResult.Err = errors.Errorf(msg)
		return
	}

	nvmeResult.Ctrlrs = resp.GetCtrlrs()
	return
}

func (c *connList) getScmResult(resp *ctlpb.ScanScmResp) (scmResult *ScmScanResult) {
	var err error
	scmResult = &ScmScanResult{}

	sState := resp.GetState()
	if sState.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
		msg := sState.GetError()
		if msg == "" {
			msg = fmt.Sprintf("scm %+v", sState.GetStatus())
		}
		scmResult.Err = errors.Errorf(msg)
		return
	}

	modules := resp.GetModules()
	scmResult.Modules, err = (*proto.ScmModules)(&modules).ToNative()
	if err != nil {
		scmResult.Err = errors.Wrap(err, "scm modules")
		return
	}

	namespaces := resp.GetNamespaces()
	scmResult.Namespaces, err = (*proto.ScmNamespaces)(&namespaces).ToNative()
	if err != nil {
		scmResult.Err = errors.Wrap(err, "scm namespaces")
		return
	}

	return
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

		cNvmeScan[addr] = c.getNvmeScanResult(resp.Nvme)
		cScmScan[addr] = c.getScmScanResult(resp.Scm)
		servers = append(servers, addr)
	}

	sort.Strings(servers)

	return &StorageScanResp{Servers: servers, Nvme: cNvmeScan, Scm: cScmScan}
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

		sRes.Nvme = resp.Crets
		sRes.Scm = resp.Mrets

		ch <- ClientResult{mc.getAddress(), sRes, nil}
	}
}

// StorageFormat prepares nonvolatile storage devices attached to each
// remote server in the connection list for use with DAOS.
func (c *connList) StorageFormat(reformat bool) StorageFormatResults {
	req := &ctlpb.StorageFormatReq{Reformat: reformat}
	cResults := c.makeRequests(req, StorageFormatRequest)
	formatResults := make(StorageFormatResults)

	for _, res := range cResults {
		if res.Err != nil {
			formatResults[res.Address] = StorageFormatResult{Err: res.Err}
			continue
		}

		storageRes, ok := res.Value.(StorageFormatResult)
		if !ok {
			err := fmt.Errorf(msgBadType, StorageFormatResult{}, res.Value)

			formatResults[res.Address] = StorageFormatResult{Err: err}
			continue
		}

		formatResults[res.Address] = storageRes
	}

	return formatResults
}
