//
// (C) Copyright 2026 Hewlett Packard Enterprise Development LP
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"testing"

	"github.com/google/go-cmp/cmp"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
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

			rc := callServerSetLogmasks(handle, "", tc.masks, tc.streams, tc.subsystems)

			if rc != tc.expRC {
				t.Fatalf("expected RC %d, got %d", tc.expRC, rc)
			}
		})
	}
}

func TestServerSetLogmasksInvalidHandle(t *testing.T) {
	rc := callServerSetLogmasks(0, "", "DEBUG", "", "")
	if rc == 0 {
		t.Fatal("expected error for invalid handle, got success")
	}
}

func TestServerSetLogmasksHostFilter(t *testing.T) {
	for name, tc := range map[string]struct {
		host     string
		wantList []string
	}{
		"no host forwards empty list": {host: "", wantList: nil},
		"host forwarded as singleton": {host: "host-7", wantList: []string{"host-7"}},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			mi := control.NewMockInvoker(log, &control.MockInvokerConfig{
				UnaryResponse: control.MockMSResponse("host-7", nil, &ctlpb.SetLogMasksResp{}),
			})
			handle := makeTestHandle(mi, log)
			defer handle.Delete()

			if rc := callServerSetLogmasks(handle, tc.host, "DEBUG", "", ""); rc != 0 {
				t.Fatalf("rc=%d, want 0", rc)
			}

			if len(mi.SentReqs) != 1 {
				t.Fatalf("SentReqs=%d, want 1", len(mi.SentReqs))
			}
			req, ok := mi.SentReqs[0].(*control.SetEngineLogMasksReq)
			if !ok {
				t.Fatalf("SentReqs[0] type=%T, want *control.SetEngineLogMasksReq", mi.SentReqs[0])
			}
			if diff := cmp.Diff(tc.wantList, req.HostList); diff != "" {
				t.Fatalf("HostList mismatch (-want +got):\n%s", diff)
			}
		})
	}
}
