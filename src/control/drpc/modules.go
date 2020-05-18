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

// This file imports all of the DAOS dRPC module/method IDs.

package drpc

import (
	"github.com/golang/protobuf/proto"

	"github.com/daos-stack/daos/src/control/system"
)

// #cgo CFLAGS: -I${SRCDIR}/../../include
// #include <daos/drpc_modules.h>
import "C"

const (
	// ModuleSecurityAgent is the dRPC module for security tasks in the
	// DAOS agent
	ModuleSecurityAgent = C.DRPC_MODULE_SEC_AGENT
	// ModuleMgmt is the dRPC module for management service tasks
	ModuleMgmt = C.DRPC_MODULE_MGMT
	// ModuleSrv is the dRPC module for tasks relating to server setup
	ModuleSrv = C.DRPC_MODULE_SRV
	// ModuleSecurity is the dRPC module for security tasks in the DAOS
	// server
	ModuleSecurity = C.DRPC_MODULE_SEC
)

// Method interface is implemented by aliases for methods of each dRPC module.
type Method interface {
	Module() int32
	String() string
}

// GetMethod returns object of type that implements method interface based on
// module and method IDs.
func GetMethod(moduleID int32, methodID int32) (Method, bool) {
	switch moduleID {
	case ModuleSecurityAgent:
		m := SecurityAgentMethod(methodID)
		return &m, true
	case ModuleMgmt:
		return MgmtMethod(methodID), true
	case ModuleSrv:
		return SrvMethod(methodID), true
	case ModuleSecurity:
		return SecurityMethod(methodID), true
	default:
		return nil, false
	}
}

// SecurityAgentMethod is a type alias for a drpc agent security method.
type SecurityAgentMethod int32

func (sam *SecurityAgentMethod) String() string {
	return map[*SecurityAgentMethod]string{}[sam]
}

// Module returns the module that the method belongs to.
func (sam *SecurityAgentMethod) Module() int32 {
	return ModuleSecurityAgent
}

const (
	// MethodRequestCredentials is a ModuleSecurityAgent method
	MethodRequestCredentials SecurityAgentMethod = C.DRPC_METHOD_SEC_AGENT_REQUEST_CREDS
)

// MgmtMethod is a type alias for a drpc mgmt method.
type MgmtMethod int32

func (mm MgmtMethod) String() string {
	return map[MgmtMethod]string{
		MethodPrepShutdown: "prep shutdown",
		MethodPingRank:     "ping",
		MethodSetRank:      "set rank",
		MethodSetUp:        "setup MS",
	}[mm]
}

// Module returns the module that the method belongs to.
func (mm MgmtMethod) Module() int32 {
	return ModuleMgmt
}

// TargetState returns the system member state that should be set on successful
// completion of the method action.
func (mm MgmtMethod) TargetState() system.MemberState {
	return map[MgmtMethod]system.MemberState{
		MethodPrepShutdown: system.MemberStateStopping,
		MethodPingRank:     system.MemberStateReady,
	}[mm]
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

// SrvMethod is a type alias for a drpc srv method.
type SrvMethod int32

func (sm SrvMethod) String() string {
	return map[SrvMethod]string{}[sm]
}

// Module returns the module that the method belongs to.
func (sm SrvMethod) Module() int32 {
	return ModuleSrv
}

const (
	// MethodNotifyReady is a ModuleSrv method
	MethodNotifyReady SrvMethod = C.DRPC_METHOD_SRV_NOTIFY_READY
	// MethodBIOError is a ModuleSrv method
	MethodBIOError SrvMethod = C.DRPC_METHOD_SRV_BIO_ERR
)

// SecurityMethod is a type alias for a drpc security method.
type SecurityMethod int32

func (sm SecurityMethod) String() string {
	return map[SecurityMethod]string{}[sm]
}

// Module returns the module that the method belongs to.
func (sm SecurityMethod) Module() int32 {
	return ModuleSecurity
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
