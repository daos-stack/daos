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

// scanStorage returns all discovered SCM and NVMe storage devices discovered on
// a remote server, in protobuf message format, by calling over gRPC channel.
func (c *control) scanStorage() (*pb.ScanStorageResp, error) {

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	return c.client.ScanStorage(ctx, &pb.ScanStorageParams{})
}

// scanStorageRequest is to be called as a goroutine and returns result
// containing remote server's storage device details over channel with
// response from ScanStorage rpc.
func scanStorageRequest(mc Control, ch chan ClientResult) {
	sRes := storageResult{}

	resp, err := mc.scanStorage()
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
		sRes.nvme.Err = errors.Errorf(msg)
	} else {
		sRes.nvme.Ctrlrs = resp.Ctrlrs
	}

	sState := resp.GetScmstate()
	if sState.GetStatus() != pb.ResponseStatus_CTRL_SUCCESS {
		msg := sState.GetError()
		if msg == "" {
			msg = fmt.Sprintf("scm %+v", sState.GetStatus())
		}
		sRes.scm.Err = errors.Errorf(msg)
	} else {
		sRes.scm.Modules = resp.Modules
	}

	ch <- ClientResult{mc.getAddress(), sRes, nil}
}

// ScanStorage returns details of nonvolatile storage devices attached to each
// remote server. Data received over channel from requests running in parallel.
func (c *connList) ScanStorage() (ClientNvmeMap, ClientScmMap) {
	cResults := c.makeRequests(scanStorageRequest)
	cCtrlrs := make(ClientNvmeMap) // mapping of server address to NVMe SSDs
	cModules := make(ClientScmMap) // mapping of server address to SCM modules

	for _, res := range cResults {
		if res.Err != nil {
			cCtrlrs[res.Address] = NvmeResult{Err: res.Err}
			cModules[res.Address] = ScmResult{Err: res.Err}
			continue
		}

		storageRes, ok := res.Value.(storageResult)
		if !ok {
			err := fmt.Errorf(
				"type assertion failed, wanted %+v got %+v",
				storageResult{}, res.Value)

			cCtrlrs[res.Address] = NvmeResult{Err: err}
			cModules[res.Address] = ScmResult{Err: err}
			continue
		}

		cCtrlrs[res.Address] = storageRes.nvme
		cModules[res.Address] = storageRes.scm
	}

	return cCtrlrs, cModules
}

// formatStorage attempts to format nonvolatile storage devices on a remote
// server by calling over gRPC channel.
func (c *control) formatStorage(ctx context.Context) (
	pb.MgmtControl_FormatStorageClient, error) {

	return c.client.FormatStorage(ctx, &pb.FormatStorageParams{})
}

// formatStorageRequest is to be called as a goroutine.
//
// Calls control formatStorage routine which activates FormatStorage service rpc
// and returns an open stream handle. Receive on stream and send ClientResult
// over channel for each.
func formatStorageRequest(mc Control, ch chan ClientResult) {
	sRes := storageResult{}
	// Maximum time limit for format is 2hrs to account for lengthy low
	// level formatting of multiple devices sequentially.
	ctx, cancel := context.WithTimeout(context.Background(), 120*time.Minute)
	defer cancel()

	stream, err := mc.formatStorage(ctx)
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
			log.Errorf(
				"%v.FormatStorage(_) = _, %v\n",
				mc, err)
			ch <- ClientResult{mc.getAddress(), nil, err}
			return // recv err
		}

		sRes.nvme.Responses = resp.Crets
		sRes.mount.Responses = resp.Mrets

		ch <- ClientResult{mc.getAddress(), sRes, nil}
	}
}

// FormatStorage prepares nonvolatile storage devices attached to each
// remote server in the connection list for use with DAOS.
func (c *connList) FormatStorage() (ClientNvmeMap, ClientMountMap) {
	cResults := c.makeRequests(formatStorageRequest)
	cNvmeResults := make(ClientNvmeMap) // srv address:NVMe SSDs
	cScmResults := make(ClientMountMap) // srv address:SCM mounts

	for _, res := range cResults {
		if res.Err != nil {
			cNvmeResults[res.Address] = NvmeResult{Err: res.Err}
			cScmResults[res.Address] = MountResult{Err: res.Err}
			continue
		}

		storageRes, ok := res.Value.(storageResult)
		if !ok {
			err := fmt.Errorf(
				"type assertion failed, wanted %+v got %+v",
				storageResult{}, res.Value)

			cNvmeResults[res.Address] = NvmeResult{Err: err}
			cScmResults[res.Address] = MountResult{Err: err}
			continue
		}

		cNvmeResults[res.Address] = storageRes.nvme
		cScmResults[res.Address] = storageRes.mount
		// storageRes.scm ignored for format
	}

	return cNvmeResults, cScmResults
}

// TODO: implement update and burnin in a similar way to format
//	updateStorage(pb.UpdateStorageParams) (*pb.UpdateStorageResp, error)
//	burninStorage(pb.BurninStorageParams) (*pb.BurninStorageResp, error)

// updateStorage updates firmware of a given controller over grpc channel.
func (c *control) UpdateStorage(params *pb.UpdateStorageParams) (
	pb.MgmtControl_UpdateStorageClient, error) {

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	return c.client.UpdateStorage(ctx, params)
}

// FetchFioConfigPaths retrieves absolute file paths for fio configurations
// residing in spdk fio_plugin directory on server.
func (c *control) FetchFioConfigPaths() (paths []string, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	stream, err := c.client.FetchFioConfigPaths(ctx, &pb.EmptyParams{})
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

	params := &pb.BurninNvmeParams{
		//		Pciaddr: pciAddr,
		Fioconfig: &pb.FilePath{Path: configPath},
	}
	_, err = c.client.BurninStorage(
		ctx, &pb.BurninStorageParams{Nvme: params})
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
