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

package security

import (
	"errors"

	"utils/log"

	pb "security/proto"

	uuid "github.com/satori/go.uuid"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/peer"
)

// ControlService implements our gRPC security control service interface
type ControlService struct {
	logger *log.Logger
	ctxmap *ContextMap
}

// Helper function for generating SecurityReply protobuf objects
//	status: Whether our operation succeeded or not (Required)
//	token: The response data from our request (May be nil in case of error)
//	err: Error object in case of error (May be nil if we succeeded)
func createAuthResponse(status pb.AuthStatus, token *pb.AuthToken, err error) *pb.SecurityReply {
	var reply *pb.SecurityReply
	var rStatus *pb.ReplyStatus

	if status != pb.AuthStatus_AUTH_OK {
		rStatus = &pb.ReplyStatus{
			Status: pb.Status_AUTH_ERROR,
			Error:  &pb.ReplyStatus_Auth{status},
		}
		if err != nil {
			rStatus.ErrorMessage = err.Error()
		}
	} else {
		rStatus = &pb.ReplyStatus{
			Status: pb.Status_SUCCESS,
		}
	}

	reply = &pb.SecurityReply{
		Status: rStatus,
	}
	if token != nil {
		reply.Response = token
	}
	return reply
}

// SecurityAction handler for the RPC which is a multiplexed call for performaing security Actionse
func (s *ControlService) SecurityAction(ctx context.Context, request *pb.SecurityRequest) (*pb.SecurityReply, error) {

	if request == nil {
		return nil, errors.New("No request provided")
	}

	var response *pb.SecurityReply

	switch action := request.Action; action {
	case pb.AuthAction_AUTH_INIT:
		key, err := s.ctxmap.AddToken(request.Host, request.GetToken())

		if err != nil {
			s.logger.LogGrpcErr(err)
			response = createAuthResponse(pb.AuthStatus_AUTH_FAILED, nil, err)
			break
		}

		handle := &pb.AuthToken{
			Flavor: request.GetToken().GetFlavor(),
			Token:  key.Bytes(),
		}
		response = createAuthResponse(pb.AuthStatus_AUTH_OK, handle, nil)

	case pb.AuthAction_AUTH_FINI:
		handle, err := uuid.FromBytes(request.GetHandle().GetHandle())
		if err != nil {
			response = createAuthResponse(pb.AuthStatus_AUTH_FAILED, nil, err)
			break
		}
		err = s.ctxmap.FinalizeToken(request.Host, handle)
		if err != nil {
			response = createAuthResponse(pb.AuthStatus_AUTH_FAILED, nil, err)
			break
		}
		response = createAuthResponse(pb.AuthStatus_AUTH_OK, nil, nil)

	case pb.AuthAction_AUTH_SHARE:
		err := errors.New("NotImplementedYet")
		response = createAuthResponse(pb.AuthStatus_AUTH_FAILED, nil, err)
	case pb.AuthAction_AUTH_UNSHARE:
		err := errors.New("NotImplementedYet")
		response = createAuthResponse(pb.AuthStatus_AUTH_FAILED, nil, err)
	case pb.AuthAction_AUTH_GET:
		handle, err := uuid.FromBytes(request.GetHandle().GetHandle())
		if err != nil {
			response = createAuthResponse(pb.AuthStatus_AUTH_FAILED, nil, err)
			break
		}
		token := s.ctxmap.GetToken(handle)
		if token == nil {
			response = createAuthResponse(pb.AuthStatus_AUTH_BADCRED, nil, errors.New("Invalid Handle"))
			break
		}
		response = createAuthResponse(pb.AuthStatus_AUTH_OK, token, nil)

	case pb.AuthAction_AUTH_PUT:
		handle, err := uuid.FromBytes(request.GetHandle().GetHandle())
		if err != nil {
			response = createAuthResponse(pb.AuthStatus_AUTH_FAILED, nil, err)
			break
		}
		err = s.ctxmap.PutToken(handle)
		if err == nil {
			response = createAuthResponse(pb.AuthStatus_AUTH_BADCRED, nil, errors.New("Invalid Handle"))
			break
		}
		response = createAuthResponse(pb.AuthStatus_AUTH_OK, nil, nil)
	default:
		response = createAuthResponse(pb.AuthStatus_AUTH_FAILED, nil, errors.New("Invalid AuthAction"))
	}

	return response, nil
}

// NewControlServer creates a new instance of our serverService struct.
func NewControlServer() *ControlService {
	return &ControlService{log.NewLogger(), NewContextMap()}
}

// AgentServiceServer implements our service interface and holds
// our client to contact the management server.
type AgentServiceServer struct {
	logger *log.Logger
	client pb.SecurityControlClient
}

// RequestSecurityContext gRPC service handler
func (s *AgentServiceServer) RequestSecurityContext(ctx context.Context, request *pb.AuthToken) (*pb.SecurityReply, error) {
	pr, _ := peer.FromContext(ctx)
	creds := pr.AuthInfo.(*DomainInfo)

	action, err := AuthSysRequestFromCreds(creds, s.logger)

	if err != nil {
		return nil, err
	}
	result, err := s.client.SecurityAction(context.Background(), action)
	return result, err
}

// VerifySecurityHandle gRPC service handler
func (s *AgentServiceServer) VerifySecurityHandle(ctx context.Context, handle *pb.SecurityHandle) (*pb.SecurityReply, error) {
	action := &pb.SecurityRequest{
		Action: pb.AuthAction_AUTH_GET,
		Data:   &pb.SecurityRequest_Handle{handle},
	}

	result, err := s.client.SecurityAction(context.Background(), action)
	return result, err
}

// NewAgentServer constructor for instantiating an agentSericeServer object
func NewAgentServer(conn *grpc.ClientConn) *AgentServiceServer {
	client := pb.NewSecurityControlClient(conn)
	return &AgentServiceServer{log.NewLogger(), client}
}
