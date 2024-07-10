//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package promexp

import (
	"fmt"
	"regexp"
	"sort"
	"strings"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/google/uuid"
	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/telemetry"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestPromexp_NewEngineSource(t *testing.T) {
	testIdx := uint32(telemetry.NextTestID(telemetry.PromexpIDBase))
	telemetry.InitTestMetricsProducer(t, int(testIdx), 1024)
	defer telemetry.CleanupTestMetricsProducer(t)

	for name, tc := range map[string]struct {
		idx       uint32
		rank      uint32
		expErr    error
		expResult *EngineSource
	}{
		"bad idx": {
			idx:    (1 << 31),
			expErr: errors.New("failed to init"),
		},
		"success": {
			idx:  testIdx,
			rank: 123,
			expResult: &EngineSource{
				Index: uint32(testIdx),
				Rank:  123,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			result, cleanup, err := NewEngineSource(test.Context(t), tc.idx, tc.rank)
			if cleanup != nil {
				defer cleanup()
			}

			test.CmpErr(t, tc.expErr, err)

			cmpOpts := cmp.Options{
				cmpopts.IgnoreUnexported(MetricSource{}),
			}
			if diff := cmp.Diff(tc.expResult, result, cmpOpts...); diff != "" {
				t.Fatalf("(-want, +got)\n%s", diff)
			}

			if tc.expResult != nil && cleanup == nil {
				t.Fatal("expected a cleanup function")
			}
		})
	}
}

func TestPromExp_EngineSource_IsEnabled(t *testing.T) {
	testIdx := uint32(telemetry.NextTestID(telemetry.PromexpIDBase))
	telemetry.InitTestMetricsProducer(t, int(testIdx), 1024)
	defer telemetry.CleanupTestMetricsProducer(t)

	es, cleanup, err := NewEngineSource(test.Context(t), uint32(testIdx), 2)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanup()
	if es == nil {
		t.Fatal("EngineSource was nil")
	}

	test.AssertTrue(t, es.IsEnabled(), "default state should be enabled")

	es.Enable()
	test.AssertTrue(t, es.IsEnabled(), "Enable() while enabled")

	es.Disable()
	test.AssertFalse(t, es.IsEnabled(), "Disable() while enabled")

	es.Disable()
	test.AssertFalse(t, es.IsEnabled(), "Disable() while disabled")

	es.Enable()
	test.AssertTrue(t, es.IsEnabled(), "Enable() while disabled")
}

func allTestMetrics(t *testing.T) telemetry.TestMetricsMap {
	t.Helper()

	return telemetry.TestMetricsMap{
		telemetry.MetricTypeCounter: &telemetry.TestMetric{
			Name: "simple/counter1",
			Cur:  25,
		},
		telemetry.MetricTypeGauge: &telemetry.TestMetric{
			Name: "simple/gauge1",
			Cur:  1,
		},
		telemetry.MetricTypeStatsGauge: &telemetry.TestMetric{
			Name: "stats/gauge2",
			Cur:  100.5,
		},
		telemetry.MetricTypeTimestamp: &telemetry.TestMetric{
			Name: "timer/stamp",
		},
		telemetry.MetricTypeSnapshot: &telemetry.TestMetric{
			Name: "timer/snapshot",
		},
		telemetry.MetricTypeDuration: &telemetry.TestMetric{
			Name: "timer/duration",
			Cur:  float64(time.Millisecond),
		},
	}
}

func TestPromExp_EngineSource_Collect(t *testing.T) {
	testIdx := uint32(telemetry.NextTestID(telemetry.PromexpIDBase))
	testRank := uint32(123)
	telemetry.InitTestMetricsProducer(t, int(testIdx), 2048)
	defer telemetry.CleanupTestMetricsProducer(t)

	realMetrics := allTestMetrics(t)
	telemetry.AddTestMetrics(t, realMetrics)

	validSrc, cleanupValid, err := NewEngineSource(test.Context(t), testIdx, testRank)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanupValid()

	disabledSrc, cleanupDisabled, err := NewEngineSource(test.Context(t), testIdx, testRank)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanupDisabled()
	disabledSrc.Disable()

	for name, tc := range map[string]struct {
		es         *EngineSource
		resultChan chan *sourceMetric
		expMetrics telemetry.TestMetricsMap
	}{
		"nil channel": {
			es: validSrc,
		},
		"success": {
			es:         validSrc,
			resultChan: make(chan *sourceMetric),
			expMetrics: realMetrics,
		},
		"disabled": {
			es:         disabledSrc,
			resultChan: make(chan *sourceMetric),
			expMetrics: telemetry.TestMetricsMap{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			go tc.es.Collect(log, tc.resultChan)

			gotMetrics := []*sourceMetric{}
			for {
				done := false
				select {
				case <-time.After(500 * time.Millisecond):
					done = true
				case m := <-tc.resultChan:
					gotMetrics = append(gotMetrics, m)
				}

				if done {
					break
				}
			}

			test.AssertEqual(t, len(tc.expMetrics), len(gotMetrics), "wrong number of metrics returned")
			for _, got := range gotMetrics {
				test.AssertEqual(t, fmt.Sprintf("%d", testRank), got.labels["rank"], "wrong rank")
				expM, ok := tc.expMetrics[got.metric.Type()]
				if !ok {
					t.Fatalf("metric type %d not expected", got.metric.Type())
				}

				tokens := strings.Split(expM.Name, "/")
				expName := tokens[len(tokens)-1]
				test.AssertEqual(t, expName, got.metric.Name(), "unexpected name")
			}
		})
	}
}

func TestPromExp_NewEngineCollector(t *testing.T) {
	testSrc := []*EngineSource{
		{
			Rank: 1,
		},
		{
			Rank: 2,
		},
	}

	for name, tc := range map[string]struct {
		sources   []*EngineSource
		opts      *CollectorOpts
		expErr    error
		expResult *EngineCollector
	}{
		"no sources": {
			expResult: &EngineCollector{
				metricsCollector: metricsCollector{
					summary: &prometheus.SummaryVec{
						MetricVec: &prometheus.MetricVec{},
					},
				},
			},
		},
		"defaults": {
			sources: testSrc,
			expResult: &EngineCollector{
				metricsCollector: metricsCollector{
					summary: &prometheus.SummaryVec{
						MetricVec: &prometheus.MetricVec{},
					},
				},
				sources: testSrc,
			},
		},
		"opts with ignores": {
			sources: testSrc,
			opts:    &CollectorOpts{Ignores: []string{"one", "two"}},
			expResult: &EngineCollector{
				metricsCollector: metricsCollector{
					summary: &prometheus.SummaryVec{
						MetricVec: &prometheus.MetricVec{},
					},
					ignoredMetrics: []*regexp.Regexp{
						regexp.MustCompile("one"),
						regexp.MustCompile("two"),
					},
				},
				sources: testSrc,
			},
		},
		"bad regexp in ignores": {
			sources: testSrc,
			opts:    &CollectorOpts{Ignores: []string{"one", "(two////********["}},
			expErr:  errors.New("failed to compile"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			result, err := NewEngineCollector(log, tc.opts, tc.sources...)

			test.CmpErr(t, tc.expErr, err)

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(MetricSource{}),
				cmpopts.IgnoreUnexported(prometheus.SummaryVec{}),
				cmpopts.IgnoreUnexported(prometheus.MetricVec{}),
				cmpopts.IgnoreUnexported(regexp.Regexp{}),
				cmp.AllowUnexported(EngineCollector{}),
				cmp.AllowUnexported(metricsCollector{}),
				cmp.FilterPath(func(p cmp.Path) bool {
					// Ignore a few specific fields
					return (strings.HasSuffix(p.String(), "log") ||
						strings.HasSuffix(p.String(), "sourceMutex") ||
						strings.HasSuffix(p.String(), "cleanupSource") ||
						strings.HasSuffix(p.String(), "collectFn"))
				}, cmp.Ignore()),
			}
			if diff := cmp.Diff(tc.expResult, result, cmpOpts...); diff != "" {
				t.Fatalf("(-want, +got)\n%s", diff)
			}
		})
	}
}

func TestPromExp_Collector_Prune(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	testIdx := uint32(telemetry.NextTestID(telemetry.PromexpIDBase))
	testRank := uint32(123)
	telemetry.InitTestMetricsProducer(t, int(testIdx), 4096)
	defer telemetry.CleanupTestMetricsProducer(t)

	staticMetrics := telemetry.TestMetricsMap{
		telemetry.MetricTypeGauge: &telemetry.TestMetric{
			Name: "test/gauge",
			Cur:  42.42,
		},
		telemetry.MetricTypeDirectory: &telemetry.TestMetric{
			Name: "test/pool",
		},
	}

	pu := uuid.New()
	poolLink := telemetry.TestMetricsMap{
		telemetry.MetricTypeLink: &telemetry.TestMetric{
			Name: fmt.Sprintf("test/pool/%s", pu),
		},
	}
	poolCtr := telemetry.TestMetricsMap{
		telemetry.MetricTypeCounter: &telemetry.TestMetric{
			Name: fmt.Sprintf("test/pool/%s/counter1", pu),
			Cur:  1,
		},
	}

	engSrc, cleanup, err := NewEngineSource(test.Context(t), testIdx, testRank)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanup()

	defaultCollector, err := NewEngineCollector(log, nil, engSrc)
	if err != nil {
		t.Fatalf("failed to create collector: %s", err.Error())
	}

	getMetrics := func(t *testing.T) (names []string) {
		resultChan := make(chan prometheus.Metric)

		go defaultCollector.Collect(resultChan)

		done := false
		for !done {
			select {
			case <-time.After(500 * time.Millisecond):
				done = true
			case _ = <-resultChan:
			}
		}

		engSrc.smSchema.mu.Lock()
		for m := range engSrc.smSchema.sourceMetrics {
			_, name := extractLabels(log, m)
			names = append(names, name)
		}
		engSrc.smSchema.mu.Unlock()

		sort.Strings(names)
		return
	}

	expNames := func(maps ...telemetry.TestMetricsMap) []string {
		unique := map[string]struct{}{}
		for _, m := range maps {
			for t, m := range m {
				if t != telemetry.MetricTypeDirectory && t != telemetry.MetricTypeLink {
					_, name := extractLabels(log, m.FullPath())
					unique[name] = struct{}{}
				}
			}
		}

		names := []string{}
		for u := range unique {
			names = append(names, u)
		}
		sort.Strings(names)
		return names
	}

	telemetry.AddTestMetrics(t, staticMetrics)
	telemetry.AddTestMetrics(t, poolLink)
	telemetry.AddTestMetrics(t, poolCtr)

	expected := expNames(staticMetrics, poolLink, poolCtr)
	if diff := cmp.Diff(expected, getMetrics(t)); diff != "" {
		t.Fatalf("before prune: (-want, +got)\n%s", diff)
	}

	telemetry.RemoveTestMetrics(t, poolLink)

	expected = expNames(staticMetrics)
	if diff := cmp.Diff(expected, getMetrics(t)); diff != "" {
		t.Fatalf("after prune: (-want, +got)\n%s", diff)
	}
}

func TestPromExp_Collector_Collect(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	testIdx := uint32(telemetry.NextTestID(telemetry.PromexpIDBase))
	testRank := uint32(123)
	telemetry.InitTestMetricsProducer(t, int(testIdx), 4096)
	defer telemetry.CleanupTestMetricsProducer(t)

	realMetrics := allTestMetrics(t)
	telemetry.AddTestMetrics(t, realMetrics)

	engSrc, cleanup, err := NewEngineSource(test.Context(t), testIdx, testRank)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanup()

	defaultCollector, err := NewEngineCollector(log, nil, engSrc)
	if err != nil {
		t.Fatalf("failed to create collector: %s", err.Error())
	}

	ignores := []string{
		"engine_simple_counter1",
		"engine_simple_gauge1",
		"engine_stats_gauge2",
		"engine_timer_duration",
	}
	ignoreCollector, err := NewEngineCollector(log, &CollectorOpts{
		Ignores: ignores,
	}, engSrc)
	if err != nil {
		t.Fatalf("failed to create collector with ignore list: %s", err.Error())
	}

	for name, tc := range map[string]struct {
		collector      *EngineCollector
		resultChan     chan prometheus.Metric
		expMetricNames []string
	}{
		"nil channel": {
			collector: defaultCollector,
		},
		"success": {
			collector:  defaultCollector,
			resultChan: make(chan prometheus.Metric),
			expMetricNames: []string{
				"engine_simple_counter1",
				"engine_simple_gauge1",
				"engine_stats_gauge2",
				"engine_stats_gauge2_min",
				"engine_stats_gauge2_max",
				"engine_stats_gauge2_mean",
				"engine_stats_gauge2_sum",
				"engine_stats_gauge2_stddev",
				"engine_stats_gauge2_sumsquares",
				"engine_stats_gauge2_samples",
				"engine_timer_stamp",
				"engine_timer_snapshot",
				"engine_timer_duration",
				"engine_timer_duration_min",
				"engine_timer_duration_max",
				"engine_timer_duration_mean",
				"engine_timer_duration_sum",
				"engine_timer_duration_stddev",
				"engine_timer_duration_sumsquares",
				"engine_timer_duration_samples",
			},
		},
		"ignore some metrics": {
			collector:  ignoreCollector,
			resultChan: make(chan prometheus.Metric),
			expMetricNames: []string{
				"engine_timer_stamp",
				"engine_timer_snapshot",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			go tc.collector.Collect(tc.resultChan)

			gotMetrics := []prometheus.Metric{}
			done := false
			for !done {
				select {
				case <-time.After(500 * time.Millisecond):
					done = true
				case m := <-tc.resultChan:
					gotMetrics = append(gotMetrics, m)
				}
			}

			test.AssertEqual(t, len(tc.expMetricNames), len(gotMetrics), "wrong number of metrics returned")
			for _, exp := range tc.expMetricNames {
				found := false
				for _, got := range gotMetrics {
					if strings.Contains(got.Desc().String(), exp) {
						found = true
						break
					}
				}

				if !found {
					t.Errorf("expected metric %q not found", exp)
				}
			}
		})
	}
}

func TestPromExp_extractEngineLabels(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expName   string
		expLabels labelMap
	}{
		"empty": {
			expLabels: labelMap{},
		},
		"ID stripped": {
			input:     "ID: 123",
			expLabels: labelMap{},
		},
		"net_uri_lookup_self": {
			input:     "ID: 1/net/uri/lookup_self",
			expName:   "net_uri_lookup_self",
			expLabels: labelMap{},
		},
		"net_provider_req_timeout": {
			input:   "ID: 0/net/ofi+tcp;ofi_rxm/req_timeout/ctx_0",
			expName: "net_req_timeout",
			expLabels: labelMap{
				"provider": "ofi+tcp;ofi_rxm",
				"context":  "0",
			},
		},
		"dmabuff_queued_reqs": {
			input:   "ID: 0/dmabuff/queued_reqs/tgt_6",
			expName: "dmabuff_queued_reqs",
			expLabels: labelMap{
				"target": "6",
			},
		},
		"sched_total_time": {
			input:   "ID: 0/sched/total_time/xs_3",
			expName: "sched_total_time",
			expLabels: labelMap{
				"xstream": "3",
			},
		},
		"io latency fetch": {
			input:   "ID: 2/io/latency/fetch/16B",
			expName: "io_latency_fetch",
			expLabels: labelMap{
				"size": "16B",
			},
		},
		"io latency update": {
			input:   "ID: /io/latency/update/128KB",
			expName: "io_latency_update",
			expLabels: labelMap{
				"size": "128KB",
			},
		},
		"io latency >size": {
			input:   "ID: 1/io/latency/update/GT4MB",
			expName: "io_latency_update",
			expLabels: labelMap{
				"size": "GT4MB",
			},
		},
		"io_dtx_committable": {
			input:   "ID: 0/io/dtx/committable/tgt_5",
			expName: "io_dtx_committable",
			expLabels: labelMap{
				"target": "5",
			},
		},
		"io_ops_update_active": {
			input:   "ID: 0/io/ops/update_active/tgt_2",
			expName: "io_ops_update_active",
			expLabels: labelMap{
				"target": "2",
			},
		},
		"io_ops_tgt_punch_latency": {
			input:   "ID: 0/io/ops/tgt_punch/latency/tgt_2",
			expName: "io_ops_tgt_punch_latency",
			expLabels: labelMap{
				"target": "2",
			},
		},
		"pool_started_at": {
			input:   "ID: 0/pool/11111111-2222-3333-4444-555555555555/started_at",
			expName: "pool_started_at",
			expLabels: labelMap{
				"pool": "11111111-2222-3333-4444-555555555555",
			},
		},
		"pool_ops_tgt_dkey_punch": {
			input:   "ID: 0/pool/11111111-2222-3333-4444-555555555555/ops/tgt_dkey_punch/tgt_7",
			expName: "pool_ops_tgt_dkey_punch",
			expLabels: labelMap{
				"pool":   "11111111-2222-3333-4444-555555555555",
				"target": "7",
			},
		},
		"pool_tgt_scrubber_corruption_total": {
			input:   "ID: 1/pool/86eacd2c-eceb-4054-8621-017f4f661fe2/tgt_5/scrubber/corruption/total",
			expName: "pool_scrubber_corruption_total",
			expLabels: labelMap{
				"pool":   "86eacd2c-eceb-4054-8621-017f4f661fe2",
				"target": "5",
			},
		},
		"nvme_vendor_host_reads_raw": {
			input:   "ID: 1/nvme/d70505:05:00.0/vendor/host_reads_raw",
			expName: "nvme_vendor_host_reads_raw",
			expLabels: labelMap{
				"device": "d70505:05:00.0",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			labels, name := extractLabels(log, tc.input)

			test.AssertEqual(t, name, tc.expName, "")
			if diff := cmp.Diff(labels, tc.expLabels); diff != "" {
				t.Errorf("labels mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

func TestPromExp_Collector_AddSource(t *testing.T) {
	testSrc := func() []*EngineSource {
		return []*EngineSource{
			{Index: 1},
			{Index: 2},
			{Index: 3},
		}
	}

	for name, tc := range map[string]struct {
		startSrc   []*EngineSource
		es         *EngineSource
		fn         func()
		expSrc     []*EngineSource
		expAddedFn bool
	}{
		"nil EngineSource": {
			startSrc: testSrc(),
			fn:       func() {},
			expSrc:   testSrc(),
		},
		"nil func": {
			es:     &EngineSource{},
			expSrc: []*EngineSource{{}},
		},
		"add to empty": {
			es:         &EngineSource{},
			fn:         func() {},
			expSrc:     []*EngineSource{{}},
			expAddedFn: true,
		},
		"add to existing list": {
			startSrc:   testSrc(),
			es:         &EngineSource{},
			fn:         func() {},
			expSrc:     append(testSrc(), &EngineSource{}),
			expAddedFn: true,
		},
		"add as duplicate": {
			startSrc:   testSrc(),
			es:         testSrc()[1],
			fn:         func() {},
			expSrc:     testSrc(),
			expAddedFn: true,
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			collector, err := NewEngineCollector(log, nil, tc.startSrc...)
			if err != nil {
				t.Fatalf("failed to set up collector: %s", err)
			}
			collector.AddSource(tc.es, tc.fn)

			// Ordering changes are possible, so we can't directly compare the structs
			test.AssertEqual(t, len(tc.expSrc), len(collector.sources), "")
			for _, exp := range tc.expSrc {
				found := false
				for _, actual := range collector.sources {
					if actual.Index == exp.Index {
						found = true
						break
					}
				}

				if !found {
					t.Errorf("expected EngineSource %d not found", exp.Index)
				}
			}

			var found bool
			if tc.es != nil {
				_, found = collector.cleanupSource[tc.es.Index]
			}
			test.AssertEqual(t, tc.expAddedFn, found, "")
		})
	}
}

func TestPromExp_Collector_RemoveSource(t *testing.T) {
	badCleanup := func() {
		t.Fatal("wrong cleanup function called")
	}

	var goodCleanupCalled int
	goodCleanup := func() {
		goodCleanupCalled++
	}

	for name, tc := range map[string]struct {
		startSrc         []*EngineSource
		startCleanup     map[uint32]func()
		idx              uint32
		expSrc           []*EngineSource
		expCleanupKeys   []uint32
		expCleanupCalled int
	}{
		"not found": {
			startSrc: []*EngineSource{
				{Index: 1},
			},
			startCleanup: map[uint32]func(){
				1: badCleanup,
			},
			idx: 42,
			expSrc: []*EngineSource{
				{Index: 1},
			},
			expCleanupKeys: []uint32{1},
		},
		"success": {
			startSrc: []*EngineSource{
				{Index: 1},
				{Index: 2},
				{Index: 3},
			},
			startCleanup: map[uint32]func(){
				1: badCleanup,
				2: goodCleanup,
				3: badCleanup,
			},
			idx: 2,
			expSrc: []*EngineSource{
				{Index: 1},
				{Index: 3},
			},
			expCleanupKeys:   []uint32{1, 3},
			expCleanupCalled: 1,
		},
		"remove engine with no cleanup": {
			startSrc: []*EngineSource{
				{Index: 1},
				{Index: 2},
				{Index: 3},
			},
			startCleanup: map[uint32]func(){
				1: badCleanup,
				3: badCleanup,
			},
			idx: 2,
			expSrc: []*EngineSource{
				{Index: 1},
				{Index: 3},
			},
			expCleanupKeys: []uint32{1, 3},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			collector, err := NewEngineCollector(log, nil, tc.startSrc...)
			if err != nil {
				t.Fatalf("failed to set up collector: %s", err)
			}

			collector.cleanupSource = tc.startCleanup
			goodCleanupCalled = 0

			collector.RemoveSource(tc.idx)

			cmpOpts := cmp.Options{
				cmpopts.IgnoreUnexported(MetricSource{}),
			}
			if diff := cmp.Diff(tc.expSrc, collector.sources, cmpOpts...); diff != "" {
				t.Fatalf("(-want, +got)\n%s", diff)
			}

			test.AssertEqual(t, tc.expCleanupCalled, goodCleanupCalled, "")

			test.AssertEqual(t, len(tc.expCleanupKeys), len(collector.cleanupSource), "")
			for _, key := range tc.expCleanupKeys {
				fn, found := collector.cleanupSource[key]
				test.AssertTrue(t, found, fmt.Sprintf("expected to find %d in cleanup map", key))
				if fn == nil {
					t.Fatal("cleanup function was nil")
				}
			}
		})
	}
}
