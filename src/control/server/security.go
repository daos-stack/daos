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

package main

// #cgo CFLAGS: -I${SRCDIR}/../../include
// #include <daos/drpc_modules.h>
import "C"
import (
	"context"

	"github.com/daos-stack/daos/src/control/drpc"
	pb "github.com/daos-stack/daos/src/control/security/proto"
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
)

const moduleID int32 = C.DRPC_MODULE_SECURITY_AGENT

// TODO: Get Method IDs from the IO server header file, when it exists
const (
	methodSetAcl     int32 = 104
	methodGetAcl     int32 = 105
	methodDestroyAcl int32 = 106
)

// SecurityService contains the data for the service that performs tasks related to
// security and access control
type SecurityService struct {
	drpc drpc.DomainSocketClient
}

// processDrpcReponse extracts the AclResponse from the drpc Response, and
// checks for some basic formatting errors
func (s *SecurityService) processDrpcResponse(drpcResp *drpc.Response) (*pb.AclResponse, error) {
	if drpcResp == nil {
		return nil, errors.Errorf("dRPC returned no response")
	}

	if drpcResp.Status != drpc.Status_SUCCESS {
		return nil, errors.Errorf("bad dRPC response status: %v",
			drpcResp.Status.String())
	}

	resp := &pb.AclResponse{}
	err := proto.Unmarshal(drpcResp.Body, resp)
	if err != nil {
		return nil, errors.Errorf("invalid dRPC response body: %v", err)
	}

	return resp, nil
}

// newDrpcCall creates a new drpc Call instance for the security module, with
// the protobuf message marshalled in the body
func (s *SecurityService) newDrpcCall(method int32, bodyMessage proto.Message) (*drpc.Call, error) {
	bodyBytes, err := proto.Marshal(bodyMessage)
	if err != nil {
		return nil, err
	}

	return &drpc.Call{
		Module: moduleID,
		Method: method,
		Body:   bodyBytes,
	}, nil
}

// callDrpcMethodWithMessage opens a drpc connection, sends a message with the
// protobuf message marshalled in the body, and closes the connection.
func (s *SecurityService) callDrpcMethodWithMessage(method int32, body proto.Message) (*pb.AclResponse, error) {
	drpcCall, err := s.newDrpcCall(method, body)
	if err != nil {
		return nil, err
	}

	// Forward the request to the I/O server via dRPC
	err = s.drpc.Connect()
	if err != nil {
		return nil, err
	}
	defer s.drpc.Close()

	drpcResp, err := s.drpc.SendMsg(drpcCall)
	if err != nil {
		return nil, err
	}

	return s.processDrpcResponse(drpcResp)
}

// SetPermissions sets the permissions for a given Access Control Entry, and creates
// it if it doesn't already exist.
func (s *SecurityService) SetPermissions(ctx context.Context, perms *pb.AclEntryPermissions) (*pb.AclResponse, error) {
	if perms == nil {
		return nil, errors.Errorf("requested permissions were nil")
	}

	return s.callDrpcMethodWithMessage(methodSetAcl, perms)
}

// GetPermissions fetches the current permissions for a given Access Control Entry.
func (s *SecurityService) GetPermissions(ctx context.Context, entry *pb.AclEntry) (*pb.AclResponse, error) {
	if entry == nil {
		return nil, errors.Errorf("requested entry was nil")
	}

	return s.callDrpcMethodWithMessage(methodGetAcl, entry)
}

// DestroyAclEntry destroys the given Access Control Entry. The permissions for the
// principal on that object will be inferred from the remaining entries.
func (s *SecurityService) DestroyAclEntry(ctx context.Context, entry *pb.AclEntry) (*pb.AclResponse, error) {
	if entry == nil {
		return nil, errors.Errorf("requested entry was nil")
	}

	return s.callDrpcMethodWithMessage(methodDestroyAcl, entry)
}

// newSecurityService creates and initializes a new security SecurityService instance
func newSecurityService(client drpc.DomainSocketClient) *SecurityService {
	return &SecurityService{
		drpc: client,
	}
}
