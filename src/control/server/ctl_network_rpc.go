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
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	pb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

func (c *NetworkScanService) RequestProviderList(ctx context.Context, in *pb.ProviderListRequest) (*pb.ProviderListReply, error) {
	c.nsslog.Debugf("RequestProviderList() Received")
	results := netdetect.GetSupportedProviderString()
	c.nsslog.Debugf("The DAOS system supports the following providers: %s", results)
	return &pb.ProviderListReply{Provider: results}, nil
}

func (c *NetworkScanService) RequestDeviceScan(in *pb.DeviceScanRequest, stream pb.MgmtCtl_RequestDeviceScanServer) error {
	c.nsslog.Debugf("RequestDeviceScan() Received request: %s", in.GetProvider())

	results, err := netdetect.ScanFabric(in.GetProvider())
	if err != nil {
		return errors.WithMessage(err, "failed to execute the fabric and device scan")
	}
	for _, sr := range results {
		c.nsslog.Infof("\n******* Sending this item ******* \n%v\n\n", sr)
		err := stream.Send(&pb.DeviceScanReply{Provider: sr.Provider, Device: sr.DeviceName, Numanode: uint32(sr.NUMANode)})
		if err != nil {
			return err
		}
	}
	c.nsslog.Infof("\n******* Done sending items ******* \n\n")
	return nil
}
