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
//	"bytes"
//	"fmt"
//	"io"
//	"sort"
//	"time"

	"github.com/pkg/errors"
	"golang.org/x/net/context"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
)

func (c *connList) GetProviderList(req *ctlpb.ProviderListRequest) ResultMap {
	c.log.Debugf("RequestProviderList() Received")
	return c.makeRequests(req, providerListRequest)
}

func providerListRequest(mc Control, req interface{}, ch chan ClientResult) {
	listReq, ok := req.(*ctlpb.ProviderListRequest)
	if !ok {
		err := errors.Errorf(msgTypeAssert, &ctlpb.ProviderListRequest{}, req)
		mc.logger().Errorf(err.Error())
		ch <- ClientResult{mc.getAddress(), nil, err}
		return // type err
	}

	resp, err := mc.getCtlClient().RequestProviderList(context.Background(), listReq)
	if err != nil {
		ch <- ClientResult{mc.getAddress(), nil, err} // return comms error
		return
	}

	ch <- ClientResult{mc.getAddress(), resp.GetProvider(), nil}
}
