//
// (C) Copyright 2020 Intel Corporation.
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

package control

import (
	"encoding/json"
	"sort"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
)

type (
	// HostResponse contains a single host's response to an unary RPC, or
	// an error if the host was unable to respond successfully.
	HostResponse struct {
		Addr    string
		Error   error
		Message proto.Message
	}

	// HostResponseChan defines a channel of *HostResponse items returned
	// from asynchronous unary RPC invokers.
	HostResponseChan chan *HostResponse

	// hostErrorsGetter define an interface for responses which return
	// a HostErrorsMap.
	hostErrorsGetter interface {
		getHostErrors() HostErrorsMap
	}
)

type HostErrorsResp struct {
	HostErrors HostErrorsMap
}

func (her *HostErrorsResp) addHostError(hostAddr string, hostErr error) error {
	if her.HostErrors == nil {
		her.HostErrors = make(HostErrorsMap)
	}
	return her.HostErrors.Add(hostAddr, hostErr)
}

func (her *HostErrorsResp) getHostErrors() HostErrorsMap {
	return her.HostErrors
}

func (her *HostErrorsResp) Errors() error {
	if len(her.HostErrors) > 0 {
		return errors.Errorf("%d hosts had errors", len(her.HostErrors))
	}
	return nil
}

// HostErrorsMap provides a mapping from error strings to a set of
// hosts to which the error applies.
type HostErrorsMap map[string]*hostlist.HostSet

// MarshalJSON implements a custom marshaller to include
// the hostset as a ranged string.
func (hem HostErrorsMap) MarshalJSON() ([]byte, error) {
	out := make(map[string]string)
	for k, v := range hem {
		out[k] = v.RangedString()
	}
	return json.Marshal(out)
}

// Add creates or updates the err/addr keyval pair.
func (hem HostErrorsMap) Add(hostAddr string, hostErr error) (err error) {
	if hostErr == nil {
		return nil
	}

	errStr := hostErr.Error() // stringify the error as map key
	if _, exists := hem[errStr]; !exists {
		hem[errStr], err = hostlist.CreateSet(hostAddr)
		return
	}
	_, err = hem[errStr].Insert(hostAddr)
	return
}

// Keys returns a stable sorted slice of the errors map keys.
func (hem HostErrorsMap) Keys() []string {
	setToKeys := make(map[string]map[string]struct{})
	for errStr, set := range hem {
		rs := set.RangedString()
		if _, exists := setToKeys[rs]; !exists {
			setToKeys[rs] = make(map[string]struct{})
		}
		setToKeys[rs][errStr] = struct{}{}
	}

	sets := make([]string, 0, len(hem))
	for set := range setToKeys {
		sets = append(sets, set)
	}
	sort.Strings(sets)

	keys := make([]string, 0, len(hem))
	for _, set := range sets {
		setKeys := make([]string, 0, len(setToKeys[set]))
		for key := range setToKeys[set] {
			setKeys = append(setKeys, key)
		}
		sort.Strings(setKeys)
		keys = append(keys, setKeys...)
	}
	return keys
}

// UnaryResponse contains a slice of *HostResponse items returned
// from synchronous unary RPC invokers.
type UnaryResponse struct {
	Responses []*HostResponse
	fromMS    bool
}

// getMSResponse is a helper method to return the MS response
// message from a UnaryResponse.
func (ur *UnaryResponse) getMSResponse() (proto.Message, error) {
	if ur == nil {
		return nil, errors.Errorf("nil %T", ur)
	}

	if !ur.fromMS {
		return nil, errors.New("response did not come from management service")
	}

	if len(ur.Responses) == 0 {
		return nil, errors.New("response did not contain a management service response")
	}

	msResp := ur.Responses[0]
	if msResp.Error != nil {
		return nil, msResp.Error
	}

	if msResp.Message == nil {
		return nil, errors.New("management service response message was nil")
	}

	return msResp.Message, nil
}

// convertMSResponse is a helper function to extract the MS response
// message from a generic UnaryResponse. The out parameter must be
// a reference to a compatible concrete type (e.g. PoolQueryResp).
func convertMSResponse(ur *UnaryResponse, out interface{}) error {
	msResp, err := ur.getMSResponse()
	if err != nil {
		return errors.Wrap(err, "failed to get MS response")
	}

	return convert.Types(msResp, out)
}

// ctlStateToErr is a helper function for turning an
// unsuccessful control response state into an error.
func ctlStateToErr(state *ctlpb.ResponseState) error {
	if state.GetStatus() == ctlpb.ResponseStatus_CTL_SUCCESS {
		return nil
	}

	if errMsg := state.GetError(); errMsg != "" {
		return errors.New(errMsg)
	}
	if infoMsg := state.GetInfo(); infoMsg != "" {
		return errors.Errorf("%s: %s", state.GetStatus(), infoMsg)
	}
	return errors.Errorf("%s: unknown error", state.GetStatus())
}
