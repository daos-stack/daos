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
