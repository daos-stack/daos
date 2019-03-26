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
	"fmt"

	"github.com/daos-stack/daos/src/control/common"
)

// Addresses is an alias for a slice of <ipv4/hostname>:<port> addresses.
type Addresses []string

// ClientResult is a container for output of any type of client request.
type ClientResult struct {
	Address string
	Value   interface{}
	Err     error
}

// ResultMap map client addresses to method call ClientResults
type ResultMap map[string]ClientResult

// ControllerFactory is an interface providing capability to connect clients.
type ControllerFactory interface {
	create(string) (Control, error)
}

// controllerFactory as an implementation of ControllerFactory.
type controllerFactory struct{}

// create instantiates and connects a client to server at given address.
func (c *controllerFactory) create(address string) (Control, error) {
	return newControl(address)
}

// Connect is an interface providing functionality across multiple
// connected clients (controllers).
type Connect interface {
	// ConnectClients attempts to connect a list of addresses, returns
	// list of connected clients (controllers) and map of any errors.
	ConnectClients(Addresses) (Addresses, ResultMap)
	// GetActiveConns verifies states of controllers and removes inactive
	// from stored. Adds failure to failure map and returns active.
	GetActiveConns(ResultMap) (Addresses, ResultMap)
	ClearConns() ResultMap
	ListFeatures() ClientFeatureMap
	ListStorage() (ClientNvmeMap, ClientScmMap)
	KillRank(uuid string, rank uint32) ResultMap
}

// connList is an implementation of Connect and stores controllers
// (connections to clients, one per target server).
type connList struct {
	factory     ControllerFactory
	controllers []Control
}

// ConnectClients populates collection of client-server controllers.
//
// Returns errors if server addresses doesn't resolve but will add
// controllers for any server addresses that are connectable.
func (c *connList) ConnectClients(addresses Addresses) (Addresses, ResultMap) {
	errors := make(ResultMap)
	ch := make(chan ClientResult)

	for _, address := range addresses {
		go func(f ControllerFactory, addr string, ch chan ClientResult) {
			c, err := f.create(addr)
			ch <- ClientResult{addr, c, err}
		}(c.factory, address, ch)
	}

	for range addresses {
		res := <-ch

		if res.Err != nil {
			errors[res.Address] = res
			continue
		}

		controller, ok := res.Value.(Control)
		if !ok {
			res.Err = fmt.Errorf(
				"type assertion failed, wanted %+v got %+v",
				control{}, res.Value)

			errors[res.Address] = res
			continue
		}

		c.controllers = append(c.controllers, controller)
	}

	return c.GetActiveConns(errors)
}

// GetActiveConns verifies active connections and (re)builds connection list.
//
// TODO: resolve hostname and compare destination IPs for duplicates.
func (c *connList) GetActiveConns(errors ResultMap) (Addresses, ResultMap) {
	if errors == nil {
		errors = make(ResultMap)
	}
	addresses := []string{}

	controllers := c.controllers[:0]
	for _, mc := range c.controllers {
		address := mc.getAddress()
		if common.Include(addresses, address) {
			continue // ignore duplicate
		}

		state, ok := mc.connected()
		if ok {
			addresses = append(addresses, address)
			controllers = append(controllers, mc)
			continue
		}

		errors[address] = ClientResult{
			address, state,
			fmt.Errorf(
				"socket connection is not active (%s)", state),
		}
	}

	// purge inactive connections by replacing with active list
	c.controllers = controllers
	return Addresses(addresses), errors
}

// ClearConns clears all stored connections.
func (c *connList) ClearConns() ResultMap {
	errors := make(ResultMap)
	ch := make(chan ClientResult)

	for _, controller := range c.controllers {
		go func(c Control, ch chan ClientResult) {
			err := c.disconnect()
			ch <- ClientResult{c.getAddress(), nil, err}
		}(controller, ch)
	}

	for range c.controllers {
		res := <-ch

		if res.Err != nil {
			errors[res.Address] = res
		}
	}
	c.controllers = nil

	return errors
}

// makeRequests performs supplied method over each controller in connList and
// stores generic result object for each in map keyed on address.
func (c *connList) makeRequests(
	requestFn func(Control, chan ClientResult)) ResultMap {

	cMap := make(ResultMap) // mapping of server host addresses to results
	ch := make(chan ClientResult)

	for _, mc := range c.controllers {
		go requestFn(mc, ch)
	}

	for range c.controllers {
		res := <-ch
		cMap[res.Address] = res
	}

	return cMap
}
