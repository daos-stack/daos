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
	"strings"

	"google.golang.org/grpc"
	"google.golang.org/grpc/status"

	"github.com/daos-stack/daos/src/control/common/proto"
)

// connErrToFault attempts to resolve a network connection
// error to a more informative Fault with resolution.
func connErrToFault(st *status.Status, target string) error {
	// Bleh. Can't find a better way to make these work.
	switch {
	case strings.Contains(st.Message(), "connection refused"):
		return FaultConnectionRefused(target)
	case strings.Contains(st.Message(), "connection closed"):
		return FaultConnectionClosed(target)
	case strings.Contains(st.Message(), "no route to host"):
		return FaultConnectionNoRoute(target)
	case strings.Contains(st.Message(), "no such host"):
		return FaultConnectionBadHost(target)
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
func unaryErrorInterceptor() grpc.DialOption {
	return grpc.WithUnaryInterceptor(func(ctx context.Context, method string, req, reply interface{}, cc *grpc.ClientConn, invoker grpc.UnaryInvoker, opts ...grpc.CallOption) error {
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
	})
}
