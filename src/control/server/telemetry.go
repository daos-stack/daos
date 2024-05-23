//
// (C) Copyright 2018-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"

	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/lib/telemetry/promexp"
	"github.com/daos-stack/daos/src/control/logging"
)

func regPromEngineSources(ctx context.Context, log logging.Logger, engines []Engine) error {
	numEngines := len(engines)
	if numEngines == 0 {
		return nil
	}

	c, err := promexp.NewEngineCollector(log, &promexp.CollectorOpts{})
	if err != nil {
		return err
	}
	prometheus.MustRegister(c)

	addFn := func(idx uint32, rank ranklist.Rank) func(context.Context) error {
		return func(context.Context) error {
			log.Debugf("Setting up metrics collection for engine %d", idx)
			es, cleanup, err := promexp.NewEngineSource(ctx, idx, rank.Uint32())
			if err != nil {
				return errors.Wrapf(err, "failed to create EngineSource for idx %d", idx)
			}
			c.AddSource(es, cleanup)
			return nil
		}
	}

	delFn := func(idx uint32) func(context.Context, uint32, ranklist.Rank, error, int) error {
		return func(_ context.Context, _ uint32, rank ranklist.Rank, _ error, _ int) error {
			log.Debugf("Tearing down metrics collection for engine %d (rank %s)", idx, rank.String())
			c.RemoveSource(idx)
			return nil
		}
	}

	for i := 0; i < numEngines; i++ {
		er, err := engines[i].GetRank()
		if err != nil {
			return errors.Wrapf(err, "failed to get rank for idx %d", i)
		}

		addEngineSrc := addFn(uint32(i), er)
		if err := addEngineSrc(ctx); err != nil {
			return err
		}

		// Set up engine to add/remove source on exit/restart
		engines[i].OnReady(addEngineSrc)
		engines[i].OnInstanceExit(delFn(uint32(i)))
	}

	return nil
}

func startPrometheusExporter(ctx context.Context, log logging.Logger, port int, engines []Engine) (func(), error) {
	expCfg := &promexp.ExporterConfig{
		Port:  port,
		Title: "DAOS Engine Telemetry",
		Register: func(ctx context.Context, log logging.Logger) error {
			return regPromEngineSources(ctx, log, engines)
		},
	}

	return promexp.StartExporter(ctx, log, expCfg)
}
