//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/google/uuid"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestPoolCreate(t *testing.T) {
	for name, tc := range map[string]struct {
		mic         *control.MockInvokerConfig
		expRC       int
		expSvcRanks []uint32
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{
					SvcReps: []uint32{0, 1, 2},
				}),
			},
			expRC:       0,
			expSvcRanks: []uint32{0, 1, 2},
		},
		"failure - no space": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoSpace,
			},
			expRC: int(daos.NoSpace),
		},
		"failure - permission denied": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoPermission,
			},
			expRC: int(daos.NoPermission),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			// Create mock invoker and test context
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			// Allocate test types for outputs
			poolUUID := newTestUUID()
			svc := newTestRankList(3) // Allocate space for 3 ranks
			defer svc.free()

			// Call the exported function via wrapper
			rc := callPoolCreate(
				handle,
				1000,  // uid
				1000,  // gid
				1<<30, // 1GB SCM
				0,     // 0 NVMe
				svc,
				poolUUID,
			)

			// Verify return code
			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}

			// If we expected an error, we're done
			if tc.expRC != 0 {
				return
			}

			// Verify service ranks were copied
			if len(tc.expSvcRanks) > 0 {
				if svc.nr() != uint32(len(tc.expSvcRanks)) {
					t.Fatalf("expected %d svc ranks, got %d", len(tc.expSvcRanks), svc.nr())
				}
				for i, expRank := range tc.expSvcRanks {
					gotRank := svc.getRank(uint32(i))
					if gotRank != expRank {
						t.Fatalf("svc rank[%d]: expected %d, got %d", i, expRank, gotRank)
					}
				}
			}
		})
	}
}

func TestPoolCreateInvalidHandle(t *testing.T) {
	poolUUID := newTestUUID()

	// Call with invalid handle (0)
	rc := callPoolCreateInvalidHandle(poolUUID)

	// Should return error for invalid handle
	if rc == 0 {
		t.Fatal("expected error for invalid handle, got success")
	}
}

func TestUUIDConversion(t *testing.T) {
	// Test UUID round-trip conversion
	testUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")

	tu := newTestUUID()
	tu.set(testUUID)

	gotUUID := tu.get()
	if gotUUID != testUUID {
		t.Fatalf("UUID round-trip failed: expected %s, got %s", testUUID, gotUUID)
	}
}

func TestUUIDFromCNil(t *testing.T) {
	// The uuidFromC function should handle nil input
	// We can't directly test nil with the wrapper, but we verify the wrapper works
	tu := newTestUUID()
	// Zero UUID should round-trip correctly
	tu.set(uuid.Nil)
	gotUUID := tu.get()
	if gotUUID != uuid.Nil {
		t.Fatalf("expected nil UUID, got %s", gotUUID)
	}
}

func TestRankListConversion(t *testing.T) {
	for name, tc := range map[string]struct {
		ranks []uint32
	}{
		"empty": {
			ranks: nil,
		},
		"single rank": {
			ranks: []uint32{5},
		},
		"multiple ranks": {
			ranks: []uint32{0, 1, 2, 3, 4},
		},
	} {
		t.Run(name, func(t *testing.T) {
			if len(tc.ranks) == 0 {
				// Test nil input via empty rank list
				rl := newTestRankList(0)
				defer rl.free()

				got := testConvertRankListFromC(rl)
				if len(got) != 0 {
					t.Fatalf("expected empty slice, got %v", got)
				}
				return
			}

			// Allocate and populate rank list
			rl := newTestRankList(uint32(len(tc.ranks)))
			if rl.ptr() == nil {
				t.Fatal("failed to allocate rank list")
			}
			defer rl.free()

			for i, r := range tc.ranks {
				rl.setRank(uint32(i), r)
			}

			// Convert to Go and verify
			goRanks := testConvertRankListFromC(rl)
			if len(goRanks) != len(tc.ranks) {
				t.Fatalf("expected %d ranks, got %d", len(tc.ranks), len(goRanks))
			}

			for i, exp := range tc.ranks {
				if goRanks[i] != exp {
					t.Fatalf("rank[%d]: expected %d, got %d", i, exp, goRanks[i])
				}
			}

			// Now test copying back to C
			outRL := newTestRankList(uint32(len(tc.ranks)))
			if outRL.ptr() == nil {
				t.Fatal("failed to allocate output rank list")
			}
			defer outRL.free()

			testConvertRankListToC(goRanks, outRL)

			if outRL.nr() != uint32(len(tc.ranks)) {
				t.Fatalf("expected rl_nr %d, got %d", len(tc.ranks), outRL.nr())
			}

			for i, exp := range tc.ranks {
				got := outRL.getRank(uint32(i))
				if got != exp {
					t.Fatalf("output rank[%d]: expected %d, got %d", i, exp, got)
				}
			}
		})
	}
}

func TestGoString(t *testing.T) {
	for name, tc := range map[string]struct {
		input    string
		makeNil  bool
		expected string
	}{
		"nil": {
			makeNil:  true,
			expected: "",
		},
		"empty": {
			input:    "",
			expected: "",
		},
		"simple": {
			input:    "hello",
			expected: "hello",
		},
		"with spaces": {
			input:    "hello world",
			expected: "hello world",
		},
	} {
		t.Run(name, func(t *testing.T) {
			var cs *testCString
			if tc.makeNil {
				cs = &testCString{cstr: nil}
			} else {
				cs = newTestCString(tc.input)
				defer cs.free()
			}

			got := cs.toGo()
			if got != tc.expected {
				t.Fatalf("expected %q, got %q", tc.expected, got)
			}
		})
	}
}

func TestPoolDestroy(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		force bool
		expRC int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolDestroyResp{}),
			},
			expRC: 0,
		},
		"success with force": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolDestroyResp{}),
			},
			force: true,
			expRC: 0,
		},
		"failure - pool not found": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Nonexistent,
			},
			expRC: int(daos.Nonexistent),
		},
		"failure - busy": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Busy,
			},
			expRC: int(daos.Busy),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolDestroy(handle, poolUUID, tc.force)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolEvict(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		expRC int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolEvictResp{}),
			},
			expRC: 0,
		},
		"failure - pool not found": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Nonexistent,
			},
			expRC: int(daos.Nonexistent),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolEvict(handle, poolUUID)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolExclude(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *control.MockInvokerConfig
		rank   uint32
		tgtIdx int
		expRC  int
	}{
		"success - rank only": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolExcludeResp{}),
			},
			rank:   0,
			tgtIdx: -1, // no target index
			expRC:  0,
		},
		"success - rank and target": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolExcludeResp{}),
			},
			rank:   1,
			tgtIdx: 2,
			expRC:  0,
		},
		"failure - response error": {
			// Note: Status errors get wrapped by resp.Errors() into a generic error,
			// which maps to MiscError. The original status is not preserved.
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolExcludeResp{
					Status: int32(daos.Nonexistent),
				}),
			},
			rank:   0,
			tgtIdx: -1,
			expRC:  int(daos.MiscError),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolExclude(handle, poolUUID, tc.rank, tc.tgtIdx)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolDrain(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *control.MockInvokerConfig
		rank   uint32
		tgtIdx int
		expRC  int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolDrainResp{}),
			},
			rank:   0,
			tgtIdx: -1,
			expRC:  0,
		},
		"failure - response error": {
			// Note: Status errors get wrapped by resp.Errors() into a generic error,
			// which maps to MiscError. The original status is not preserved.
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolDrainResp{
					Status: int32(daos.Busy),
				}),
			},
			rank:   0,
			tgtIdx: -1,
			expRC:  int(daos.MiscError),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolDrain(handle, poolUUID, tc.rank, tc.tgtIdx)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolReintegrate(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *control.MockInvokerConfig
		rank   uint32
		tgtIdx int
		expRC  int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolReintResp{}),
			},
			rank:   0,
			tgtIdx: -1,
			expRC:  0,
		},
		"failure - response error": {
			// Note: Status errors get wrapped by resp.Errors() into a generic error,
			// which maps to MiscError. The original status is not preserved.
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolReintResp{
					Status: int32(daos.Busy),
				}),
			},
			rank:   0,
			tgtIdx: -1,
			expRC:  int(daos.MiscError),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolReintegrate(handle, poolUUID, tc.rank, tc.tgtIdx)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolOperationsInvalidHandle(t *testing.T) {
	poolUUID := newTestUUID()
	poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

	// All pool operations should fail with invalid handle
	tests := []struct {
		name string
		fn   func() int
	}{
		{"destroy", func() int { return callPoolDestroy(0, poolUUID, false) }},
		{"evict", func() int { return callPoolEvict(0, poolUUID) }},
		{"exclude", func() int { return callPoolExclude(0, poolUUID, 0, -1) }},
		{"drain", func() int { return callPoolDrain(0, poolUUID, 0, -1) }},
		{"reintegrate", func() int { return callPoolReintegrate(0, poolUUID, 0, -1) }},
		{"extend", func() int { return callPoolExtend(0, poolUUID, []uint32{1, 2}) }},
		{"set_prop", func() int { return callPoolSetProp(0, poolUUID, "label", "test") }},
		{"get_prop", func() int { _, rc := callPoolGetProp(0, "", poolUUID, "label"); return rc }},
		{"update_ace", func() int { return callPoolUpdateACE(0, poolUUID, "A::user@:rw") }},
		{"delete_ace", func() int { return callPoolDeleteACE(0, poolUUID, "user@") }},
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

func TestPoolExtend(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		ranks []uint32
		expRC int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolExtendResp{}),
			},
			ranks: []uint32{1, 2},
			expRC: 0,
		},
		"failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoSpace,
			},
			ranks: []uint32{1},
			expRC: int(daos.NoSpace),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolExtend(handle, poolUUID, tc.ranks)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolSetProp(t *testing.T) {
	for name, tc := range map[string]struct {
		mic      *control.MockInvokerConfig
		propName string
		propVal  string
		expRC    int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolSetPropResp{}),
			},
			propName: "label",
			propVal:  "testpool",
			expRC:    0,
		},
		"invalid property name": {
			mic:      &control.MockInvokerConfig{},
			propName: "invalid_prop_name",
			propVal:  "value",
			expRC:    int(daos.InvalidInput),
		},
		"failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoPermission,
			},
			propName: "label",
			propVal:  "testpool",
			expRC:    int(daos.NoPermission),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolSetProp(handle, poolUUID, tc.propName, tc.propVal)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolGetProp(t *testing.T) {
	for name, tc := range map[string]struct {
		mic      *control.MockInvokerConfig
		label    string
		propName string
		expVal   string
		expRC    int
	}{
		"success with UUID": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolGetPropResp{
					Properties: []*mgmtpb.PoolProperty{
						{Number: 1, Value: &mgmtpb.PoolProperty_Strval{Strval: "mypool"}},
					},
				}),
			},
			propName: "label",
			expVal:   "mypool",
			expRC:    0,
		},
		"success with label": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolGetPropResp{
					Properties: []*mgmtpb.PoolProperty{
						{Number: 1, Value: &mgmtpb.PoolProperty_Strval{Strval: "mypool"}},
					},
				}),
			},
			label:    "mypool",
			propName: "label",
			expVal:   "mypool",
			expRC:    0,
		},
		"invalid property name": {
			mic:      &control.MockInvokerConfig{},
			propName: "invalid_prop_name",
			expRC:    int(daos.InvalidInput),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			val, rc := callPoolGetProp(handle, tc.label, poolUUID, tc.propName)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}

			if rc == 0 && val != tc.expVal {
				t.Fatalf("expected value %q, got %q", tc.expVal, val)
			}
		})
	}
}

func TestPoolUpdateACE(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		ace   string
		expRC int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ACLResp{}),
			},
			ace:   "A::user@:rw",
			expRC: 0,
		},
		"failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoPermission,
			},
			ace:   "A::user@:rw",
			expRC: int(daos.NoPermission),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolUpdateACE(handle, poolUUID, tc.ace)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolDeleteACE(t *testing.T) {
	for name, tc := range map[string]struct {
		mic       *control.MockInvokerConfig
		principal string
		expRC     int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ACLResp{}),
			},
			principal: "user@",
			expRC:     0,
		},
		"failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoPermission,
			},
			principal: "user@",
			expRC:     int(daos.NoPermission),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolDeleteACE(handle, poolUUID, tc.principal)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolRebuildStop(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		force bool
		expRC int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.DaosResp{}),
			},
			expRC: 0,
		},
		"success with force": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.DaosResp{}),
			},
			force: true,
			expRC: 0,
		},
		"failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Busy,
			},
			expRC: int(daos.Busy),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolRebuildStop(handle, poolUUID, tc.force)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolRebuildStart(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		expRC int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.DaosResp{}),
			},
			expRC: 0,
		},
		"failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Busy,
			},
			expRC: int(daos.Busy),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			poolUUID := newTestUUID()
			poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

			rc := callPoolRebuildStart(handle, poolUUID)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestPoolList(t *testing.T) {
	for name, tc := range map[string]struct {
		mic       *control.MockInvokerConfig
		npoolsIn  uint64
		expNpools uint64
		expRC     int
	}{
		"success - count only": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
					Pools: []*mgmtpb.ListPoolsResp_Pool{
						{Uuid: "12345678-1234-1234-1234-123456789abc", Label: "pool1"},
						{Uuid: "22345678-1234-1234-1234-123456789abc", Label: "pool2"},
					},
				}),
			},
			npoolsIn:  0,
			expNpools: 2,
			expRC:     0,
		},
		"failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoPermission,
			},
			expRC: int(daos.NoPermission),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			npools := tc.npoolsIn
			rc := callPoolList(handle, &npools, nil)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}

			if rc == 0 && npools != tc.expNpools {
				t.Fatalf("expected %d pools, got %d", tc.expNpools, npools)
			}
		})
	}
}

func TestPoolListWithData(t *testing.T) {
	// Test that pool list properly populates the output structures
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mic := &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
			Pools: []*mgmtpb.ListPoolsResp_Pool{
				{
					Uuid:         "12345678-1234-1234-1234-123456789abc",
					Label:        "pool1",
					SvcReps:      []uint32{0, 1, 2},
					RebuildState: "idle",
					State:        "Ready",
				},
				{
					Uuid:         "22345678-1234-1234-1234-123456789abc",
					Label:        "pool2",
					SvcReps:      []uint32{0},
					RebuildState: "idle",
					State:        "Ready",
				},
			},
		}),
	}

	mi := control.NewMockInvoker(log, mic)
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	// First get count
	var npools uint64
	rc := callPoolList(handle, &npools, nil)
	if rc != 0 {
		t.Fatalf("expected RC 0, got %d", rc)
	}
	if npools != 2 {
		t.Fatalf("expected 2 pools, got %d", npools)
	}

	// Now get with data - need a fresh invoker since mock is consumed
	mi = control.NewMockInvoker(log, mic)
	handle2 := makeTestHandle(mi, log)
	defer handle2.Delete()

	pools := newTestPoolListInfo(int(npools))
	defer pools.free()

	npools = 2 // Set capacity
	rc = callPoolList(handle2, &npools, pools)
	if rc != 0 {
		t.Fatalf("expected RC 0, got %d", rc)
	}

	// Verify pool data was populated
	expUUID1 := uuid.MustParse("12345678-1234-1234-1234-123456789abc")
	gotUUID1 := pools.getUUID(0)
	if gotUUID1 != expUUID1 {
		t.Fatalf("pool[0] UUID: expected %s, got %s", expUUID1, gotUUID1)
	}

	gotLabel1 := pools.getLabel(0)
	if gotLabel1 != "pool1" {
		t.Fatalf("pool[0] label: expected 'pool1', got %q", gotLabel1)
	}

	expUUID2 := uuid.MustParse("22345678-1234-1234-1234-123456789abc")
	gotUUID2 := pools.getUUID(1)
	if gotUUID2 != expUUID2 {
		t.Fatalf("pool[1] UUID: expected %s, got %s", expUUID2, gotUUID2)
	}

	gotLabel2 := pools.getLabel(1)
	if gotLabel2 != "pool2" {
		t.Fatalf("pool[1] label: expected 'pool2', got %q", gotLabel2)
	}
}

func TestPoolListBufferTooSmall(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mic := &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
			Pools: []*mgmtpb.ListPoolsResp_Pool{
				{Uuid: "12345678-1234-1234-1234-123456789abc", Label: "pool1"},
				{Uuid: "22345678-1234-1234-1234-123456789abc", Label: "pool2"},
				{Uuid: "32345678-1234-1234-1234-123456789abc", Label: "pool3"},
			},
		}),
	}

	mi := control.NewMockInvoker(log, mic)
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	// Allocate buffer for only 1 pool but there are 3
	pools := newTestPoolListInfo(1)
	defer pools.free()

	npools := uint64(1) // Capacity is 1
	rc := callPoolList(handle, &npools, pools)

	// Should return buffer too small error
	if rc != int(daos.BufTooSmall) {
		t.Fatalf("expected RC %d (BufTooSmall), got %d", int(daos.BufTooSmall), rc)
	}
}

func TestPoolListInvalidHandle(t *testing.T) {
	var npools uint64
	rc := callPoolList(0, &npools, nil)
	if rc == 0 {
		t.Fatal("expected error for invalid handle, got success")
	}
}

func TestPoolRebuildInvalidHandle(t *testing.T) {
	poolUUID := newTestUUID()
	poolUUID.set(uuid.MustParse("12345678-1234-1234-1234-123456789abc"))

	rc := callPoolRebuildStop(0, poolUUID, false)
	if rc == 0 {
		t.Fatal("expected error for invalid handle on rebuild stop, got success")
	}

	rc = callPoolRebuildStart(0, poolUUID)
	if rc == 0 {
		t.Fatal("expected error for invalid handle on rebuild start, got success")
	}
}
