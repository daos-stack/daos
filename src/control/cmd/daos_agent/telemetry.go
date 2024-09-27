//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"

	"github.com/prometheus/client_golang/prometheus"

	"github.com/daos-stack/daos/src/control/lib/telemetry/promexp"
	"github.com/daos-stack/daos/src/control/logging"
)

func startPrometheusExporter(ctx context.Context, log logging.Logger, cs *promexp.ClientSource, cfg *Config) (func(), error) {
	expCfg := &promexp.ExporterConfig{
		Port:          cfg.TelemetryConfig.Port,
		Title:         "DAOS Client Telemetry",
		AllowInsecure: cfg.TelemetryConfig.AllowInsecure,
		HttpsCert:     cfg.TelemetryConfig.ServerCert,
		HttpsKey:      cfg.TelemetryConfig.ServerKey,
		Register: func(ctx context.Context, log logging.Logger) error {
			c, err := promexp.NewClientCollector(ctx, log, cs, &promexp.CollectorOpts{
				RetainDuration: cfg.TelemetryConfig.Retain,
			})
			if err != nil {
				return err
			}
			prometheus.MustRegister(c)

			return nil
		},
	}

	return promexp.StartExporter(ctx, log, expCfg)
}
