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

import "time"

type (
	// hostListGetter defines an interface to be implemented
	// by requests that can provide the hostList set by the
	// caller.
	hostListGetter interface {
		getHostList() []string
	}

	// targetChooser defines an interface to be implemented by
	// requests that can choose the target of the request (e.g.
	// sending Pool requests to the Management Server Replica(s)).
	targetChooser interface {
		hostListGetter
		isMSRequest() bool
	}

	// timeoutGetter defines an interface to be implemented by
	// requests that can set a timeout for the request invocation.
	timeoutGetter interface {
		getTimeout() time.Duration
	}

	// UnaryRequest defines an interface to be implemented by
	// unary request types (1 response to 1 request).
	UnaryRequest interface {
		targetChooser
		unaryRPCGetter
	}
)

// request is an embeddable struct to provide basic functionality
// common to all request types.
type request struct {
	HostList []string
}

// getHostList returns the hostlist set for the request, which
// may override the configured hostlist.
func (r *request) getHostList() []string {
	return r.HostList
}

// SetHostList sets the request's hostlist, which may override
// the configured hostlist.
func (r *request) SetHostList(hl []string) {
	r.HostList = hl
}

// AddHost appends the given host to the request's hostlist,
// which may override the configured hostlist.
func (r *request) AddHost(hostAddr string) {
	r.HostList = append(r.HostList, hostAddr)
}

// isMSRequest implements part of the targetChooser interface,
// and will always return false for a basic request.
func (r *request) isMSRequest() bool {
	return false
}

// msRequest is an embeddable struct to implement the targetChooser
// interface and will always return true. Should only be embedded
// in request types that are actually MS Requests (e.g. Pool requests).
type msRequest struct{}

// isMSRequest implements part of the targetChooser interface,
// and will always return true for a msRequest.
func (r *msRequest) isMSRequest() bool {
	return true
}

// timeoutRequest is an embeddable struct to implement the
// timeoutGetter interface. Embedding requests can use it
// to set an optional per-request timeout.
type timeoutRequest struct {
	RequestTimeout time.Duration
}

// getTimeout implements the timeoutGetter interface.
func (r *timeoutRequest) getTimeout() time.Duration {
	return r.RequestTimeout
}
