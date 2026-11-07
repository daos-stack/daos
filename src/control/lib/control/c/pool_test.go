//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"os"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/uuid"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

const testPoolUUID = "12345678-1234-1234-1234-123456789abc"

func TestControlC_PoolCreate(t *testing.T) {
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
			expSvcRanks: []uint32{0, 1, 2},
		},
		"failure - no space":          {mic: &control.MockInvokerConfig{UnaryError: daos.NoSpace}, expRC: int(daos.NoSpace)},
		"failure - permission denied": {mic: &control.MockInvokerConfig{UnaryError: daos.NoPermission}, expRC: int(daos.NoPermission)},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			res := callPoolCreate(handle, uint32(os.Getuid()), uint32(os.Getgid()), 1<<30, 0, 3)
			if res.rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", res.rc, tc.expRC)
			}
			if tc.expRC != 0 {
				return
			}
			if diff := cmp.Diff(tc.expSvcRanks, res.svcRanks); diff != "" {
				t.Fatalf("svc ranks mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

// TestPoolCreateServerPicksSvcCount passes nsvc=0 and verifies the library
// surfaces whatever replica set the server chose.
func TestControlC_PoolCreateServerPicksSvcCount(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{
			SvcReps: []uint32{0, 1, 2},
		}),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	res := callPoolCreate(handle, uint32(os.Getuid()), uint32(os.Getgid()), 1<<30, 0, 0)
	if res.rc != 0 {
		t.Fatalf("rc=%d, want 0", res.rc)
	}
	if diff := cmp.Diff([]uint32{0, 1, 2}, res.svcRanks); diff != "" {
		t.Fatalf("svc ranks mismatch (-want +got):\n%s", diff)
	}

	req := mi.SentReqs[0].(*control.PoolCreateReq)
	if req.NumSvcReps != 0 {
		t.Fatalf("NumSvcReps=%d, want 0 (nsvc=0 means server picks)", req.NumSvcReps)
	}
}

func TestControlC_PoolCreateWithProps(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{
			SvcReps: []uint32{0},
		}),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	entries := []testPropEntry{
		testPropStr(testPropPoLabel, "my-pool"),
		testPropNum(testPropPoRedunFac, 1),
	}
	res := callPoolCreateWithProp(handle, uint32(os.Getuid()), uint32(os.Getgid()), 1<<30, 0, 1, entries)
	if res.rc != 0 {
		t.Fatalf("rc=%d, want 0", res.rc)
	}

	req := mi.SentReqs[0].(*control.PoolCreateReq)
	if len(req.Properties) != 2 {
		t.Fatalf("Properties=%d, want 2", len(req.Properties))
	}
	byName := map[string]*daos.PoolProperty{}
	for _, p := range req.Properties {
		byName[p.Name] = p
	}
	if byName["label"].Value.String() != "my-pool" {
		t.Errorf("label=%q, want my-pool", byName["label"].Value.String())
	}
	if n, _ := byName["rd_fac"].Value.GetNumber(); n != 1 {
		t.Errorf("rd_fac=%d, want 1", n)
	}
}

func TestControlC_PoolCreateInvalidHandle(t *testing.T) {
	if rc := callPoolCreateInvalidHandle(); rc == 0 {
		t.Fatal("expected error for invalid handle, got success")
	}
}

func TestControlC_RankListConversion(t *testing.T) {
	for name, tc := range map[string]struct{ ranks []uint32 }{
		"empty":          {nil},
		"single rank":    {[]uint32{5}},
		"multiple ranks": {[]uint32{0, 1, 2, 3, 4}},
	} {
		t.Run(name, func(t *testing.T) {
			got := testRankListRoundTrip(tc.ranks)
			if len(tc.ranks) == 0 {
				if len(got) != 0 {
					t.Fatalf("got %v, want empty", got)
				}
				return
			}
			if diff := cmp.Diff(tc.ranks, got); diff != "" {
				t.Fatalf("round-trip mismatch (-want +got):\n%s", diff)
			}

			out, nr := testCopyRankListTo(tc.ranks, len(tc.ranks))
			if nr != uint32(len(tc.ranks)) {
				t.Fatalf("rl_nr=%d, want %d", nr, len(tc.ranks))
			}
			if diff := cmp.Diff(tc.ranks, out); diff != "" {
				t.Fatalf("copy-back mismatch (-want +got):\n%s", diff)
			}
		})
	}
}

// TestCopyRankListToCTruncation pins that copyRankListToC respects the
// caller's output capacity and does not overrun when input > cap.
func TestControlC_CopyRankListToCTruncation(t *testing.T) {
	input := []uint32{10, 20, 30, 40, 50}
	got, nr := testCopyRankListTo(input, 2)
	if nr != 2 {
		t.Fatalf("rl_nr=%d, want 2", nr)
	}
	if diff := cmp.Diff([]uint32{10, 20}, got); diff != "" {
		t.Fatalf("mismatch (-want +got):\n%s", diff)
	}
}

func TestControlC_PoolDestroy(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		force bool
		expRC int
	}{
		"success":                  {mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolDestroyResp{})}},
		"success with force":       {mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolDestroyResp{})}, force: true},
		"failure - pool not found": {mic: &control.MockInvokerConfig{UnaryError: daos.Nonexistent}, expRC: int(daos.Nonexistent)},
		"failure - busy":           {mic: &control.MockInvokerConfig{UnaryError: daos.Busy}, expRC: int(daos.Busy)},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			if rc := callPoolDestroy(handle, uuid.MustParse(testPoolUUID), tc.force); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolEvict(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		expRC int
	}{
		"success":                  {mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolEvictResp{})}},
		"failure - pool not found": {mic: &control.MockInvokerConfig{UnaryError: daos.Nonexistent}, expRC: int(daos.Nonexistent)},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			if rc := callPoolEvict(handle, uuid.MustParse(testPoolUUID)); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolExclude(t *testing.T) {
	for name, tc := range map[string]struct {
		mic    *control.MockInvokerConfig
		rank   uint32
		tgtIdx int
		expRC  int
	}{
		"success - rank only": {
			mic:    &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolExcludeResp{})},
			tgtIdx: -1,
		},
		"success - rank and target": {
			mic:    &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolExcludeResp{})},
			rank:   1,
			tgtIdx: 2,
		},
		"failure - response error": {
			mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolExcludeResp{
				Status: int32(daos.Nonexistent),
			})},
			tgtIdx: -1,
			expRC:  int(daos.Nonexistent),
		},
		"CRT_NO_RANK rejected": {
			mic:    &control.MockInvokerConfig{},
			rank:   0xffffffff,
			tgtIdx: -1,
			expRC:  int(daos.InvalidInput),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			if rc := callPoolExclude(handle, uuid.MustParse(testPoolUUID), tc.rank, tc.tgtIdx); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolDrain(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		expRC int
	}{
		"success": {mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolDrainResp{})}},
		"failure - response error": {
			mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolDrainResp{
				Status: int32(daos.Busy),
			})},
			expRC: int(daos.Busy),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			if rc := callPoolDrain(handle, uuid.MustParse(testPoolUUID), 0, -1); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolReintegrate(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		expRC int
	}{
		"success": {mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolReintResp{})}},
		"failure - response error": {
			mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolReintResp{
				Status: int32(daos.Busy),
			})},
			expRC: int(daos.Busy),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			if rc := callPoolReintegrate(handle, uuid.MustParse(testPoolUUID), 0, -1); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolOperationsInvalidHandle(t *testing.T) {
	u := uuid.MustParse(testPoolUUID)
	tests := []struct {
		name string
		fn   func() int
	}{
		{"destroy", func() int { return callPoolDestroy(0, u, false) }},
		{"evict", func() int { return callPoolEvict(0, u) }},
		{"exclude", func() int { return callPoolExclude(0, u, 0, -1) }},
		{"drain", func() int { return callPoolDrain(0, u, 0, -1) }},
		{"reintegrate", func() int { return callPoolReintegrate(0, u, 0, -1) }},
		{"extend", func() int { return callPoolExtend(0, u, []uint32{1, 2}) }},
		{"set_prop", func() int { return callPoolSetProp(0, "", u, "label", "test") }},
		{"get_prop", func() int { _, rc := callPoolGetProp(0, "", u, "label"); return rc }},
		{"update_ace", func() int { return callPoolUpdateACE(0, u, "A::user@:rw") }},
		{"delete_ace", func() int { return callPoolDeleteACE(0, u, "user@") }},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			if rc := tt.fn(); rc == 0 {
				t.Fatalf("expected error for invalid handle on %s", tt.name)
			}
		})
	}
}

func TestControlC_PoolExtend(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		ranks []uint32
		expRC int
	}{
		"success": {
			mic:   &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolExtendResp{})},
			ranks: []uint32{1, 2},
		},
		"failure": {
			mic:   &control.MockInvokerConfig{UnaryError: daos.NoSpace},
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

			if rc := callPoolExtend(handle, uuid.MustParse(testPoolUUID), tc.ranks); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolSetProp(t *testing.T) {
	for name, tc := range map[string]struct {
		mic               *control.MockInvokerConfig
		propName, propVal string
		expRC             int
	}{
		"success": {
			mic:      &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolSetPropResp{})},
			propName: "label",
			propVal:  "testpool",
		},
		"invalid property name": {
			mic:      &control.MockInvokerConfig{},
			propName: "invalid_prop_name",
			propVal:  "value",
			expRC:    int(daos.InvalidInput),
		},
		"failure": {
			mic:      &control.MockInvokerConfig{UnaryError: daos.NoPermission},
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

			if rc := callPoolSetProp(handle, "", uuid.MustParse(testPoolUUID), tc.propName, tc.propVal); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolGetProp(t *testing.T) {
	for name, tc := range map[string]struct {
		mic      *control.MockInvokerConfig
		label    string
		propName string
		expVal   string
		expRC    int
	}{
		"success with UUID": {
			mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolGetPropResp{
				Properties: []*mgmtpb.PoolProperty{
					{Number: 1, Value: &mgmtpb.PoolProperty_Strval{Strval: "mypool"}},
				},
			})},
			propName: "label",
			expVal:   "mypool",
		},
		"success with label": {
			mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolGetPropResp{
				Properties: []*mgmtpb.PoolProperty{
					{Number: 1, Value: &mgmtpb.PoolProperty_Strval{Strval: "mypool"}},
				},
			})},
			label:    "mypool",
			propName: "label",
			expVal:   "mypool",
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

			val, rc := callPoolGetProp(handle, tc.label, uuid.MustParse(testPoolUUID), tc.propName)
			if rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
			if rc == 0 && val != tc.expVal {
				t.Fatalf("val=%q, want %q", val, tc.expVal)
			}
		})
	}
}

func TestControlC_PoolUpdateACE(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		expRC int
	}{
		"success": {mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ACLResp{})}},
		"failure": {mic: &control.MockInvokerConfig{UnaryError: daos.NoPermission}, expRC: int(daos.NoPermission)},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			if rc := callPoolUpdateACE(handle, uuid.MustParse(testPoolUUID), "A::user@:rw"); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolDeleteACE(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		expRC int
	}{
		"success": {mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ACLResp{})}},
		"failure": {mic: &control.MockInvokerConfig{UnaryError: daos.NoPermission}, expRC: int(daos.NoPermission)},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			if rc := callPoolDeleteACE(handle, uuid.MustParse(testPoolUUID), "user@"); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolRebuildStop(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		force bool
		expRC int
	}{
		"success":            {mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.DaosResp{})}},
		"success with force": {mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.DaosResp{})}, force: true},
		"failure":            {mic: &control.MockInvokerConfig{UnaryError: daos.Busy}, expRC: int(daos.Busy)},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			if rc := callPoolRebuildStop(handle, uuid.MustParse(testPoolUUID), tc.force); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolRebuildStart(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		expRC int
	}{
		"success": {mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.DaosResp{})}},
		"failure": {mic: &control.MockInvokerConfig{UnaryError: daos.Busy}, expRC: int(daos.Busy)},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			if rc := callPoolRebuildStart(handle, uuid.MustParse(testPoolUUID)); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolList(t *testing.T) {
	for name, tc := range map[string]struct {
		mic       *control.MockInvokerConfig
		expNpools uint64
		expRC     int
	}{
		"success - count only": {
			mic: &control.MockInvokerConfig{UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
				Pools: []*mgmtpb.ListPoolsResp_Pool{
					{Uuid: "12345678-1234-1234-1234-123456789abc", Label: "pool1"},
					{Uuid: "22345678-1234-1234-1234-123456789abc", Label: "pool2"},
				},
			})},
			expNpools: 2,
		},
		"failure": {mic: &control.MockInvokerConfig{UnaryError: daos.NoPermission}, expRC: int(daos.NoPermission)},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)
			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			n, rc := callPoolListCount(handle, 0)
			if rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
			if rc == 0 && n != tc.expNpools {
				t.Fatalf("count=%d, want %d", n, tc.expNpools)
			}
		})
	}
}

func TestControlC_PoolListWithData(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mic := &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
			Pools: []*mgmtpb.ListPoolsResp_Pool{
				{Uuid: "12345678-1234-1234-1234-123456789abc", Label: "pool1", SvcReps: []uint32{0, 1, 2}},
				{Uuid: "22345678-1234-1234-1234-123456789abc", Label: "pool2", SvcReps: []uint32{0}},
			},
		}),
	}
	mi := control.NewMockInvoker(log, mic)
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	entries, n, rc := callPoolList(handle, 2)
	if rc != 0 {
		t.Fatalf("rc=%d, want 0", rc)
	}
	if n != 2 {
		t.Fatalf("n=%d, want 2", n)
	}
	want := []testPoolInfo{
		{UUID: uuid.MustParse("12345678-1234-1234-1234-123456789abc"), Label: "pool1"},
		{UUID: uuid.MustParse("22345678-1234-1234-1234-123456789abc"), Label: "pool2"},
	}
	if diff := cmp.Diff(want, entries); diff != "" {
		t.Fatalf("entries mismatch (-want +got):\n%s", diff)
	}
}

func TestControlC_PoolListBufferTooSmall(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
			Pools: []*mgmtpb.ListPoolsResp_Pool{
				{Uuid: "12345678-1234-1234-1234-123456789abc", Label: "pool1"},
				{Uuid: "22345678-1234-1234-1234-123456789abc", Label: "pool2"},
				{Uuid: "32345678-1234-1234-1234-123456789abc", Label: "pool3"},
			},
		}),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	_, _, rc := callPoolList(handle, 1)
	if rc != int(daos.BufTooSmall) {
		t.Fatalf("rc=%d, want BufTooSmall(%d)", rc, int(daos.BufTooSmall))
	}
}

func TestControlC_PoolListInvalidHandle(t *testing.T) {
	if _, rc := callPoolListCount(0, 0); rc == 0 {
		t.Fatal("expected error for invalid handle, got success")
	}
}

func TestControlC_PoolListErrorResetsNpools(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{UnaryError: daos.NoPermission})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	n, rc := callPoolListCount(handle, 99)
	if rc != int(daos.NoPermission) {
		t.Fatalf("rc=%d, want NoPermission(%d)", rc, int(daos.NoPermission))
	}
	if n != 0 {
		t.Fatalf("n=%d, want 0 on error", n)
	}
}

func TestControlC_PoolListFreeIdempotent(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.ListPoolsResp{
			Pools: []*mgmtpb.ListPoolsResp_Pool{
				{Uuid: "12345678-1234-1234-1234-123456789abc", Label: "pool1", SvcReps: []uint32{0, 1, 2}},
			},
		}),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	if rc := callPoolListDoubleFree(handle); rc != 0 {
		t.Fatalf("double-free path rc=%d, want 0", rc)
	}
}

func TestControlC_ValidatePoolCreateArgs(t *testing.T) {
	for name, tc := range map[string]struct {
		spec  *validatePoolCreateArgsSpec
		expRC int
	}{
		"valid - scm only":       {&validatePoolCreateArgsSpec{scmSize: 1 << 30}, 0},
		"valid - nvme only":      {&validatePoolCreateArgsSpec{nvmeSize: 1 << 30}, 0},
		"valid - both sizes set": {&validatePoolCreateArgsSpec{scmSize: 1 << 30, nvmeSize: 1 << 30}, 0},
		"invalid - nil args":     {&validatePoolCreateArgsSpec{nilArgs: true}, int(daos.InvalidInput)},
		"invalid - missing pool_uuid": {
			&validatePoolCreateArgsSpec{omitPoolUUID: true, scmSize: 1 << 30},
			int(daos.InvalidInput),
		},
		"invalid - both sizes zero": {&validatePoolCreateArgsSpec{}, int(daos.InvalidInput)},
		"invalid - non-nil svc on input": {
			&validatePoolCreateArgsSpec{scmSize: 1 << 30, nonNilSvc: true},
			int(daos.InvalidInput),
		},
	} {
		t.Run(name, func(t *testing.T) {
			if rc := callValidatePoolCreateArgs(tc.spec); rc != tc.expRC {
				t.Fatalf("rc=%d, want %d", rc, tc.expRC)
			}
		})
	}
}

func TestControlC_PoolRebuildInvalidHandle(t *testing.T) {
	u := uuid.MustParse(testPoolUUID)
	if rc := callPoolRebuildStop(0, u, false); rc == 0 {
		t.Fatal("rebuild stop: expected error for invalid handle")
	}
	if rc := callPoolRebuildStart(0, u); rc == 0 {
		t.Fatal("rebuild start: expected error for invalid handle")
	}
}

// TestControlC_PoolCreateWithACL verifies that a DAOS_PROP_PO_ACL entry in the
// create prop is converted to req.ACL entries, not dropped with NotSupported.
func TestControlC_PoolCreateWithACL(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{
			SvcReps: []uint32{0},
		}),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	// DAOS_ACL_PERM_READ | DAOS_ACL_PERM_WRITE (0x03) as allow perms
	const testPerms = uint64(0x03)
	res := callPoolCreateWithACL(handle, uint32(os.Getuid()), uint32(os.Getgid()), 1<<30, testPerms)
	if res.rc != 0 {
		t.Fatalf("rc=%d, want 0", res.rc)
	}

	if len(mi.SentReqs) == 0 {
		t.Fatal("no request sent to invoker")
	}
	req, ok := mi.SentReqs[0].(*control.PoolCreateReq)
	if !ok {
		t.Fatalf("sent req type %T, want *control.PoolCreateReq", mi.SentReqs[0])
	}
	if req.ACL == nil {
		t.Fatal("req.ACL is nil, want non-nil ACL from prop")
	}
	// The make_test_acl helper creates one OWNER@ Allow ACE; we should get
	// exactly one ACE string back.
	if len(req.ACL.Entries) != 1 {
		t.Fatalf("req.ACL.Entries=%d, want 1", len(req.ACL.Entries))
	}
	// ACE strings are formatted as "type:flags:identity:perms" — the OWNER@
	// Allow entry should start with "A::" (Allow, no flags, OWNER@).
	if !strings.HasPrefix(req.ACL.Entries[0], "A::OWNER@:") {
		t.Errorf("ACE[0]=%q, want prefix A::OWNER@:", req.ACL.Entries[0])
	}
}

// TestControlC_PoolCreateWithOwnerProp verifies that DAOS_PROP_PO_OWNER and
// DAOS_PROP_PO_OWNER_GROUP entries override the uid/gid-derived defaults in
// PoolCreateReq.
func TestControlC_PoolCreateWithOwnerProp(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{
			SvcReps: []uint32{0},
		}),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	const wantOwner = "alice@"
	const wantGroup = "admins@"
	res := callPoolCreateWithOwnerProp(handle, uint32(os.Getuid()), uint32(os.Getgid()), 1<<30, wantOwner, wantGroup)
	if res.rc != 0 {
		t.Fatalf("rc=%d, want 0", res.rc)
	}

	if len(mi.SentReqs) == 0 {
		t.Fatal("no request sent to invoker")
	}
	req, ok := mi.SentReqs[0].(*control.PoolCreateReq)
	if !ok {
		t.Fatalf("sent req type %T, want *control.PoolCreateReq", mi.SentReqs[0])
	}
	// formatNameGroup appends "@" if missing; since wantOwner already ends
	// with "@", the field should be exactly wantOwner.
	if req.User != wantOwner {
		t.Errorf("req.User=%q, want %q", req.User, wantOwner)
	}
	if req.UserGroup != wantGroup {
		t.Errorf("req.UserGroup=%q, want %q", req.UserGroup, wantGroup)
	}
}

// TestControlC_PoolCreateWithSvcListPropSkipped verifies that a
// DAOS_PROP_PO_SVC_LIST entry does not cause a NotSupported error and is
// silently skipped (svc_list is read-only and unsettable at create time).
func TestControlC_PoolCreateWithSvcListPropSkipped(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.PoolCreateResp{
			SvcReps: []uint32{0},
		}),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	res := callPoolCreateWithSvcListProp(handle, uint32(os.Getuid()), uint32(os.Getgid()), 1<<30)
	if res.rc != 0 {
		t.Fatalf("rc=%d, want 0 (svc_list skipped)", res.rc)
	}
}
