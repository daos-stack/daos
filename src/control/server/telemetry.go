//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"net/http"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"github.com/daos-stack/daos/src/control/lib/telemetry/promexp"
	"github.com/daos-stack/daos/src/control/logging"
)

func regPromEngineSources(ctx context.Context, log logging.Logger, engines []*EngineInstance) error {
	numEngines := len(engines)
	if numEngines == 0 {
		return nil
	}

	sources := make([]*promexp.EngineSource, numEngines)
	for i := 0; i < numEngines; i++ {
		er, err := engines[i].GetRank()
		if err != nil {
			return errors.Wrapf(err, "failed to get rank for idx %d", i)
		}
		es, err := promexp.NewEngineSource(ctx, uint32(i), er.Uint32())
		if err != nil {
			return errors.Wrapf(err, "failed to create EngineSource for idx %d", i)
		}
		sources[i] = es
	}

	opts := &promexp.CollectorOpts{
		Ignores: []string{
			`.*_ID_(\d+)_rank`,
		},
	}
	c, err := promexp.NewCollector(log, opts, sources...)
	if err != nil {
		return err
	}
	prometheus.MustRegister(c)

	return nil
}

func startPrometheusExporter(ctx context.Context, log logging.Logger, port int, engines []*EngineInstance) error {
	if err := regPromEngineSources(ctx, log, engines); err != nil {
		return err
	}

	http.Handle("/metrics", promhttp.HandlerFor(
		prometheus.DefaultGatherer, promhttp.HandlerOpts{},
	))
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		num, err := w.Write([]byte(`<html>
				<head><title>DAOS Exporter</title></head>
				<body>
				<h1>DAOS Exporter</h1>
				<p><a href="/metrics">Metrics</a></p>
				</body>
				</html>`))
		if err != nil {
			log.Errorf("%d: %s", num, err)
		}
	})

	listenAddress := fmt.Sprintf("0.0.0.0:%d", port)
	log.Infof("Listening on %s", listenAddress)

	if err := http.ListenAndServe(listenAddress, nil); err != nil {
		return err
	}

	return nil
}
