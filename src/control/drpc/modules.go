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
)

// #cgo CFLAGS: -I${SRCDIR}/../../include
// #include <daos/drpc_modules.h>
import "C"

const moduleMethodOffset = 100

const (
	// ModuleSecurityAgent is the dRPC module for security tasks in DAOS agent
	ModuleSecurityAgent = C.DRPC_MODULE_SEC_AGENT
	// ModuleMgmt is the dRPC module for management service tasks
	ModuleMgmt = C.DRPC_MODULE_MGMT
	// ModuleSrv is the dRPC module for tasks relating to server setup
	ModuleSrv = C.DRPC_MODULE_SRV
	// ModuleSecurity is the dRPC module for security tasks in DAOS server
	ModuleSecurity = C.DRPC_MODULE_SEC
)

type Method struct {
	id     int32
	module int32
}

func (meth *Method) Module() int32 {
	return meth.module
}

func (meth *Method) ID() int32 {
	return meth.id
}

func (meth *Method) Equals(m *Method) bool {
	return meth.id == m.id && meth.module == m.module
}

func (meth *Method) String() string {
	s := map[int32]string{
		MethodPrepShutdown: "prep shutdown",
		MethodPingRank:     "ping",
		MethodSetRank:      "set rank",
		MethodSetUp:        "setup MS",
	}[meth.id]

	if s == "" {
		return fmt.Sprintf("%d", meth.id)
	}

	return s
}

// NewMethod constructs a Method object and returns a reference.
func NewMethod(moduleID, methodID int32) *Method {
	return &Method{id: methodID, module: moduleID}
}

// GetMethod returns Method reference after validating method and module IDs.
//
// Returns reference and boolean indicating a valid module.
// Reference will be nil if method validation fails.
func GetMethod(moduleID int32, methodID int32) (*Method, bool) {
	var numMethods int32
	startMethodID := moduleID * moduleMethodOffset

	switch moduleID {
	case ModuleSecurityAgent:
		numMethods = int32(C.NUM_DRPC_SEC_AGENT_METHODS)
	case ModuleMgmt:
		numMethods = int32(C.NUM_DRPC_MGMT_METHODS)
	case ModuleSrv:
		numMethods = int32(C.NUM_DRPC_SRV_METHODS)
	case ModuleSecurity:
		numMethods = int32(C.NUM_DRPC_SEC_METHODS)
	default:
		return nil, false
	}

	if methodID <= startMethodID || methodID > (startMethodID+numMethods) {
		return nil, true
	}

	return NewMethod(methodID, moduleID), true
}

//// SecurityAgentMethod is a type alias for a drpc agent security method.
//type SecurityAgentMethod int32
//
//func (meth *SecurityAgentMethod) String() string {
//	s := map[SecurityAgentMethod]string{}[*meth]
//
//	if s == "" {
//		return fmt.Sprintf("%d", meth)
//	}
//
//	return s
//}
//
//// Module returns the module that the method belongs to.
//func (meth *SecurityAgentMethod) Module() int32 {
//	return ModuleSecurityAgent
//}

const (
	// MethodRequestCredentials is a ModuleSecurityAgent method
	MethodRequestCredentials = C.DRPC_METHOD_SEC_AGENT_REQUEST_CREDS
)

//// MgmtMethod is a type alias for a drpc mgmt method.
//type MgmtMethod int32
//
//func (meth *MgmtMethod) String() string {
//	s := map[MgmtMethod]string{
//		MethodPrepShutdown: "prep shutdown",
//		MethodPingRank:     "ping",
//		MethodSetRank:      "set rank",
//		MethodSetUp:        "setup MS",
//	}[*meth]
//
//	if s == "" {
//		return fmt.Sprintf("%d", meth)
//	}
//
//	return s
//}
//
//// Module returns the module that the method belongs to.
//func (meth *MgmtMethod) Module() int32 {
//	return ModuleMgmt
//}

const (
	// MethodPrepShutdown is a ModuleMgmt method
	MethodPrepShutdown = C.DRPC_METHOD_MGMT_PREP_SHUTDOWN
	// MethodPingRank is a ModuleMgmt method
	MethodPingRank = C.DRPC_METHOD_MGMT_PING_RANK
	// MethodSetRank is a ModuleMgmt method
	MethodSetRank = C.DRPC_METHOD_MGMT_SET_RANK
	// MethodCreateMS is a ModuleMgmt method
	MethodCreateMS = C.DRPC_METHOD_MGMT_CREATE_MS
	// MethodStartMS is a ModuleMgmt method
	MethodStartMS = C.DRPC_METHOD_MGMT_START_MS
	// MethodJoin is a ModuleMgmt method
	MethodJoin = C.DRPC_METHOD_MGMT_JOIN
	// MethodGetAttachInfo is a ModuleMgmt method
	MethodGetAttachInfo = C.DRPC_METHOD_MGMT_GET_ATTACH_INFO
	// MethodPoolCreate is a ModuleMgmt method
	MethodPoolCreate = C.DRPC_METHOD_MGMT_POOL_CREATE
	// MethodPoolDestroy is a ModuleMgmt method
	MethodPoolDestroy = C.DRPC_METHOD_MGMT_POOL_DESTROY
	// MethodPoolExclude is a ModuleMgmt method
	MethodPoolExclude = C.DRPC_METHOD_MGMT_EXCLUDE
	// MethodPoolReintegrate is a ModuleMgmt method
	MethodPoolReintegrate = C.DRPC_METHOD_MGMT_REINTEGRATE
	// MethodBioHealth is a ModuleMgmt method
	MethodBioHealth = C.DRPC_METHOD_MGMT_BIO_HEALTH_QUERY
	// MethodSetUp is a ModuleMgmt method
	MethodSetUp = C.DRPC_METHOD_MGMT_SET_UP
	// MethodSmdDevs is a ModuleMgmt method
	MethodSmdDevs = C.DRPC_METHOD_MGMT_SMD_LIST_DEVS
	// MethodSmdPools is a ModuleMgmt method
	MethodSmdPools = C.DRPC_METHOD_MGMT_SMD_LIST_POOLS
	// MethodPoolGetACL is a ModuleMgmt method
	MethodPoolGetACL = C.DRPC_METHOD_MGMT_POOL_GET_ACL
	// MethodListPools is a ModuleMgmt method
	MethodListPools = C.DRPC_METHOD_MGMT_LIST_POOLS
	// MethodPoolOverwriteACL is a ModuleMgmt method
	MethodPoolOverwriteACL = C.DRPC_METHOD_MGMT_POOL_OVERWRITE_ACL
	// MethodPoolUpdateACL is a ModuleMgmt method
	MethodPoolUpdateACL = C.DRPC_METHOD_MGMT_POOL_UPDATE_ACL
	// MethodPoolDeleteACL is a ModuleMgmt method
	MethodPoolDeleteACL = C.DRPC_METHOD_MGMT_POOL_DELETE_ACL
	// MethodDevStateQuery is a ModuleMgmt method
	MethodDevStateQuery = C.DRPC_METHOD_MGMT_DEV_STATE_QUERY
	// MethodSetFaultyState is a ModuleMgmt method
	MethodSetFaultyState = C.DRPC_METHOD_MGMT_DEV_SET_FAULTY
	// MethodListContainers is a ModuleMgmt method
	MethodListContainers = C.DRPC_METHOD_MGMT_LIST_CONTAINERS
	// MethodPoolQuery defines a method for querying a pool
	MethodPoolQuery = C.DRPC_METHOD_MGMT_POOL_QUERY
	// MethodPoolSetProp defines a method for setting a pool property
	MethodPoolSetProp = C.DRPC_METHOD_MGMT_POOL_SET_PROP
	// MethodContSetOwner defines a method for setting the container's owner
	MethodContSetOwner = C.DRPC_METHOD_MGMT_CONT_SET_OWNER
)

//// SrvMethod is a type alias for a drpc srv method.
//type SrvMethod int32
//
//func (meth *SrvMethod) String() string {
//	s := map[SrvMethod]string{}[*meth]
//
//	if s == "" {
//		return fmt.Sprintf("%d", meth)
//	}
//
//	return s
//}
//
//// Module returns the module that the method belongs to.
//func (meth *SrvMethod) Module() int32 {
//	return ModuleSrv
//}

const (
	// MethodNotifyReady is a ModuleSrv method
	MethodNotifyReady = C.DRPC_METHOD_SRV_NOTIFY_READY
	// MethodBIOError is a ModuleSrv method
	MethodBIOError = C.DRPC_METHOD_SRV_BIO_ERR
)

//// SecurityMethod is a type alias for a drpc security method.
//type SecurityMethod int32
//
//func (meth *SecurityMethod) String() string {
//	s := map[SecurityMethod]string{}[*meth]
//
//	if s == "" {
//		return fmt.Sprintf("%d", meth)
//	}
//
//	return s
//}
//
//// Module returns the module that the method belongs to.
//func (meth *SecurityMethod) Module() int32 {
//	return ModuleSecurity
//}

const (
	// MethodValidateCredentials is a ModuleSecurity method
	MethodValidateCredentials = C.DRPC_METHOD_SEC_VALIDATE_CREDS
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
