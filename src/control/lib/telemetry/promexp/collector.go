//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build linux && amd64
// +build linux,amd64

//

package promexp

import (
	"context"
	"fmt"
	"regexp"
	"strings"
	"sync"
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
		cleanupSource  map[uint32]func()
		sourceMutex    sync.RWMutex // To protect sources
	}

	CollectorOpts struct {
		Ignores []string
	}

	EngineSource struct {
		ctx      context.Context
		tmMutex  sync.RWMutex // To protect telemetry collection
		Index    uint32
		Rank     uint32
		enabled  atm.Bool
		tmSchema *telemetry.Schema
		rmSchema rankMetricSchema
	}

	rankMetricSchema struct {
		mu          sync.Mutex
		rankMetrics map[string]*rankMetric
		seen        map[string]struct{}
	}
)

func (s *rankMetricSchema) Prune() {
	s.mu.Lock()
	defer s.mu.Unlock()

	for id := range s.rankMetrics {
		if _, found := s.seen[id]; !found {
			delete(s.rankMetrics, id)
		}
	}
	s.seen = make(map[string]struct{})
}

func (s *rankMetricSchema) add(log logging.Logger, rank uint32, metric telemetry.Metric) (rm *rankMetric) {
	s.mu.Lock()
	defer s.mu.Unlock()

	id := metric.FullPath()
	s.seen[id] = struct{}{}

	var found bool
	if rm, found = s.rankMetrics[id]; !found {
		rm = newRankMetric(log, rank, metric)
		s.rankMetrics[id] = rm
	} else {
		rm.resetVecs()
	}

	return
}

func NewEngineSource(parent context.Context, idx uint32, rank uint32) (*EngineSource, func(), error) {
	ctx, err := telemetry.Init(parent, idx)
	if err != nil {
		return nil, nil, errors.Wrap(err, "failed to init telemetry")
	}

	cleanupFn := func() {
		telemetry.Detach(ctx)
	}

	return &EngineSource{
		ctx:      ctx,
		Index:    idx,
		Rank:     rank,
		enabled:  atm.NewBool(true),
		tmSchema: telemetry.NewSchema(),
		rmSchema: rankMetricSchema{
			rankMetrics: make(map[string]*rankMetric),
			seen:        make(map[string]struct{}),
		},
	}, cleanupFn, nil
}

func defaultCollectorOpts() *CollectorOpts {
	return &CollectorOpts{}
}

func NewCollector(log logging.Logger, opts *CollectorOpts, sources ...*EngineSource) (*Collector, error) {
	if opts == nil {
		opts = defaultCollectorOpts()
	}

	c := &Collector{
		log:           log,
		sources:       sources,
		cleanupSource: make(map[uint32]func()),
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

type labelMap map[string]string

func (lm labelMap) keys() (keys []string) {
	for label := range lm {
		keys = append(keys, label)
	}

	return
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

	name = parseNameSubstr(labels, name, `_?xs_(\d+)`, "",
		func(labels labelMap, matches []string) {
			labels["xstream"] = matches[1]
		})

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
	if !es.IsEnabled() {
		return
	}
	if ch == nil {
		log.Error("nil channel")
		return
	}

	es.tmMutex.RLock()
	defer es.tmMutex.RUnlock()

	metrics := make(chan telemetry.Metric)
	go func() {
		if err := telemetry.CollectMetrics(es.ctx, es.tmSchema, metrics); err != nil {
			log.Errorf("failed to collect metrics for engine rank %d: %s", es.Rank, err)
			return
		}
		es.tmSchema.Prune()
	}()

	for metric := range metrics {
		ch <- es.rmSchema.add(log, es.Rank, metric)
	}
	es.rmSchema.Prune()
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

type gvMap map[string]*prometheus.GaugeVec

func (m gvMap) add(name, help string, labels labelMap) {
	if _, found := m[name]; !found {
		gv := prometheus.NewGaugeVec(prometheus.GaugeOpts{
			Name: name,
			Help: help,
		}, labels.keys())
		m[name] = gv
	}
}

func (m gvMap) set(name string, value float64, labels labelMap) error {
	gv, found := m[name]
	if !found {
		return errors.Errorf("gauge vector %s not found", name)
	}
	gv.With(prometheus.Labels(labels)).Set(value)

	return nil
}

type cvMap map[string]*prometheus.CounterVec

func (m cvMap) add(name, help string, labels labelMap) {
	if _, found := m[name]; !found {
		cv := prometheus.NewCounterVec(prometheus.CounterOpts{
			Name: name,
			Help: help,
		}, labels.keys())
		m[name] = cv
	}
}

func (m cvMap) set(name string, value float64, labels labelMap) error {
	cv, found := m[name]
	if !found {
		return errors.Errorf("counter vector %s not found", name)
	}
	cv.With(prometheus.Labels(labels)).Add(value)

	return nil
}

type rankMetric struct {
	rank     uint32
	metric   telemetry.Metric
	baseName string
	labels   labelMap
	gvm      gvMap
	cvm      cvMap
}

func (rm *rankMetric) collect(ch chan<- prometheus.Metric) {
	for _, gv := range rm.gvm {
		gv.Collect(ch)
	}
	for _, cv := range rm.cvm {
		cv.Collect(ch)
	}
}

func (rm *rankMetric) resetVecs() {
	for _, gv := range rm.gvm {
		gv.Reset()
	}
	for _, cv := range rm.cvm {
		cv.Reset()
	}
}

func newRankMetric(log logging.Logger, rank uint32, m telemetry.Metric) *rankMetric {
	rm := &rankMetric{
		metric: m,
		rank:   rank,
		gvm:    make(gvMap),
		cvm:    make(cvMap),
	}

	var name string
	rm.labels, name = extractLabels(m.FullPath())
	rm.labels["rank"] = fmt.Sprintf("%d", rm.rank)

	rm.baseName = strings.Join([]string{"engine", name}, "_")
	desc := m.Desc()

	switch rm.metric.Type() {
	case telemetry.MetricTypeGauge, telemetry.MetricTypeTimestamp,
		telemetry.MetricTypeSnapshot:
		rm.gvm.add(rm.baseName, desc, rm.labels)
	case telemetry.MetricTypeStatsGauge, telemetry.MetricTypeDuration:
		rm.gvm.add(rm.baseName, desc, rm.labels)
		for _, ms := range getMetricStats(rm.baseName, rm.metric) {
			rm.gvm.add(ms.name, ms.desc, rm.labels)
		}
	case telemetry.MetricTypeCounter:
		rm.cvm.add(rm.baseName, desc, rm.labels)
	default:
		log.Errorf("[%s]: metric type %d not supported", name, rm.metric.Type())
	}

	return rm
}

func (c *Collector) isIgnored(name string) bool {
	for _, re := range c.ignoredMetrics {
		if re.MatchString(name) {
			return true
		}
	}

	return false
}

type metricStat struct {
	name  string
	desc  string
	value float64
}

func getMetricStats(baseName string, m telemetry.Metric) (stats []*metricStat) {
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
			desc:  m.Desc() + s.desc,
			value: s.fn(),
		})
	}

	return
}

// AddSource adds an EngineSource to the Collector.
func (c *Collector) AddSource(es *EngineSource, cleanup func()) {
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
func (c *Collector) RemoveSource(engineIdx uint32) {
	c.sourceMutex.Lock()
	defer c.sourceMutex.Unlock()

	c.removeSourceNoLock(engineIdx)
}

func (c *Collector) removeSourceNoLock(engineIdx uint32) {
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

func (c *Collector) getSources() []*EngineSource {
	c.sourceMutex.RLock()
	defer c.sourceMutex.RUnlock()

	sourceCopy := make([]*EngineSource, len(c.sources))
	_ = copy(sourceCopy, c.sources)
	return sourceCopy
}

// Collect collects metrics from all EngineSources.
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
		for _, source := range sources {
			source.Collect(c.log, rankMetrics)
		}
		close(rankMetrics)
	}(c.getSources())

	for rm := range rankMetrics {
		if c.isIgnored(rm.baseName) {
			continue
		}

		var err error
		switch rm.metric.Type() {
		case telemetry.MetricTypeGauge, telemetry.MetricTypeTimestamp,
			telemetry.MetricTypeSnapshot:
			err = rm.gvm.set(rm.baseName, rm.metric.FloatValue(), rm.labels)
		case telemetry.MetricTypeStatsGauge, telemetry.MetricTypeDuration:
			if err = rm.gvm.set(rm.baseName, rm.metric.FloatValue(), rm.labels); err != nil {
				break
			}
			for _, ms := range getMetricStats(rm.baseName, rm.metric) {
				if err = rm.gvm.set(ms.name, ms.value, rm.labels); err != nil {
					break
				}
			}
		case telemetry.MetricTypeCounter:
			err = rm.cvm.set(rm.baseName, rm.metric.FloatValue(), rm.labels)
		default:
			c.log.Errorf("[%s]: metric type %d not supported", rm.baseName, rm.metric.Type())
		}

		if err != nil {
			c.log.Errorf("[%s]: %s", rm.baseName, err)
			continue
		}

		rm.collect(ch)
	}
}

func (c *Collector) Describe(ch chan<- *prometheus.Desc) {
	c.summary.Describe(ch)
}
