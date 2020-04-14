//
// (C) Copyright 2018-2020 Intel Corporation.
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
	"sort"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
)

const (
	msgBadType      = "type assertion failed, wanted %+v got %+v"
	msgConnInactive = "socket connection is not active (%s)"
)

// chooseServiceLeader will decide which connection to send request on.
//
// Currently expect only one connection to be available and return that.
// TODO: this should probably be implemented on the Connect interface.
func chooseServiceLeader(cs []Control) (Control, error) {
	if len(cs) == 0 {
		return nil, errors.New("no active connections")
	}

	// just return the first connection, expected to be the service leader
	return cs[0], nil
}

// Connect is an external interface providing functionality across multiple
// connected clients (controllers).
type Connect interface {
	BioHealthQuery(*mgmtpb.BioHealthReq) ResultQueryMap
	ClearConns() ResultMap
	ConnectClients(Addresses) ResultMap
	GetActiveConns(ResultMap) ResultMap
	NetworkListProviders() ResultMap
	NetworkScanDevices(searchProvider string) NetworkScanResultMap
	PoolReintegrate(*PoolReintegrateReq) error
	PoolGetACL(PoolGetACLReq) (*PoolGetACLResp, error)
	PoolOverwriteACL(PoolOverwriteACLReq) (*PoolOverwriteACLResp, error)
	PoolUpdateACL(PoolUpdateACLReq) (*PoolUpdateACLResp, error)
	PoolDeleteACL(PoolDeleteACLReq) (*PoolDeleteACLResp, error)
	SetTransportConfig(*security.TransportConfig)
	SmdListDevs(*mgmtpb.SmdDevReq) ResultSmdMap
	SmdListPools(*mgmtpb.SmdPoolReq) ResultSmdMap
	DevStateQuery(*mgmtpb.DevStateReq) ResultStateMap
	StorageSetFaulty(*mgmtpb.DevStateReq) ResultStateMap
	SystemQuery(SystemQueryReq) (*SystemQueryResp, error)
	SystemStop(SystemStopReq) (*SystemStopResp, error)
	SystemStart(SystemStartReq) (*SystemStartResp, error)
	LeaderQuery(LeaderQueryReq) (*LeaderQueryResp, error)
	ListPools(ListPoolsReq) (*ListPoolsResp, error)
}

// connList is an implementation of Connect and stores controllers
// (connections to clients, one per DAOS server).
type connList struct {
	log             logging.Logger
	transportConfig *security.TransportConfig
	factory         ControllerFactory
	controllers     []Control
}

// SetTransportConfig sets the internal transport credentials to be passed
// to subsequent connect calls as the gRPC dial credentials.
func (c *connList) SetTransportConfig(cfg *security.TransportConfig) {
	c.transportConfig = cfg
}

// ConnectClients populates collection of client-server controllers.
//
// Returns errors if server addresses doesn't resolve but will add
// controllers for any server addresses that are connectable.
func (c *connList) ConnectClients(addresses Addresses) ResultMap {
	results := make(ResultMap)
	ch := make(chan ClientResult)

	for _, address := range addresses {
		go func(f ControllerFactory, addr string, ch chan ClientResult) {
			c, err := f.create(addr, c.transportConfig)
			ch <- ClientResult{addr, c, err}
		}(c.factory, address, ch)
	}

	for range addresses {
		res := <-ch
		results[res.Address] = res

		if res.Err != nil {
			continue
		}

		controller, ok := res.Value.(Control)
		if !ok {
			res.Err = fmt.Errorf(msgBadType, control{}, res.Value)
			results[res.Address] = res
			continue
		}

		c.controllers = append(c.controllers, controller)
	}

	time.Sleep(100 * time.Millisecond)
	return c.GetActiveConns(results)
}

// GetActiveConns verifies active connections and (re)builds connection list.
//
// TODO: resolve hostname and compare destination IPs for duplicates.
func (c *connList) GetActiveConns(results ResultMap) ResultMap {
	if results == nil {
		results = make(ResultMap)
	}
	addresses := []string{}

	controllers := c.controllers[:0]
	for _, mc := range c.controllers {
		address := mc.getAddress()
		if common.Includes(addresses, address) {
			continue // ignore duplicate
		}

		var err error

		state, ok := mc.connected()
		if ok {
			addresses = append(addresses, address)
			controllers = append(controllers, mc)
		} else {
			err = fmt.Errorf(msgConnInactive, state)
		}

		results[address] = ClientResult{address, state, err}
	}

	// purge inactive connections by replacing with active list
	c.controllers = controllers
	return results
}

// ClearConns clears all stored connections.
func (c *connList) ClearConns() ResultMap {
	results := make(ResultMap)
	ch := make(chan ClientResult)

	for _, controller := range c.controllers {
		go func(c Control, ch chan ClientResult) {
			err := c.disconnect()
			ch <- ClientResult{c.getAddress(), nil, err}
		}(controller, ch)
	}

	for range c.controllers {
		res := <-ch
		results[res.Address] = res
	}
	c.controllers = nil

	return results
}

// makeRequests performs supplied method over each controller in connList and
// stores generic result object for each in map keyed on address.
func (c *connList) makeRequests(req interface{},
	requestFn func(Control, interface{}, chan ClientResult)) ResultMap {

	cMap := make(ResultMap) // mapping of server host addresses to results
	ch := make(chan ClientResult)
	conns := c.controllers

	addrs := make([]string, 0, len(conns))
	sort.Slice(conns, func(i, j int) bool { return conns[i].getAddress() < conns[j].getAddress() })
	for _, mc := range conns {
		addrs = append(addrs, mc.getAddress())
		go requestFn(mc, req, ch)
	}

	for {
		res := <-ch

		// remove received address from list
		for i, v := range addrs {
			if v == res.Address {
				addrs = append(addrs[:i], addrs[i+1:]...)
				cMap[res.Address] = res
				break
			}
		}

		if len(addrs) == 0 {
			break // received responses from all connections
		}
	}

	return cMap
}

// NewConnect is a factory for Connect interface to operate over
// multiple clients.
func NewConnect(log logging.Logger) Connect {
	return &connList{
		log:             log,
		transportConfig: nil,
		factory: &controllerFactory{
			log: log,
		},
		controllers: []Control{},
	}
}
