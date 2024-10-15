//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"encoding/json"
	"fmt"
	"math"
	"sort"
	"time"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

/*
#cgo CFLAGS: -I${SRCDIR}/../../../cart/utils -I${SRCDIR}/../../../utils/self_test

#include "self_test_lib.h"
*/
import "C"

type (
	// EndpointLatency represents the results of running latency tests against
	// a single rank:target endpoint.
	EndpointLatency struct {
		rawValues  []uint64
		sorted     bool
		TotalRPCs  uint64  `json:"total_rpcs"`
		Min        uint64  `json:"min"`
		Max        uint64  `json:"max"`
		Sum        uint64  `json:"-"`
		SumSquares float64 `json:"-"`
		FailCount  uint64  `json:"fail_count"`
	}

	// SelfTestEndpoint represents a rank:target test endpoint.
	SelfTestEndpoint struct {
		Rank ranklist.Rank
		Tag  uint32
	}

	// SelfTestConfig defines the parameters for a set of self_test runs.
	SelfTestConfig struct {
		GroupName       string             `json:"group_name"`
		MasterEndpoints []SelfTestEndpoint `json:"master_endpoints,omitempty"`
		EndpointRanks   []ranklist.Rank    `json:"endpoint_ranks"`
		EndpointTags    []uint32           `json:"endpoint_tags"`
		Repetitions     uint               `json:"repetitions"`
		SendSizes       []uint64           `json:"send_sizes"`
		ReplySizes      []uint64           `json:"reply_sizes"`
		BufferAlignment int16              `json:"buffer_alignment"`
		MaxInflightRPCs uint               `json:"max_inflight_rpcs"`
	}

	// SelfTestResult represents the results of a single self_test run.
	SelfTestResult struct {
		MasterEndpoint  SelfTestEndpoint                      `json:"-"`
		TargetEndpoints []SelfTestEndpoint                    `json:"-"`
		Repetitions     uint                                  `json:"repetitions"`
		SendSize        uint64                                `json:"send_size"`
		ReplySize       uint64                                `json:"reply_size"`
		BufferAlignment int16                                 `json:"buffer_alignment"`
		Duration        time.Duration                         `json:"duration"`
		MasterLatency   *EndpointLatency                      `json:"master_latency"`
		TargetLatencies map[SelfTestEndpoint]*EndpointLatency `json:"-"`
	}
)

var defaultLatencyPercentiles []uint64 = []uint64{50, 75, 90, 95, 99}

const (
	defaultSendSize     = 1 << 10 // 1KiB
	defaultReplySize    = defaultSendSize
	defaultRepCount     = 10000
	defaultMaxInflight  = 16
	defaultBufAlignment = C.CRT_ST_BUF_ALIGN_DEFAULT
)

// SetDefaults replaces unset parameters with default values.
func (cfg *SelfTestConfig) SetDefaults() error {
	if cfg == nil {
		return errors.New("nil config")
	}

	if cfg.GroupName == "" {
		cfg.GroupName = build.DefaultSystemName
	}
	if len(cfg.EndpointTags) == 0 {
		cfg.EndpointTags = []uint32{0}
	}
	if len(cfg.SendSizes) == 0 {
		cfg.SendSizes = []uint64{defaultSendSize}
	}
	if len(cfg.ReplySizes) == 0 {
		cfg.ReplySizes = []uint64{defaultReplySize}
	}
	if cfg.Repetitions == 0 {
		cfg.Repetitions = defaultRepCount
	}
	if cfg.MaxInflightRPCs == 0 {
		cfg.MaxInflightRPCs = defaultMaxInflight
	}
	if cfg.BufferAlignment == 0 {
		cfg.BufferAlignment = defaultBufAlignment
	}

	return cfg.Validate()
}

// Validate checks the configuration for validity.
func (cfg *SelfTestConfig) Validate() error {
	if cfg == nil {
		return errors.New("nil config")
	}

	if cfg.GroupName == "" {
		return errors.New("group name is required")
	}
	if len(cfg.EndpointTags) == 0 {
		return errors.New("endpoint tag list is required")
	}
	if len(cfg.SendSizes) == 0 {
		return errors.New("send size list is required")
	}
	if len(cfg.ReplySizes) == 0 {
		return errors.New("reply size list is required")
	}
	if cfg.Repetitions == 0 {
		return errors.New("repetitions is required")
	}
	if cfg.MaxInflightRPCs == 0 {
		return errors.New("max inflight RPCs is required")
	}
	if cfg.MaxInflightRPCs == 0 {
		return errors.New("max inflight RPCs is required")
	}
	if cfg.BufferAlignment == 0 {
		return errors.New("buffer alignment is required")
	}
	if len(cfg.SendSizes) != len(cfg.ReplySizes) {
		return errors.New("send/reply size list mismatch")
	}

	return nil
}

// Copy returns a copy of the configuration.
func (cfg *SelfTestConfig) Copy() *SelfTestConfig {
	if cfg == nil {
		return nil
	}

	cp := &SelfTestConfig{}
	*cp = *cfg
	copy(cp.MasterEndpoints, cfg.MasterEndpoints)
	copy(cp.EndpointRanks, cfg.EndpointRanks)
	copy(cp.EndpointTags, cfg.EndpointTags)
	copy(cp.SendSizes, cfg.SendSizes)
	copy(cp.ReplySizes, cfg.ReplySizes)

	return cp
}

// Succeeded returns the number of RPCs that succeeded.
func (epl *EndpointLatency) Succeeded() uint64 {
	return epl.TotalRPCs - epl.FailCount
}

// AddValue adds a sampled latency value (or -1 to increment the failure count).
func (epl *EndpointLatency) AddValue(value int64) {
	epl.TotalRPCs++
	if value < 0 {
		epl.FailCount++
		return
	}

	// TODO: Figure out if there's a more clever way to do this... Seems
	// like with histograms we need to pre-bucket the values.
	epl.rawValues = append(epl.rawValues, uint64(value))
	epl.sorted = false
	if epl.TotalRPCs == 1 || value < int64(epl.Min) {
		epl.Min = uint64(value)
	}
	if value > int64(epl.Max) {
		epl.Max = uint64(value)
	}

	epl.SumSquares += float64(value) * float64(value)
	epl.Sum += uint64(value)
}

func (epl *EndpointLatency) sortValues() {
	if epl.sorted {
		return
	}

	sort.Slice(epl.rawValues, func(a, b int) bool {
		return epl.rawValues[a] < epl.rawValues[b]
	})
	epl.sorted = true
}

// Percentiles returns a sorted slice of bucket keys and a map of buckets
// holding percentile values.
func (epl *EndpointLatency) Percentiles(percentiles ...uint64) ([]uint64, map[uint64]*MetricBucket) {
	epl.sortValues()

	if len(percentiles) == 0 {
		percentiles = defaultLatencyPercentiles
	}
	sort.Slice(percentiles, func(a, b int) bool {
		return percentiles[a] < percentiles[b]
	})
	buckets := make(map[uint64]*MetricBucket)

	for _, p := range percentiles {
		valIdx := epl.Succeeded() * p / 100
		if uint64(len(epl.rawValues)) <= valIdx {
			continue
		}
		buckets[p] = &MetricBucket{
			Label:           fmt.Sprintf("%d", p),
			CumulativeCount: valIdx,
			UpperBound:      float64(epl.rawValues[valIdx]),
		}
	}

	return percentiles, buckets
}

// PercentileBuckets returns a sorted slice of buckets holding percentile values.
func (epl *EndpointLatency) PercentileBuckets(percentiles ...uint64) []*MetricBucket {
	keys, bucketMap := epl.Percentiles(percentiles...)
	buckets := make([]*MetricBucket, 0, len(bucketMap))
	for _, key := range keys {
		buckets = append(buckets, bucketMap[key])
	}

	return buckets
}

// Average returns the average latency value of successful RPCs.
func (epl *EndpointLatency) Average() float64 {
	if epl.Succeeded() == 0 {
		return 0
	}
	return float64(epl.Sum) / float64(epl.Succeeded())
}

// StdDev returns the standard deviation of the latency values of successful RPCs.
func (epl *EndpointLatency) StdDev() float64 {
	if epl.Succeeded() < 2 {
		return 0
	}
	avg := epl.Average()
	return math.Sqrt((epl.SumSquares - (float64(epl.Succeeded()) * avg * avg)) / float64(epl.Succeeded()-1))
}

func roundFloat(val float64, places int) float64 {
	return math.Round(val*math.Pow10(places)) / math.Pow10(places)
}

func (epl *EndpointLatency) MarshalJSON() ([]byte, error) {
	type toJSON EndpointLatency
	return json.Marshal(struct {
		Average     float64         `json:"avg"`
		StdDev      float64         `json:"std_dev"`
		Percentiles []*MetricBucket `json:"percentiles"`
		*toJSON
	}{
		Average:     roundFloat(epl.Average(), 4),
		StdDev:      roundFloat(epl.StdDev(), 4),
		Percentiles: epl.PercentileBuckets(),
		toJSON:      (*toJSON)(epl),
	})
}

func (ste SelfTestEndpoint) String() string {
	return fmt.Sprintf("%d:%d", ste.Rank, ste.Tag)
}

func (str *SelfTestResult) MarshalJSON() ([]byte, error) {
	epLatencies := make(map[string]*EndpointLatency)
	for ep, lr := range str.TargetLatencies {
		epLatencies[ep.String()] = lr
	}

	type toJSON SelfTestResult
	return json.Marshal(struct {
		MasterEndpoint    string                      `json:"master_endpoint"`
		TargetEndpoints   []string                    `json:"target_endpoints"`
		EndpointLatencies map[string]*EndpointLatency `json:"target_latencies,omitempty"`
		RPCThroughput     float64                     `json:"rpc_count_per_second"`
		RPCBandwidth      float64                     `json:"rpc_bytes_per_second"`
		*toJSON
	}{
		MasterEndpoint: str.MasterEndpoint.String(),
		TargetEndpoints: func() []string {
			eps := make([]string, len(str.TargetEndpoints))
			for i, ep := range str.TargetEndpoints {
				eps[i] = ep.String()
			}
			return eps
		}(),
		EndpointLatencies: epLatencies,
		RPCThroughput:     str.RPCThroughput(),
		RPCBandwidth:      str.RPCBandwidth(),
		toJSON:            (*toJSON)(str),
	})
}

// AddTargetLatency adds a latency value for a target endpoint.
func (str *SelfTestResult) AddTargetLatency(rank ranklist.Rank, tag uint32, value int64) {
	var found bool
	for _, ep := range str.TargetEndpoints {
		if ep.Rank == rank && ep.Tag == tag {
			found = true
			break
		}
	}
	if !found {
		return
	}

	if str.TargetLatencies == nil {
		str.TargetLatencies = make(map[SelfTestEndpoint]*EndpointLatency)
	}

	ep := SelfTestEndpoint{
		Rank: rank,
		Tag:  tag,
	}
	epl, found := str.TargetLatencies[ep]
	if !found {
		epl = &EndpointLatency{
			rawValues: make([]uint64, 0, str.Repetitions/uint(len(str.TargetEndpoints))),
		}
		str.TargetLatencies[ep] = epl
	}

	epl.AddValue(value)
}

// TargetRanks returns a slice of target ranks in the same order
// as the configured target endpoints.
func (str *SelfTestResult) TargetRanks() (ranks []ranklist.Rank) {
	for _, ep := range str.TargetEndpoints {
		ranks = append(ranks, ep.Rank)
	}
	return
}

// RPCThroughput calculates the number of RPCs per second.
func (str *SelfTestResult) RPCThroughput() float64 {
	return float64(str.MasterLatency.Succeeded()) / str.Duration.Seconds()
}

// RPCBandwidth calculates the bytes per second value based on the number of
// RPCs sent for the duration of the test.
func (str *SelfTestResult) RPCBandwidth() float64 {
	return str.RPCThroughput() * (float64(str.SendSize) + float64(str.ReplySize))
}
