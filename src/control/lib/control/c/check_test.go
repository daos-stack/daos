//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"errors"
	"strings"
	"testing"

	"github.com/google/uuid"

	chk "github.com/daos-stack/daos/src/control/common/proto/chk"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

// TestParsePolicies pins two properties of parsePolicies on malformed input:
// (1) the returned error names the offending element so operators can tell
// which policy was bad from the log, and (2) it still reduces to
// daos.InvalidInput at the cgo boundary via errors.Is.
func TestParsePolicies(t *testing.T) {
	for name, tc := range map[string]struct {
		input     string
		expErrSub string // substring expected in err.Error()
	}{
		"missing separator": {
			input:     "POOL_BAD_LABEL",
			expErrSub: `policy[0] "POOL_BAD_LABEL"`,
		},
		"bad class/action": {
			input:     "BOGUS:NONSENSE",
			expErrSub: `policy[0] "BOGUS:NONSENSE"`,
		},
		"second element bad": {
			input:     "CIC_POOL_NONEXIST_ON_MS:CIA_IGNORE,bogus-entry",
			expErrSub: `policy[1] "bogus-entry"`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			_, err := parsePolicies(tc.input)
			if err == nil {
				t.Fatalf("expected error for %q", tc.input)
			}
			if !strings.Contains(err.Error(), tc.expErrSub) {
				t.Errorf("error %q missing %q", err.Error(), tc.expErrSub)
			}
			if !errors.Is(err, daos.InvalidInput) {
				t.Errorf("errors.Is(daos.InvalidInput) false for %v", err)
			}
		})
	}
}

func TestCheckSwitch(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *control.MockInvokerConfig
		enable bool
		expRC  int
	}{
		"enable success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.DaosResp{}),
			},
			enable: true,
			expRC:  0,
		},
		"disable success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.DaosResp{}),
			},
			enable: false,
			expRC:  0,
		},
		"enable failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoPermission,
			},
			enable: true,
			expRC:  int(daos.NoPermission),
		},
		"disable failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoPermission,
			},
			enable: false,
			expRC:  int(daos.NoPermission),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			rc := callCheckSwitch(handle, tc.enable)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestCheckStart(t *testing.T) {
	testUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")

	for name, tc := range map[string]struct {
		mic       *control.MockInvokerConfig
		flags     uint32
		poolUUIDs []uuid.UUID
		policies  string
		expRC     int
	}{
		"success - no pools": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.CheckStartResp{}),
			},
			expRC: 0,
		},
		"success - with pools": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.CheckStartResp{}),
			},
			poolUUIDs: []uuid.UUID{testUUID},
			expRC:     0,
		},
		"success - with policies": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.CheckStartResp{}),
			},
			policies: "CIC_POOL_BAD_LABEL:CIA_INTERACT",
			expRC:    0,
		},
		"failure - invalid policy format": {
			mic:      &control.MockInvokerConfig{},
			policies: "invalid",
			expRC:    int(daos.InvalidInput),
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

			rc := callCheckStart(handle, tc.flags, tc.poolUUIDs, tc.policies)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestCheckStop(t *testing.T) {
	testUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")

	for name, tc := range map[string]struct {
		mic       *control.MockInvokerConfig
		poolUUIDs []uuid.UUID
		expRC     int
	}{
		"success - no pools": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.CheckStopResp{}),
			},
			expRC: 0,
		},
		"success - with pools": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.CheckStopResp{}),
			},
			poolUUIDs: []uuid.UUID{testUUID},
			expRC:     0,
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

			rc := callCheckStop(handle, tc.poolUUIDs)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestCheckQuery(t *testing.T) {
	testUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")

	for name, tc := range map[string]struct {
		mic       *control.MockInvokerConfig
		poolUUIDs []uuid.UUID
		expRC     int
	}{
		"success - no pools": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.CheckQueryResp{}),
			},
			expRC: 0,
		},
		"success - with pools": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.CheckQueryResp{}),
			},
			poolUUIDs: []uuid.UUID{testUUID},
			expRC:     0,
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

			rc := callCheckQuery(handle, tc.poolUUIDs)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestCheckQueryWithInfoAndFree(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mic := &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.CheckQueryResp{}),
	}

	mi := control.NewMockInvoker(log, mic)
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	dci, rc := callCheckQueryWithInfo(handle, nil)
	if rc != 0 {
		t.Fatalf("expected RC 0, got %d", rc)
	}

	// Free should not panic even on a zeroed/empty struct
	callCheckInfoFree(dci)
}

func TestCheckInfoFreeNil(t *testing.T) {
	// Should not panic with nil input
	callCheckInfoFree(nil)
}

// TestCheckQueryWithInfoPopulated exercises daos_control_check_query with a
// non-empty response: pools are only populated for UUIDs the caller asked
// about, reports always populate, and daos_control_check_info_free zeroes
// the struct's counts/pointers.
func TestCheckQueryWithInfoPopulated(t *testing.T) {
	uuidA := uuid.MustParse("10000000-0000-0000-0000-000000000001")
	uuidB := uuid.MustParse("20000000-0000-0000-0000-000000000002")
	uuidC := uuid.MustParse("30000000-0000-0000-0000-000000000003") // sent by server but not requested

	// Server-side phase/status enum values are small ints; exact name
	// strings are enum .String() — we just verify the non-empty set the
	// control layer stringifies.
	resp := &mgmtpb.CheckQueryResp{
		InsStatus: chk.CheckInstStatus_CIS_RUNNING,
		InsPhase:  chk.CheckScanPhase_CSP_POOL_MBS,
		Pools: []*mgmtpb.CheckQueryPool{
			{
				Uuid:   uuidA.String(),
				Status: chk.CheckPoolStatus_CPS_CHECKING,
				Phase:  chk.CheckScanPhase_CSP_POOL_MBS,
				// Time must be non-nil: getPoolCheckInfo in the control
				// library dereferences pbPool.Time without a guard.
				Time: &mgmtpb.CheckQueryTime{},
			},
			{
				Uuid:   uuidB.String(),
				Status: chk.CheckPoolStatus_CPS_CHECKED,
				Phase:  chk.CheckScanPhase_CSP_DONE,
				Time:   &mgmtpb.CheckQueryTime{},
			},
			{
				Uuid:   uuidC.String(),
				Status: chk.CheckPoolStatus_CPS_CHECKING,
				Time:   &mgmtpb.CheckQueryTime{},
			},
		},
		Reports: []*chk.CheckReport{
			{
				Seq:      42,
				Class:    chk.CheckInconsistClass_CIC_POOL_NONEXIST_ON_MS,
				Action:   chk.CheckInconsistAction_CIA_INTERACT,
				Result:   0,
				PoolUuid: uuidA.String(),
				ActChoices: []chk.CheckInconsistAction{
					chk.CheckInconsistAction_CIA_IGNORE,
					chk.CheckInconsistAction_CIA_DISCARD,
				},
			},
			{
				Seq:      43,
				Class:    chk.CheckInconsistClass_CIC_POOL_LESS_SVC_WITH_QUORUM,
				Action:   chk.CheckInconsistAction_CIA_DISCARD,
				Result:   0,
				PoolUuid: uuidB.String(),
			},
		},
	}

	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, resp),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	// Request pool info for A and B only (skip C to verify filter).
	dci, rc := callCheckQueryWithInfo(handle, []uuid.UUID{uuidA, uuidB})
	if rc != 0 {
		t.Fatalf("rc=%d, want 0", rc)
	}

	status, phase, pools, reports := readCheckInfo(dci)
	if status == "" {
		t.Error("dci_status not populated")
	}
	if phase == "" {
		t.Error("dci_phase not populated")
	}

	// Pools: only the two requested UUIDs should show up, in request order.
	if len(pools) != 2 {
		t.Fatalf("got %d pools, want 2", len(pools))
	}
	gotUUIDs := map[uuid.UUID]bool{pools[0].UUID: true, pools[1].UUID: true}
	for _, want := range []uuid.UUID{uuidA, uuidB} {
		if !gotUUIDs[want] {
			t.Errorf("pools missing UUID %s", want)
		}
	}
	if gotUUIDs[uuidC] {
		t.Errorf("pools contains un-requested UUID %s", uuidC)
	}
	for i, p := range pools {
		if p.Status == "" {
			t.Errorf("pools[%d].Status empty", i)
		}
		if p.Phase == "" {
			t.Errorf("pools[%d].Phase empty", i)
		}
	}

	// Reports: both should be present with option choices populated.
	if len(reports) != 2 {
		t.Fatalf("got %d reports, want 2", len(reports))
	}
	// Reports are sorted by class, then seq; CIC_POOL_NONEXIST_ON_MS (seq
	// 42) < CIC_POOL_LESS_SVC_WITH_QUORUM (seq 43) by enum value, so the
	// order depends on the class enum ordering rather than input order.
	// Check by UUID instead of index to stay robust.
	reportsByUUID := make(map[uuid.UUID]testCheckReportInfo, len(reports))
	for _, r := range reports {
		reportsByUUID[r.UUID] = r
	}
	if r, ok := reportsByUUID[uuidA]; !ok {
		t.Error("report for uuidA missing")
	} else {
		if r.Seq != 42 {
			t.Errorf("report[uuidA].Seq=%d, want 42", r.Seq)
		}
		if len(r.Options) != 2 {
			t.Errorf("report[uuidA].Options has %d entries, want 2", len(r.Options))
		}
	}
	if r, ok := reportsByUUID[uuidB]; !ok {
		t.Error("report for uuidB missing")
	} else if r.Seq != 43 {
		t.Errorf("report[uuidB].Seq=%d, want 43", r.Seq)
	}

	// Free should zero out the counts and null the allocation pointers so
	// a second free (common in bracketed error paths) is a no-op.
	callCheckInfoFree(dci)
	poolNr, reportNr, poolsNil, reportsNil, statusNil, phaseNil := checkInfoCounts(dci)
	if poolNr != 0 || reportNr != 0 {
		t.Errorf("after free: pool_nr=%d, report_nr=%d (want 0,0)", poolNr, reportNr)
	}
	if !poolsNil || !reportsNil {
		t.Errorf("after free: pools/reports not nil (pools=%v, reports=%v)", !poolsNil, !reportsNil)
	}
	if !statusNil || !phaseNil {
		t.Errorf("after free: status/phase CStrings not nil (status=%v, phase=%v)", !statusNil, !phaseNil)
	}

	// Second free must be a no-op (not a double-free crash).
	callCheckInfoFree(dci)
}

func TestCheckRepair(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *control.MockInvokerConfig
		seq    uint64
		action uint32
		expRC  int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.CheckActResp{}),
			},
			seq:    1,
			action: 1,
			expRC:  0,
		},
		"failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Nonexistent,
			},
			seq:    1,
			action: 1,
			expRC:  int(daos.Nonexistent),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			rc := callCheckRepair(handle, tc.seq, tc.action)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestCheckSetPolicy(t *testing.T) {
	for name, tc := range map[string]struct {
		mic      *control.MockInvokerConfig
		flags    uint32
		policies string
		expRC    int
	}{
		"success - reset flag": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.DaosResp{}),
			},
			flags: 1, // TCPF_RESET
			expRC: 0,
		},
		"success - with policies": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.DaosResp{}),
			},
			policies: "CIC_POOL_BAD_LABEL:CIA_INTERACT",
			expRC:    0,
		},
		"failure - invalid policy format": {
			mic:      &control.MockInvokerConfig{},
			policies: "invalid",
			expRC:    int(daos.InvalidInput),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			rc := callCheckSetPolicy(handle, tc.flags, tc.policies)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestCheckOperationsInvalidHandle(t *testing.T) {
	testUUID := uuid.MustParse("12345678-1234-1234-1234-123456789abc")

	tests := []struct {
		name string
		fn   func() int
	}{
		{"switch", func() int { return callCheckSwitch(0, true) }},
		{"start", func() int { return callCheckStart(0, 0, []uuid.UUID{testUUID}, "") }},
		{"stop", func() int { return callCheckStop(0, []uuid.UUID{testUUID}) }},
		{"query", func() int { return callCheckQuery(0, []uuid.UUID{testUUID}) }},
		{"repair", func() int { return callCheckRepair(0, 1, 1) }},
		{"set_policy", func() int { return callCheckSetPolicy(0, 0, "") }},
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
