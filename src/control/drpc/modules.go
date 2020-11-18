//
// (C) Copyright 2019-2020 Intel Corporation.
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

// This file imports all of the DAOS dRPC module/method IDs.

package drpc

import (
	fmt "fmt"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
)

// #cgo CFLAGS: -I${SRCDIR}/../../include
// #include <daos/drpc_modules.h>
import "C"

const moduleMethodOffset = 100

type ModuleID int32

func (id ModuleID) String() string {
	if name, ok := map[ModuleID]string{
		ModuleSecurityAgent: "Agent Security",
		ModuleMgmt:          "Management",
		ModuleSrv:           "Server",
		ModuleSecurity:      "Security",
	}[id]; ok {
		return name
	}

	return fmt.Sprintf("unknown module id: %d", id)
}

func (id ModuleID) GetMethod(methodID int32) (Method, error) {
	if m, ok := map[ModuleID]Method{
		ModuleSecurityAgent: securityAgentMethod(methodID),
		ModuleMgmt:          MgmtMethod(methodID),
		ModuleSrv:           srvMethod(methodID),
		ModuleSecurity:      securityMethod(methodID),
	}[id]; ok {
		if !m.IsValid() {
			return nil, errors.Errorf("invalid method %d for module %s",
				methodID, id)
		}
		return m, nil
	}

	return nil, errors.Errorf("unknown module id %d", id)
}

func (id ModuleID) ID() int32 {
	return int32(id)
}

const (
	// ModuleSecurityAgent is the dRPC module for security tasks in DAOS agent
	ModuleSecurityAgent ModuleID = C.DRPC_MODULE_SEC_AGENT
	// ModuleMgmt is the dRPC module for management service tasks
	ModuleMgmt ModuleID = C.DRPC_MODULE_MGMT
	// ModuleSrv is the dRPC module for tasks relating to server setup
	ModuleSrv ModuleID = C.DRPC_MODULE_SRV
	// ModuleSecurity is the dRPC module for security tasks in DAOS server
	ModuleSecurity ModuleID = C.DRPC_MODULE_SEC
)

type Method interface {
	ID() int32
	Module() ModuleID
	String() string
	IsValid() bool
}

type securityAgentMethod int32

func (m securityAgentMethod) Module() ModuleID {
	return ModuleSecurityAgent
}

func (m securityAgentMethod) ID() int32 {
	return int32(m)
}

func (m securityAgentMethod) String() string {
	if s, ok := map[securityAgentMethod]string{
		MethodRequestCredentials: "request agent credentials",
	}[m]; ok {
		return s
	}

	return fmt.Sprintf("%s:%d", m.Module(), m.ID())
}

// IsValid sanity checks the Method ID is within expected bounds.
func (m securityAgentMethod) IsValid() bool {
	startMethodID := int32(m.Module()) * moduleMethodOffset

	if m.ID() <= startMethodID || m.ID() >= int32(C.NUM_DRPC_SEC_AGENT_METHODS) {
		return false
	}

	return true
}

const (
	// MethodRequestCredentials is a ModuleSecurityAgent method
	MethodRequestCredentials securityAgentMethod = C.DRPC_METHOD_SEC_AGENT_REQUEST_CREDS
)

type MgmtMethod int32

func (m MgmtMethod) Module() ModuleID {
	return ModuleMgmt
}

func (m MgmtMethod) ID() int32 {
	return int32(m)
}

func (m MgmtMethod) String() string {
	if s, ok := map[MgmtMethod]string{
		MethodPrepShutdown:    "PrepShutdown",
		MethodPingRank:        "Ping",
		MethodSetRank:         "SetRank",
		MethodSetUp:           "Setup",
		MethodGroupUpdate:     "GroupUpdate",
		MethodPoolCreate:      "PoolCreate",
		MethodPoolDestroy:     "PoolDestroy",
		MethodPoolEvict:       "PoolEvict",
		MethodPoolExclude:     "PoolExclude",
		MethodPoolDrain:       "PoolDrain",
		MethodPoolExtend:      "PoolExtend",
		MethodPoolReintegrate: "PoolReintegrate",
		MethodPoolQuery:       "PoolQuery",
		MethodPoolSetProp:     "PoolSetProp",
		MethodListPools:       "ListPools",
	}[m]; ok {
		return s
	}

	return fmt.Sprintf("%s:%d", m.Module(), m.ID())
}

// IsValid sanity checks the Method ID is within expected bounds.
func (m MgmtMethod) IsValid() bool {
	startMethodID := int32(m.Module()) * moduleMethodOffset

	if m.ID() <= startMethodID || m.ID() >= int32(C.NUM_DRPC_MGMT_METHODS) {
		return false
	}

	return true
}

const (
	// MethodPrepShutdown is a ModuleMgmt method
	MethodPrepShutdown MgmtMethod = C.DRPC_METHOD_MGMT_PREP_SHUTDOWN
	// MethodPingRank is a ModuleMgmt method
	MethodPingRank MgmtMethod = C.DRPC_METHOD_MGMT_PING_RANK
	// MethodSetRank is a ModuleMgmt method
	MethodSetRank MgmtMethod = C.DRPC_METHOD_MGMT_SET_RANK
	// MethodCreateMS is a ModuleMgmt method
	MethodCreateMS MgmtMethod = C.DRPC_METHOD_MGMT_CREATE_MS
	// MethodStartMS is a ModuleMgmt method
	MethodStartMS MgmtMethod = C.DRPC_METHOD_MGMT_START_MS
	// MethodJoin is a ModuleMgmt method
	MethodJoin MgmtMethod = C.DRPC_METHOD_MGMT_JOIN
	// MethodGetAttachInfo is a ModuleMgmt method
	MethodGetAttachInfo MgmtMethod = C.DRPC_METHOD_MGMT_GET_ATTACH_INFO
	// MethodPoolCreate is a ModuleMgmt method
	MethodPoolCreate MgmtMethod = C.DRPC_METHOD_MGMT_POOL_CREATE
	// MethodPoolDestroy is a ModuleMgmt method
	MethodPoolDestroy MgmtMethod = C.DRPC_METHOD_MGMT_POOL_DESTROY
	// MethodPoolEvict is a ModuleMgmt method
	MethodPoolEvict MgmtMethod = C.DRPC_METHOD_MGMT_POOL_EVICT
	// MethodPoolExclude is a ModuleMgmt method
	MethodPoolExclude MgmtMethod = C.DRPC_METHOD_MGMT_EXCLUDE
	// MethodPoolDrain is a ModuleMgmt method
	MethodPoolDrain MgmtMethod = C.DRPC_METHOD_MGMT_DRAIN
	// MethodPoolExtend is a ModuleMgmt method
	MethodPoolExtend MgmtMethod = C.DRPC_METHOD_MGMT_EXTEND
	// MethodPoolReintegrate is a ModuleMgmt method
	MethodPoolReintegrate MgmtMethod = C.DRPC_METHOD_MGMT_REINTEGRATE
	// MethodBioHealth is a ModuleMgmt method
	MethodBioHealth MgmtMethod = C.DRPC_METHOD_MGMT_BIO_HEALTH_QUERY
	// MethodSetUp is a ModuleMgmt method
	MethodSetUp MgmtMethod = C.DRPC_METHOD_MGMT_SET_UP
	// MethodSmdDevs is a ModuleMgmt method
	MethodSmdDevs MgmtMethod = C.DRPC_METHOD_MGMT_SMD_LIST_DEVS
	// MethodSmdPools is a ModuleMgmt method
	MethodSmdPools MgmtMethod = C.DRPC_METHOD_MGMT_SMD_LIST_POOLS
	// MethodPoolGetACL is a ModuleMgmt method
	MethodPoolGetACL MgmtMethod = C.DRPC_METHOD_MGMT_POOL_GET_ACL
	// MethodListPools is a ModuleMgmt method
	MethodListPools MgmtMethod = C.DRPC_METHOD_MGMT_LIST_POOLS
	// MethodPoolOverwriteACL is a ModuleMgmt method
	MethodPoolOverwriteACL MgmtMethod = C.DRPC_METHOD_MGMT_POOL_OVERWRITE_ACL
	// MethodPoolUpdateACL is a ModuleMgmt method
	MethodPoolUpdateACL MgmtMethod = C.DRPC_METHOD_MGMT_POOL_UPDATE_ACL
	// MethodPoolDeleteACL is a ModuleMgmt method
	MethodPoolDeleteACL MgmtMethod = C.DRPC_METHOD_MGMT_POOL_DELETE_ACL
	// MethodDevStateQuery is a ModuleMgmt method
	MethodDevStateQuery MgmtMethod = C.DRPC_METHOD_MGMT_DEV_STATE_QUERY
	// MethodSetFaultyState is a ModuleMgmt method
	MethodSetFaultyState MgmtMethod = C.DRPC_METHOD_MGMT_DEV_SET_FAULTY
	// MethodListContainers is a ModuleMgmt method
	MethodListContainers MgmtMethod = C.DRPC_METHOD_MGMT_LIST_CONTAINERS
	// MethodPoolQuery defines a method for querying a pool
	MethodPoolQuery MgmtMethod = C.DRPC_METHOD_MGMT_POOL_QUERY
	// MethodPoolSetProp defines a method for setting a pool property
	MethodPoolSetProp MgmtMethod = C.DRPC_METHOD_MGMT_POOL_SET_PROP
	// MethodContSetOwner defines a method for setting the container's owner
	MethodContSetOwner MgmtMethod = C.DRPC_METHOD_MGMT_CONT_SET_OWNER
	// MethodGroupUpdate defines a method for updating the group map
	MethodGroupUpdate MgmtMethod = C.DRPC_METHOD_MGMT_GROUP_UPDATE
	// MethodDisconnect defines a method for signaling a clean client shutdown
	MethodDisconnect MgmtMethod = C.DRPC_METHOD_MGMT_DISCONNECT
)

type srvMethod int32

func (m srvMethod) Module() ModuleID {
	return ModuleSrv
}

func (m srvMethod) ID() int32 {
	return int32(m)
}

func (m srvMethod) String() string {
	if s, ok := map[srvMethod]string{
		MethodNotifyReady: "notify ready",
		MethodBIOError:    "block i/o error",
	}[m]; ok {
		return s
	}

	return fmt.Sprintf("%s:%d", m.Module(), m.ID())
}

// IsValid sanity checks the Method ID is within expected bounds.
func (m srvMethod) IsValid() bool {
	startMethodID := int32(m.Module()) * moduleMethodOffset

	if m.ID() <= startMethodID || m.ID() >= int32(C.NUM_DRPC_SRV_METHODS) {
		return false
	}

	return true
}

const (
	// MethodNotifyReady is a ModuleSrv method
	MethodNotifyReady srvMethod = C.DRPC_METHOD_SRV_NOTIFY_READY
	// MethodBIOError is a ModuleSrv method
	MethodBIOError srvMethod = C.DRPC_METHOD_SRV_BIO_ERR
	// MethodGetPoolServiceRanks requests the service ranks for a pool
	MethodGetPoolServiceRanks srvMethod = C.DRPC_METHOD_SRV_GET_POOL_SVC
)

type securityMethod int32

func (m securityMethod) Module() ModuleID {
	return ModuleSecurity
}

func (m securityMethod) ID() int32 {
	return int32(m)
}

func (m securityMethod) String() string {
	if s, ok := map[securityMethod]string{
		MethodValidateCredentials: "validate credentials",
	}[m]; ok {
		return s
	}

	return fmt.Sprintf("%s:%d", m.Module(), m.ID())
}

// IsValid sanity checks the Method ID is within expected bounds.
func (m securityMethod) IsValid() bool {
	startMethodID := int32(m.Module()) * moduleMethodOffset

	if m.ID() <= startMethodID || m.ID() >= int32(C.NUM_DRPC_SEC_METHODS) {
		return false
	}

	return true
}

const (
	// MethodValidateCredentials is a ModuleSecurity method
	MethodValidateCredentials securityMethod = C.DRPC_METHOD_SEC_VALIDATE_CREDS
)

// Marshal is a utility function that can be used by dRPC method handlers to
// marshal their method-specific response to be passed back to the ModuleService.
func Marshal(message proto.Message) ([]byte, error) {
	msgBytes, err := proto.Marshal(message)
	if err != nil {
		return nil, MarshalingFailure()
	}
	return msgBytes, nil
}
