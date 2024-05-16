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
	"regexp"
	"time"

	"github.com/prometheus/client_golang/prometheus"

	"github.com/daos-stack/daos/src/control/lib/telemetry"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	// CollectorOpts contains options for the metrics collector.
	CollectorOpts struct {
		Ignores        []string
		RetainDuration time.Duration
	}

	metricsCollector struct {
		log            logging.Logger
		summary        *prometheus.SummaryVec
		ignoredMetrics []*regexp.Regexp
		collectFn      func(ch chan *sourceMetric)
	}
)

func (c *metricsCollector) isIgnored(name string) bool {
	for _, re := range c.ignoredMetrics {
		// TODO: We may want to look into removing the use of regexp here
		// in favor of a less-flexible but more efficient approach.
		// For the moment, use of ignored metrics should be avoided if possible.
		if re.MatchString(name) {
			return true
		}
	}

	return false
}

func (c *metricsCollector) Collect(ch chan<- prometheus.Metric) {
	if c == nil {
		return
	}
	if ch == nil {
		c.log.Error("passed a nil channel")
		return
	}
	if c.collectFn == nil {
		c.log.Error("collectFn is nil")
		return
	}

	sourceMetrics := make(chan *sourceMetric)
	go func() {
		c.collectFn(sourceMetrics)
		close(sourceMetrics)
	}()

	for sm := range sourceMetrics {
		if c.isIgnored(sm.baseName) {
			continue
		}

		var err error
		switch sm.metric.Type() {
		case telemetry.MetricTypeGauge, telemetry.MetricTypeTimestamp,
			telemetry.MetricTypeSnapshot:
			err = sm.gvm.set(sm.baseName, sm.metric.FloatValue(), sm.labels)
		case telemetry.MetricTypeStatsGauge, telemetry.MetricTypeDuration:
			if err = sm.gvm.set(sm.baseName, sm.metric.FloatValue(), sm.labels); err != nil {
				break
			}
			for _, ms := range getMetricStats(sm.baseName, sm.metric) {
				if ms.isCounter {
					if err = sm.cvm.set(ms.name, ms.value, sm.labels); err != nil {
						break
					}
				} else {
					if err = sm.gvm.set(ms.name, ms.value, sm.labels); err != nil {
						break
					}
				}
			}
		case telemetry.MetricTypeCounter:
			err = sm.cvm.set(sm.baseName, sm.metric.FloatValue(), sm.labels)
		default:
			c.log.Errorf("[%s]: metric type %d not supported", sm.baseName, sm.metric.Type())
		}

		if err != nil {
			c.log.Errorf("[%s]: %s", sm.baseName, err)
			continue
		}

		sm.collect(ch)
	}
}

func (c *metricsCollector) Describe(ch chan<- *prometheus.Desc) {
	c.summary.Describe(ch)
}
