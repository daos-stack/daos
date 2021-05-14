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
	"time"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"github.com/daos-stack/daos/src/control/lib/telemetry/promexp"
	"github.com/daos-stack/daos/src/control/logging"
)

func regPromEngineSources(ctx context.Context, log logging.Logger, engines []*EngineInstance) ([]func(), error) {
	numEngines := len(engines)
	if numEngines == 0 {
		return []func(){}, nil
	}

	var err error
	cleanupFns := make([]func(), 0, numEngines)
	defer func() {
		if err != nil {
			for _, cleanup := range cleanupFns {
				cleanup()
			}
		}
	}()

	sources := make([]*promexp.EngineSource, numEngines)
	for i := 0; i < numEngines; i++ {
		er, err := engines[i].GetRank()
		if err != nil {
			return nil, errors.Wrapf(err, "failed to get rank for idx %d", i)
		}
		es, cleanup, err := promexp.NewEngineSource(ctx, uint32(i), er.Uint32())
		if err != nil {
			return nil, errors.Wrapf(err, "failed to create EngineSource for idx %d", i)
		}
		sources[i] = es
		cleanupFns = append(cleanupFns, cleanup)
	}

	opts := &promexp.CollectorOpts{
		Ignores: []string{
			`.*_ID_(\d+)_rank`,
		},
	}
	c, err := promexp.NewCollector(log, opts, sources...)
	if err != nil {
		return nil, err
	}
	prometheus.MustRegister(c)

	return cleanupFns, nil
}

func startPrometheusExporter(ctx context.Context, log logging.Logger, port int, engines []*EngineInstance) (func(), error) {
	cleanupFns, err := regPromEngineSources(ctx, log, engines)
	if err != nil {
		return nil, err
	}

	listenAddress := fmt.Sprintf("0.0.0.0:%d", port)

	srv := http.Server{Addr: listenAddress}
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

	// http listener is a blocking call
	go func() {
		log.Infof("Listening on %s", listenAddress)
		err := srv.ListenAndServe()
		log.Infof("Prometheus web exporter stopped: %s", err.Error())
	}()

	return func() {
		log.Debug("Shutting down Prometheus web exporter")

		// When this cleanup function is called, the original context
		// will probably have already been canceled.
		timedCtx, cancel := context.WithTimeout(context.Background(), 1*time.Second)
		defer cancel()
		if err := srv.Shutdown(timedCtx); err != nil {
			log.Infof("HTTP server didn't shut down within timeout: %s", err.Error())
		}

		for _, cleanup := range cleanupFns {
			cleanup()
		}
	}, nil
}
