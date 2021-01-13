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
	"context"
	"time"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/pkg/errors"
)

type (
	// hostListGetter defines an interface to be implemented
	// by requests that can provide the hostList set by the
	// caller.
	hostListGetter interface {
		SetHostList([]string)
		getHostList() []string
	}

	// targetChooser defines an interface to be implemented by
	// requests that can choose the target of the request (e.g.
	// sending Pool requests to the Management Server Replica(s)).
	targetChooser interface {
		hostListGetter
		isMSRequest() bool
	}

	// retryer defines an interface to be implemented by
	// requests that may be retryable.
	retryer interface {
		// canRetry should be called before calling onRetry, in order to
		// evaluate whether or not the error is retryable. If no test
		// logic is defined, the assumption is that the error is retryable
		// because the request embedded this type.
		canRetry(error, uint) bool
		// onRetry is intended to be called on every retry iteration. It can be
		// used to limit the number of retries and/or execute some custom retry
		// logic on each iteration.
		onRetry(context.Context, uint) error
		// retryAfter returns a duration to specify how long the caller should
		// wait before trying the request again. It accepts a default duration
		// which is returned if the request does not specify its own retry interval.
		retryAfter(time.Duration) time.Duration
		// getRetryTimeout returns a duration to specify how long each retry attempt
		// may take. This should be shorter than any overall per-request Deadline.
		getRetryTimeout() time.Duration
	}

	// deadliner defines an interface to be implemented by
	// requests that can impose a deadline on the request
	// completion.
	deadliner interface {
		SetTimeout(time.Duration)
		getDeadline() time.Time
	}

	// UnaryRequest defines an interface to be implemented by
	// unary request types (1 response to 1 request).
	UnaryRequest interface {
		targetChooser
		deadliner
		retryer
		unaryRPCGetter
	}
)

// request is an embeddable struct to provide basic functionality
// common to all request types.
type request struct {
	deadline time.Time
	Sys      string // DAOS system name
	HostList []string
}

// SetSystem sets the request's system name.
func (r *request) SetSystem(name string) {
	r.Sys = name
}

// getSystem returns the system name set for the request, or
// the default name.
func (r *request) getSystem() string {
	if r.Sys == "" {
		return build.DefaultSystemName
	}
	return r.Sys
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

// SetTimeout sets a deadline by which the request must have
// completed. It is calculated from the supplied time.Duration.
func (r *request) SetTimeout(timeout time.Duration) {
	r.deadline = time.Now().Add(timeout)
}

// getDeadline retrieves the deadline set for the request, if any.
// Callers should check the returned time.Time for the zero value.
func (r *request) getDeadline() time.Time {
	return r.deadline
}

// isMSRequest implements part of the targetChooser interface,
// and will always return false for a basic request.
func (r *request) isMSRequest() bool {
	return false
}

// canRetry implements the retryer interface and always
// returns false, indicating that the default request implementation
// is not retryable.
func (r *request) canRetry(_ error, _ uint) bool {
	return false
}

// onRetry implements the retryer interface and always returns
// an error. Callers should check the result of canRetry() before
// calling retry, in order to avoid wasting effort on a request
// that does not implement its own retry logic.
func (r *request) onRetry(_ context.Context, _ uint) error {
	return errors.New("request is not retryable")
}

// retryAfter implements the retrier interface and always returns
// the supplied retry interval.
func (r *request) retryAfter(interval time.Duration) time.Duration {
	return interval
}

// getRetryTimeout implements the retrier interface and always returns
// zero.
func (r *request) getRetryTimeout() time.Duration {
	return 0
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

// retryableRequest is the default implementation of the retryer interface.
type retryableRequest struct {
	// retryTimeout sets an optional timeout for each retry.
	retryTimeout time.Duration
	// retryInterval is an optional interval to override the default.
	retryInterval time.Duration
	// retryMaxTries is an optional max number of retry attempts.
	retryMaxTries uint
	// retryTestFn defines a function that can test an error for retryability;
	// if not set, the assumption is that the request is retryable because it
	// embeds this type.
	retryTestFn func(error, uint) bool
	// retryFun defines a function that will run on every retry iteration.
	retryFn func(context.Context, uint) error
}

func (r *retryableRequest) getRetryTimeout() time.Duration {
	return r.retryTimeout
}

func (r *retryableRequest) retryAfter(defInterval time.Duration) time.Duration {
	if r.retryInterval > 0 {
		return r.retryInterval
	}
	return defInterval
}

func (r *retryableRequest) canRetry(err error, cur uint) bool {
	// don't bother with anything else if there was no error
	if err == nil {
		return false
	}

	if r.retryTestFn != nil {
		return r.retryTestFn(err, cur)
	}
	return true
}

func (r *retryableRequest) onRetry(ctx context.Context, cur uint) error {
	if r.retryMaxTries > 0 && cur > r.retryMaxTries {
		return errors.Errorf("max retries exceeded (%d > %d)", cur, r.retryMaxTries)
	}

	if r.retryFn != nil {
		return r.retryFn(ctx, cur)
	}

	return nil
}
