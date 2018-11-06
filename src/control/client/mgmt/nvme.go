//
// (C) Copyright 2018 Intel Corporation.
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

package mgmt

import (
	"io"
	"time"

	pb "github.com/daos-stack/daos/src/control/mgmt/proto"

	"golang.org/x/net/context"
)

// ListNvmeCtrlrs returns NVMe controllers in protobuf format.
func (mc *DAOSMgmtClient) ListNvmeCtrlrs() (cs []*pb.NvmeController, err error) {
	if mc.Connected() == false {
		return nil, errConnect
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	stream, err := mc.client.ListNvmeCtrlrs(ctx, &pb.EmptyParams{})
	if err != nil {
		return
	}

	var c *pb.NvmeController
	for {
		c, err = stream.Recv()
		if err == io.EOF {
			err = nil
			break
		} else if err != nil {
			return
		}
		cs = append(cs, c)
	}

	return
}

// ListNvmeNss returns NVMe namespaces in protobuf format.
func (mc *DAOSMgmtClient) ListNvmeNss(ctrlr *pb.NvmeController) (
	nss []*pb.NvmeNamespace, err error) {
	if mc.Connected() == false {
		return nil, errConnect
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	stream, err := mc.client.ListNvmeNss(ctx, ctrlr)
	if err != nil {
		return
	}
	var ns *pb.NvmeNamespace
	for {
		ns, err = stream.Recv()
		if err == io.EOF {
			err = nil
			break
		} else if err != nil {
			return
		}
		nss = append(nss, ns)
	}

	return
}

// UpdateNvmeCtrlr updates firmware of a given controller.
// Returns new firmware revision.
func (mc *DAOSMgmtClient) UpdateNvmeCtrlr(
	params *pb.UpdateNvmeCtrlrParams) (string, error) {
	if mc.Connected() == false {
		return "", errConnect
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	ctrlr, err := mc.client.UpdateNvmeCtrlr(ctx, params)
	if err != nil {
		return "", err
	}

	return ctrlr.Fwrev, nil
}

// FetchFioConfigPaths retrieves absolute file paths for fio configurations
// residing in spdk fio_plugin directory on server.
func (mc *DAOSMgmtClient) FetchFioConfigPaths() (paths []string, err error) {
	if mc.Connected() == false {
		return nil, errConnect
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	stream, err := mc.client.FetchFioConfigPaths(ctx, &pb.EmptyParams{})
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
func (mc *DAOSMgmtClient) BurnInNvme(ctrlrID int32, configPath string) (
	reports []string, err error) {
	if mc.Connected() == false {
		return nil, errConnect
	}

	// Maximum time limit for BurnIn is 2hrs
	ctx, cancel := context.WithTimeout(context.Background(), 120*time.Minute)
	defer cancel()

	params := &pb.BurnInNvmeParams{
		Ctrlrid: ctrlrID,
		Path:    &pb.FioConfigPath{Path: configPath},
	}
	stream, err := mc.client.BurnInNvme(ctx, params)
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
		println(report.Report)
		reports = append(reports, report.Report)
	}
	return
}
