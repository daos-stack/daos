//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build linux && (amd64 || arm64)
// +build linux
// +build amd64 arm64

package promexp

import (
	"context"
	"fmt"
	"net/http"
	"time"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"
	"github.com/prometheus/client_golang/prometheus/promhttp"

	"github.com/daos-stack/daos/src/control/logging"
)

type (
	// RegMonFn defines a function signature for registering a Prometheus
	// monitor.
	RegMonFn func(context.Context, logging.Logger) error

	// ExporterConfig defines the configuration for the Prometheus exporter.
	ExporterConfig struct {
		Port     int
		Title    string
		Register RegMonFn
	}
)

const (
	// EngineTelemetryPort specifies the default port for engine telemetry.
	EngineTelemetryPort = 9191
	// ClientTelemetryPort specifies the default port for client telemetry.
	ClientTelemetryPort = 9192
)

// StartExporter starts the Prometheus exporter.
func StartExporter(ctx context.Context, log logging.Logger, cfg *ExporterConfig) (func(), error) {
	if cfg == nil {
		return nil, errors.New("invalid exporter config: nil config")
	}

	if cfg.Port <= 0 {
		return nil, errors.New("invalid exporter config: bad port")
	}

	if cfg.Register == nil {
		return nil, errors.New("invalid exporter config: nil register function")
	}

	if err := cfg.Register(ctx, log); err != nil {
		return nil, errors.Wrap(err, "failed to register client monitor")
	}

	listenAddress := fmt.Sprintf("0.0.0.0:%d", cfg.Port)

	srv := http.Server{Addr: listenAddress}
	http.Handle("/metrics", promhttp.HandlerFor(
		prometheus.DefaultGatherer, promhttp.HandlerOpts{},
	))
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		num, err := w.Write([]byte(fmt.Sprintf(`<html>
				<head><title>%s</title></head>
				<body>
				<h1>%s</h1>
				<p><a href="/metrics">Metrics</a></p>
				</body>
				</html>`, cfg.Title, cfg.Title)))
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
			log.Noticef("HTTP server didn't shut down within timeout: %s", err.Error())
		}
	}, nil
}
