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
	"time"

	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"

	"github.com/pkg/errors"
	"golang.org/x/net/context"
)

func (c *control) killRank(uuid string, rank uint32) error {
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	resp, err := c.client.KillRank(ctx, &pb.DaosRank{PoolUuid: uuid, Rank: rank})
	if err != nil {
		return err
	}
	if resp.Status != pb.DaosRequestStatus_SUCCESS {
		return errors.New("DAOS request status " + resp.Status.String())
	}

	return nil
}

// KillRank Will terminate server running at given rank on pool specified by
// uuid.
func (c *connList) KillRank(uuid string, rank uint32) ResultMap {
	results := make(ResultMap)
	ch := make(chan ClientResult)

	for _, mc := range c.controllers {
		go func(mc Control, u string, r uint32, ch chan ClientResult) {
			ch <- ClientResult{
				mc.getAddress(), nil, mc.killRank(u, r),
			}
		}(mc, uuid, rank, ch)
	}

	for range c.controllers {
		res := <-ch
		results[res.Address] = res
	}

	return results
}
