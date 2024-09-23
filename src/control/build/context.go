//
// (C) Copyright 2023-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package build

import (
	"context"

	"github.com/pkg/errors"
	"google.golang.org/grpc/metadata"
)

const (
	// DaosComponentHeader defines the header name used to convey the component name.
	DaosComponentHeader = "x-daos-component"
	// DaosVersionHeader defines the header name used to convey the component version.
	DaosVersionHeader = "x-daos-version"
)

// FromContext returns a versioned component obtained from the context.
func FromContext(ctx context.Context) (*VersionedComponent, error) {
	if ctx == nil {
		return nil, errors.New("nil context")
	}

	md, hasMD := metadata.FromIncomingContext(ctx)
	if !hasMD {
		return nil, ErrNoCtxMetadata
	}
	compName, hasName := md[DaosComponentHeader]
	if !hasName {
		return nil, ErrNoCtxMetadata
	}
	comp := Component(compName[0])
	compVersion, hasVersion := md[DaosVersionHeader]
	if !hasVersion {
		return nil, ErrNoCtxMetadata
	}

	return NewVersionedComponent(comp, compVersion[0])
}

// ToContext adds the component and version to the context.
func ToContext(parent context.Context, comp Component, verStr string) (context.Context, error) {
	if parent == nil {
		return nil, errors.New("nil context")
	}

	if comp == ComponentAny {
		return nil, errors.New("component cannot be ComponentAny")
	}

	if _, exists := metadata.FromOutgoingContext(parent); exists {
		return nil, ErrCtxMetadataExists
	}

	version, err := NewVersion(verStr)
	if err != nil {
		return nil, err
	}

	return metadata.AppendToOutgoingContext(parent,
		DaosComponentHeader, comp.String(),
		DaosVersionHeader, version.String(),
	), nil
}
