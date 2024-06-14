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
	"regexp"
	"strings"
	"sync"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"

	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/telemetry"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	// EngineCollector collects metrics from DAOS Engine sources.
	EngineCollector struct {
		metricsCollector
		sources       []*EngineSource
		cleanupSource map[uint32]func()
		sourceMutex   sync.RWMutex // To protect sources
	}

	// EngineSource provides metrics for a single DAOS Engine.
	EngineSource struct {
		MetricSource
		Index uint32
		Rank  uint32
	}
)

// NewEngineSource initializes a new metrics source for a DAOS Engine.
func NewEngineSource(parent context.Context, idx uint32, rank uint32) (*EngineSource, func(), error) {
	ctx, err := telemetry.Init(parent, idx)
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to init telemetry")
	}

	cleanupFn := func() {
		telemetry.Detach(ctx)
	}

	return &EngineSource{
		MetricSource: MetricSource{
			ctx:      ctx,
			enabled:  atm.NewBool(true),
			tmSchema: telemetry.NewSchema(),
			smSchema: newSourceMetricSchema(func(l logging.Logger, m telemetry.Metric) *sourceMetric {
				return newRankMetric(l, rank, m)
			}),
		},
		Index: idx,
		Rank:  rank,
	}, cleanupFn, nil
}

// NewEngineCollector initializes a new collector for DAOS Engine sources.
func NewEngineCollector(log logging.Logger, opts *CollectorOpts, sources ...*EngineSource) (*EngineCollector, error) {
	if opts == nil {
		opts = defaultCollectorOpts()
	}

	c := &EngineCollector{
		metricsCollector: metricsCollector{
			log: log,
			summary: prometheus.NewSummaryVec(
				prometheus.SummaryOpts{
					Namespace: "engine",
					Subsystem: "exporter",
					Name:      "scrape_duration_seconds",
					Help:      "daos_exporter: Duration of a scrape job.",
				},
				[]string{"source", "result"},
			),
		},
		sources:       sources,
		cleanupSource: make(map[uint32]func()),
	}

	c.collectFn = func(metrics chan *sourceMetric) {
		for _, source := range c.getSources() {
			source.Collect(c.log, metrics)
		}
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

// extractLabels takes a "/"-separated DAOS metric name in order to
// create a normalized Prometheus name and label map.
//
// NB: Prometheus metric names should follow best practices as
// outlined at https://prometheus.io/docs/practices/naming/
//
// In particular, a metric name should describe the measurement,
// not the entity the measurement is about. In other words, if 4
// different entities share the same measurement, then there should
// be a single metric with a label that distinguishes between
// individual measurement values.
//
// Good: pool_started_at {pool="00000000-1111-2222-3333-4444444444"}
// Bad: pool_00000000_1111_2222_3333_4444444444_started_at
func extractLabels(log logging.Logger, in string) (labels labelMap, name string) {
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

	switch comps[compsIdx] {
	case "pool":
		name = "pool"
		compsIdx++
		labels["pool"] = comps[compsIdx]
		compsIdx++
		switch comps[compsIdx] {
		case "ops":
			compsIdx++
			name += "_ops_" + comps[compsIdx]
			compsIdx++
		}
	case "io":
		name = "io"
		compsIdx++
		switch comps[compsIdx] {
		case "latency":
			compsIdx++
			name += "_latency_" + comps[compsIdx]
			compsIdx++
			labels["size"] = comps[compsIdx]
			compsIdx++
		case "ops":
			compsIdx++
			name += "_ops_" + comps[compsIdx]
			compsIdx++
		default:
			name += "_" + comps[compsIdx]
			compsIdx++
		}
	case "net":
		compsIdx++
		if comps[compsIdx] == "uri" {
			compsIdx++
			name = "net_uri_" + comps[compsIdx]
			compsIdx++
			break
		}

		name = "net"
		labels["provider"] = comps[compsIdx]
		compsIdx++
	case "nvme":
		name = "nvme"
		compsIdx++
		labels["device"] = comps[compsIdx]
		compsIdx++
	}

	for {
		if len(comps) == compsIdx {
			break
		}

		switch {
		case matchLabel(labels, comps[compsIdx], "tgt_", "target"):
			compsIdx++
		case matchLabel(labels, comps[compsIdx], "xs_", "xstream"):
			compsIdx++
		case matchLabel(labels, comps[compsIdx], "ctx_", "context"):
			compsIdx++
		default:
			name = appendName(name, comps[compsIdx])
			compsIdx++
		}
	}

	name = sanitizeMetricName(name)
	return
}

func newRankMetric(log logging.Logger, rank uint32, m telemetry.Metric) *sourceMetric {
	labels, name := extractLabels(log, m.FullPath())
	baseName := "engine_" + name
	labels["rank"] = fmt.Sprintf("%d", rank)

	return newSourceMetric(log, m, baseName, labels)
}

// AddSource adds an EngineSource to the Collector.
func (c *EngineCollector) AddSource(es *EngineSource, cleanup func()) {
	if es == nil {
		c.log.Error("attempted to add nil EngineSource")
		return
	}

	c.sourceMutex.Lock()
	defer c.sourceMutex.Unlock()

	// If we attempt to add a duplicate, remove the old one.
	c.removeSourceNoLock(es.Index)

	c.sources = append(c.sources, es)
	if cleanup != nil {
		c.cleanupSource[es.Index] = cleanup
	}
}

// RemoveSource removes an EngineSource with a given index from the Collector.
func (c *EngineCollector) RemoveSource(engineIdx uint32) {
	c.sourceMutex.Lock()
	defer c.sourceMutex.Unlock()

	c.removeSourceNoLock(engineIdx)
}

func (c *EngineCollector) removeSourceNoLock(engineIdx uint32) {
	for i, es := range c.sources {
		if es.Index == engineIdx {
			es.Disable()
			c.sources = append(c.sources[:i], c.sources[i+1:]...)

			// Ensure that EngineSource isn't collecting during cleanup
			es.tmMutex.Lock()
			if cleanup, found := c.cleanupSource[engineIdx]; found && cleanup != nil {
				cleanup()
			}
			es.tmMutex.Unlock()
			delete(c.cleanupSource, engineIdx)
			break
		}
	}
}

func (c *EngineCollector) getSources() []*EngineSource {
	c.sourceMutex.RLock()
	defer c.sourceMutex.RUnlock()

	sourceCopy := make([]*EngineSource, len(c.sources))
	_ = copy(sourceCopy, c.sources)
	return sourceCopy
}
