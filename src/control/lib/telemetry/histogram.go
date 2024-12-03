//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build linux && (amd64 || arm64)
// +build linux
// +build amd64 arm64

package telemetry

/*
#cgo LDFLAGS: -lgurt

#include "gurt/telemetry_common.h"
#include "gurt/telemetry_consumer.h"
*/
import "C"

import (
	"github.com/pkg/errors"
)

// HistogramBucket contains the min/max values and count for a single bucket.
type HistogramBucket struct {
	Min   uint64
	Max   uint64
	Count uint64
}

// HistogramSample contains the results of a histogram sample.
type HistogramSample struct {
	Count   uint64
	Sum     uint64
	Buckets []HistogramBucket
	Values  []uint64
}

// Histogram provides access to histogram data associated with
// a parent metric.
type Histogram struct {
	parent *statsMetric
	cHist  C.struct_d_tm_histogram_t
}

func (h *Histogram) sample() (uint64, []uint64, []HistogramBucket) {
	if h.parent.handle == nil || h.parent.node == nil || h.cHist.dth_num_buckets == 0 {
		return 0, nil, nil
	}

	if h.NumBuckets() == 0 {
		return 0, nil, nil
	}

	var cBucket C.struct_d_tm_bucket_t
	var cVal C.uint64_t
	var sum uint64
	// NB: We don't need to include the final bucket as its max is +Inf,
	// and its min value can be derived from the max of the previous bucket.
	// This plays well with Prometheus Histograms... May need to be adjusted
	// if other implementations don't like this.
	buckets := make([]HistogramBucket, h.cHist.dth_num_buckets-1)
	vals := make([]uint64, len(buckets))
	for i := 0; i < len(buckets); i++ {
		res := C.d_tm_get_bucket_range(h.parent.handle.ctx, &cBucket, C.int(i), h.parent.node)
		if res != C.DER_SUCCESS {
			return 0, nil, nil
		}

		res = C.d_tm_get_counter(h.parent.handle.ctx, &cVal, cBucket.dtb_bucket)
		if res != C.DER_SUCCESS {
			return 0, nil, nil
		}

		// NB: Histogram bucket values are cumulative!
		// https://en.wikipedia.org/wiki/Histogram#Cumulative_histogram
		sum += uint64(cVal)
		buckets[i] = HistogramBucket{
			Min:   uint64(cBucket.dtb_min),
			Max:   uint64(cBucket.dtb_max),
			Count: sum,
		}
		vals[i] = buckets[i].Count
	}

	return h.parent.Sum(), vals, buckets
}

// NumBuckets returns a count of buckets in the histogram.
func (h *Histogram) NumBuckets() uint64 {
	res := C.d_tm_get_num_buckets(h.parent.handle.ctx, &h.cHist, h.parent.node)
	if res != C.DER_SUCCESS {
		return 0
	}

	return uint64(h.cHist.dth_num_buckets - 1)
}

// Buckets returns the histogram buckets.
func (h *Histogram) Buckets() []HistogramBucket {
	_, _, buckets := h.sample()
	return buckets
}

// Sample returns a point-in-time sample of the histogram.
func (h *Histogram) Sample(cur float64) *HistogramSample {
	_, vals, buckets := h.sample()

	return &HistogramSample{
		Count:   h.parent.SampleSize(),
		Sum:     uint64(cur),
		Buckets: buckets,
		Values:  vals,
	}
}

func newHistogram(parent *statsMetric) *Histogram {
	h := &Histogram{
		parent: parent,
	}

	if h.NumBuckets() == 0 {
		return nil
	}

	return h
}

func getHistogram(m Metric) *Histogram {
	if m == nil {
		return nil
	}

	switch o := m.(type) {
	case *StatsGauge:
		return o.hist
	case *Duration:
		return o.hist
	default:
		return nil
	}
}

// HasBuckets returns true if the metric has histogram data.
func HasBuckets(m Metric) bool {
	if h := getHistogram(m); h != nil {
		return h.NumBuckets() > 0
	}

	return false
}

// GetBuckets returns the histogram buckets for the metric, if available.
func GetBuckets(m Metric) ([]HistogramBucket, error) {
	if h := getHistogram(m); h != nil {
		return h.Buckets(), nil
	}

	return nil, errors.Errorf("[%s]: no histogram data", m.FullPath())
}

// SampleHistogram returns a point-in-time sample of the histogram for
// the metric, if available.
func SampleHistogram(m Metric) (*HistogramSample, error) {
	if h := getHistogram(m); h != nil {
		return h.Sample(m.FloatValue()), nil
	}

	return nil, errors.Errorf("[%s]: no histogram data", m.FullPath())
}
