//
// (C) Copyright 2020-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
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
		getTimeout() time.Duration
	}

	hostResponseReporter interface {
		reportResponse(*HostResponse)
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

var (
	errNoRetryHandler = errors.New("request has not set a retry handler")
)

// request is an embeddable struct to provide basic functionality
// common to all request types.
type request struct {
	timeout      time.Duration
	deadline     time.Time
	agentRequest bool
	agentCompat  bool
	Sys          string // DAOS system name
	HostList     []string
}

// SetSystem sets the request's system name.
func (r *request) SetSystem(name string) {
	r.Sys = name
}

type agentRequest interface {
	getAgentRequest() bool
	setAgentCompat()
	getAgentCompat() bool
}

// SetAgentRequest sets a flag to indicate that the request
// is being made by an agent.
func (r *request) SetAgentRequest() {
	r.agentRequest = true
}

func (r *request) setAgentCompat() {
	r.agentCompat = true
}

func (r *request) getAgentCompat() bool {
	return r.agentCompat
}

func (r *request) getAgentRequest() bool {
	return r.agentRequest
}

func isAgentRequest(req targetChooser) bool {
	ar, ok := req.(agentRequest)
	if !ok {
		return false
	}
	return ar.getAgentRequest()
}

func setAgentCompat(req targetChooser) {
	ar, ok := req.(agentRequest)
	if !ok {
		return
	}
	ar.setAgentCompat()
}

func getAgentCompat(req targetChooser) bool {
	ar, ok := req.(agentRequest)
	if !ok {
		return false
	}
	return ar.getAgentCompat()
}

// getSystem returns the system name set on the request or that returned by the
// supplied sysGetter implementation or the build defined default name.
//
// NB: As of DAOS 2.2.0, the version is appended
// to the system name in order to validate component
// interoperability.
func (r *request) getSystem(getter sysGetter) string {
	sysName := build.DefaultSystemName
	switch {
	case r.Sys != "":
		sysName = r.Sys
	case getter.GetSystem() != "":
		sysName = getter.GetSystem()
	}
	// For the 2.2 release, we need to support 2.2 agents talking
	// to 2.0 servers, which don't understand the versioned system name.
	if r.agentRequest && r.agentCompat {
		return sysName
	}
	return sysName + "-" + build.DaosVersion
}

// getHostList returns the hostlist set for the request, which
// may override the configured hostlist.
func (r *request) getHostList() []string {
	return r.HostList
}

// SetHostList sets the request's hostlist to a copy of the
// supplied hostlist, and will override the configured hostlist.
func (r *request) SetHostList(hl []string) {
	if len(hl) == 0 {
		return
	}

	r.HostList = make([]string, len(hl))
	copy(r.HostList, hl)
}

// AddHost appends the given host to the request's hostlist,
// which may override the configured hostlist.
func (r *request) AddHost(hostAddr string) {
	r.HostList = append(r.HostList, hostAddr)
}

// SetTimeout sets a deadline by which the request must have
// completed. It is calculated from the supplied time.Duration.
func (r *request) SetTimeout(timeout time.Duration) {
	r.timeout = timeout
	r.deadline = time.Now().Add(timeout)
}

// getDeadline retrieves the deadline set for the request, if any.
// Callers should check the returned time.Time for the zero value.
func (r *request) getDeadline() time.Time {
	return r.deadline
}

// getTimeout returns the timeout set for the request, if any.
// The primary use case for this method is to provide information
// about the timeout in the event that the request's deadline
// is exceeded.
func (r *request) getTimeout() time.Duration {
	return r.timeout
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
	return errNoRetryHandler
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

func (r *retryableRequest) setRetryTimeout(timeout time.Duration) {
	r.retryTimeout = timeout
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

	return errNoRetryHandler
}
