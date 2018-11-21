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

package drpc

import (
	"fmt"

	"github.com/golang/protobuf/proto"
)

// ModuleState is an interface to allow a module to pass in private
// information to the rpcModule that it will need to perform its duties.
// An empty interface is Go's equivalent of a void *
type ModuleState interface{}

// Module is an interface that a type must implement to provide the
// functionality needed by the rpcService to be able to process
// requests.
type Module interface {
	HandleCall(*Client, int32, []byte) ([]byte, error)
	InitModule(ModuleState)
	ID() int32
}

// Service is the type representing the collection of Modules used by
// DomainSocketServer to be used to process messages.
type Service struct {
	modules map[int32]Module
}

// NewRPCService creates an initialized Service instance
func NewRPCService() *Service {
	modules := make(map[int32]Module)
	return &Service{modules}
}

// RegisterModule will take in a type that implements the rpcModule interface
// and ensure that no other module is already registered with that module
// identifier.
func (r *Service) RegisterModule(mod Module) error {
	_, ok := r.modules[mod.ID()]
	if ok == true {
		return fmt.Errorf("module with Id %d already exists", mod.ID())
	}
	r.modules[mod.ID()] = mod
	return nil
}

// marshalResponse is an internal function that will take the necessary
// and create a dRPC Response protobuf bytes to send back
//
//	Arguments:
//	sequence: the sequence number associated with the call processed
//	status: the drpc.Status of the response
//	body: the bytes associated with the response data.
//
//	Returns:
//	(bytes representing response protobuf, marshalling error if one exits)
func marshalResponse(sequence int64, status Status, body []byte) ([]byte, error) {
	var response Response

	if status == Status_SUCCESS {
		response = Response{
			Sequence: sequence,
			Status:   status,
			Body:     body,
		}
	} else {
		response = Response{
			Sequence: sequence,
			Status:   status,
		}
	}

	responseBytes, mErr := proto.Marshal(&response)
	if mErr != nil {
		return nil, mErr
	}
	return responseBytes, nil
}

// ProcessMessage is the main entry point into the rpcService for
// consumers where it can pass the bytes of the drpc.Call instance.
// That instance is then unmarshaled and processed and a response is
// returned.
func (r *Service) ProcessMessage(client *Client, callBytes []byte) ([]byte, error) {
	rpcMsg := &Call{}

	err := proto.Unmarshal(callBytes, rpcMsg)
	if err != nil {
		return marshalResponse(-1, Status_FAILURE, nil)
	}
	module, ok := r.modules[rpcMsg.GetModule()]
	if !ok {
		err = fmt.Errorf("Attempted to call unregistered module")
		return marshalResponse(rpcMsg.GetSequence(), Status_FAILURE, nil)
	}
	respBody, err := module.HandleCall(client, rpcMsg.GetMethod(), rpcMsg.GetBody())
	if err != nil {
		return marshalResponse(rpcMsg.GetSequence(), Status_FAILURE, nil)
	}

	return marshalResponse(rpcMsg.GetSequence(), Status_SUCCESS, respBody)
}
