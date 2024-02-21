//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build linux && (amd64 || arm64)
// +build linux
// +build amd64 arm64

package promexp

import (
	"context"
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"

	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/telemetry"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	// ClientCollector is a metrics collector for DAOS client metrics.
	ClientCollector struct {
		metricsCollector
	}
)

func extractClientLabels(log logging.Logger, in string) (labels labelMap, name string) {
	log.Tracef("in: %q", in)

	labels = make(labelMap)
	compsIdx := 0
	comps := strings.Split(in, string(telemetry.PathSep))
	if len(comps) == 0 {
		return labels, ""
	}

	if strings.HasPrefix(comps[compsIdx], "ID") {
		if len(comps) == 1 {
			return labels, ""
		}
		compsIdx++
	}

	for i, label := range []string{"job", "pid", "tid"} {
		if i > 0 {
			// After jobid, we should have a pid and/or tid, and
			// then move on to the engine labels.
			_, err := strconv.Atoi(comps[compsIdx])
			if err != nil {
				break
			}
		}

		if len(comps) == compsIdx+1 {
			// If we have a weird path ending on a pid or tid, treat it
			// as empty of labels.
			if _, err := strconv.Atoi(comps[compsIdx]); err == nil && i > 0 {
				return labelMap{}, ""
			}
			return labels, comps[compsIdx]
		}
		labels[label] = comps[compsIdx]
		compsIdx++
	}

	var engLabels labelMap
	engLabels, name = extractEngineLabels(log, strings.Join(comps[compsIdx:], string(telemetry.PathSep)))
	for k, v := range engLabels {
		labels[k] = v
	}

	return
}

func newClientMetric(log logging.Logger, m telemetry.Metric) *sourceMetric {
	labels, name := extractClientLabels(log, m.FullPath())
	baseName := "client_" + name

	return newSourceMetric(log, m, baseName, labels)
}

// newClientSource creates a new MetricSource for client metrics.
func newClientSource(parent context.Context) (*MetricSource, func(), error) {
	ctx, err := telemetry.InitClientRoot(parent)
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to init telemetry")
	}

	cleanupFn := func() {
		telemetry.Detach(ctx)
	}

	return &MetricSource{
		ctx:      ctx,
		enabled:  atm.NewBool(true),
		tmSchema: telemetry.NewSchema(),
		smSchema: newSourceMetricSchema(newClientMetric),
	}, cleanupFn, nil
}

// NewClientCollector creates a new ClientCollector instance.
func NewClientCollector(ctx context.Context, log logging.Logger, opts *CollectorOpts) (*ClientCollector, error) {
	source, cleanup, err := newClientSource(ctx)
	if err != nil {
		return nil, errors.Wrap(err, "failed to create client source")
	}

	if opts == nil {
		opts = defaultCollectorOpts()
	}

	go func() {
		<-ctx.Done()
		cleanup()
	}()

	if opts.RetainDuration != 0 {
		log.Debugf("pruning client metrics every %s", opts.RetainDuration)

		go func() {
			pruneTicker := time.NewTicker(opts.RetainDuration)
			defer pruneTicker.Stop()

			for {
				select {
				case <-ctx.Done():
				case <-pruneTicker.C:
					source.PruneSegments(log, opts.RetainDuration)
				}
			}
		}()
	}

	c := &ClientCollector{
		metricsCollector: metricsCollector{
			log: log,
			summary: prometheus.NewSummaryVec(
				prometheus.SummaryOpts{
					Namespace: "client",
					Subsystem: "exporter",
					Name:      "scrape_duration_seconds",
					Help:      "daos_client_exporter: Duration of a scrape job.",
				},
				[]string{"source", "result"},
			),
			collectFn: func(ch chan *sourceMetric) {
				source.Collect(log, ch)
			},
		},
	}

	for _, pat := range opts.Ignores {
		re, err := regexp.Compile(pat)
		if err != nil {
			return nil, errors.Wrapf(err, "failed to compile %q", pat)
		}
		c.ignoredMetrics = append(c.ignoredMetrics, re)
	}

	return c, nil
}
