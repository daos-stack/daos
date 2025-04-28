//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package promexp

import (
	"sort"
	"sync"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"
	dto "github.com/prometheus/client_model/go"
	"github.com/prometheus/common/model"
)

var (
	_ prometheus.Metric    = &daosHistogram{}
	_ prometheus.Collector = &daosHistogramVec{}
)

// daosHistogram is a pass-through structure for pre-bucketed histogram
// data retrieved from the DAOS telemetry library.
type daosHistogram struct {
	desc      *prometheus.Desc
	name      string
	help      string
	sum       float64
	count     uint64
	labelVals []string
	buckets   []float64
	bucketMap map[float64]uint64
}

func newDaosHistogram(name, help string, desc *prometheus.Desc, buckets []float64, labelVals []string) *daosHistogram {
	dh := &daosHistogram{
		name:      name,
		help:      help,
		desc:      desc,
		labelVals: labelVals,
		buckets:   buckets,
		bucketMap: make(map[float64]uint64),
	}

	for _, b := range buckets {
		dh.bucketMap[b] = 0
	}

	return dh
}

func (dh *daosHistogram) Desc() *prometheus.Desc {
	return dh.desc
}

func (dh *daosHistogram) Write(out *dto.Metric) error {
	ch, err := prometheus.NewConstHistogram(dh.desc, dh.count, dh.sum, dh.bucketMap, dh.labelVals...)
	if err != nil {
		return err
	}

	return ch.Write(out)
}

func (dh *daosHistogram) AddSample(sampleCount uint64, sum float64, values []uint64) error {
	if len(values) != len(dh.buckets) {
		return errors.Errorf("expected %d values, got %d", len(dh.buckets), len(values))
	}

	for i, b := range dh.buckets {
		dh.bucketMap[b] = values[i]
	}

	dh.count = sampleCount
	dh.sum = sum

	return nil
}

func (dh *daosHistogram) MustAddSample(sampleCount uint64, sum float64, values []uint64) {
	if err := dh.AddSample(sampleCount, sum, values); err != nil {
		panic(err)
	}
}

type hashedMetricValue struct {
	metric    prometheus.Metric
	labelVals []string
}

type hashedMetrics map[uint64][]*hashedMetricValue

// daosHistogramVec is a simplified custom implementation of prometheus.HistogramVec.
// It is not designed for concurrency or currying.
type daosHistogramVec struct {
	opts       prometheus.HistogramOpts
	desc       *prometheus.Desc
	labelKeys  []string // stored here because prometheus.Desc is opaque to us
	histograms hashedMetrics
	mu         sync.RWMutex
}

func (dhv *daosHistogramVec) Describe(ch chan<- *prometheus.Desc) {
	ch <- dhv.desc
}

func (dhv *daosHistogramVec) Collect(ch chan<- prometheus.Metric) {
	dhv.mu.RLock()
	defer dhv.mu.RUnlock()

	for _, histograms := range dhv.histograms {
		for _, histogram := range histograms {
			ch <- histogram.metric
		}
	}
}

func labelVals(labels prometheus.Labels) []string {
	keys := make([]string, 0, len(labels))
	for key := range labels {
		keys = append(keys, key)
	}
	sort.Strings(keys)

	vals := make([]string, 0, len(labels))
	for _, key := range keys {
		vals = append(vals, labels[key])
	}
	return vals
}

func cmpLabelVals(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}

	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}

	return true
}

func (dhv *daosHistogramVec) addWithLabelValues(hashKey uint64, lvs []string) *hashedMetricValue {
	dh := newDaosHistogram(dhv.opts.Name, dhv.opts.Help, dhv.desc, dhv.opts.Buckets, lvs)
	hmv := &hashedMetricValue{
		metric:    dh,
		labelVals: lvs,
	}
	// NB: must be done under lock to be thread-safe
	dhv.histograms[hashKey] = append(dhv.histograms[hashKey], hmv)

	return hmv
}

func (dhv *daosHistogramVec) GetWith(labels prometheus.Labels) (*daosHistogram, error) {
	hashKey := model.LabelsToSignature(labels)

	var hmv *hashedMetricValue
	lvs := labelVals(labels)

	dhv.mu.Lock()
	_, found := dhv.histograms[hashKey]
	if !found {
		hmv = dhv.addWithLabelValues(hashKey, lvs)
	}

	if hmv == nil {
		for _, h := range dhv.histograms[hashKey] {
			if cmpLabelVals(h.labelVals, lvs) {
				hmv = h
				break
			}
		}

		if hmv == nil {
			hmv = dhv.addWithLabelValues(hashKey, lvs)
		}
	}
	dhv.mu.Unlock()

	dh, ok := hmv.metric.(*daosHistogram)
	if !ok {
		return nil, errors.New("stored something other than *daosHistogram")
	}
	return dh, nil
}

func (dhv *daosHistogramVec) With(labels prometheus.Labels) *daosHistogram {
	dh, err := dhv.GetWith(labels)
	if err != nil {
		panic(err)
	}
	return dh
}

func (dhv *daosHistogramVec) Reset() {
	dhv.mu.Lock()
	defer dhv.mu.Unlock()

	for k := range dhv.histograms {
		delete(dhv.histograms, k)
	}
}

func newDaosHistogramVec(opts prometheus.HistogramOpts, labelNames []string) *daosHistogramVec {
	return &daosHistogramVec{
		desc: prometheus.NewDesc(
			prometheus.BuildFQName(opts.Namespace, opts.Subsystem, opts.Name),
			opts.Help,
			labelNames,
			opts.ConstLabels,
		),
		opts:       opts,
		labelKeys:  labelNames,
		histograms: make(hashedMetrics),
	}
}
