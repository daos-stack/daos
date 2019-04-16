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

import (
	"testing"

	. "github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/drpc"
	pb "github.com/daos-stack/daos/src/control/security/proto"
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
)

// mockDrpcClient is a mock of the DomainSocketClient interface
type mockDrpcClient struct {
	ConnectOutputError    error
	CloseOutputError      error
	CloseCallCount        int
	SendMsgInputCall      *drpc.Call
	SendMsgOutputResponse *drpc.Response
	SendMsgOutputError    error
}

func (c *mockDrpcClient) IsConnected() bool {
	return false
}

func (c *mockDrpcClient) Connect() error {
	return c.ConnectOutputError
}

func (c *mockDrpcClient) Close() error {
	c.CloseCallCount++
	return c.CloseOutputError
}

func (c *mockDrpcClient) SendMsg(call *drpc.Call) (*drpc.Response, error) {
	c.SendMsgInputCall = call
	return c.SendMsgOutputResponse, c.SendMsgOutputError
}

func (c *mockDrpcClient) setSendMsgResponse(status drpc.Status, body []byte) {
	c.SendMsgOutputResponse = &drpc.Response{
		Status: status,
		Body:   body,
	}
}

func newMockDrpcClient() *mockDrpcClient {
	return &mockDrpcClient{}
}

// newTestSecurityService sets up a new service with mocks
func newTestSecurityService(client *mockDrpcClient) *SecurityService {
	return &SecurityService{
		drpc: client,
	}
}

// newValidAclEntry sets up a valid-looking ACL Entry for testing
func newValidAclEntry() *pb.AclEntry {
	return &pb.AclEntry{
		Type:     pb.AclEntryType_ALLOW,
		Flags:    uint32(pb.AclFlags_GROUP),
		Entity:   "12345678-1234-1234-1234-123456789ABC",
		Identity: "group1",
	}
}

// newValidAclEntryPermissions sets up valid-looking ACL Permissions for testing
func newValidAclEntryPermissions() *pb.AclEntryPermissions {
	return &pb.AclEntryPermissions{
		PermissionBits: uint64(pb.AclPermissions_READ),
		Entry:          newValidAclEntry(),
	}
}

func aclResponseToBytes(resp *pb.AclResponse) []byte {
	bytes, _ := proto.Marshal(resp)
	return bytes
}

func TestNewSecurityService(t *testing.T) {
	expectedClient := newMockDrpcClient()
	service := newSecurityService(expectedClient)

	AssertTrue(t, service != nil, "NewSecurityService returned nil")
	AssertEqual(t, service.drpc, expectedClient, "Wrong dRPC client")
}

func TestSetPermissions_NilPermissions(t *testing.T) {
	service := newTestSecurityService(newMockDrpcClient())

	result, err := service.SetPermissions(nil, nil)

	AssertEqual(t, result, (*pb.AclResponse)(nil), "Expected no response")
	ExpectError(t, err, "requested permissions were nil",
		"Should report bad input")
}

func expectAclResponse(t *testing.T, result *pb.AclResponse, err error,
	expectedResp *pb.AclResponse) {
	expectedPerms := expectedResp.Permissions

	AssertTrue(t, err == nil, "Expected no error")
	AssertTrue(t, result != nil, "Expected a response")
	AssertEqual(t, result.Status, expectedResp.Status,
		"Reponse status was wrong")
	if expectedPerms == nil {
		AssertEqual(t, result.Permissions,
			(*pb.AclEntryPermissions)(nil),
			"Permissions should be nil")
	} else {
		AssertEqual(t, result.Permissions.PermissionBits,
			expectedPerms.PermissionBits,
			"Permission bits were wrong")
		AssertEqual(t, result.Permissions.Entry.Entity,
			expectedPerms.Entry.Entity,
			"UUID was wrong")
		AssertEqual(t, result.Permissions.Entry.Type,
			expectedPerms.Entry.Type, "Entry type was wrong")
		AssertEqual(t, result.Permissions.Entry.Flags,
			expectedPerms.Entry.Flags, "Entry flags were wrong")
		AssertEqual(t, result.Permissions.Entry.Identity,
			expectedPerms.Entry.Identity,
			"Principal identity was wrong")
	}

}

func expectDrpcCall(t *testing.T, client *mockDrpcClient,
	expectedModuleId int32, expectedMethodId int32,
	expectedCallBody []byte) {
	AssertTrue(t, client.SendMsgInputCall != nil, "SendMsg called with nil")
	AssertEqual(t, client.SendMsgInputCall.Module, expectedModuleId,
		"Wrong dRPC module")
	AssertEqual(t, client.SendMsgInputCall.Method, expectedMethodId,
		"Wrong dRPC method")
	AssertEqual(t, client.SendMsgInputCall.Body, expectedCallBody,
		"dRPC call should have permissions request in body")
}

func TestSetPermissions_Success(t *testing.T) {
	client := newMockDrpcClient()
	service := newTestSecurityService(client)
	perms := newValidAclEntryPermissions()
	expectedResp := &pb.AclResponse{
		Status:      pb.AclRequestStatus_SUCCESS,
		Permissions: perms,
	}
	client.setSendMsgResponse(drpc.Status_SUCCESS,
		aclResponseToBytes(expectedResp))

	result, err := service.SetPermissions(nil, perms)

	// Check the results
	expectAclResponse(t, result, err, expectedResp)

	// Check that the dRPC call was correct
	expectedCallBody, _ := proto.Marshal(perms)
	expectDrpcCall(t, client, moduleID, methodSetAcl, expectedCallBody)
}

func TestSetPermissions_SendMsgFailed(t *testing.T) {
	client := newMockDrpcClient()
	expectedError := "mock dRPC call failed"
	client.SendMsgOutputError = errors.Errorf(expectedError)
	service := newTestSecurityService(client)

	result, err := service.SetPermissions(nil,
		newValidAclEntryPermissions())

	AssertEqual(t, result, (*pb.AclResponse)(nil), "Expected no response")
	ExpectError(t, err, expectedError, "Should pass up the dRPC call error")
}

func TestSetPermissions_SendMsgResponseStatusFailed(t *testing.T) {
	client := newMockDrpcClient()
	service := newTestSecurityService(client)
	client.setSendMsgResponse(drpc.Status_FAILURE, nil)

	result, err := service.SetPermissions(nil,
		newValidAclEntryPermissions())

	AssertEqual(t, result, (*pb.AclResponse)(nil), "Expected no response")
	ExpectError(t, err, "bad dRPC response status: FAILURE",
		"Should report dRPC call failed")
}

func TestSetPermissions_SendMsgResponseBodyInvalid(t *testing.T) {
	client := newMockDrpcClient()
	service := newTestSecurityService(client)
	badResp := make([]byte, 256)
	for i := 0; i < len(badResp); i++ {
		badResp[i] = 0xFF
	}
	client.setSendMsgResponse(drpc.Status_SUCCESS, badResp)

	result, err := service.SetPermissions(nil,
		newValidAclEntryPermissions())

	AssertEqual(t, result, (*pb.AclResponse)(nil), "Expected no response")
	ExpectError(t, err, "invalid dRPC response body: unexpected EOF",
		"Should report failed to unmarshal")
}

func TestSetPermissions_SendMsgResponseNil(t *testing.T) {
	client := newMockDrpcClient()
	service := newTestSecurityService(client)

	result, err := service.SetPermissions(nil,
		newValidAclEntryPermissions())

	AssertEqual(t, result, (*pb.AclResponse)(nil), "Expected no response")
	ExpectError(t, err, "dRPC returned no response",
		"Should report nil response")
}

func TestSetPermissions_ConnectFailed(t *testing.T) {
	client := newMockDrpcClient()
	expectedError := "mock dRPC connect failed"
	client.ConnectOutputError = errors.Errorf(expectedError)
	service := newTestSecurityService(client)

	result, err := service.SetPermissions(nil,
		newValidAclEntryPermissions())

	AssertEqual(t, result, (*pb.AclResponse)(nil), "Expected no response")
	ExpectError(t, err, expectedError, "Should pass up the dRPC call error")
}

func TestSetPermissions_CloseFailed(t *testing.T) {
	client := newMockDrpcClient()
	expectedError := "mock dRPC close failed"
	client.CloseOutputError = errors.Errorf(expectedError)
	service := newTestSecurityService(client)
	client.setSendMsgResponse(drpc.Status_SUCCESS,
		aclResponseToBytes(&pb.AclResponse{}))

	result, err := service.SetPermissions(nil,
		newValidAclEntryPermissions())

	// We ignore Close errors - not useful to us if we got a good message
	AssertEqual(t, err, (error)(nil), "Expected no error")
	AssertTrue(t, result != nil, "Expected the response")

	// Make sure it was actually called
	AssertEqual(t, client.CloseCallCount, 1, "Close should have been called")
}

func TestGetPermissions_NilEntry(t *testing.T) {
	service := newTestSecurityService(newMockDrpcClient())

	result, err := service.GetPermissions(nil, nil)

	AssertEqual(t, result, (*pb.AclResponse)(nil), "Expected no response")
	ExpectError(t, err, "requested entry was nil",
		"Should detect invalid input")
}

func TestGetPermissions_Success(t *testing.T) {
	client := newMockDrpcClient()
	service := newTestSecurityService(client)
	entry := newValidAclEntry()

	expectedPerms := &pb.AclEntryPermissions{
		Entry: entry,
		PermissionBits: uint64(
			pb.AclPermissions_READ | pb.AclPermissions_WRITE),
	}
	expectedResp := &pb.AclResponse{
		Status:      pb.AclRequestStatus_SUCCESS,
		Permissions: expectedPerms,
	}
	client.setSendMsgResponse(drpc.Status_SUCCESS,
		aclResponseToBytes(expectedResp))

	result, err := service.GetPermissions(nil, entry)

	// Check the results
	expectAclResponse(t, result, err, expectedResp)

	// Check that the dRPC call was correct
	expectedCallBody, _ := proto.Marshal(entry)
	expectDrpcCall(t, client, moduleID, methodGetAcl, expectedCallBody)
}

func TestGetPermissions_SendMsgFailed(t *testing.T) {
	client := newMockDrpcClient()
	expectedError := "mock dRPC call failed"
	client.SendMsgOutputError = errors.Errorf(expectedError)
	service := newTestSecurityService(client)

	result, err := service.GetPermissions(nil, newValidAclEntry())

	AssertEqual(t, result, (*pb.AclResponse)(nil), "Expected no response")
	ExpectError(t, err, expectedError, "Should pass up the dRPC call error")
}

func TestGetPermissions_ConnectFailed(t *testing.T) {
	client := newMockDrpcClient()
	expectedError := "mock dRPC connect failed"
	client.ConnectOutputError = errors.Errorf(expectedError)
	service := newTestSecurityService(client)

	result, err := service.GetPermissions(nil, newValidAclEntry())

	AssertEqual(t, result, (*pb.AclResponse)(nil), "Expected no response")
	ExpectError(t, err, expectedError, "Should pass up the dRPC call error")
}

func TestGetPermissions_CloseFailed(t *testing.T) {
	client := newMockDrpcClient()
	expectedError := "mock dRPC close failed"
	client.CloseOutputError = errors.Errorf(expectedError)
	service := newTestSecurityService(client)
	client.setSendMsgResponse(drpc.Status_SUCCESS,
		aclResponseToBytes(&pb.AclResponse{}))

	result, err := service.GetPermissions(nil, newValidAclEntry())

	// We ignore Close errors - not useful to us if we got a good message
	AssertEqual(t, err, (error)(nil), "Expected no error")
	AssertTrue(t, result != nil, "Expected the response")

	// Make sure it was actually called
	AssertEqual(t, client.CloseCallCount, 1, "Close should have been called")
}

func TestDestroyAclEntry_NilEntry(t *testing.T) {
	service := newTestSecurityService(newMockDrpcClient())

	result, err := service.DestroyAclEntry(nil, nil)

	AssertEqual(t, result, (*pb.AclResponse)(nil), "Expected no response")
	ExpectError(t, err, "requested entry was nil",
		"Should detect invalid input")
}

func TestDestroyAclEntry_Success(t *testing.T) {
	client := newMockDrpcClient()
	service := newTestSecurityService(client)
	entry := newValidAclEntry()

	expectedResp := &pb.AclResponse{
		Status:      pb.AclRequestStatus_SUCCESS,
		Permissions: nil,
	}
	client.setSendMsgResponse(drpc.Status_SUCCESS,
		aclResponseToBytes(expectedResp))

	result, err := service.DestroyAclEntry(nil, entry)

	// Check the results
	expectAclResponse(t, result, err, expectedResp)

	// Check that the dRPC call was correct
	expectedCallBody, _ := proto.Marshal(entry)
	expectDrpcCall(t, client, moduleID, methodDestroyAcl, expectedCallBody)
}
