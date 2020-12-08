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
	"sync"
	"time"

	"github.com/golang/protobuf/proto"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/security"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// Start with this value... If anything takes longer than this,
	// it almost certainly needs to be fixed.
	defaultRequestTimeout = 5 * time.Minute

	defaultMSRetry = 250 * time.Millisecond
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
// across all hosts in the provided host list. The returned HostResponseChan
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
func invokeUnaryRPC(parent context.Context, log debugLogger, c UnaryInvoker, req UnaryRequest) (*UnaryResponse, error) {
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
	ctx, cancel := setDeadlineIfUnset(parent, req)
	defer cancel()

	var tries uint = 0
	for {
		tries++
		respChan, err := c.InvokeUnaryRPCAsync(ctx, req)
		if err != nil {
			return nil, err
		}

		ur := &UnaryResponse{
			fromMS: req.isMSRequest(),
		}
		if err := gatherResponses(ctx, respChan, ur); err != nil {
			return nil, err
		}

		if !ur.fromMS {
			return ur, nil
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
				req.SetHostList(e.Replicas)
				break
			}
			req.SetHostList([]string{e.LeaderHint})
		case *system.ErrNotReplica:
			// If we went the request to a non-replica host, then
			// the error should give us the list of replicas to try.
			// One of them should be the current leader and will
			// service the request.
			req.SetHostList(e.Replicas)
		default:
			// If the request defines its own retry criteria, run
			// that logic and break out early.
			if req.canRetry(err, tries) {
				if err := req.onRetry(ctx, tries); err != nil {
					return ur, nil
				}
				break
			}

			// If the request succeeded, or there was only one host to try,
			// return now. Otherwise, remove the failed host from the list
			// and try again.
			if err == nil || len(req.getHostList()) < 2 {
				return ur, nil
			}
			log.Debugf("removing %s from request hosts due to %s", req.getHostList()[0], e)
			req.SetHostList(req.getHostList()[1:])
		}

		log.Debugf("MS request error: %v; retrying after %s", err, defaultMSRetry)
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case <-req.retryAfter(defaultMSRetry):
		}
	}
}

// InvokeUnaryRPC performs a synchronous (blocking) invocation of the request's
// RPC across all hosts in the request. The response contains a slice of HostResponse
// items which represent the success or failure of the RPC invocation for each host
// in the request.
func (c *Client) InvokeUnaryRPC(ctx context.Context, req UnaryRequest) (*UnaryResponse, error) {
	return invokeUnaryRPC(ctx, c.log, c, req)
}
