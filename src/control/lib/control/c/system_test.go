//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestControlC_SystemStopRank(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		rank  uint32
		force bool
		expRC int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.SystemStopResp{
					Results: []*sharedpb.RankResult{
						{Rank: 0, State: "stopped"},
					},
				}),
			},
			rank:  0,
			force: false,
			expRC: 0,
		},
		"success with force": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.SystemStopResp{
					Results: []*sharedpb.RankResult{
						{Rank: 1, State: "stopped"},
					},
				}),
			},
			rank:  1,
			force: true,
			expRC: 0,
		},
		"failure - connection error": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Unreachable,
			},
			rank:  0,
			expRC: int(daos.Unreachable),
		},
		"CRT_NO_RANK rejected": {
			mic:   &control.MockInvokerConfig{},
			rank:  0xffffffff,
			expRC: int(daos.InvalidInput),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			rc := callSystemStopRank(handle, tc.rank, tc.force)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestControlC_SystemStartRank(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		rank  uint32
		expRC int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.SystemStartResp{
					Results: []*sharedpb.RankResult{
						{Rank: 0, State: "joined"},
					},
				}),
			},
			rank:  0,
			expRC: 0,
		},
		"CRT_NO_RANK rejected": {
			mic:   &control.MockInvokerConfig{},
			rank:  0xffffffff,
			expRC: int(daos.InvalidInput),
		},
		"failure - already started": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.SystemStartResp{
					Results: []*sharedpb.RankResult{
						{Rank: 0, State: "joined", Errored: true, Msg: "already started"},
					},
				}),
			},
			rank:  0,
			expRC: int(daos.MiscError), // RankResult error maps to MiscError
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			rc := callSystemStartRank(handle, tc.rank)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

// Whole-system stop: the request must carry an empty rank set.
func TestControlC_SystemStop(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.SystemStopResp{
			Results: []*sharedpb.RankResult{
				{Rank: 0, State: "stopped"},
				{Rank: 1, State: "stopped"},
			},
		}),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	if rc := callSystemStop(handle, true); rc != 0 {
		t.Fatalf("rc=%d, want 0", rc)
	}

	req := mi.SentReqs[0].(*control.SystemStopReq)
	if req.Ranks.Count() != 0 {
		t.Fatalf("Ranks=%s, want empty set for whole-system stop", req.Ranks.String())
	}
	if !req.Force {
		t.Fatal("Force not propagated")
	}
}

func TestControlC_SystemStart(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
		UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.SystemStartResp{
			Results: []*sharedpb.RankResult{
				{Rank: 0, State: "joined"},
				{Rank: 1, State: "joined"},
			},
		}),
	})
	handle := makeTestHandle(mi, log)
	defer handle.Delete()

	if rc := callSystemStart(handle); rc != 0 {
		t.Fatalf("rc=%d, want 0", rc)
	}

	req := mi.SentReqs[0].(*control.SystemStartReq)
	if req.Ranks.Count() != 0 {
		t.Fatalf("Ranks=%s, want empty set for whole-system start", req.Ranks.String())
	}
}

func TestControlC_SystemRankInvalidHandle(t *testing.T) {
	// Test that system operations return error for invalid handle
	rc := callSystemStopRank(0, 0, false)
	if rc == 0 {
		t.Fatal("expected error for invalid handle on stop, got success")
	}

	rc = callSystemStartRank(0, 0)
	if rc == 0 {
		t.Fatal("expected error for invalid handle on start, got success")
	}

	rc = callSystemReintRank(0, 0)
	if rc == 0 {
		t.Fatal("expected error for invalid handle on reint, got success")
	}

	rc = callSystemExcludeRank(0, 0)
	if rc == 0 {
		t.Fatal("expected error for invalid handle on exclude, got success")
	}
}

func TestControlC_SystemReintRank(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		rank  uint32
		expRC int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.SystemExcludeResp{
					Results: []*sharedpb.RankResult{
						{Rank: 0, State: "joined"},
					},
				}),
			},
			rank:  0,
			expRC: 0,
		},
		"rpc failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoPermission,
			},
			rank:  0,
			expRC: int(daos.NoPermission),
		},
		"rank result errored": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.SystemExcludeResp{
					Results: []*sharedpb.RankResult{
						{Rank: 0, Errored: true, Msg: "rank reint failed"},
					},
				}),
			},
			rank:  0,
			expRC: int(daos.MiscError),
		},
		"CRT_NO_RANK rejected": {
			mic:   &control.MockInvokerConfig{},
			rank:  0xffffffff,
			expRC: int(daos.InvalidInput),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			rc := callSystemReintRank(handle, tc.rank)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestControlC_SystemExcludeRank(t *testing.T) {
	for name, tc := range map[string]struct {
		mic   *control.MockInvokerConfig
		rank  uint32
		expRC int
	}{
		"success": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.SystemExcludeResp{
					Results: []*sharedpb.RankResult{
						{Rank: 0, State: "excluded"},
					},
				}),
			},
			rank:  0,
			expRC: 0,
		},
		"rpc failure": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.NoPermission,
			},
			rank:  0,
			expRC: int(daos.NoPermission),
		},
		"rank result errored": {
			mic: &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host1", nil, &mgmtpb.SystemExcludeResp{
					Results: []*sharedpb.RankResult{
						{Rank: 0, Errored: true, Msg: "rank exclude failed"},
					},
				}),
			},
			rank:  0,
			expRC: int(daos.MiscError),
		},
		"CRT_NO_RANK rejected": {
			mic:   &control.MockInvokerConfig{},
			rank:  0xffffffff,
			expRC: int(daos.InvalidInput),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			rc := callSystemExcludeRank(handle, tc.rank)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}
