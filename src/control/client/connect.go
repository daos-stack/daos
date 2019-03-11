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
)

// ClientFeatureMap is an alias for management features supported on server
// connected to given client.
type ClientFeatureMap map[string]FeatureMap

// ClientNvmeMap is an alias for NVMe controllers (and any residing namespaces)
// on server node connected to given client.
type ClientNvmeMap map[string]NvmeControllers

// ClientScmMap is an alias for SCM modules installed on server node
// connected to given client.
type ClientScmMap map[string]ScmModules

// ErrorMap is an alias to return errors keyed on address.
type ErrorMap map[string]error

// Addresses is an alias for a slice of <ipv4/hostname>:<port> addresses.
type Addresses []string

// ConnFactory is an interface providing capability to create clients.
type ConnFactory interface {
	createConn(string) Control
}

// Connections is an interface providing functionality across multiple
// connected clients.
type Connections interface {
	// ConnectClients attempts to connect a list of addresses, returns
	// list of connected clients and map of any errors.
	ConnectClients(Addresses) (Addresses, ErrorMap)
	// GetActiveConns verifies states of stored conns and removes inactive
	// from stored. Adds failure to failure map and returns active conns.
	GetActiveConns(ErrorMap) (Addresses, ErrorMap)
	ClearConns() ErrorMap
	ListFeatures() (ClientFeatureMap, error)
	ListNvme() (ClientNvmeMap, error)
	ListScm() (ClientScmMap, error)
	KillRank(uuid string, rank uint32) error
}

// connList is an implementation of Connections, a collection of connected
// client instances, one per target server.
type connList struct {
	factory ConnFactory
	clients []Control
}

// connFactory as an implementation of ConnFactory.
type connFactory struct{}

// createConn instantiates and connects a client to server at given address.
func (c *connFactory) createConn(address string) Control {
	return &control{}
}

// ConnectClients populates collection of client-server connections.
//
// Returns errors if server addresses doesn't resolve but will add
// clients for any server addresses that are connectable.
func (c *connList) ConnectClients(addresses Addresses) (
	Addresses, ErrorMap) {

	failures := make(ErrorMap)
	for _, address := range addresses {
		client := c.factory.createConn(address)
		if err := client.connect(address); err != nil {
			failures[address] = err
			continue
		}
		c.clients = append(c.clients, client)
	}
	// small delay to allow correct states to register
	time.Sleep(100 * time.Millisecond)
	return c.GetActiveConns(failures)
}

// GetActiveConns is an access method to verify active connections.
//
// Mutate client slice with only valid unique connected conns, return
// addresses and map of failed connections.
// todo: resolve hostname and compare destination IPs for duplicates.
func (c *connList) GetActiveConns(failures ErrorMap) (
	addresses Addresses, eMap ErrorMap) {

	if failures == nil {
		eMap = make(ErrorMap)
	} else {
		eMap = failures
	}
	clients := c.clients[:0]
	for _, mc := range c.clients {
		address := mc.getAddress()
		if common.Include(addresses, address) {
			eMap[address] = fmt.Errorf("duplicate connection to %s", address)
			continue
		}
		state, ok := mc.connected()
		if ok {
			addresses = append(addresses, address)
			clients = append(clients, mc)
			continue
		}
		eMap[address] = fmt.Errorf("socket connection is not active (%s)", state)
	}
	c.clients = clients
	return
}

// ClearConns clears all stored connections.
func (c *connList) ClearConns() ErrorMap {
	eMap := make(ErrorMap)
	for _, client := range c.clients {
		addr := client.getAddress()
		if err := client.close(); err != nil {
			eMap[addr] = err
		}
	}
	c.clients = nil
	return eMap
}

// ListFeatures returns supported management features for each server connected.
func (c *connList) ListFeatures() (ClientFeatureMap, error) {
	cf := make(ClientFeatureMap)
	for _, mc := range c.clients {
		fMap, err := mc.listAllFeatures()
		if err != nil {
			return cf, err
		}
		cf[mc.getAddress()] = fMap
	}
	return cf, nil
}

// ListNvme returns installed NVMe SSD controllers for each server connected.
func (c *connList) ListNvme() (ClientNvmeMap, error) {
	cCtrlrs := make(ClientNvmeMap)
	for _, mc := range c.clients {
		ctrlrs, err := mc.listNvmeCtrlrs()
		if err != nil {
			return cCtrlrs, err
		}
		cCtrlrs[mc.getAddress()] = ctrlrs
	}
	return cCtrlrs, nil
}

// ListScm returns installed SCM module details for each server connected.
func (c *connList) ListScm() (ClientScmMap, error) {
	cmms := make(ClientScmMap)
	for _, mc := range c.clients {
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
	for _, mc := range c.clients {
		err := mc.killRank(uuid, rank)
		if err != nil {
			return err
		}
	}
	return nil
}

// NewConnections is a factory for Connections interface to operate over
// multiple clients.
func NewConnections() Connections {
	var clients []Control
	return &connList{factory: &connFactory{}, clients: clients}
}
