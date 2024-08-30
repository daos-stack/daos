//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"strings"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/grpc/status"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/security"
)

// connErrToFault attempts to resolve a network connection
// error to a more informative Fault with resolution.
func connErrToFault(st *status.Status, target string) error {
	// Bleh. Can't find a better way to make these work.
	switch {
	case strings.Contains(st.Message(), "connection refused"):
		return FaultConnectionRefused(target)
	case strings.Contains(st.Message(), "connection closed"),
		strings.Contains(st.Message(), "transport is closing"),
		strings.Contains(st.Message(), "closed the connection"):
		return FaultConnectionClosed(target)
	case strings.Contains(st.Message(), "no route to host"):
		return FaultConnectionNoRoute(target)
	case strings.Contains(st.Message(), "no such host"):
		return FaultConnectionBadHost(target)
	case strings.Contains(st.Message(), "certificate has expired"):
		return security.FaultInvalidCert(errors.New(st.Message()))
	case strings.Contains(st.Message(), "i/o timeout"):
		return FaultConnectionTimedOut(target)
	default:
		return st.Err()
	}
}

// streamErrorInterceptor calls the specified streaming RPC and returns any unwrapped errors.
func streamErrorInterceptor() grpc.DialOption {
	return grpc.WithStreamInterceptor(func(ctx context.Context, desc *grpc.StreamDesc, cc *grpc.ClientConn, method string, streamer grpc.Streamer, opts ...grpc.CallOption) (grpc.ClientStream, error) {
		cs, err := streamer(ctx, desc, cc, method, opts...)
		if err != nil {
			st := status.Convert(err)
			err = proto.UnwrapError(st)
			if err.Error() != st.Err().Error() {
				return cs, err
			}
			return cs, connErrToFault(st, cc.Target())
		}
		return cs, nil
	})
}

// unaryErrorInterceptor calls the specified unary RPC and returns any unwrapped errors.
func unaryErrorInterceptor() grpc.UnaryClientInterceptor {
	return func(ctx context.Context, method string, req, reply interface{}, cc *grpc.ClientConn, invoker grpc.UnaryInvoker, opts ...grpc.CallOption) error {
		err := invoker(ctx, method, req, reply, cc, opts...)
		if err != nil {
			st := status.Convert(err)
			err = proto.UnwrapError(st)
			if err.Error() != st.Err().Error() {
				return err
			}
			return connErrToFault(st, cc.Target())
		}
		return nil
	}
}

// unaryVersionedComponentInterceptor appends the component name and version to the
// outgoing request headers.
func unaryVersionedComponentInterceptor(comp build.Component) grpc.UnaryClientInterceptor {
	return func(parent context.Context, method string, req, reply interface{}, cc *grpc.ClientConn, invoker grpc.UnaryInvoker, opts ...grpc.CallOption) error {
		// NB: The caller should specify its component, but as a fallback, we
		// can make a decent guess about the calling component based on the method.
		if comp == build.ComponentAny {
			var err error
			if comp, err = security.MethodToComponent(method); err != nil {
				return errors.Wrap(err, "unable to determine component from method")
			}
		}
		ctx, err := build.ToContext(parent, comp, build.DaosVersion)
		if err != nil {
			// Don't fail if a component version was already set somewhere else.
			// Any other error is fatal.
			if err != build.ErrCtxMetadataExists {
				return err
			}
			ctx = parent
		}
		return invoker(ctx, method, req, reply, cc, opts...)
	}
}
