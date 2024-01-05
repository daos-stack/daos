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
	"sync"
	"time"

	"github.com/prometheus/client_golang/prometheus"

	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/telemetry"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	sourceMetricSchema struct {
		mu            sync.Mutex
		sourceMetrics map[string]*sourceMetric
		seen          map[string]struct{}
		addFn         func(logging.Logger, telemetry.Metric) *sourceMetric
	}

	MetricSource struct {
		ctx      context.Context
		tmMutex  sync.RWMutex // To protect telemetry collection
		enabled  atm.Bool
		tmSchema *telemetry.Schema
		smSchema *sourceMetricSchema
	}
)

func newSourceMetricSchema(addFn func(logging.Logger, telemetry.Metric) *sourceMetric) *sourceMetricSchema {
	return &sourceMetricSchema{
		sourceMetrics: make(map[string]*sourceMetric),
		seen:          make(map[string]struct{}),
		addFn:         addFn,
	}
}

func (s *sourceMetricSchema) Prune() {
	s.mu.Lock()
	defer s.mu.Unlock()

	for id := range s.sourceMetrics {
		if _, found := s.seen[id]; !found {
			delete(s.sourceMetrics, id)
		}
	}
	s.seen = make(map[string]struct{})
}

func (s *sourceMetricSchema) add(log logging.Logger, metric telemetry.Metric) (sm *sourceMetric) {
	s.mu.Lock()
	defer s.mu.Unlock()

	id := metric.FullPath()
	s.seen[id] = struct{}{}

	var found bool
	if sm, found = s.sourceMetrics[id]; !found {
		sm = s.addFn(log, metric)
		s.sourceMetrics[id] = sm
	} else {
		sm.resetVecs()
	}

	return
}

func defaultCollectorOpts() *CollectorOpts {
	return &CollectorOpts{}
}

type sourceMetric struct {
	metric   telemetry.Metric
	baseName string
	labels   labelMap
	gvm      gvMap
	cvm      cvMap
}

func (bm *sourceMetric) collect(ch chan<- prometheus.Metric) {
	for _, gv := range bm.gvm {
		gv.Collect(ch)
	}
	for _, cv := range bm.cvm {
		cv.Collect(ch)
	}
}

func (bm *sourceMetric) resetVecs() {
	for _, gv := range bm.gvm {
		gv.Reset()
	}
	for _, cv := range bm.cvm {
		cv.Reset()
	}
}

func newSourceMetric(log logging.Logger, m telemetry.Metric, baseName string, labels labelMap) *sourceMetric {
	sm := &sourceMetric{
		metric:   m,
		baseName: baseName,
		labels:   labels,
		gvm:      make(gvMap),
		cvm:      make(cvMap),
	}

	desc := m.Desc()

	switch sm.metric.Type() {
	case telemetry.MetricTypeGauge, telemetry.MetricTypeTimestamp,
		telemetry.MetricTypeSnapshot:
		sm.gvm.add(sm.baseName, desc, sm.labels)
	case telemetry.MetricTypeStatsGauge, telemetry.MetricTypeDuration:
		sm.gvm.add(sm.baseName, desc, sm.labels)
		for _, ms := range getMetricStats(sm.baseName, sm.metric) {
			if ms.isCounter {
				sm.cvm.add(ms.name, ms.desc, sm.labels)
			} else {
				sm.gvm.add(ms.name, ms.desc, sm.labels)
			}
		}
	case telemetry.MetricTypeCounter:
		sm.cvm.add(sm.baseName, desc, sm.labels)
	default:
		log.Errorf("[%s]: metric type %d not supported", baseName, sm.metric.Type())
	}

	return sm
}

// IsEnabled checks if the source is enabled.
func (s *MetricSource) IsEnabled() bool {
	return s.enabled.IsTrue()
}

// Enable enables the source.
func (s *MetricSource) Enable() {
	s.enabled.SetTrue()
}

// Disable disables the source.
func (s *MetricSource) Disable() {
	s.enabled.SetFalse()
}

func (s *MetricSource) Collect(log logging.Logger, ch chan<- *sourceMetric) {
	if s == nil {
		log.Error("nil source")
		return
	}
	if !s.IsEnabled() {
		return
	}
	if ch == nil {
		log.Error("nil channel")
		return
	}

	s.tmMutex.RLock()
	defer s.tmMutex.RUnlock()

	metrics := make(chan telemetry.Metric)
	go func() {
		if err := telemetry.CollectMetrics(s.ctx, s.tmSchema, metrics); err != nil {
			log.Errorf("failed to collect metrics: %s", err)
			return
		}
		s.tmSchema.Prune()
	}()

	for metric := range metrics {
		ch <- s.smSchema.add(log, metric)
	}
	s.smSchema.Prune()
}

func (s *MetricSource) PruneSegments(log logging.Logger, maxSegAge time.Duration) {
	if s == nil {
		log.Error("nil source")
		return
	}
	if !s.IsEnabled() {
		return
	}

	if err := telemetry.PruneUnusedSegments(s.ctx, maxSegAge); err != nil {
		log.Errorf("failed to prune segments: %s", err)
		return
	}

	s.tmSchema.Prune()
	s.smSchema.Prune()
}
