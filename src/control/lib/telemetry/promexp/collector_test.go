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
			result, cleanup, err := NewEngineSource(context.TODO(), tc.idx, tc.rank)
			if cleanup != nil {
				defer cleanup()
			}

			test.CmpErr(t, tc.expErr, err)

			if diff := cmp.Diff(tc.expResult, result, cmpopts.IgnoreUnexported(EngineSource{})); diff != "" {
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

	es, cleanup, err := NewEngineSource(context.Background(), uint32(testIdx), 2)
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

	validSrc, cleanupValid, err := NewEngineSource(context.Background(), testIdx, testRank)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanupValid()

	disabledSrc, cleanupDisabled, err := NewEngineSource(context.Background(), testIdx, testRank)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanupDisabled()
	disabledSrc.Disable()

	for name, tc := range map[string]struct {
		es         *EngineSource
		resultChan chan *rankMetric
		expMetrics telemetry.TestMetricsMap
	}{
		"nil source": {
			resultChan: make(chan *rankMetric),
		},
		"nil channel": {
			es: validSrc,
		},
		"bad source": {
			es: &EngineSource{
				ctx:   context.Background(),
				Rank:  123,
				Index: testIdx + 1,
			},
			resultChan: make(chan *rankMetric),
		},
		"success": {
			es:         validSrc,
			resultChan: make(chan *rankMetric),
			expMetrics: realMetrics,
		},
		"disabled": {
			es:         disabledSrc,
			resultChan: make(chan *rankMetric),
			expMetrics: telemetry.TestMetricsMap{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			go tc.es.Collect(log, tc.resultChan)

			gotMetrics := []*rankMetric{}
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
				test.AssertEqual(t, testRank, got.rank, "wrong rank")
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

func TestPromExp_NewCollector(t *testing.T) {
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
		expResult *Collector
	}{
		"no sources": {
			expResult: &Collector{
				summary: &prometheus.SummaryVec{
					MetricVec: &prometheus.MetricVec{},
				},
			},
		},
		"defaults": {
			sources: testSrc,
			expResult: &Collector{
				summary: &prometheus.SummaryVec{
					MetricVec: &prometheus.MetricVec{},
				},
				sources: testSrc,
			},
		},
		"opts with ignores": {
			sources: testSrc,
			opts:    &CollectorOpts{Ignores: []string{"one", "two"}},
			expResult: &Collector{
				summary: &prometheus.SummaryVec{
					MetricVec: &prometheus.MetricVec{},
				},
				sources: testSrc,
				ignoredMetrics: []*regexp.Regexp{
					regexp.MustCompile("one"),
					regexp.MustCompile("two"),
				},
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

			result, err := NewCollector(log, tc.opts, tc.sources...)

			test.CmpErr(t, tc.expErr, err)

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(EngineSource{}),
				cmpopts.IgnoreUnexported(prometheus.SummaryVec{}),
				cmpopts.IgnoreUnexported(prometheus.MetricVec{}),
				cmpopts.IgnoreUnexported(regexp.Regexp{}),
				cmp.AllowUnexported(Collector{}),
				cmp.FilterPath(func(p cmp.Path) bool {
					// Ignore a few specific fields
					return (strings.HasSuffix(p.String(), "log") ||
						strings.HasSuffix(p.String(), "sourceMutex") ||
						strings.HasSuffix(p.String(), "cleanupSource"))
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

	engSrc, cleanup, err := NewEngineSource(context.Background(), testIdx, testRank)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanup()

	defaultCollector, err := NewCollector(log, nil, engSrc)
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

		engSrc.rmSchema.mu.Lock()
		for m := range engSrc.rmSchema.rankMetrics {
			_, name := extractLabels(m)
			names = append(names, name)
		}
		engSrc.rmSchema.mu.Unlock()

		sort.Strings(names)
		return
	}

	expNames := func(maps ...telemetry.TestMetricsMap) []string {
		unique := map[string]struct{}{}
		for _, m := range maps {
			for t, m := range m {
				if t != telemetry.MetricTypeDirectory && t != telemetry.MetricTypeLink {
					_, name := extractLabels(m.FullPath())
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

	engSrc, cleanup, err := NewEngineSource(context.Background(), testIdx, testRank)
	if err != nil {
		t.Fatal(err)
	}
	defer cleanup()

	defaultCollector, err := NewCollector(log, nil, engSrc)
	if err != nil {
		t.Fatalf("failed to create collector: %s", err.Error())
	}

	ignores := []string{
		"engine_simple_counter1",
		"engine_simple_gauge1",
		"engine_stats_gauge2",
		"engine_timer_duration",
	}
	ignoreCollector, err := NewCollector(log, &CollectorOpts{
		Ignores: ignores,
	}, engSrc)
	if err != nil {
		t.Fatalf("failed to create collector with ignore list: %s", err.Error())
	}

	for name, tc := range map[string]struct {
		collector      *Collector
		resultChan     chan prometheus.Metric
		expMetricNames []string
	}{
		"nil collector": {
			resultChan: make(chan prometheus.Metric),
		},
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
				"engine_stats_gauge2_stddev",
				"engine_timer_stamp",
				"engine_timer_snapshot",
				"engine_timer_duration",
				"engine_timer_duration_min",
				"engine_timer_duration_max",
				"engine_timer_duration_mean",
				"engine_timer_duration_stddev",
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

func TestPromExp_extractLabels(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expName   string
		expLabels labelMap
	}{
		"empty": {},
		"nothing to change": {
			input:   "goodname",
			expName: "goodname",
		},
		"fix spaces": {
			input:   "a fine name",
			expName: "a_fine_name",
		},
		"ID": {
			input:   "ID: 2_stat",
			expName: "stat",
		},
		"io latency B": {
			input:   "io_latency_fetch_16B",
			expName: "io_latency_fetch",
			expLabels: labelMap{
				"size": "16B",
			},
		},
		"io latency KB": {
			input:   "io_latency_update_128KB",
			expName: "io_latency_update",
			expLabels: labelMap{
				"size": "128KB",
			},
		},
		"io latency MB": {
			input:   "io_latency_update_256MB",
			expName: "io_latency_update",
			expLabels: labelMap{
				"size": "256MB",
			},
		},
		"io latency >size": {
			input:   "io_latency_update_GT4MB",
			expName: "io_latency_update",
			expLabels: labelMap{
				"size": "GT4MB",
			},
		},
		"net rank": {
			input:   "net_15_stat",
			expName: "net_stat",
			expLabels: labelMap{
				"rank": "15",
			},
		},
		"pool UUID": {
			input:   "pool_11111111_2222_3333_4444_555555555555_info",
			expName: "pool_info",
			expLabels: labelMap{
				"pool": "11111111-2222-3333-4444-555555555555",
			},
		},
		"target": {
			input:   "io_update_latency_tgt_1",
			expName: "io_update_latency",
			expLabels: labelMap{
				"target": "1",
			},
		},
		"context": {
			input:   "failed_addr_ctx_1",
			expName: "failed_addr",
			expLabels: labelMap{
				"context": "1",
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			labels, name := extractLabels(tc.input)

			test.AssertEqual(t, tc.expName, name, "")
			test.AssertEqual(t, len(tc.expLabels), len(labels), "wrong number of labels")
			for key, val := range labels {
				expVal, exists := tc.expLabels[key]
				if !exists {
					t.Fatalf("key %q was not expected", key)
				}
				test.AssertEqual(t, expVal, val, "incorrect value")
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

			collector, err := NewCollector(log, nil, tc.startSrc...)
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

			collector, err := NewCollector(log, nil, tc.startSrc...)
			if err != nil {
				t.Fatalf("failed to set up collector: %s", err)
			}

			collector.cleanupSource = tc.startCleanup
			goodCleanupCalled = 0

			collector.RemoveSource(tc.idx)

			if diff := cmp.Diff(tc.expSrc, collector.sources, cmpopts.IgnoreUnexported(EngineSource{})); diff != "" {
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
