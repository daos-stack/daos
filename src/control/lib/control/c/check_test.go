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
