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

package server

import (
	"fmt"

	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/peer"
	"google.golang.org/grpc/status"

	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/security"
)

func checkAccess(ctx context.Context, FullMethod string) error {
	component, err := componentFromContext(ctx)

	if err != nil {
		return err
	}

	hasAccess := component.HasAccess(FullMethod)

	if !hasAccess {
		errMsg := fmt.Sprintf("%s does not have permission to call %s", component, FullMethod)
		return status.Error(codes.PermissionDenied, errMsg)
	}

	return nil
}

func componentFromContext(ctx context.Context) (comp *security.Component, err error) {
	clientPeer, ok := peer.FromContext(ctx)
	if !ok {
		return nil, status.Error(codes.Unauthenticated, "no peer information found")
	}

	authInfo, ok := clientPeer.AuthInfo.(credentials.TLSInfo)
	if !ok {
		return nil, status.Error(codes.Unauthenticated, "unable to obtain TLS info where it should be available")
	}

	certs := authInfo.State.VerifiedChains
	if len(certs) == 0 || len(certs[0]) == 0 {
		//This should never happen since we require it on the TLS handshake and don't allow skipping.
		return nil, status.Error(codes.Unauthenticated, "unable to verify client certificates")
	}

	peerCert := certs[0][0]

	component := security.CommonNameToComponent(peerCert.Subject.CommonName)

	return &component, nil
}

func unaryAccessInterceptor(ctx context.Context, req interface{}, info *grpc.UnaryServerInfo, handler grpc.UnaryHandler) (interface{}, error) {

	err := checkAccess(ctx, info.FullMethod)

	if err != nil {
		return nil, err
	}

	return handler(ctx, req)
}

func streamAccessInterceptor(srv interface{}, ss grpc.ServerStream, info *grpc.StreamServerInfo, handler grpc.StreamHandler) error {
	ctx := ss.Context()
	err := checkAccess(ctx, info.FullMethod)

	if err != nil {
		return err
	}

	return handler(srv, ss)
}

func unaryInterceptorForTransportConfig(cfg *security.TransportConfig) (grpc.UnaryServerInterceptor, error) {
	if cfg == nil {
		return nil, errors.New("nil TransportConfig")
	}

	if cfg.AllowInsecure {
		return nil, nil
	}

	return unaryAccessInterceptor, nil
}

func streamInterceptorForTransportConfig(cfg *security.TransportConfig) (grpc.StreamServerInterceptor, error) {
	if cfg == nil {
		return nil, errors.New("nil TransportConfig")
	}

	if cfg.AllowInsecure {
		return nil, nil
	}

	return streamAccessInterceptor, nil
}

func unaryErrorInterceptor(ctx context.Context, req interface{}, info *grpc.UnaryServerInfo, handler grpc.UnaryHandler) (interface{}, error) {
	res, err := handler(ctx, req)
	return res, proto.AnnotateError(err)
}

func streamErrorInterceptor(srv interface{}, ss grpc.ServerStream, info *grpc.StreamServerInfo, handler grpc.StreamHandler) error {
	err := handler(srv, ss)
	return proto.AnnotateError(err)
}

type statusGetter interface {
	GetStatus() int32
}

// dErrFromStatus converts a numeric DAOS return status code
// into an error.
func dErrFromStatus(sg statusGetter) error {
	dStatus := sg.GetStatus()
	if dStatus == 0 {
		return nil
	}

	return drpc.DaosStatus(dStatus)
}

func unaryStatusInterceptor(ctx context.Context, req interface{}, info *grpc.UnaryServerInfo, handler grpc.UnaryHandler) (interface{}, error) {
	res, err := handler(ctx, req)
	if err != nil {
		return res, err
	}

	if sg, ok := res.(statusGetter); ok {
		return res, dErrFromStatus(sg)
	}

	return res, err
}
