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
	"strings"
	"unicode"

	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"

	"github.com/daos-stack/daos/src/control/lib/telemetry"
)

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

func matchLabel(labels labelMap, input, match, label string) bool {
	if !strings.HasPrefix(input, match) {
		return false
	}

	splitStr := strings.SplitN(input, "_", 2)
	if len(splitStr) == 2 {
		labels[label] = splitStr[1]
		return true
	}
	return false
}

func appendName(cur, name string) string {
	if cur == "" {
		return name
	}
	return cur + "_" + name
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

type metricStat struct {
	name      string
	desc      string
	value     float64
	isCounter bool
}

func getMetricStats(baseName string, m telemetry.Metric) (stats []*metricStat) {
	ms, ok := m.(telemetry.StatsMetric)
	if !ok {
		return
	}

	for name, s := range map[string]struct {
		fn        func() float64
		desc      string
		isCounter bool
	}{
		"min": {
			fn:   func() float64 { return float64(ms.Min()) },
			desc: " (min value)",
		},
		"max": {
			fn:   func() float64 { return float64(ms.Max()) },
			desc: " (max value)",
		},
		"mean": {
			fn:   ms.Mean,
			desc: " (mean)",
		},
		"sum": {
			fn:   func() float64 { return float64(ms.Sum()) },
			desc: " (sum)",
		},
		"stddev": {
			fn:   ms.StdDev,
			desc: " (std dev)",
		},
		"sumsquares": {
			fn:   ms.SumSquares,
			desc: " (sum of squares)",
		},
		"samples": {
			fn:        func() float64 { return float64(ms.SampleSize()) },
			desc:      " (samples)",
			isCounter: true,
		},
	} {
		stats = append(stats, &metricStat{
			name:      baseName + "_" + name,
			desc:      m.Desc() + s.desc,
			value:     s.fn(),
			isCounter: s.isCounter,
		})
	}

	return
}
