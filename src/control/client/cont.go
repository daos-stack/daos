//
// (C) Copyright 2020 Intel Corporation.
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
	"github.com/google/uuid"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

// ContSetOwnerReq struct contains request
type ContSetOwnerReq struct {
	ContUUID string // Container UUID
	PoolUUID string // UUID of the pool for the container
	User     string // User to own the container, or empty if none
	Group    string // Group to own the container, or empty if none
}

// ContSetOwner will create a DAOS pool using provided parameters and generated
// UUID. Return values will be UUID, list of service replicas and error
// (including any DER code from DAOS).
//
// Isolate protobuf encapsulation in client and don't expose to calling code.
func (c *connList) ContSetOwner(req ContSetOwnerReq) error {
	mc, err := chooseServiceLeader(c.controllers)
	if err != nil {
		return err
	}

	_, err = uuid.Parse(req.ContUUID)
	if err != nil {
		return errors.Wrapf(err, "bad container UUID: %q", req.ContUUID)
	}

	_, err = uuid.Parse(req.PoolUUID)
	if err != nil {
		return errors.Wrapf(err, "bad pool UUID: %q", req.PoolUUID)
	}

	if req.User == "" && req.Group == "" {
		return errors.New("no user or group specified")
	}

	rpcReq := &mgmtpb.ContSetOwnerReq{
		ContUUID:   req.ContUUID,
		PoolUUID:   req.PoolUUID,
		Owneruser:  req.User,
		Ownergroup: req.Group,
	}

	c.log.Debugf("DAOS container set-owner request: %s\n", rpcReq)

	rpcResp, err := mc.getSvcClient().ContSetOwner(context.Background(), rpcReq)
	if err != nil {
		return err
	}

	c.log.Debugf("DAOS container set-owner response: %s\n", rpcResp)

	if rpcResp.GetStatus() != 0 {
		return errors.Errorf("DAOS returned error code: %d",
			rpcResp.GetStatus())
	}

	return nil
}
