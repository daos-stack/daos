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
	"bytes"
	"fmt"
	"sort"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security"
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

// ClientBioResult is a container for output of BIO health
// query client requests.
type ClientBioResult struct {
	Address string
	Stats   *mgmtpb.BioHealthResp
	Err     error
}

func (cr ClientBioResult) String() string {
	var buf bytes.Buffer

	if cr.Err != nil {
		return fmt.Sprintf("error: " + cr.Err.Error())
	}

	if cr.Stats.Status != 0 {
		return fmt.Sprintf("error: %v\n", cr.Stats.Status)
	}

	fmt.Fprintf(&buf, "Device UUID: %v\n", cr.Stats.DevUuid)
	fmt.Fprintf(&buf, "\tRead errors: %v\n", cr.Stats.ReadErrs)
	fmt.Fprintf(&buf, "\tWrite errors: %v\n", cr.Stats.WriteErrs)
	fmt.Fprintf(&buf, "\tUnmap errors: %v\n", cr.Stats.UnmapErrs)
	fmt.Fprintf(&buf, "\tChecksum errors: %v\n", cr.Stats.ChecksumErrs)
	fmt.Fprintf(&buf, "\tDevice Health:\n")
	fmt.Fprintf(&buf, "\t\tError log entries: %v\n", cr.Stats.ErrorCount)
	fmt.Fprintf(&buf, "\t\tMedia errors: %v\n", cr.Stats.MediaErrors)
	fmt.Fprintf(&buf, "\t\tTemperature: %v\n", cr.Stats.Temperature)
	fmt.Fprintf(&buf, "\t\tTemperature: ")
	if cr.Stats.Temp {
		fmt.Fprintf(&buf, "WARNING\n")
	} else {
		fmt.Fprintf(&buf, "OK\n")
	}
	fmt.Fprintf(&buf, "\t\tAvailable Spare: ")
	if cr.Stats.Spare {
		fmt.Fprintf(&buf, "WARNING\n")
	} else {
		fmt.Fprintf(&buf, "OK\n")
	}
	fmt.Fprintf(&buf, "\t\tDevice Reliability: ")
	if cr.Stats.DeviceReliability {
		fmt.Fprintf(&buf, "WARNING\n")
	} else {
		fmt.Fprintf(&buf, "OK\n")
	}
	fmt.Fprintf(&buf, "\t\tRead Only: ")
	if cr.Stats.Readonly {
		fmt.Fprintf(&buf, "WARNING\n")
	} else {
		fmt.Fprintf(&buf, "OK\n")
	}
	fmt.Fprintf(&buf, "\t\tVolatile Memory Backup: ")
	if cr.Stats.VolatileMemory {
		fmt.Fprintf(&buf, "WARNING\n")
	} else {
		fmt.Fprintf(&buf, "OK\n")
	}

	return buf.String()
}

// ClientSmdResult is a container for output of SMD dev list
// query client requests.
type ClientSmdResult struct {
	Address string
	Devs    *mgmtpb.SmdDevResp
	Pools   *mgmtpb.SmdPoolResp
	Err     error
}

func (cr ClientSmdResult) String() string {
	var buf bytes.Buffer

	if cr.Err != nil {
		return fmt.Sprintf("error: " + cr.Err.Error())
	}

	if cr.Devs != nil {
		if cr.Devs.Status != 0 {
			return fmt.Sprintf("error: %v\n", cr.Devs.Status)
		}

		if len(cr.Devs.Devices) == 0 {
			fmt.Fprintf(&buf, "No Devices Found\n")
		}

		for i, d := range cr.Devs.Devices {
			if i != 0 {
				fmt.Fprintf(&buf, "\n\t")
			}
			fmt.Fprintf(&buf, "Device:\n")
			fmt.Fprintf(&buf, "\t\tUUID: %+v\n", d.Uuid)
			fmt.Fprintf(&buf, "\t\tVOS Target IDs: ")
			for _, t := range d.TgtIds {
				fmt.Fprintf(&buf, "%d ", t)
			}
		}
	}

	if cr.Pools != nil {
		if cr.Pools.Status != 0 {
			return fmt.Sprintf("error: %v\n", cr.Pools.Status)
		}

		if len(cr.Pools.Pools) == 0 {
			fmt.Fprintf(&buf, "No Pools Found\n")
		}

		for i, p := range cr.Pools.Pools {
			if i != 0 {
				fmt.Fprintf(&buf, "\n\t")
			}
			fmt.Fprintf(&buf, "Pool:\n")
			fmt.Fprintf(&buf, "\t\tUUID: %+v\n", p.Uuid)
			fmt.Fprintf(&buf, "\t\tVOS Target IDs: ")
			for _, t := range p.TgtIds {
				fmt.Fprintf(&buf, "%d ", t)
			}
			fmt.Fprintf(&buf, "\n")
			fmt.Fprintf(&buf, "\t\tSPDK Blobs: ")
			for _, b := range p.Blobs {
				fmt.Fprintf(&buf, "%v ", b)
			}
		}
	}

	return buf.String()
}

// ResultMap map client addresses to method call ClientResults
type ResultMap map[string]ClientResult
type ResultQueryMap map[string]ClientBioResult
type ResultSmdMap map[string]ClientSmdResult

func (rm ResultMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(rm))

	for server := range rm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n\t%s\n", server, rm[server])
	}

	return buf.String()
}

func (rm ResultQueryMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(rm))

	for server := range rm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n\t%s\n", server, rm[server])
	}

	return buf.String()
}

func (rm ResultSmdMap) String() string {
	var buf bytes.Buffer
	servers := make([]string, 0, len(rm))

	for server := range rm {
		servers = append(servers, server)
	}
	sort.Strings(servers)

	for _, server := range servers {
		fmt.Fprintf(&buf, "%s:\n\t%s\n", server, rm[server])
	}

	return buf.String()
}

// ScmModules is an alias for protobuf ScmModule message slice representing
// a number of SCM modules installed on a storage node.

// ControllerFactory is an interface providing capability to connect clients.
type ControllerFactory interface {
	create(string, *security.TransportConfig) (Control, error)
}

// controllerFactory as an implementation of ControllerFactory.
type controllerFactory struct {
	log logging.Logger
}

// create instantiates and connects a client to server at given address.
func (c *controllerFactory) create(address string, cfg *security.TransportConfig) (Control, error) {
	controller := &control{
		log: c.log,
	}

	err := controller.connect(address, cfg)

	return controller, err
}

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
	// SetTransportConfig sets the gRPC transport confguration
	SetTransportConfig(*security.TransportConfig)
	// ConnectClients attempts to connect a list of addresses
	ConnectClients(Addresses) ResultMap
	// GetActiveConns verifies states and removes inactive conns
	GetActiveConns(ResultMap) ResultMap
	ClearConns() ResultMap
	StoragePrepare(*ctlpb.StoragePrepareReq) ResultMap
	StorageScan() (ClientCtrlrMap, ClientModuleMap, ClientPmemMap)
	StorageFormat() (ClientCtrlrMap, ClientMountMap)
	StorageUpdate(*ctlpb.StorageUpdateReq) (ClientCtrlrMap, ClientModuleMap)
	// TODO: implement Burnin client features
	//StorageBurnIn() (ClientCtrlrMap, ClientModuleMap)
	ListFeatures() ClientFeatureMap
	KillRank(uuid string, rank uint32) ResultMap
	PoolCreate(*PoolCreateReq) (*PoolCreateResp, error)
	PoolDestroy(*PoolDestroyReq) error
	BioHealthQuery(*mgmtpb.BioHealthReq) ResultQueryMap
	SmdListDevs(*mgmtpb.SmdDevReq) ResultSmdMap
	GetProviderList(*ctlpb.ProviderListRequest) ResultMap
//	RequestDeviceScanStreamer(*ctlpb.DeviceScanRequest, *ctlpb.MgmtCtl_RequestDeviceScanStreamerServer) error
	SmdListPools(*mgmtpb.SmdPoolReq) ResultSmdMap
	SystemMemberQuery() (common.SystemMembers, error)
	SystemStop() (common.SystemMembers, error)
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
