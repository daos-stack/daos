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

package client

import (
	"golang.org/x/net/context"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/pkg/errors"
)

func bioHealthRequest(mc Control, req interface{}, ch chan ClientResult) {
	prepareReq, ok := req.(*mgmtpb.BioHealthReq)
	if !ok {
		err := errors.Errorf(msgTypeAssert, &mgmtpb.BioHealthReq{}, req)

		mc.logger().Errorf(err.Error())
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // type err
	}

	resp, err := mc.getSvcClient().BioHealthQuery(context.Background(), prepareReq)
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err} // return comms error
		return
	}

	ch <- ClientResult{mc.getAddress(), resp, nil}
}

func (c *connList) BioHealthQuery(req *mgmtpb.BioHealthReq) ResultMap {
	// TODO: convert the result of the following call to ResultQueryMap
	return c.makeRequests(req, bioHealthRequest)
}

// BioHealthQuery will return all BIO device health and I/O error stats for
// given device UUID
//func (c *connList) BioHealthQuery(req *mgmtpb.BioHealthReq) ResultQueryMap {
//	results := make(ResultQueryMap)
//
//	resp, err := mc.getSvcClient().BioHealthQuery(context.Background(), req)
//
//	result := ClientBioResult{mc.getAddress(), resp, err}
//	results[result.Address] = result
//
//	return results
//}

// SmdListDevs will list all devices in SMD device table
func (c *connList) SmdListDevs(req *mgmtpb.SmdDevReq) ResultSmdMap {
	results := make(ResultSmdMap)

	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		results[""] = ClientSmdResult{"", nil, nil, err}
		return results
	}

	resp, err := mc.getSvcClient().SmdListDevs(context.Background(), req)

	result := ClientSmdResult{mc.getAddress(), resp, nil, err}
	results[result.Address] = result

	return results
}

// SmdListPools will list all VOS pools in SMD pool table
func (c *connList) SmdListPools(req *mgmtpb.SmdPoolReq) ResultSmdMap {
	results := make(ResultSmdMap)

	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		results[""] = ClientSmdResult{"", nil, nil, err}
		return results
	}

	resp, err := mc.getSvcClient().SmdListPools(context.Background(), req)

	result := ClientSmdResult{mc.getAddress(), nil, resp, err}
	results[result.Address] = result

	return results
}

// DevStateQuery will print the state of the given device UUID
func (c *connList) DevStateQuery(req *mgmtpb.DevStateReq) ResultStateMap {
	results := make(ResultStateMap)

	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		results[""] = ClientStateResult{"", nil, err}
		return results
	}

	resp, err := mc.getSvcClient().DevStateQuery(context.Background(), req)

	result := ClientStateResult{mc.getAddress(), resp, err}
	results[result.Address] = result

	return results
}

// StorageSetFaulty will set the state of the given device UUID to FAULTY
func (c *connList) StorageSetFaulty(req *mgmtpb.DevStateReq) ResultStateMap {
	results := make(ResultStateMap)

	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		results[""] = ClientStateResult{"", nil, err}
		return results
	}

	resp, err := mc.getSvcClient().StorageSetFaulty(context.Background(), req)

	result := ClientStateResult{mc.getAddress(), resp, err}
	results[result.Address] = result

	return results
}
