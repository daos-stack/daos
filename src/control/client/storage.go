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

	"golang.org/x/net/context"
)

// nvmeResult contains results and error of a request
type nvmeResult struct {
	cs NvmeControllers
	e  error
}

// scmResult contains results and error of a request
type scmResult struct {
	mms ScmModules
	e   error
}

// storageResult container for results from multiple storage subsystems queries.
type storageResult struct {
	nvme nvmeResult
	scm  scmResult
}

// cNvmeMap is an alias for query results of NVMe controllers (and
// any residing namespaces) on connected servers keyed on address.
type cNvmeMap map[string]nvmeResult

// cScmMap is an alias for query results of SCM modules installed
// on connected servers keyed on address.
type cScmMap map[string]scmResult

// listNvmeCtrlrs returns NVMe controllers in protobuf format.
func (c *control) listNvmeCtrlrs() (ctrlrs NvmeControllers, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	stream, err := c.client.ListNvmeCtrlrs(ctx, &pb.EmptyParams{})
	if err != nil {
		return
	}

	var ctrlr *pb.NvmeController
	for {
		ctrlr, err = stream.Recv()
		if err == io.EOF {
			err = nil
			break
		} else if err != nil {
			return
		}
		ctrlrs = append(ctrlrs, ctrlr)
	}

	return
}

// listScmModules prints all discovered Storage Class Memory modules installed.
func (c *control) listScmModules() (mms ScmModules, err error) {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	stream, err := c.client.ListScmModules(ctx, &pb.EmptyParams{})
	if err != nil {
		return
	}

	var mm *pb.ScmModule
	for {
		mm, err = stream.Recv()
		if err == io.EOF {
			err = nil
			break
		} else if err != nil {
			return
		}
		mms = append(mms, mm)
	}

	return
}

// listStorageRequest is to be called as a goroutine and returns result
// containing locally attached NVMe and SCM storage devices over channel.
func listStorageRequest(mc Control, ch chan result) {
	sRes := storageResult{}

	ctrlrs, err := mc.listNvmeCtrlrs()
	sRes.nvme = nvmeResult{ctrlrs, err}

	mms, err := mc.listScmModules()
	sRes.scm = scmResult{mms, err}

	ch <- result{mc.getAddress(), sRes, nil} // result.err is ignored
}

// ListStorage returns locally-attached nonvolatile storage devices for each
// connected server.
func (c *connList) ListStorage() (cNvmeMap, cScmMap) {
	cResults := c.makeRequests(listStorageRequest)
	cCtrlrs := make(cNvmeMap) // mapping of server address to NVMe SSDs
	cModules := make(cScmMap) // mapping of server address to SCM modules

	for _, res := range cResults {
		// we want to extract obj regardless of error as may only refer
		// to one of the subsystems, ignore res.err
		storageRes, ok := res.value.(storageResult)
		if !ok {
			err := fmt.Errorf(
				"type assertion failed, wanted %+v got %+v",
				storageResult{}, res.value)

			cCtrlrs[res.address] = nvmeResult{nil, err}
			cModules[res.address] = scmResult{nil, err}
			continue
		}

		cCtrlrs[res.address] = storageRes.nvme
		cModules[res.address] = storageRes.scm
	}

	return cCtrlrs, cModules
}

// UpdateNvmeCtrlr updates firmware of a given controller.
// Returns new firmware revision.
func (c *control) UpdateNvmeCtrlr(
	params *pb.UpdateNvmeParams) (string, error) {

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	ctrlr, err := c.client.UpdateNvmeCtrlr(ctx, params)
	if err != nil {
		return "", err
	}

	return ctrlr.Fwrev, nil
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
	var p *pb.FioConfigPath
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

	params := &pb.BurnInNvmeParams{
		Pciaddr: pciAddr,
		Path:    &pb.FioConfigPath{Path: configPath},
	}
	stream, err := c.client.BurnInNvme(ctx, params)
	if err != nil {
		return
	}
	var report *pb.BurnInNvmeReport
	for {
		report, err = stream.Recv()
		if err == io.EOF {
			err = nil
			break
		} else if err != nil {
			return
		}
		fmt.Println(report.Report)
		reports = append(reports, report.Report)
	}
	return
}
