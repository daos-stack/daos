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

package client

import (
	"bytes"
	"fmt"
	"sort"
	"time"

	"github.com/daos-stack/daos/src/control/common"
	pb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
)

const (
	msgBadType      = "type assertion failed, wanted %+v got %+v"
	msgConnInactive = "socket connection is not active (%s)"
)

// Addresses is an alias for a slice of <ipv4/hostname>:<port> addresses.
type Addresses []string

// ClientResult is a container for output of any type of client request.
type ClientResult struct {
	Address string
	Value   interface{}
	Err     error
}

func (cr ClientResult) String() string {
	if cr.Err != nil {
		return fmt.Sprintf("error: " + cr.Err.Error())
	}
	return fmt.Sprintf("%+v", cr.Value)
}

// ResultMap map client addresses to method call ClientResults
type ResultMap map[string]ClientResult

func (rm ResultMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(rm))

	for server := range rm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n%s\n", server, rm[server])
	}

	return buf.String()
}

// ScmModules is an alias for protobuf ScmModule message slice representing
// a number of SCM modules installed on a storage node.

// ControllerFactory is an interface providing capability to connect clients.
type ControllerFactory interface {
	create(string) (Control, error)
}

// controllerFactory as an implementation of ControllerFactory.
type controllerFactory struct{}

// create instantiates and connects a client to server at given address.
func (c *controllerFactory) create(address string) (Control, error) {
	controller := &control{}

	err := controller.connect(address)

	return controller, err
}

// Connect is an external interface providing functionality across multiple
// connected clients (controllers).
type Connect interface {
	ConnectClients(Addresses) ResultMap // connect addresses
	GetActiveConns(ResultMap) ResultMap // remove inactive conns
	ClearConns() ResultMap
	ScanStorage() (ClientCtrlrMap, ClientModuleMap)
	FormatStorage() (ClientCtrlrMap, ClientMountMap)
	UpdateStorage(*pb.UpdateStorageReq) (ClientCtrlrMap, ClientModuleMap)
	// TODO: implement Burnin client features
	//BurninStorage() (ClientCtrlrMap, ClientModuleMap)
	ListFeatures() ClientFeatureMap
	KillRank(uuid string, rank uint32) ResultMap
	CreatePool(*pb.CreatePoolReq) ResultMap
}

// connList is an implementation of Connect and stores controllers
// (connections to clients, one per DAOS server).
type connList struct {
	factory     ControllerFactory
	controllers []Control
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
			c, err := f.create(addr)
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
		if common.Include(addresses, address) {
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
func (c *connList) makeRequests(
	req interface{},
	requestFn func(Control, interface{}, chan ClientResult)) ResultMap {

	cMap := make(ResultMap) // mapping of server host addresses to results
	ch := make(chan ClientResult)

	addrs := []string{}
	for _, mc := range c.controllers {
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
func NewConnect() Connect {
	return &connList{
		factory:     &controllerFactory{},
		controllers: []Control{},
	}
}
