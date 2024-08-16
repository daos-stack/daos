//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
)

type (
	debugTraceLogger interface {
		logging.TraceLogger
		logging.DebugLogger
	}

	// Provider wraps the C DAOS API with methods that accept and return
	// standard Go data types.
	Provider struct {
		log     debugTraceLogger
		api     *api
		cleanup func()
	}
)

// NewProvider returns an initialized DAOS API provider.
func NewProvider(log debugTraceLogger) (*Provider, error) {
	api := &api{}
	cleanup, err := api.Init()
	if err != nil {
		return nil, errors.Wrap(err, "failed to initialize DAOS API")
	}

	return &Provider{
		log:     log,
		api:     api,
		cleanup: cleanup,
	}, nil
}

// Cleanup releases resources obtained during API initialization.
func (p *Provider) Cleanup() {
	p.cleanup()
}
