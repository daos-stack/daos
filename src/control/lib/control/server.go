//
// (C) Copyright 2021-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"strings"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/engine"
)

// SetEngineLogMasksReq contains the inputs for the set engine log level request.
type SetEngineLogMasksReq struct {
	unaryRequest
	Masks      *string `json:"masks"`
	Streams    *string `json:"streams"`
	Subsystems *string `json:"subsystems"`
}

// SetEngineLogMasksResp contains the results of a set engine log level request.
type SetEngineLogMasksResp struct {
	HostErrorsResp
	HostStorage HostStorageMap
}

// addHostResponse is responsible for validating the given HostResponse and adding it to the
// SetEngineLogMaskResp. HostStorageSet will always be empty so the map will only ever have one
// key for this response type.
func (resp *SetEngineLogMasksResp) addHostResponse(hr *HostResponse) error {
	pbResp, ok := hr.Message.(*ctlpb.SetLogMasksResp)
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	hasErr := false
	hostErrStrs := pbResp.GetErrors()

	for _, strErr := range hostErrStrs {
		if strErr != "" {
			hasErr = true
			break
		}
	}

	if hasErr {
		msgEngines := make([]string, len(hostErrStrs))
		for i, se := range hostErrStrs {
			if se == "" {
				se = "updated"
			}
			msgEngines[i] = fmt.Sprintf("engine-%d: %s", i, se)
		}
		errEngines := errors.New(strings.Join(msgEngines, ", "))

		if err := resp.addHostError(hr.Addr, errEngines); err != nil {
			return errors.Wrap(err, "adding host error to response")
		}

		return nil
	}

	if resp.HostStorage == nil {
		resp.HostStorage = make(HostStorageMap)
	}
	if err := resp.HostStorage.Add(hr.Addr, new(HostStorage)); err != nil {
		return err
	}

	return nil
}

// Set reset flags if parameters have not been supplied in the input request and dereference values
// if they have after validating.
func setLogMasksReqToPB(req *SetEngineLogMasksReq) (*ctlpb.SetLogMasksReq, error) {
	pbReq := new(ctlpb.SetLogMasksReq)

	if req.Masks == nil {
		pbReq.ResetMasks = true
	} else {
		if err := engine.ValidateLogMasks(*req.Masks); err != nil {
			return nil, err
		}
		pbReq.Masks = *req.Masks
	}

	if req.Streams == nil {
		pbReq.ResetStreams = true
	} else {
		if err := engine.ValidateLogStreams(*req.Streams); err != nil {
			return nil, err
		}
		pbReq.Streams = *req.Streams
	}

	if req.Subsystems == nil {
		pbReq.ResetSubsystems = true
	} else {
		if err := engine.ValidateLogSubsystems(*req.Subsystems); err != nil {
			return nil, err
		}
		pbReq.Subsystems = *req.Subsystems
	}

	return pbReq, nil
}

// SetEngineLogMasks will send RPC to hostlist to request changes to log level of all DAOS engines
// on each host in list.
func SetEngineLogMasks(ctx context.Context, rpcClient UnaryInvoker, req *SetEngineLogMasksReq) (*SetEngineLogMasksResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	pbReq, err := setLogMasksReqToPB(req)
	if err != nil {
		return nil, err
	}

	pbReq.Sys = req.getSystem(rpcClient)
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).SetEngineLogMasks(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS set engine log masks request: %+v", pbReq)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		rpcClient.Debugf("failed to invoke set engine log masks RPC: %s", err)
		return nil, err
	}

	resp := new(SetEngineLogMasksResp)
	for _, hr := range ur.Responses {
		if hr.Error != nil {
			if err := resp.addHostError(hr.Addr, hr.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := resp.addHostResponse(hr); err != nil {
			return nil, err
		}
	}

	rpcClient.Debugf("DAOS set engine log masks response: %+v", resp)
	return resp, nil
}
