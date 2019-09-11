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

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/pkg/errors"
)

func verifyUnicast(cs []Control) (Control, error) {
	if len(cs) > 1 {
		return nil, errors.Errorf("unexpected number of connections, "+
			"want 1, have %d", len(cs))
	}

	return cs[0], nil
}

// PoolCreate will create a DAOS pool using provided parameters and return
// uuid, list of service replicas and error (including any DER code from DAOS).
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) PoolCreate(log *logging.LeveledLogger, scmBytes uint64,
	nvmeBytes uint64, rankList string, numSvcReps uint32, sys string,
	usr string, grp string) (uuid string, svcreps string, err error) {

	var mc Control
	var resp *pb.PoolCreateResp

	mc, err = verifyUnicast(c.controllers)
	if err != nil {
		return
	}

	req := &pb.PoolCreateReq{
		Scmbytes: uint64(scmBytes), Nvmebytes: uint64(nvmeBytes),
		Ranks: rankList, Numsvcreps: numSvcReps, Sys: sys,
		User: usr, Usergroup: grp,
	}

	log.Infof("Creating DAOS pool: %s\n", req)

	resp, err = mc.getSvcClient().PoolCreate(context.Background(), req)
	if err != nil {
		return
	}

	if resp.GetStatus() != 0 {
		err = errors.Errorf("DAOS returned error code: %d\n")
		return
	}

	return resp.GetUuid(), resp.GetSvcreps(), nil
}

// PoolDestroy will Destroy a DAOS pool identified by its uuid and returns
// error (including any DER code from DAOS).
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) PoolDestroy(log *logging.LeveledLogger, uuid string,
	force bool) (err error) {

	var mc Control
	var resp *pb.PoolDestroyResp

	mc, err = verifyUnicast(c.controllers)
	if err != nil {
		return
	}

	req := &pb.PoolDestroyReq{Uuid: uuid, Force: force}

	log.Infof("Destroying DAOS pool: %s\n", req)

	resp, err = mc.getSvcClient().PoolDestroy(context.Background(), req)
	if err != nil {
		return
	}

	if resp.GetStatus() != 0 {
		return errors.Errorf("DAOS returned error code: %d\n")
	}

	return
}
