//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package promexp

import (
	"fmt"
	"regexp"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/prometheus/client_golang/prometheus"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/telemetry"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestPromExp_extractClientLabels(t *testing.T) {
	shmID := 256
	jobID := "testJob"
	pid := "12345"
	tid := "67890"
	poolUUID := test.MockPoolUUID(1)
	contUUID := test.MockPoolUUID(2)

	testPath := func(suffix string) string {
		return fmt.Sprintf("ID: %d/%s/%s/%s/%s", shmID, jobID, pid, tid, suffix)
	}

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
		"weird truncation": {
			input:     "ID: 123/jobbo/6783/90",
			expLabels: labelMap{},
		},
		"active update ops": {
			input:   testPath("io/ops/update/active"),
			expName: "io_ops_update_active",
			expLabels: labelMap{
				"jobid": jobID,
				"pid":   pid,
				"tid":   tid,
			},
		},
		"fetch latency 1MB": {
			input:   testPath("io/latency/fetch/1MB"),
			expName: "io_latency_fetch",
			expLabels: labelMap{
				"jobid": jobID,
				"pid":   pid,
				"tid":   tid,
				"size":  "1MB",
			},
		},
		"started_at": {
			input:   fmt.Sprintf("ID: %d/%s/%s/started_at", shmID, jobID, pid),
			expName: "started_at",
			expLabels: labelMap{
				"jobid": jobID,
				"pid":   pid,
			},
		},
		"pool ops": {
			input:   fmt.Sprintf("ID: %d/%s/%s/pool/%s/ops/foo", shmID, jobID, pid, poolUUID),
			expName: "pool_ops_foo",
			expLabels: labelMap{
				"jobid": jobID,
				"pid":   pid,
				"pool":  poolUUID.String(),
			},
		},
		"dfs ops": {
			input:   fmt.Sprintf("ID: %d/%s/%s/pool/%s/container/%s/dfs/ops/CHMOD", shmID, jobID, pid, poolUUID, contUUID),
			expName: "dfs_ops_chmod",
			expLabels: labelMap{
				"jobid":     jobID,
				"pid":       pid,
				"pool":      poolUUID.String(),
				"container": contUUID.String(),
			},
		},
		"dfs read bytes": {
			input:   fmt.Sprintf("ID: %d/%s/%s/pool/%s/container/%s/dfs/read_bytes", shmID, jobID, pid, poolUUID, contUUID),
			expName: "dfs_read_bytes",
			expLabels: labelMap{
				"jobid":     jobID,
				"pid":       pid,
				"pool":      poolUUID.String(),
				"container": contUUID.String(),
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			labels, name := extractClientLabels(log, tc.input)

			test.AssertEqual(t, name, tc.expName, "")
			if diff := cmp.Diff(labels, tc.expLabels); diff != "" {
				t.Errorf("labels mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

func TestPromExp_NewClientCollector(t *testing.T) {
	for name, tc := range map[string]struct {
		opts      *CollectorOpts
		expErr    error
		expResult *ClientCollector
	}{
		"defaults": {
			expResult: &ClientCollector{
				metricsCollector: metricsCollector{
					summary: &prometheus.SummaryVec{
						MetricVec: &prometheus.MetricVec{},
					},
				},
			},
		},
		"opts with ignores": {
			opts: &CollectorOpts{Ignores: []string{"one", "two"}},
			expResult: &ClientCollector{
				metricsCollector: metricsCollector{
					summary: &prometheus.SummaryVec{
						MetricVec: &prometheus.MetricVec{},
					},
					ignoredMetrics: []*regexp.Regexp{
						regexp.MustCompile("one"),
						regexp.MustCompile("two"),
					},
				},
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			parent := test.MustLogContext(t, log)
			ctx, cs, err := NewClientSource(parent)
			if err != nil {
				t.Fatal(err)
			}
			defer telemetry.Fini()
			result, err := NewClientCollector(ctx, log, cs, tc.opts)

			test.CmpErr(t, tc.expErr, err)

			cmpOpts := []cmp.Option{
				cmpopts.IgnoreUnexported(MetricSource{}),
				cmpopts.IgnoreUnexported(prometheus.SummaryVec{}),
				cmpopts.IgnoreUnexported(prometheus.MetricVec{}),
				cmpopts.IgnoreUnexported(regexp.Regexp{}),
				cmp.AllowUnexported(ClientCollector{}),
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
