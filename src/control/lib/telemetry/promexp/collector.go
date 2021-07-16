//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
// +build linux,amd64
//

package promexp

import (
	"context"
	"fmt"
	"regexp"
	"strings"
	"unicode"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"

	"github.com/daos-stack/daos/src/control/lib/atm"
	"github.com/daos-stack/daos/src/control/lib/telemetry"
	"github.com/daos-stack/daos/src/control/logging"
)

type (
	Collector struct {
		log            logging.Logger
		summary        *prometheus.SummaryVec
		ignoredMetrics []*regexp.Regexp
		sources        []*EngineSource
	}

	CollectorOpts struct {
		Ignores []string
	}

	EngineSource struct {
		ctx     context.Context
		Index   uint32
		Rank    uint32
		enabled atm.Bool
	}

	labelMap map[string]string
)

func NewEngineSource(parent context.Context, idx uint32, rank uint32) (*EngineSource, func(), error) {
	ctx, err := telemetry.Init(parent, idx)
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to init telemetry")
	}

	cleanupFn := func() {
		telemetry.Detach(ctx)
	}

	return &EngineSource{
		ctx:     ctx,
		Index:   idx,
		Rank:    rank,
		enabled: atm.NewBool(true),
	}, cleanupFn, nil
}

func defaultCollectorOpts() *CollectorOpts {
	return &CollectorOpts{}
}

func NewCollector(log logging.Logger, opts *CollectorOpts, sources ...*EngineSource) (*Collector, error) {
	if len(sources) == 0 {
		return nil, errors.New("Collector must have > 0 sources")
	}

	if opts == nil {
		opts = defaultCollectorOpts()
	}

	c := &Collector{
		log:     log,
		sources: sources,
		summary: prometheus.NewSummaryVec(
			prometheus.SummaryOpts{
				Namespace: "engine",
				Subsystem: "exporter",
				Name:      "scrape_duration_seconds",
				Help:      "daos_exporter: Duration of a scrape job.",
			},
			[]string{"source", "result"},
		),
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

func sanitizeMetricName(in string) string {
	return strings.Map(func(r rune) rune {
		switch {
		// Valid names for Prometheus are limited to:
		case r >= 'a' && r <= 'z': // lowercase letters
		case r >= 'A' && r <= 'Z': // uppercase letters
		case unicode.IsDigit(r): // digits
		default: // sanitize any other character
			return '_'
		}

		return r
	}, strings.TrimLeft(in, "/"))
}

func parseNameSubstr(labels labelMap, name string, matchRE string, replacement string, assignLabels func(labelMap, []string)) string {
	re := regexp.MustCompile(matchRE)
	matches := re.FindStringSubmatch(name)
	if len(matches) > 0 {
		assignLabels(labels, matches)

		if strings.HasSuffix(matches[0], "_") {
			replacement += "_"
		}
		name = re.ReplaceAllString(name, replacement)
	}

	return name
}

func extractLabels(in string) (labels labelMap, name string) {
	name = sanitizeMetricName(in)

	labels = make(labelMap)

	// Clean up metric names and parse out useful labels

	ID_re := regexp.MustCompile(`ID_+(\d+)_?`)
	name = ID_re.ReplaceAllString(name, "")

	name = extractLatencySize(labels, name, "fetch")
	name = extractLatencySize(labels, name, "update")

	name = parseNameSubstr(labels, name, `_?tgt_(\d+)`, "",
		func(labels labelMap, matches []string) {
			labels["target"] = matches[1]
		})

	name = parseNameSubstr(labels, name, `_?ctx_(\d+)`, "",
		func(labels labelMap, matches []string) {
			labels["context"] = matches[1]
		})

	name = parseNameSubstr(labels, name, `net_+(\d+)`, "net",
		func(labels labelMap, matches []string) {
			labels["rank"] = matches[1]
		})

	getHexRE := func(numDigits int) string {
		return strings.Repeat(`[[:xdigit:]]`, numDigits)
	}
	uuid_re := fmt.Sprintf("%s_%s_%s_%s_%s", getHexRE(8), getHexRE(4), getHexRE(4),
		getHexRE(4), getHexRE(12))

	name = parseNameSubstr(labels, name, `pool_+(`+uuid_re+`)`, "pool",
		func(labels labelMap, matches []string) {
			labels["pool"] = strings.Replace(matches[1], "_", "-", -1)
		})
	return
}

func extractLatencySize(labels labelMap, name, latencyType string) string {
	return parseNameSubstr(labels, name, `_+latency_+`+latencyType+`_+((?:GT)?[0-9]+[A-Z]?B)`,
		"_latency_"+latencyType,
		func(labels labelMap, matches []string) {
			labels["size"] = matches[1]
		})
}

func (es *EngineSource) Collect(log logging.Logger, ch chan<- *rankMetric) {
	if es == nil {
		log.Error("nil engine source")
		return
	}
	if ch == nil {
		log.Error("nil channel")
		return
	}
	metrics := make(chan telemetry.Metric)
	go func() {
		if err := telemetry.CollectMetrics(es.ctx, metrics); err != nil {
			log.Errorf("failed to collect metrics for engine rank %d: %s", es.Rank, err)
			return
		}
	}()

	for metric := range metrics {
		if es.IsEnabled() {
			ch <- &rankMetric{
				r: es.Rank,
				m: metric,
			}
		}
	}
}

// IsEnabled checks if the engine source is enabled.
func (es *EngineSource) IsEnabled() bool {
	return es.enabled.IsTrue()
}

// Enable enables the engine source.
func (es *EngineSource) Enable() {
	es.enabled.SetTrue()
}

// Disable disables the engine source.
func (es *EngineSource) Disable() {
	es.enabled.SetFalse()
}

type rankMetric struct {
	r uint32
	m telemetry.Metric
}

func (c *Collector) isIgnored(name string) bool {
	for _, re := range c.ignoredMetrics {
		if re.MatchString(name) {
			return true
		}
	}

	return false
}

func (lm labelMap) keys() (keys []string) {
	for label := range lm {
		keys = append(keys, label)
	}

	return
}

type gvMap map[string]*prometheus.GaugeVec

func (m gvMap) add(name, help string, value float64, labels labelMap) {
	var gv *prometheus.GaugeVec
	var found bool

	gv, found = m[name]
	if !found {
		gv = prometheus.NewGaugeVec(prometheus.GaugeOpts{
			Name: name,
			Help: help,
		}, labels.keys())
		m[name] = gv
	}
	gv.With(prometheus.Labels(labels)).Set(value)
}

type cvMap map[string]*prometheus.CounterVec

func (m cvMap) add(name, help string, value float64, labels labelMap) {
	var cv *prometheus.CounterVec
	var found bool

	cv, found = m[name]
	if !found {
		cv = prometheus.NewCounterVec(prometheus.CounterOpts{
			Name: name,
			Help: help,
		}, labels.keys())
		m[name] = cv
	}
	cv.With(prometheus.Labels(labels)).Add(value)
}

type metricStat struct {
	name  string
	desc  string
	value float64
}

func getMetricStats(baseName, desc string, m telemetry.Metric) (stats []*metricStat) {
	ms, ok := m.(telemetry.StatsMetric)
	if !ok {
		return
	}

	for name, s := range map[string]struct {
		fn   func() float64
		desc string
	}{
		"min": {
			fn:   ms.FloatMin,
			desc: " (min value)",
		},
		"max": {
			fn:   ms.FloatMax,
			desc: " (max value)",
		},
		"mean": {
			fn:   ms.Mean,
			desc: " (mean)",
		},
		"stddev": {
			fn:   ms.StdDev,
			desc: " (std dev)",
		},
	} {
		stats = append(stats, &metricStat{
			name:  baseName + "_" + name,
			desc:  desc + s.desc,
			value: s.fn(),
		})
	}

	return
}

func (c *Collector) Collect(ch chan<- prometheus.Metric) {
	if c == nil {
		return
	}
	if ch == nil {
		c.log.Error("passed a nil channel")
		return
	}

	rankMetrics := make(chan *rankMetric)
	go func(sources []*EngineSource) {
		for _, source := range c.sources {
			source.Collect(c.log, rankMetrics)
		}
		close(rankMetrics)
	}(c.sources)

	gauges := make(gvMap)
	counters := make(cvMap)

	for rm := range rankMetrics {
		labels, name := extractLabels(rm.m.FullPath())
		labels["rank"] = fmt.Sprintf("%d", rm.r)

		baseName := strings.Join([]string{"engine", name}, "_")
		desc := rm.m.Desc()

		if c.isIgnored(baseName) {
			continue
		}

		switch rm.m.Type() {
		case telemetry.MetricTypeGauge:
			gauges.add(baseName, desc, rm.m.FloatValue(), labels)
		case telemetry.MetricTypeStatsGauge:
			gauges.add(baseName, desc, rm.m.FloatValue(), labels)
			for _, ms := range getMetricStats(baseName, desc, rm.m) {
				gauges.add(ms.name, ms.desc, ms.value, labels)
			}
		case telemetry.MetricTypeCounter:
			counters.add(baseName, desc, rm.m.FloatValue(), labels)
		case telemetry.MetricTypeTimestamp:
			gauges.add(baseName, desc, rm.m.FloatValue(), labels)
		case telemetry.MetricTypeSnapshot:
			gauges.add(baseName, desc, rm.m.FloatValue(), labels)
		case telemetry.MetricTypeDuration:
			gauges.add(baseName, desc, rm.m.FloatValue(), labels)
			for _, ms := range getMetricStats(baseName, desc, rm.m) {
				gauges.add(ms.name, ms.desc, ms.value, labels)
			}
		default:
			c.log.Errorf("[%s]: metric type %d not supported", name, rm.m.Type())
		}
	}

	for _, gv := range gauges {
		gv.Collect(ch)
	}
	for _, cv := range counters {
		cv.Collect(ch)
	}
}

func (c *Collector) Describe(ch chan<- *prometheus.Desc) {
	c.summary.Describe(ch)
}
