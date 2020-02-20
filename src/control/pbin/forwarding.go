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
package pbin

import (
	"context"
	"encoding/json"
	"os"
	"strconv"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	// Forwarder provides a common implementation of a request forwarder.
	Forwarder struct {
		Disabled bool

		log      logging.Logger
		pbinName string
	}

	// ForwardableRequest is intended to be embedded into
	// request types that can be forwarded to the privileged
	// binary.
	ForwardableRequest struct {
		Forwarded bool
	}

	// ForwardChecker defines an interface for any request that
	// could have been forwarded.
	ForwardChecker interface {
		IsForwarded() bool
	}
)

// IsForwarded implements the ForwardChecker interface.
func (r ForwardableRequest) IsForwarded() bool {
	return r.Forwarded
}

// NewForwarder returns a configured *Forwarder.
func NewForwarder(log logging.Logger, pbinName string) *Forwarder {
	fwd := &Forwarder{
		log:      log,
		pbinName: pbinName,
	}

	if val, set := os.LookupEnv(DisableReqFwdEnvVar); set {
		disabled, err := strconv.ParseBool(val)
		if err != nil {
			log.Errorf("%s was set to non-boolean value (%q); not disabling",
				DisableReqFwdEnvVar, val)
			return fwd
		}
		fwd.Disabled = disabled
	}

	return fwd
}

// GetBinaryName returns the name of the binary requests will be forwarded to.
func (f *Forwarder) GetBinaryName() string {
	return f.pbinName
}

// CanForward indicates whether commands can be forwarded to the forwarder's
// designated binary.
func (f *Forwarder) CanForward() bool {
	if _, err := common.FindBinary(f.GetBinaryName()); os.IsNotExist(err) {
		return false
	}

	return true
}

// SendReq is responsible for marshaling the forwarded request into a message
// that is sent to the privileged binary, then unmarshaling the response for
// the caller.
func (f *Forwarder) SendReq(method string, fwdReq interface{}, fwdRes interface{}) error {
	if fwdReq == nil {
		return errors.New("nil request")
	}
	if fwdRes == nil {
		return errors.New("nil response")
	}

	pbinPath, err := common.FindBinary(f.pbinName)
	if err != nil {
		return err
	}

	payload, err := json.Marshal(fwdReq)
	if err != nil {
		return errors.Wrap(err, "failed to marshal forwarded request as payload")
	}

	req := &Request{
		Method:  method,
		Payload: payload,
	}

	ctx := context.TODO()
	res, err := ExecReq(ctx, f.log, pbinPath, req)
	if err != nil {
		if fault.IsFault(err) {
			return err
		}
		return errors.Wrap(err, "privileged binary execution failed")
	}

	if err := json.Unmarshal(res.Payload, fwdRes); err != nil {
		return errors.Wrap(err, "failed to unmarshal forwarded response")
	}

	return nil
}
