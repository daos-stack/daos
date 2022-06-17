//
// (C) Copyright 2018-2022 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/system"
)

func regPromEngineSources(ctx context.Context, log logging.Logger, engines []Engine) error {
	numEngines := len(engines)
	if numEngines == 0 {
		return nil
	}

	opts := &promexp.CollectorOpts{
		Ignores: []string{
			`.*_ID_(\d+)_rank`,
		},
	}
	c, err := promexp.NewCollector(log, opts)
	if err != nil {
		return err
	}
	prometheus.MustRegister(c)

	addFn := func(idx uint32, rank system.Rank) func(context.Context) error {
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

	delFn := func(idx uint32) func(context.Context, uint32, system.Rank, error, uint64) error {
		return func(_ context.Context, _ uint32, rank system.Rank, _ error, _ uint64) error {
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
	if err := regPromEngineSources(ctx, log, engines); err != nil {
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
	}, nil
}
