//
// (C) Copyright 2021-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"encoding/json"
	"math"
	"sort"
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

// MetricType defines the different types of metrics.
type MetricType uint32

const (
	MetricTypeUnknown MetricType = iota
	MetricTypeGeneric
	MetricTypeCounter
	MetricTypeGauge
	MetricTypeSummary
	MetricTypeHistogram

	metricTypeUnknownStr   = "Unknown"
	metricTypeGenericStr   = "Generic"
	metricTypeCounterStr   = "Counter"
	metricTypeGaugeStr     = "Gauge"
	metricTypeSummaryStr   = "Summary"
	metricTypeHistogramStr = "Histogram"
)

func (t MetricType) String() string {
	switch t {
	case MetricTypeGeneric:
		return metricTypeGenericStr
	case MetricTypeCounter:
		return metricTypeCounterStr
	case MetricTypeGauge:
		return metricTypeGaugeStr
	case MetricTypeSummary:
		return metricTypeSummaryStr
	case MetricTypeHistogram:
		return metricTypeHistogramStr
	}

	return metricTypeUnknownStr
}

func metricTypeFromString(typeStr string) MetricType {
	// normalize the strings for comparison
	switch strings.ToLower(typeStr) {
	case strings.ToLower(metricTypeCounterStr):
		return MetricTypeCounter
	case strings.ToLower(metricTypeGaugeStr):
		return MetricTypeGauge
	case strings.ToLower(metricTypeSummaryStr):
		return MetricTypeSummary
	case strings.ToLower(metricTypeHistogramStr):
		return MetricTypeHistogram
	case strings.ToLower(metricTypeGenericStr):
		return MetricTypeGeneric
	}
	return MetricTypeUnknown
}

type (
	// Metric is an interface implemented by all metric types.
	Metric interface {
		IsMetric()
	}

	// MetricLabelMap is the set of key-value label pairs.
	MetricLabelMap map[string]string

	// SimpleMetric is a specific metric with a value.
	SimpleMetric struct {
		Labels MetricLabelMap `json:"labels"`
		Value  float64        `json:"value"`
	}

	// QuantileMap is the set of quantile measurements.
	QuantileMap map[float64]float64

	// SummaryMetric represents a group of observations.
	SummaryMetric struct {
		Labels      MetricLabelMap `json:"labels"`
		SampleCount uint64         `json:"sample_count"`
		SampleSum   float64        `json:"sample_sum"`
		Quantiles   QuantileMap    `json:"quantiles"`
	}

	// MetricBucket represents a bucket for observations to be sorted into.
	MetricBucket struct {
		Label           string  `json:"label"`
		CumulativeCount uint64  `json:"cumulative_count"`
		UpperBound      float64 `json:"upper_bound"`
	}

	// HistogramMetric represents a group of observations sorted into
	// buckets.
	HistogramMetric struct {
		Labels      MetricLabelMap  `json:"labels"`
		SampleCount uint64          `json:"sample_count"`
		SampleSum   float64         `json:"sample_sum"`
		Buckets     []*MetricBucket `json:"buckets"`
	}

	// MetricSet is a group of related metrics.
	MetricSet struct {
		Name        string     `json:"name"`
		Description string     `json:"description"`
		Type        MetricType `json:"type"`
		Metrics     []Metric   `json:"metrics"`
	}
)

// IsMetric identifies SimpleMetric as a Metric.
func (*SimpleMetric) IsMetric() {}

// IsMetric identifies SummaryMetric as a Metric.
func (*SummaryMetric) IsMetric() {}

// UnmarshalJSON unmarshals a SummaryMetric from JSON.
func (m *SummaryMetric) UnmarshalJSON(data []byte) error {
	if m == nil {
		return errors.New("nil SummaryMetric")
	}

	if m.Quantiles == nil {
		m.Quantiles = make(QuantileMap)
	}

	type Alias SummaryMetric
	aux := (*Alias)(m)
	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}

	return nil
}

// IsMetric identifies HistogramMetric as a Metric.
func (*HistogramMetric) IsMetric() {}

// Keys gets the sorted list of label keys.
func (m MetricLabelMap) Keys() []string {
	result := make([]string, 0, len(m))
	for label := range m {
		result = append(result, label)
	}
	sort.Strings(result)
	return result
}

// Keys gets the sorted list of quantile keys.
func (m QuantileMap) Keys() []float64 {
	result := make([]float64, 0, len(m))
	for q := range m {
		result = append(result, q)
	}
	sort.Float64s(result)
	return result
}

// MarshalJSON marshals the QuantileMap into JSON.
func (m QuantileMap) MarshalJSON() ([]byte, error) {
	strMap := make(map[string]string)

	fmtFloat := func(f float64) string {
		return strconv.FormatFloat(f, 'g', -1, 64)
	}

	for key, val := range m {
		strMap[fmtFloat(key)] = fmtFloat(val)
	}

	return json.Marshal(&strMap)
}

// UnmarshalJSON unmarshals the QuantileMap from JSON.
func (m QuantileMap) UnmarshalJSON(data []byte) error {
	if m == nil {
		return errors.New("QuantileMap is nil")
	}

	fromJSON := make(map[string]string)

	if err := json.Unmarshal(data, &fromJSON); err != nil {
		return nil
	}

	for key, val := range fromJSON {
		floatKey, err := strconv.ParseFloat(key, 64)
		if err != nil {
			return errors.Wrapf(err, "QuantileMap key %q", key)
		}

		floatVal, err := strconv.ParseFloat(val, 64)
		if err != nil {
			return errors.Wrapf(err, "QuantileMap value %q for key %q", val, key)
		}

		m[floatKey] = floatVal
	}
	return nil
}

// MarshalJSON marshals the MetricSet to JSON.
func (ms *MetricSet) MarshalJSON() ([]byte, error) {
	type toJSON MetricSet
	return json.Marshal(&struct {
		Type string `json:"type"`
		*toJSON
	}{
		Type:   strings.ToLower(ms.Type.String()),
		toJSON: (*toJSON)(ms),
	})
}

// jsonFloat is a terrible hack to deal with the stdlib's inabilility
// to deal with -Inf/+Inf/NaN: https://github.com/golang/go/issues/59627
type jsonFloat float64

func (jf jsonFloat) MarshalJSON() ([]byte, error) {
	switch {
	case math.IsInf(float64(jf), 1):
		return []byte(`"+Inf"`), nil
	case math.IsInf(float64(jf), -1):
		return []byte(`"-Inf"`), nil
	case math.IsNaN(float64(jf)):
		return []byte(`"NaN"`), nil
	}
	return json.Marshal(float64(jf))
}

func (jf *jsonFloat) UnmarshalJSON(data []byte) error {
	if err := json.Unmarshal(data, (*float64)(jf)); err == nil {
		return nil
	}

	var stringVal string
	if err := json.Unmarshal(data, &stringVal); err != nil {
		return err
	}

	val, err := strconv.ParseFloat(stringVal, 64)
	if err != nil {
		return err
	}

	*jf = jsonFloat(val)

	return nil
}

func (mb *MetricBucket) MarshalJSON() ([]byte, error) {
	type toJSON MetricBucket
	return json.Marshal(&struct {
		*toJSON
		UpperBound jsonFloat `json:"upper_bound"`
	}{
		toJSON:     (*toJSON)(mb),
		UpperBound: jsonFloat(mb.UpperBound),
	})
}

func (mb *MetricBucket) UnmarshalJSON(data []byte) error {
	type fromJSON MetricBucket

	from := &struct {
		UpperBound jsonFloat `json:"upper_bound"`
		*fromJSON
	}{
		fromJSON: (*fromJSON)(mb),
	}
	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	mb.UpperBound = float64(from.UpperBound)

	return nil
}

// jsonMetric serves as a universal metric representation for unmarshaling from
// JSON. It covers all possible fields of Metric types.
type jsonMetric struct {
	Labels      MetricLabelMap  `json:"labels"`
	Value       float64         `json:"value"`
	SampleCount uint64          `json:"sample_count"`
	SampleSum   float64         `json:"sample_sum"`
	Quantiles   QuantileMap     `json:"quantiles"`
	Buckets     []*MetricBucket `json:"buckets"`
}

// UnmarshalJSON unmarshals a Metric into the jsonMetric type.
func (jm *jsonMetric) UnmarshalJSON(data []byte) error {
	if jm == nil {
		return errors.New("nil jsonMetric")
	}

	if jm.Quantiles == nil {
		jm.Quantiles = make(QuantileMap)
	}

	type Alias jsonMetric
	aux := (*Alias)(jm)
	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}

	return nil
}

// UnmarshalJSON unmarshals the MetricSet from JSON.
func (ms *MetricSet) UnmarshalJSON(data []byte) error {
	if ms == nil {
		return errors.New("nil MetricSet")
	}

	type fromJSON MetricSet
	from := &struct {
		Type    string        `json:"type"`
		Metrics []*jsonMetric `json:"metrics"`
		*fromJSON
	}{
		fromJSON: (*fromJSON)(ms),
	}
	if err := json.Unmarshal(data, from); err != nil {
		return err
	}

	ms.Type = metricTypeFromString(from.Type)
	for _, m := range from.Metrics {
		switch ms.Type {
		case MetricTypeSummary:
			ms.Metrics = append(ms.Metrics, &SummaryMetric{
				Labels:      m.Labels,
				SampleCount: m.SampleCount,
				SampleSum:   m.SampleSum,
				Quantiles:   m.Quantiles,
			})
		case MetricTypeHistogram:
			ms.Metrics = append(ms.Metrics, &HistogramMetric{
				Labels:      m.Labels,
				SampleCount: m.SampleCount,
				SampleSum:   m.SampleSum,
				Buckets:     m.Buckets,
			})
		default:
			ms.Metrics = append(ms.Metrics, newSimpleMetric(m.Labels, m.Value))
		}
	}
	return nil
}

func newSimpleMetric(labels map[string]string, value float64) *SimpleMetric {
	return &SimpleMetric{
		Labels: labels,
		Value:  value,
	}
}
