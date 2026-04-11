//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestServerSetLogmasks(t *testing.T) {
	for name, tc := range map[string]struct {
		mic        *control.MockInvokerConfig
		masks      string
		streams    string
		subsystems string
		expRC      int
	}{
		"failure - connection error": {
			mic: &control.MockInvokerConfig{
				UnaryError: daos.Unreachable,
			},
			masks: "DEBUG",
			expRC: int(daos.Unreachable),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, tc.mic)
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			rc := callServerSetLogmasks(handle, tc.masks, tc.streams, tc.subsystems)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestServerSetLogmasksInvalidHandle(t *testing.T) {
	rc := callServerSetLogmasks(0, "DEBUG", "", "")
	if rc == 0 {
		t.Fatal("expected error for invalid handle, got success")
	}
}
