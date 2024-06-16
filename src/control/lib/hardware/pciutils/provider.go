//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pciutils

import (
	"context"

	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/pkg/errors"
)

type ctxKey string

const (
	AccessKey ctxKey = "pciutilsAccess"
)

type (
	accessProvider interface {
		PCIeCapsFromConfig(cfgBytes []byte, dev *hardware.PCIDevice) error
		Cleanup()
	}
)

func Init(parent context.Context) (context.Context, error) {
	return context.WithValue(parent, AccessKey, &api{}), nil
}

func accessFromContext(ctx context.Context) (accessProvider, error) {
	ap, ok := ctx.Value(AccessKey).(accessProvider)
	if !ok {
		return nil, errors.New("pciutils access not initialized")
	}

	return ap, nil
}

func Fini(ctx context.Context) {
	ap, err := accessFromContext(ctx)
	if err != nil {
		return
	}

	ap.Cleanup()
}

func PCIeCapsFromConfig(ctx context.Context, cfgBytes []byte) (*hardware.PCIDevice, error) {
	dev := &hardware.PCIDevice{}

	ap, err := accessFromContext(ctx)
	if err != nil {
		return nil, err
	}

	if err := ap.PCIeCapsFromConfig(cfgBytes, dev); err != nil {
		return nil, err
	}

	return dev, nil
}
