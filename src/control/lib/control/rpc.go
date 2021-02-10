//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"math/rand"
	"sync"
	"time"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// Start with this value... If anything takes longer than this,
	// it almost certainly needs to be fixed.
	defaultRequestTimeout = 5 * time.Minute

	baseMSBackoff      = 250 * time.Millisecond
	maxMSBackoffFactor = 7 // 8s
	maxMSCandidates    = 5
)

var (
	msCandidateRandSource = rand.NewSource(time.Now().UnixNano())
)

type (
	// unaryRPC defines the function signature for a closure that invokes
	// a gRPC method and returns a protobuf response or error.
	unaryRPC func(context.Context, *grpc.ClientConn) (proto.Message, error)

	// unaryRPCGetter defines the interface to be implemented by
	// requests that can invoke a gRPC method.
	unaryRPCGetter interface {
		getRPC() unaryRPC
	}

	// UnaryInvoker defines an interface to be implemented by clients
	// capable of invoking a unary RPC (1 response for 1 request).
	UnaryInvoker interface {
		debugLogger
		InvokeUnaryRPC(ctx context.Context, req UnaryRequest) (*UnaryResponse, error)
		InvokeUnaryRPCAsync(ctx context.Context, req UnaryRequest) (HostResponseChan, error)
	}

	// Invoker defines an interface to be implemented by clients
	// capable of invoking unary or stream RPCs.
	Invoker interface {
		UnaryInvoker
		//TODO: StreamInvoker
		SetConfig(*Config)
	}
)

// unaryRequest is an embeddable struct to be used by requests which
// implement the UnaryRequest interface.
type unaryRequest struct {
	request
	rpc unaryRPC
}

// getRPC returns the request's RPC closure.
func (r *unaryRequest) getRPC() unaryRPC {
	return r.rpc
}

// setRPC sets the requests's RPC closure.
func (r *unaryRequest) setRPC(rpc unaryRPC) {
	r.rpc = rpc
}

type (
	// Client implements the Invoker interface and should be provided to
	// API methods to invoke RPCs.
	Client struct {
		config *Config
		log    debugLogger
	}

	// ClientOption defines the signature for functional Client options.
	ClientOption func(c *Client)
)

// WithClientLogger sets the client's debugLogger.
func WithClientLogger(log debugLogger) ClientOption {
	return func(c *Client) {
		c.log = log
	}
}

// WithConfig sets the client's configuration.
func WithConfig(cfg *Config) ClientOption {
	return func(c *Client) {
		c.config = cfg
	}
}

// NewClient returns an initialized Client with its
// parameters set by the provided ClientOption list.
func NewClient(opts ...ClientOption) *Client {
	c := &Client{
		config: DefaultConfig(),
	}

	for _, opt := range opts {
		opt(c)
	}

	if c.log == nil {
		WithClientLogger(defaultLogger)(c)
	}

	return c
}

// DefaultClient returns an initialized Client with its
// parameters set to default values.
func DefaultClient() *Client {
	return NewClient(
		WithConfig(DefaultConfig()),
		WithClientLogger(defaultLogger),
	)
}

// SetConfig sets the client configuration for an
// existing Client.
func (c *Client) SetConfig(cfg *Config) {
	c.config = cfg
}

func (c *Client) Debug(msg string) {
	c.log.Debug(msg)
}

func (c *Client) Debugf(fmtStr string, args ...interface{}) {
	c.log.Debugf(fmtStr, args...)
}

// dialOptions is a helper method to return a set of gRPC
// client dialer options.
func (c *Client) dialOptions() ([]grpc.DialOption, error) {
	opts := []grpc.DialOption{
		streamErrorInterceptor(),
		unaryErrorInterceptor(),
		grpc.FailOnNonTempDialError(true),
	}

	creds, err := security.DialOptionForTransportConfig(c.config.TransportConfig)
	if err != nil {
		return nil, err
	}
	opts = append(opts, creds)

	return opts, nil
}

// setDeadlineIfUnset sets a deadline on the context unless there is already
// one set. If the request does not define a specific deadline, then the
// default timeout is used.
func setDeadlineIfUnset(parent context.Context, req UnaryRequest) (context.Context, context.CancelFunc) {
	if _, hasDeadline := parent.Deadline(); hasDeadline {
		return parent, func() {}
	}

	rd := req.getDeadline()
	if rd.IsZero() {
		rd = time.Now().Add(defaultRequestTimeout)
	}
	return context.WithDeadline(parent, rd)
}

// InvokeUnaryRPCAsync performs an asynchronous invocation of the given RPC
// across all hosts in the request's host list. The returned HostResponseChan
// provides access to a stream of HostResponse items as they are received, and
// is closed when no more responses are expected.
func (c *Client) InvokeUnaryRPCAsync(parent context.Context, req UnaryRequest) (HostResponseChan, error) {
	hosts, err := getRequestHosts(c.config, req)
	if err != nil {
		return nil, err
	}

	c.Debugf("request hosts: %v", hosts)

	// TODO: Explore strategies for rate-limiting or batching as necessary
	// in order to perform adequately at scale.
	respChan := make(HostResponseChan, len(hosts))
	go func() {
		// Set a deadline for all requests to fan out/in.
		ctx, cancel := setDeadlineIfUnset(parent, req)
		defer cancel()

		var wg sync.WaitGroup
		for _, host := range hosts {
			wg.Add(1)
			go func(hostAddr string) {
				var msg proto.Message
				opts, err := c.dialOptions()
				if err == nil {
					var conn *grpc.ClientConn
					conn, err = grpc.DialContext(ctx, hostAddr, opts...)
					if err == nil {
						msg, err = req.getRPC()(ctx, conn)
						conn.Close()
					}
				}

				select {
				case <-parent.Done():
					c.Debug("parent context canceled -- tearing down client invoker")
				case respChan <- &HostResponse{Addr: hostAddr, Error: err, Message: msg}:
				}
				wg.Done()
			}(host)
		}
		wg.Wait()
		close(respChan)
	}()

	return respChan, nil
}

// invokeUnaryRPC is the actual implementation which is called by the
// real Client as well as the MockInvoker. This allows us to ensure that
// the retry logic here gets adequate test coverage.
func invokeUnaryRPC(parentCtx context.Context, log debugLogger, c UnaryInvoker, req UnaryRequest, defaultHosts []string) (*UnaryResponse, error) {
	gatherResponses := func(ctx context.Context, respChan chan *HostResponse, ur *UnaryResponse) error {
		for {
			select {
			case <-ctx.Done():
				return ctx.Err()
			case hr := <-respChan:
				if hr == nil {
					return nil
				}
				ur.Responses = append(ur.Responses, hr)
			}
		}
	}

	// Set a deadline for the request across all retries.
	reqCtx, cancel := setDeadlineIfUnset(parentCtx, req)
	defer cancel()

	// For non-MS requests, just keep things simple. Fan-out, fan-in,
	// no retries possible.
	if !req.isMSRequest() {
		respChan, err := c.InvokeUnaryRPCAsync(reqCtx, req)
		if err != nil {
			return nil, err
		}

		ur := new(UnaryResponse)
		if err := gatherResponses(reqCtx, respChan, ur); err != nil {
			return nil, err
		}
		return ur, nil
	}

	if len(req.getHostList()) == 0 {
		// For a MS request with no specific hostlist, choose a random subset
		// of the default hostlist, with the idea that at least one of them
		// will be up and running enough to return ErrNotReplica in order to
		// learn the actual list of MS replicas. We may also get lucky and
		// send the request to a server that can handle the request directly.
		rnd := rand.New(msCandidateRandSource)
		msCandidates := hostlist.MustCreateSet("")

		numCandidates := maxMSCandidates
		if len(defaultHosts) < numCandidates {
			numCandidates = len(defaultHosts)
		}

		for msCandidates.Count() < numCandidates {
			if _, err := msCandidates.Insert(defaultHosts[rnd.Intn(len(defaultHosts))]); err != nil {
				return nil, errors.Wrap(err, "failed to build MS candidates set")
			}
		}
		req.SetHostList(msCandidates.Slice())
		if len(req.getHostList()) == 0 {
			return nil, errors.New("unable to select MS candidates")
		}
	}

	isHardFailure := func(err error, reqCtx context.Context) bool {
		if err == nil {
			return false
		}

		// If the error is something other than a context error,
		// then it's considered a hard failure and not retryable.
		code := status.Code(errors.Cause(err))
		if code != codes.Canceled && code != codes.DeadlineExceeded {
			return true
		}

		// If the context error is from the overall request context,
		// then it's a hard failure. Otherwise, it's a soft failure
		// and can be retried.
		return errors.Cause(err) == reqCtx.Err()
	}

	// MS requests are a little more complicated. The general idea here is that
	// we want to discover the current MS leader for requests that must be
	// handled by the leader; otherwise we want to find at least one MS replica
	// to service the request. In this case we may get multiple responses, and
	// we just return the first successful response as we assume that every
	// replica is returning the same answer.
	var try uint = 0
	for {
		tryCtx := reqCtx
		if tryTimeout := req.getRetryTimeout(); tryTimeout > 0 {
			var tryCancel context.CancelFunc
			tryCtx, tryCancel = context.WithTimeout(reqCtx, tryTimeout)
			defer tryCancel()
		}
		respChan, err := c.InvokeUnaryRPCAsync(tryCtx, req)
		if isHardFailure(err, reqCtx) {
			return nil, err
		}

		ur := &UnaryResponse{fromMS: true}
		err = gatherResponses(tryCtx, respChan, ur)
		if isHardFailure(err, reqCtx) {
			return nil, err
		}

		_, err = ur.getMSResponse()
		switch e := err.(type) {
		case *system.ErrNotLeader:
			// If we sent the request to a non-leader MS replica,
			// then the error should give us a hint for where to
			// send the retry. In the event that the hint was
			// empty (as can happen during an election), just send
			// the retry to all of the replicas.
			if e.LeaderHint == "" {
				if len(e.Replicas) > 0 {
					req.SetHostList(e.Replicas)
				}
				break
			}
			req.SetHostList([]string{e.LeaderHint})
		case *system.ErrNotReplica:
			// If we went the request to a non-replica host, then
			// the error should give us the list of replicas to try.
			// One of them should be the current leader and will
			// service the request.
			if len(e.Replicas) > 0 {
				req.SetHostList(e.Replicas)
			}
		default:
			// If the request defines its own retry logic for the error, run
			// that logic and break out early.
			if req.canRetry(err, try) {
				if err := req.onRetry(tryCtx, try); err != nil {
					return ur, nil
				}
				break
			}

			// One special case here for system startup. If the
			// request was sent to a MS replica but the DB wasn't
			// started yet, it's always valid to retry.
			if system.IsUnavailable(err) {
				break
			}

			// Otherwise, we're finished trying.
			return ur, nil
		}

		backoff := common.ExpBackoff(req.retryAfter(baseMSBackoff), uint64(try), maxMSBackoffFactor)
		log.Debugf("MS request error: %v; retrying after %s", err, backoff)
		select {
		case <-reqCtx.Done():
			return nil, reqCtx.Err()
		case <-time.After(backoff):
		}
		try++
	}
}

// InvokeUnaryRPC performs a synchronous (blocking) invocation of the request's
// RPC across all hosts in the request. The response contains a slice of HostResponse
// items which represent the success or failure of the RPC invocation for each host
// in the request.
func (c *Client) InvokeUnaryRPC(ctx context.Context, req UnaryRequest) (*UnaryResponse, error) {
	return invokeUnaryRPC(ctx, c.log, c, req, c.config.HostList)
}
