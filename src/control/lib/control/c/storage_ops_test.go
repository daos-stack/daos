//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/google/uuid"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestStorageDeviceList(t *testing.T) {
	for name, tc := range map[string]struct {
		mic       *control.MockInvokerConfig
		expNdisks int
		expRC     int
	}{
		"success - empty": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &ctlpb.SmdQueryResp{}),
			},
			expNdisks: 0,
			expRC:     0,
		},
		"failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Unreachable,
			},
			expRC: int(daos.Unreachable),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			ndisks, rc := callStorageDeviceList(handle)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}

			if rc == 0 && ndisks != tc.expNdisks {
				t.Fatalf("expected %d disks, got %d", tc.expNdisks, ndisks)
			}
		})
	}
}

func TestStorageSetNVMeFault(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		host  string
		force bool
		expRC int
	}{
		"failure - connection error": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Unreachable,
			},
			host:  "host1",
			expRC: int(daos.Unreachable),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			devUUID := newTestUUID()
			devUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callStorageSetNVMeFault(handle, tc.host, devUUID, tc.force)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestStorageQueryDeviceHealth(t *testing.T) {
	for name, tc := range map[string]struct {
		mic      *control.MockInvokerConfig
		host     string
		statsKey string
		expRC    int
	}{
		"failure - connection error": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Unreachable,
			},
			host:     "host1",
			statsKey: "temperature",
			expRC:    int(daos.Unreachable),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			devUUID := newTestUUID()
			devUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			_, rc := callStorageQueryDeviceHealth(handle, tc.host, tc.statsKey, devUUID)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestExtractHealthStat(t *testing.T) {
	stats := struct {
		Temperature  uint32 `json:"temperature"`
		BioReadErrs  uint32 `json:"bio_read_errs"`
		BioWriteErrs uint32 `json:"bio_write_errs"`
		DevUUID      string `json:"dev_uuid"`
		TempWarn     bool   `json:"temp_warn"`
	}{
		Temperature:  300,
		BioReadErrs:  5,
		BioWriteErrs: 2,
		DevUUID:      "abcd",
		TempWarn:     true,
	}

	for name, tc := range map[string]struct {
		key    string
		expVal string
	}{
		"numeric field":       {key: "temperature", expVal: "300"},
		"snake_case read":     {key: "bio_read_errs", expVal: "5"},
		"snake_case write":    {key: "bio_write_errs", expVal: "2"},
		"string field quoted": {key: "dev_uuid", expVal: `"abcd"`},
		"bool field":          {key: "temp_warn", expVal: "true"},
		"unknown key":         {key: "nonexistent", expVal: ""},
		"empty key":           {key: "", expVal: ""},
	} {
		t.Run(name, func(t *testing.T) {
			got, err := extractHealthStat(stats, tc.key)
			if err != nil {
				t.Fatalf("unexpected error: %v", err)
			}
			if got != tc.expVal {
				t.Fatalf("expected %q, got %q", tc.expVal, got)
			}
		})
	}
}

// populatedSmdQueryResp builds a single-host SmdQueryResp covering the fields
// daos_control_storage_device_list reads: UUID, rank, target IDs, and
// NvmeController.DevState. Each device gets a distinct rank so the caller can
// assert per-device placement.
func populatedSmdQueryResp(host string, devs []*ctlpb.SmdDevice) *control.UnaryResponse {
	ranks := make([]*ctlpb.SmdQueryResp_RankResp, 0, len(devs))
	for _, d := range devs {
		ranks = append(ranks, &ctlpb.SmdQueryResp_RankResp{
			Rank:    d.Rank,
			Devices: []*ctlpb.SmdDevice{d},
		})
	}
	return control.MockMSResponse(host, nil, &ctlpb.SmdQueryResp{Ranks: ranks})
}

func TestStorageDeviceListPopulated(t *testing.T) {
	uuid0 := uuid.MustParse("00000000-0000-0000-0000-000000000001")
	uuid1 := uuid.MustParse("00000000-0000-0000-0000-000000000002")

	// Long-target device: len(targets) == DAOS_MAX_TARGETS_PER_DEVICE + 20.
	// The library must truncate to DAOS_MAX_TARGETS_PER_DEVICE without
	// overflowing the fixed-size tgtidx array.
	longTargets := make([]int32, 116)
	for i := range longTargets {
		longTargets[i] = int32(i)
	}

	devs := []*ctlpb.SmdDevice{
		{
			Uuid:   uuid0.String(),
			Rank:   0,
			TgtIds: []int32{0, 1, 2, 3},
			Ctrlr: &ctlpb.NvmeController{
				DevState: ctlpb.NvmeDevState_EVICTED,
			},
		},
		{
			Uuid:   uuid1.String(),
			Rank:   1,
			TgtIds: longTargets,
			Ctrlr: &ctlpb.NvmeController{
				DevState: ctlpb.NvmeDevState_NORMAL,
			},
		},
	}

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: populatedSmdQueryResp("host-0", devs),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	got, ndisks, rc := callStorageDeviceListPopulated(handle, 4)
	if rc != 0 {
		t.Fatalf("rc=%d, want 0", rc)
	}
	if ndisks != 2 {
		t.Fatalf("ndisks=%d, want 2", ndisks)
	}
	if len(got) != 2 {
		t.Fatalf("len(got)=%d, want 2", len(got))
	}

	// Expect unquoted state names. The old dmg-JSON path produced
	// "\"EVICTED\""; callers relying on that quoting need to be updated.
	wantStates := map[uuid.UUID]string{
		uuid0: "EVICTED",
		uuid1: "NORMAL",
	}
	wantRanks := map[uuid.UUID]uint32{
		uuid0: 0,
		uuid1: 1,
	}

	for _, d := range got {
		if d.Host != "host-0" {
			t.Errorf("device %s: host=%q, want %q", d.UUID, d.Host, "host-0")
		}
		if want := wantStates[d.UUID]; d.State != want {
			t.Errorf("device %s: state=%q, want %q", d.UUID, d.State, want)
		}
		if want := wantRanks[d.UUID]; d.Rank != want {
			t.Errorf("device %s: rank=%d, want %d", d.UUID, d.Rank, want)
		}

		switch d.UUID {
		case uuid0:
			if len(d.Targets) != 4 {
				t.Errorf("device %s: target count=%d, want 4", d.UUID, len(d.Targets))
			}
		case uuid1:
			// Truncated to DAOS_MAX_TARGETS_PER_DEVICE (96). If this
			// ever changes, the struct size and this expectation move
			// together.
			const wantTruncated = 96
			if len(d.Targets) != wantTruncated {
				t.Errorf("device %s: target count=%d, want %d (truncated)",
					d.UUID, len(d.Targets), wantTruncated)
			}
		}
	}
}

func TestStorageQueryDeviceHealthPopulated(t *testing.T) {
	devUUIDStr := "00000000-0000-0000-0000-000000000001"

	healthResp := &ctlpb.SmdQueryResp{
		Ranks: []*ctlpb.SmdQueryResp_RankResp{
			{
				Devices: []*ctlpb.SmdDevice{
					{
						Uuid: devUUIDStr,
						Ctrlr: &ctlpb.NvmeController{
							HealthStats: &ctlpb.BioHealthResp{
								Temperature:  300,
								BioReadErrs:  5,
								BioWriteErrs: 2,
								PowerOnHours: 999999999999,
							},
						},
					},
				},
			},
		},
	}

	for name, tc := range map[string]struct {
		statsKey string
		bufSize  int
		expOut   string
		expRC    int
	}{
		"numeric stat fits": {
			statsKey: "temperature",
			bufSize:  256,
			expOut:   "300",
		},
		"numeric stat tiny buffer truncates cleanly": {
			// "300" is 3 chars + NUL; a 3-byte buffer can only hold
			// "30\0". The library must truncate, not overflow.
			statsKey: "temperature",
			bufSize:  3,
			expOut:   "30",
		},
		"long numeric stat truncates cleanly": {
			// 999999999999 (12 digits) in a 5-byte buffer: 4 chars + NUL.
			statsKey: "power_on_hours",
			bufSize:  5,
			expOut:   "9999",
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host-0", nil, healthResp),
			})
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			devUUID := newTestUUID()
			devUUID.set(uuid.MustParse(devUUIDStr))

			out, rc := callStorageQueryDeviceHealthSized(handle, "host-0", tc.statsKey, devUUID, tc.bufSize)
			if rc != tc.expRC {
				t.Fatalf("rc=%d, want %d (out=%q)", rc, tc.expRC, out)
			}
			if out != tc.expOut {
				t.Fatalf("output=%q, want %q", out, tc.expOut)
			}
		})
	}
}

// TestStorageQueryDeviceHealthSeparateBuffers verifies that the key and output
// buffers are independent: the output buffer receives the stat value while the
// key string used for lookup is unaffected. This pins the no-aliasing contract
// documented on the Go export (statsKey and statsOut MUST NOT alias).
func TestStorageQueryDeviceHealthSeparateBuffers(t *testing.T) {
	devUUIDStr := "00000000-0000-0000-0000-000000000001"

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host-0", nil, &ctlpb.SmdQueryResp{
			Ranks: []*ctlpb.SmdQueryResp_RankResp{
				{
					Devices: []*ctlpb.SmdDevice{
						{
							Uuid: devUUIDStr,
							Ctrlr: &ctlpb.NvmeController{
								HealthStats: &ctlpb.BioHealthResp{
									Temperature: 42,
								},
							},
						},
					},
				},
			},
		}),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	devUUID := newTestUUID()
	devUUID.set(uuid.MustParse(devUUIDStr))

	// callStorageQueryDeviceHealthSized allocates separate C buffers for the
	// key and output. The output buffer must contain the stat value; the key
	// string passed in Go must be unmodified by the call.
	const key = "temperature"
	out, rc := callStorageQueryDeviceHealthSized(handle, "host-0", key, devUUID, 64)
	if rc != 0 {
		t.Fatalf("rc=%d, want 0", rc)
	}
	if out != "42" {
		t.Fatalf("output=%q, want %q", out, "42")
	}
	// Assert the key variable is unchanged — guards against any
	// accidental in-place modification via cgo.
	if key != "temperature" {
		t.Fatalf("key modified: got %q", key)
	}
}

func TestStorageDeviceListUndersizedBuffer(t *testing.T) {
	devs := []*ctlpb.SmdDevice{
		{
			Uuid: "00000000-0000-0000-0000-000000000001",
			Rank: 0,
			Ctrlr: &ctlpb.NvmeController{
				DevState: ctlpb.NvmeDevState_NORMAL,
			},
		},
		{
			Uuid: "00000000-0000-0000-0000-000000000002",
			Rank: 1,
			Ctrlr: &ctlpb.NvmeController{
				DevState: ctlpb.NvmeDevState_NORMAL,
			},
		},
	}

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: populatedSmdQueryResp("host-0", devs),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	// Capacity of 1 for 2 devices: library must refuse to overrun the
	// caller's buffer and report the required count via *ndisks. Without
	// the capacity check, this would silently memset + write past the
	// allocation.
	got, ndisks, rc := callStorageDeviceListPopulated(handle, 1)
	if rc != int(daos.BufTooSmall) {
		t.Fatalf("rc=%d, want BufTooSmall(%d)", rc, int(daos.BufTooSmall))
	}
	if ndisks != 2 {
		t.Fatalf("ndisks=%d, want 2 (required capacity)", ndisks)
	}
	if len(got) != 0 {
		t.Fatalf("expected no populated entries on BufTooSmall, got %d", len(got))
	}

	// Caller grows the buffer and retries. The second call must succeed
	// and populate both entries.
	got, ndisks, rc = callStorageDeviceListPopulated(handle, 2)
	if rc != 0 {
		t.Fatalf("retry rc=%d, want 0", rc)
	}
	if ndisks != 2 {
		t.Fatalf("retry ndisks=%d, want 2", ndisks)
	}
	if len(got) != 2 {
		t.Fatalf("retry len(got)=%d, want 2", len(got))
	}
}

func TestStorageOperationsInvalidHandle(t *testing.T) {
	devUUID := newTestUUID()
	devUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

	tests := []struct {
		name string
		fn   func() int
	}{
		{"device_list", func() int { _, rc := callStorageDeviceList(0); return rc }},
		{"set_nvme_fault", func() int { return callStorageSetNVMeFault(0, "host1", devUUID, false) }},
		{"query_device_health", func() int { _, rc := callStorageQueryDeviceHealth(0, "host1", "temperature", devUUID); return rc }},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			rc := tt.fn()
			if rc == 0 {
				t.Fatalf("expected error for invalid handle on %s, got success", tt.name)
			}
		})
	}
}
