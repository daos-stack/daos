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
//	"github.com/pkg/errors"
	"golang.org/x/net/context"

//	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/ctl"
//	"github.com/daos-stack/daos/src/control/logging"
//	"github.com/daos-stack/daos/src/control/lib/netdetect"
)

func (c *NetworkScanService) RequestProviderList(ctx context.Context, in *pb.ProviderListRequest) (*pb.ProviderListReply, error) {
	c.log.Debugf("RequestProviderList() Received")
	// Just a quick test to see that we can return a string
	return &pb.ProviderListReply{Provider: "joel's provider list provider"}, nil
}

/*
func (c *NetworkScanService) RequestDeviceScanStreamer(in *pb.DeviceScanRequest, stream pb.MgmtCtl_RequestDeviceScanStreamerServer) error {
	c.nss.log.Debugf("RequestDeviceScanStreamer() Received request: %s", in.GetProvider())

	results, err := netdetect.ScanFabric(in.GetProvider())
	if err != nil {
		return errors.WithMessage(err, "failed to execute the fabric and device scan")
	}
	for _, sr := range results {
		// TBD fill in a reply and stream the results here
		c.log.Infof("\n%v\n\n", sr)
	}

	// For testing, just hard code two replies.
	err1 := stream.Send(&pb.DeviceScanReply{Provider: "joel's device scan provider - item 1", Device: "Eth0", Numanode: 2})
	if err1 != nil {
		return err1
	}
	err2 := stream.Send(&pb.DeviceScanReply{Provider: "joel's device scan provider - item 2", Device: "Eth1", Numanode: 3})
	if err2 != nil {
		return err2
	}
	return nil
}
*/