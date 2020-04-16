//
// (C) Copyright 2019-2020 Intel Corporation.
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
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/system"
)

// SystemStopReq contains the inputs for the system stop command.
type SystemStopReq struct {
	Prep  bool
	Kill  bool
	Ranks []uint32
	Force bool
}

// SystemStopResp contains the request response.
type SystemStopResp struct {
	Results system.MemberResults
}

// SystemStop will perform a controlled shutdown of DAOS system and a list
// of remaining system members on failure.
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) SystemStop(req SystemStopReq) (*SystemStopResp, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	rpcReq := &ctlpb.SystemStopReq{
		Prep: req.Prep, Kill: req.Kill, Force: req.Force, Ranks: req.Ranks,
	}

	c.log.Debugf("DAOS system stop request: %s\n", rpcReq)

	rpcResp, err := mc.getCtlClient().SystemStop(context.Background(), rpcReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("DAOS system stop response: %s\n", rpcResp)

	ssr := new(SystemStopResp)
	if err := convert.Types(rpcResp, ssr); err != nil {
		return nil, err
	}
	return ssr, nil
}

// SystemStartReq contains the inputs for the system start request.
type SystemStartReq struct {
	Ranks []uint32
}

// SystemStartResp contains the request response.
type SystemStartResp struct {
	Results system.MemberResults // resulting from harness starts
}

// SystemStart will perform a start after a controlled shutdown of DAOS system.
func (c *connList) SystemStart(req SystemStartReq) (*SystemStartResp, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	rpcReq := &ctlpb.SystemStartReq{Ranks: req.Ranks}

	c.log.Debugf("DAOS system start request: %s\n", rpcReq)

	rpcResp, err := mc.getCtlClient().SystemStart(context.Background(), rpcReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("DAOS system start response: %s\n", rpcResp)

	ssr := new(SystemStartResp)
	if err := convert.Types(rpcResp, ssr); err != nil {
		return nil, err
	}
	return ssr, nil
}

// SystemQueryReq contains the inputs for the system query request.
type SystemQueryReq struct {
	Ranks []uint32
}

// SystemQueryResp contains the request response.
type SystemQueryResp struct {
	Members system.Members
}

// SystemQuery requests DAOS system status.
func (c *connList) SystemQuery(req SystemQueryReq) (*SystemQueryResp, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	rpcReq := &ctlpb.SystemQueryReq{Ranks: req.Ranks}

	c.log.Debugf("DAOS system query request: %+v", rpcReq)

	rpcResp, err := mc.getCtlClient().SystemQuery(context.Background(), rpcReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("DAOS system query response: %+v", rpcResp)

	sqr := new(SystemQueryResp)
	if err := convert.Types(rpcResp, sqr); err != nil {
		return nil, err
	}
	return sqr, nil
}

// LeaderQueryReq contains the inputs for the leader query command.
type LeaderQueryReq struct {
	System string
}

// LeaderQueryResp contains the status of the request and, if successful, the
// MS leader and set of replicas in the system.
type LeaderQueryResp struct {
	Leader   string
	Replicas []string
}

// LeaderQuery requests the current Management Service leader and the set of
// MS replicas.
func (c *connList) LeaderQuery(req LeaderQueryReq) (*LeaderQueryResp, error) {
	if len(c.controllers) == 0 {
		return nil, errors.New("no controllers defined")
	}

	client := c.controllers[0].getSvcClient()
	resp, err := client.LeaderQuery(context.TODO(), &mgmtpb.LeaderQueryReq{System: req.System})
	if err != nil {
		return nil, err
	}

	return &LeaderQueryResp{
		Leader:   resp.CurrentLeader,
		Replicas: resp.Replicas,
	}, nil
}

// ListPoolsReq contains the inputs for the list pools command.
type ListPoolsReq struct {
	SysName string
}

// ListPoolsResp contains the status of the request and, if successful, the list
// of pools in the system.
type ListPoolsResp struct {
	Status int32
	Pools  []*common.PoolDiscovery
}

// ListPools fetches the list of all pools and their service replicas from the
// system.
func (c *connList) ListPools(req ListPoolsReq) (*ListPoolsResp, error) {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return nil, err
	}

	pbReq := &mgmtpb.ListPoolsReq{Sys: req.SysName}

	c.log.Debugf("List DAOS pools request: %v", pbReq)

	pbResp, err := mc.getSvcClient().ListPools(context.Background(), pbReq)
	if err != nil {
		return nil, err
	}

	c.log.Debugf("List DAOS pools response: %v", pbResp)

	if pbResp.GetStatus() != 0 {
		return nil, errors.Errorf("DAOS returned error code: %d",
			pbResp.GetStatus())
	}

	return &ListPoolsResp{
		Pools: proto.PoolDiscoveriesFromPB(pbResp.Pools),
	}, nil
}
