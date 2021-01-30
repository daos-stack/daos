//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package drpc

import (
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

// Module is an interface that a type must implement to provide the
// functionality needed by the ModuleService to process dRPC requests.
type Module interface {
	HandleCall(*Session, Method, []byte) ([]byte, error)
	ID() ModuleID
}

// ModuleService is the collection of Modules used by
// DomainSocketServer to be used to process messages.
type ModuleService struct {
	log     logging.Logger
	modules map[ModuleID]Module
}

// NewModuleService creates an initialized ModuleService instance
func NewModuleService(log logging.Logger) *ModuleService {
	modules := make(map[ModuleID]Module)
	return &ModuleService{
		log:     log,
		modules: modules,
	}
}

// RegisterModule will take in a type that implements the Module interface
// and ensure that no other module is already registered with that module
// identifier.
func (r *ModuleService) RegisterModule(mod Module) {
	_, found := r.GetModule(mod.ID())
	if found {
		// Not really an error that can be handled. It's a programming
		// error that should manifest very quickly in test.
		panic(errors.Errorf("module with ID %d already exists", mod.ID()))
	}
	r.modules[mod.ID()] = mod
}

// GetModule fetches the module for the given ID. Returns true if found, false
// otherwise.
func (r *ModuleService) GetModule(id ModuleID) (Module, bool) {
	mod, found := r.modules[id]
	return mod, found
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
		return nil, errors.Wrap(mErr, "Failed to marshal response")
	}
	return responseBytes, nil
}

// ProcessMessage is the main entry point into the ModuleService. It accepts a
// marshaled drpc.Call instance, processes it, calls the handler in the
// appropriate Module, and marshals the result into the body of a drpc.Response.
func (r *ModuleService) ProcessMessage(session *Session, msgBytes []byte) ([]byte, error) {
	msg := &Call{}

	err := proto.Unmarshal(msgBytes, msg)
	if err != nil {
		return marshalResponse(-1, Status_FAILED_UNMARSHAL_CALL, nil)
	}
	module, ok := r.GetModule(ModuleID(msg.GetModule()))
	if !ok {
		r.log.Errorf("Attempted to call unregistered module %d", msg.GetModule())
		return marshalResponse(msg.GetSequence(), Status_UNKNOWN_MODULE, nil)
	}
	var method Method
	method, err = module.ID().GetMethod(msg.GetMethod())
	if err != nil {
		return marshalResponse(msg.GetSequence(), Status_UNKNOWN_METHOD, nil)
	}
	respBody, err := module.HandleCall(session, method, msg.GetBody())
	if err != nil {
		r.log.Errorf("HandleCall for %s:%s failed: %s\n", module.ID().String(), method.String(), err)
		return marshalResponse(msg.GetSequence(), ErrorToStatus(err), nil)
	}

	return marshalResponse(msg.GetSequence(), Status_SUCCESS, respBody)
}
