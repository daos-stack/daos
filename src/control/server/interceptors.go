//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"strings"

	"github.com/pkg/errors"
	"golang.org/x/net/context"
	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/credentials"
	"google.golang.org/grpc/peer"
	"google.golang.org/grpc/status"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/security"
)

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
		// This should never happen since we require it on the TLS handshake and don't allow skipping.
		return nil, status.Error(codes.Unauthenticated, "unable to verify client certificates")
	}

	peerCert := certs[0][0]
	component := security.CommonNameToComponent(peerCert.Subject.CommonName)

	return &component, nil
}

func checkAccess(ctx context.Context, FullMethod string) error {
	component, err := componentFromContext(ctx)
	if err != nil {
		return err
	}

	if !component.HasAccess(FullMethod) {
		errMsg := fmt.Sprintf("%s does not have permission to call %s", component, FullMethod)
		return status.Error(codes.PermissionDenied, errMsg)
	}

	return nil
}

func unaryAccessInterceptor(ctx context.Context, req interface{}, info *grpc.UnaryServerInfo, handler grpc.UnaryHandler) (interface{}, error) {
	if err := checkAccess(ctx, info.FullMethod); err != nil {
		return nil, errors.Wrapf(err, "access denied for %T", req)
	}

	return handler(ctx, req)
}

func streamAccessInterceptor(srv interface{}, ss grpc.ServerStream, info *grpc.StreamServerInfo, handler grpc.StreamHandler) error {
	if err := checkAccess(ss.Context(), info.FullMethod); err != nil {
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

var selfServerComponent = func() *build.VersionedComponent {
	self, err := build.NewVersionedComponent("server", build.DaosVersion)
	if err != nil {
		return &build.VersionedComponent{
			Component: build.ComponentServer,
			Version:   build.MustNewVersion("0.0.0"),
		}
	}
	return self
}()

func checkVersion(ctx context.Context, self *build.VersionedComponent, req interface{}) error {
	// If we can't determine our own version, then there's no
	// checking to be done.
	if self.Version.IsZero() {
		return errors.New("unable to determine self server version")
	}

	// Default to the other side being a server, as the requirements
	// are most stringent for server/server communication. We have
	// to set a default because this security component lookup
	// will fail if certificates are disabled.
	buildComponent := build.ComponentServer
	secComponent, err := componentFromContext(ctx)
	if err == nil {
		buildComponent = build.Component(secComponent.String())
	}

	otherVersion := "0.0.0"
	if sReq, ok := req.(interface{ GetSys() string }); ok {
		comps := strings.Split(sReq.GetSys(), "-")
		if len(comps) > 1 {
			if ver, err := build.NewVersion(comps[len(comps)-1]); err == nil {
				otherVersion = ver.String()
			}
		}
	} else {
		// If the request message type does not implement GetSys(), then
		// there is no version to check. We leave message compatibility
		// to lower layers.
		return nil
	}

	other, err := build.NewVersionedComponent(buildComponent, otherVersion)
	if err != nil || other.Version.IsZero() {
		other = &build.VersionedComponent{
			Component: "unknown",
			Version:   build.MustNewVersion(otherVersion),
		}
		return FaultIncompatibleComponents(self, other)
	}

	if err := build.CheckCompatibility(self, other); err != nil {
		return FaultIncompatibleComponents(self, other)
	}

	return nil
}

func unaryVersionInterceptor(ctx context.Context, req interface{}, info *grpc.UnaryServerInfo, handler grpc.UnaryHandler) (interface{}, error) {
	if err := checkVersion(ctx, selfServerComponent, req); err != nil {
		return nil, errors.Wrapf(err, "version check failed for %T", req)
	}

	return handler(ctx, req)
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
