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
	"time"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/pkg/errors"
)

// cFeatureMap is an alias for management features supported on server
// connected to given client.
type cFeatureMap map[string]FeatureMap

// cNvmeMap is an alias for NVMe controllers (and any residing namespaces)
// on server node connected to given client.
type cNvmeMap map[string]NvmeControllers

// cScmMap is an alias for SCM modules installed on server node
// connected to given client.
type cScmMap map[string]ScmModules

// ErrorMap is an alias to return errors keyed on address.
type ErrorMap map[string]error

// Addresses is an alias for a slice of <ipv4/hostname>:<port> addresses.
type Addresses []string

// ControllerFactory is an interface providing capability to connect clients.
type ControllerFactory interface {
	create(string) Control
}

// Connect is an interface providing functionality across multiple
// connected clients (controllers).
type Connect interface {
	// ConnectClients attempts to connect a list of addresses, returns
	// list of connected clients (controllers) and map of any errors.
	ConnectClients(Addresses) (Addresses, ErrorMap)
	// GetActiveConns verifies states of controllers and removes inactive
	// from stored. Adds failure to failure map and returns active.
	GetActiveConns(ErrorMap) (Addresses, ErrorMap)
	ClearConns() ErrorMap
	ListFeatures() (cFeatureMap, error)
	ListNvme() (cNvmeMap, error)
	ListScm() (cScmMap, error)
	KillRank(uuid string, rank uint32) error
}

// connList is an implementation of Connect and stores controllers
// (connections to clients, one per target server).
type connList struct {
	factory     ControllerFactory
	controllers []Control
}

// controllerFactory as an implementation of ControllerFactory.
type controllerFactory struct{}

// create instantiates and connects a client to server at given address.
func (c *controllerFactory) create(address string) Control {
	return &control{}
}

// ConnectClients populates collection of client-server controllers.
//
// Returns errors if server addresses doesn't resolve but will add
// controllers for any server addresses that are connectable.
func (c *connList) ConnectClients(addresses Addresses) (
	Addresses, ErrorMap) {

	failures := make(ErrorMap)
	for _, address := range addresses {
		controller := c.factory.create(address)
		if err := controller.connect(address); err != nil {
			failures[address] = err
			continue
		}
		c.controllers = append(c.controllers, controller)
	}

	// small delay to allow correct states to register
	time.Sleep(100 * time.Millisecond)
	return c.GetActiveConns(failures)
}

// GetActiveConns is an access method to verify active connections.
//
// Mutate controller slice with only valid unique connected conns, return
// addresses and map of failed connections.
//
// TODO: resolve hostname and compare destination IPs for duplicates.
func (c *connList) GetActiveConns(failures ErrorMap) (
	addresses Addresses, eMap ErrorMap) {

	eMap = failures
	if eMap == nil {
		eMap = make(ErrorMap)
	}

	controllers := c.controllers[:0]
	for _, mc := range c.controllers {
		address := mc.getAddress()
		if common.Include(addresses, address) {
			eMap[address] = fmt.Errorf("duplicate connection to %s", address)
			continue
		}
		state, ok := mc.connected()
		if ok {
			addresses = append(addresses, address)
			controllers = append(controllers, mc)
			continue
		}
		eMap[address] = fmt.Errorf("socket connection is not active (%s)", state)
	}

	c.controllers = controllers
	return
}

// ClearConns clears all stored connections.
func (c *connList) ClearConns() ErrorMap {
	eMap := make(ErrorMap)
	for _, controller := range c.controllers {
		addr := controller.getAddress()
		if err := controller.disconnect(); err != nil {
			eMap[addr] = err
		}
	}
	c.controllers = nil
	return eMap
}

// ListFeatures returns supported management features for each server connected.
func (c *connList) ListFeatures() (cFeatureMap, error) {
	cf := make(cFeatureMap)
	for _, mc := range c.controllers {
		fMap, err := mc.listAllFeatures()
		if err != nil {
			return cf, err
		}
		cf[mc.getAddress()] = fMap
	}
	return cf, nil
}

// ListNvme returns installed NVMe SSD controllers for each server connected.
func (c *connList) ListNvme() (cNvmeMap, error) {
	cCtrlrs := make(cNvmeMap)
	for _, mc := range c.controllers {
		ctrlrs, err := mc.listNvmeCtrlrs()
		if err != nil {
			return cCtrlrs, err
		}
		cCtrlrs[mc.getAddress()] = ctrlrs
	}
	return cCtrlrs, nil
}

// ListScm returns installed SCM module details for each server connected.
func (c *connList) ListScm() (cScmMap, error) {
	cmms := make(cScmMap)
	for _, mc := range c.controllers {
		mms, err := mc.listScmModules()
		if err != nil {
			return cmms, err
		}
		cmms[mc.getAddress()] = mms
	}
	return cmms, nil
}

// KillRank Will terminate server running at given rank on pool specified by
// uuid.
func (c *connList) KillRank(uuid string, rank uint32) error {
	for _, mc := range c.controllers {
		err := mc.killRank(uuid, rank)
		if err != nil {
			return errors.WithMessage(err, mc.getAddress())
		}
	}
	return nil
}

// NewConnect is a factory for Connect interface to operate over
// multiple clients.
func NewConnect() Connect {
	return &connList{
		factory:     &controllerFactory{},
		controllers: []Control{},
	}
}
