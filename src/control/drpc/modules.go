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
	if modName, found := map[ModuleID]string{
		ModuleSecurityAgent: "Agent Security",
		ModuleMgmt:          "Management",
		ModuleSrv:           "Server",
		ModuleSecurity:      "Security",
	}[id]; found {
		return modName
	}
	return fmt.Sprintf("unknown module id: %d", id)
}

func (id ModuleID) GetMethod(methodID int32) (Method, error) {
	var m Method
	switch id {
	case ModuleSecurityAgent:
		m = SecurityAgentMethod(methodID)
	case ModuleMgmt:
		m = MgmtMethod(methodID)
	case ModuleSrv:
		m = SrvMethod(methodID)
	case ModuleSecurity:
		m = SecurityMethod(methodID)
	default:
		return nil, errors.Errorf("unknown module id %d", id)
	}

	if !m.Valid() {
		return nil, errors.Errorf("invalid method %d for module %s", methodID, id)
	}
	return m, nil
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
	ModuleID() ModuleID
	String() string
	Valid() bool
}

type SecurityAgentMethod int32

func (m SecurityAgentMethod) ModuleID() ModuleID {
	return ModuleSecurityAgent
}

func (m SecurityAgentMethod) ID() int32 {
	return int32(m)
}

func (m SecurityAgentMethod) String() string {
	if methodName, found := map[SecurityAgentMethod]string{
		MethodRequestCredentials: "agent request credentials",
	}[m]; found {
		return methodName
	}

	return fmt.Sprintf("%s:%d", m.ModuleID(), m.ID())
}

func (m SecurityAgentMethod) Valid() bool {
	startMethodID := int32(m.ModuleID()) * moduleMethodOffset
	return m.ID() >= startMethodID && m.ID() < int32(C.NUM_DRPC_SEC_AGENT_METHODS)
}

const (
	// MethodRequestCredentials is a ModuleSecurityAgent method
	MethodRequestCredentials SecurityAgentMethod = C.DRPC_METHOD_SEC_AGENT_REQUEST_CREDS
)

type MgmtMethod int32

func (m MgmtMethod) ModuleID() ModuleID {
	return ModuleMgmt
}

func (m MgmtMethod) ID() int32 {
	return int32(m)
}

func (m MgmtMethod) String() string {
	if methodName, found := map[MgmtMethod]string{
		MethodPrepShutdown: "prep shutdown",
		MethodPingRank:     "ping",
		MethodSetRank:      "set rank",
		MethodSetUp:        "setup MS",
	}[m]; found {
		return methodName
	}

	return fmt.Sprintf("%s:%d", m.ModuleID(), m.ID())
}

func (m MgmtMethod) Valid() bool {
	startMethodID := int32(m.ModuleID()) * moduleMethodOffset
	return m.ID() >= startMethodID && m.ID() < int32(C.NUM_DRPC_MGMT_METHODS)
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
	// MethodPoolExclude is a ModuleMgmt method
	MethodPoolExclude MgmtMethod = C.DRPC_METHOD_MGMT_EXCLUDE
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
)

type SrvMethod int32

func (m SrvMethod) ModuleID() ModuleID {
	return ModuleSrv
}

func (m SrvMethod) ID() int32 {
	return int32(m)
}

func (m SrvMethod) String() string {
	if methodName, found := map[SrvMethod]string{
		MethodNotifyReady: "notify ready",
		MethodBIOError:    "block i/o error",
	}[m]; found {
		return methodName
	}

	return fmt.Sprintf("%s:%d", m.ModuleID(), m.ID())
}

func (m SrvMethod) Valid() bool {
	startMethodID := int32(m.ModuleID()) * moduleMethodOffset
	return m.ID() >= startMethodID && m.ID() < int32(C.NUM_DRPC_SRV_METHODS)
}

const (
	// MethodNotifyReady is a ModuleSrv method
	MethodNotifyReady SrvMethod = C.DRPC_METHOD_SRV_NOTIFY_READY
	// MethodBIOError is a ModuleSrv method
	MethodBIOError SrvMethod = C.DRPC_METHOD_SRV_BIO_ERR
)

type SecurityMethod int32

func (m SecurityMethod) ModuleID() ModuleID {
	return ModuleSecurity
}

func (m SecurityMethod) ID() int32 {
	return int32(m)
}

func (m SecurityMethod) String() string {
	if methodName, found := map[SecurityMethod]string{
		MethodValidateCredentials: "validate credentials",
	}[m]; found {
		return methodName
	}

	return fmt.Sprintf("%s:%d", m.ModuleID(), m.ID())
}

func (m SecurityMethod) Valid() bool {
	startMethodID := int32(m.ModuleID()) * moduleMethodOffset
	return m.ID() >= startMethodID && m.ID() < int32(C.NUM_DRPC_SEC_METHODS)
}

const (
	// MethodValidateCredentials is a ModuleSecurity method
	MethodValidateCredentials SecurityMethod = C.DRPC_METHOD_SEC_VALIDATE_CREDS
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
