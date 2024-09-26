//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos_test

import (
	"encoding/json"
	"testing"
	"time"

	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
)

func TestDaos_SelfTestConfig(t *testing.T) {
	for name, tc := range map[string]struct {
		cfg    *daos.SelfTestConfig
		expErr error
	}{
		"nil config fails validation": {
			expErr: errors.New("nil"),
		},
		"imbalanced send/reply lists": {
			cfg: func() *daos.SelfTestConfig {
				cfg := new(daos.SelfTestConfig)
				cfg.SendSizes = []uint64{0, 1}
				cfg.ReplySizes = []uint64{1}
				cfg.SetDefaults()
				return cfg
			}(),
			expErr: errors.New("mismatch"),
		},
		"defaults should pass": {
			cfg: func() *daos.SelfTestConfig {
				cfg := new(daos.SelfTestConfig)
				cfg.SetDefaults()
				return cfg
			}(),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotErr := tc.cfg.Validate()
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestDaos_EndpointLatency(t *testing.T) {
	epl := new(daos.EndpointLatency)
	for i := int64(1); i <= 100; i++ {
		epl.AddValue(i)
	}
	epl.AddValue(-1)

	test.CmpAny(t, "TotalRPCs", uint64(101), epl.TotalRPCs)
	test.CmpAny(t, "Succeeded()", uint64(100), epl.Succeeded())
	test.CmpAny(t, "FailCount", uint64(1), epl.FailCount)
	test.CmpAny(t, "Min", uint64(1), epl.Min)
	test.CmpAny(t, "Max", uint64(100), epl.Max)
	test.CmpAny(t, "Sum", uint64(5050), epl.Sum)
	test.CmpAny(t, "SumSquares", float64(338350), epl.SumSquares)
	test.CmpAny(t, "Average()", float64(50.5), epl.Average())
	test.CmpAny(t, "StdDev()", float64(29.0115), epl.StdDev(), cmpopts.EquateApprox(0, 0.0001))

	keys, buckets := epl.Percentiles()
	sorted := make([]*daos.MetricBucket, len(keys))
	for i, key := range keys {
		sorted[i] = buckets[key]

		switch key {
		case 50:
			test.CmpAny(t, "50th", float64(51), buckets[key].UpperBound)
		case 75:
			test.CmpAny(t, "75th", float64(76), buckets[key].UpperBound)
		case 90:
			test.CmpAny(t, "90th", float64(91), buckets[key].UpperBound)
		case 95:
			test.CmpAny(t, "95th", float64(96), buckets[key].UpperBound)
		case 99:
			test.CmpAny(t, "99th", float64(100), buckets[key].UpperBound)
		}
	}
	test.CmpAny(t, "PercentileBuckets()", sorted, epl.PercentileBuckets())
}

func TestDaos_SelfTestResult(t *testing.T) {
	str := new(daos.SelfTestResult)

	testRank := ranklist.Rank(1)
	testTarget := uint32(0)
	testEndpoint := daos.SelfTestEndpoint{Rank: testRank, Tag: testTarget}
	str.AddTargetLatency(testRank, testTarget, 1)
	if _, found := str.TargetLatencies[testEndpoint]; found {
		t.Fatal("expected no latency for unknown endpoint")
	}

	str.TargetEndpoints = append(str.TargetEndpoints, testEndpoint)
	str.AddTargetLatency(testRank, testTarget, 1)
	if _, found := str.TargetLatencies[testEndpoint]; !found {
		t.Fatal("expected latency for known endpoint")
	}

	test.CmpAny(t, "TargetRanks()", []ranklist.Rank{testRank}, str.TargetRanks())
}

func TestDaos_SelfTestResult_MarshalJSON(t *testing.T) {
	str := &daos.SelfTestResult{
		MasterEndpoint: daos.SelfTestEndpoint{Rank: 3, Tag: 0},
		TargetEndpoints: []daos.SelfTestEndpoint{
			{Rank: 0, Tag: 0},
			{Rank: 1, Tag: 0},
			{Rank: 2, Tag: 0},
		},
		Repetitions:     3000,
		SendSize:        1024,
		ReplySize:       1024,
		BufferAlignment: -1,
		Duration:        2 * time.Second,
		MasterLatency:   &daos.EndpointLatency{},
	}

	for i := int64(1); i <= int64(str.Repetitions); i++ {
		str.MasterLatency.AddValue(i)
		str.AddTargetLatency(ranklist.Rank(i%3), 0, i)
	}

	gotBytes, err := json.MarshalIndent(str, "", "  ")
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}

	expStr := `{
  "master_endpoint": "3:0",
  "target_endpoints": [
    "0:0",
    "1:0",
    "2:0"
  ],
  "target_latencies": {
    "0:0": {
      "avg": 1501.5,
      "std_dev": 866.4583,
      "percentiles": [
        {
          "label": "50",
          "cumulative_count": 500,
          "upper_bound": 1503
        },
        {
          "label": "75",
          "cumulative_count": 750,
          "upper_bound": 2253
        },
        {
          "label": "90",
          "cumulative_count": 900,
          "upper_bound": 2703
        },
        {
          "label": "95",
          "cumulative_count": 950,
          "upper_bound": 2853
        },
        {
          "label": "99",
          "cumulative_count": 990,
          "upper_bound": 2973
        }
      ],
      "total_rpcs": 1000,
      "min": 3,
      "max": 3000,
      "fail_count": 0
    },
    "1:0": {
      "avg": 1499.5,
      "std_dev": 866.4583,
      "percentiles": [
        {
          "label": "50",
          "cumulative_count": 500,
          "upper_bound": 1501
        },
        {
          "label": "75",
          "cumulative_count": 750,
          "upper_bound": 2251
        },
        {
          "label": "90",
          "cumulative_count": 900,
          "upper_bound": 2701
        },
        {
          "label": "95",
          "cumulative_count": 950,
          "upper_bound": 2851
        },
        {
          "label": "99",
          "cumulative_count": 990,
          "upper_bound": 2971
        }
      ],
      "total_rpcs": 1000,
      "min": 1,
      "max": 2998,
      "fail_count": 0
    },
    "2:0": {
      "avg": 1500.5,
      "std_dev": 866.4583,
      "percentiles": [
        {
          "label": "50",
          "cumulative_count": 500,
          "upper_bound": 1502
        },
        {
          "label": "75",
          "cumulative_count": 750,
          "upper_bound": 2252
        },
        {
          "label": "90",
          "cumulative_count": 900,
          "upper_bound": 2702
        },
        {
          "label": "95",
          "cumulative_count": 950,
          "upper_bound": 2852
        },
        {
          "label": "99",
          "cumulative_count": 990,
          "upper_bound": 2972
        }
      ],
      "total_rpcs": 1000,
      "min": 2,
      "max": 2999,
      "fail_count": 0
    }
  },
  "rpc_count_per_second": 1500,
  "rpc_bytes_per_second": 3072000,
  "repetitions": 3000,
  "send_size": 1024,
  "reply_size": 1024,
  "buffer_alignment": -1,
  "duration": 2000000000,
  "master_latency": {
    "avg": 1500.5,
    "std_dev": 866.1697,
    "percentiles": [
      {
        "label": "50",
        "cumulative_count": 1500,
        "upper_bound": 1501
      },
      {
        "label": "75",
        "cumulative_count": 2250,
        "upper_bound": 2251
      },
      {
        "label": "90",
        "cumulative_count": 2700,
        "upper_bound": 2701
      },
      {
        "label": "95",
        "cumulative_count": 2850,
        "upper_bound": 2851
      },
      {
        "label": "99",
        "cumulative_count": 2970,
        "upper_bound": 2971
      }
    ],
    "total_rpcs": 3000,
    "min": 1,
    "max": 3000,
    "fail_count": 0
  }
}`
	test.CmpAny(t, "JSON output", expStr, string(gotBytes))
}
