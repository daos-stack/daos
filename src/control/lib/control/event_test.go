//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"fmt"
	"log"
	"log/syslog"
	"math"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
)

func mockEvtEngineDied(t *testing.T) *events.RASEvent {
	t.Helper()
	return events.NewEngineDiedEvent("foo", 0, 0, common.NormalExit, 1234)
}

func TestControl_eventNotify(t *testing.T) {
	rasEventEngineDied := mockEvtEngineDied(t)

	for name, tc := range map[string]struct {
		seq    uint64
		evt    *events.RASEvent
		aps    []string
		uErr   error
		uResp  *UnaryResponse
		expErr error
	}{
		"nil event": {
			seq:    1,
			expErr: errors.New("nil event"),
		},
		"zero sequence number": {
			evt:    rasEventEngineDied,
			expErr: errors.New("invalid sequence"),
		},
		"local failure": {
			evt:    rasEventEngineDied,
			seq:    1,
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			evt:    rasEventEngineDied,
			seq:    1,
			uResp:  MockMSResponse("host1", errors.New("remote failed"), nil),
			expErr: errors.New("remote failed"),
		},
		"empty response": {
			evt:   rasEventEngineDied,
			seq:   1,
			uResp: MockMSResponse("10.0.0.1:10001", nil, &sharedpb.ClusterEventResp{}),
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			rpcClient := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotErr := eventNotify(context.TODO(), rpcClient, tc.seq, tc.evt, tc.aps)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}
		})
	}
}

func TestControl_EventForwarder_OnEvent(t *testing.T) {
	rasEventEngineDied := mockEvtEngineDied(t).WithForwardable(false)
	rasEventEngineDiedFwdable := mockEvtEngineDied(t).WithForwardable(true)

	for name, tc := range map[string]struct {
		aps            []string
		event          *events.RASEvent
		nilClient      bool
		expInvokeCount int
	}{
		"nil event": {
			event: nil,
		},
		"missing access points": {
			event: rasEventEngineDiedFwdable,
		},
		"successful forward": {
			event:          rasEventEngineDiedFwdable,
			aps:            []string{"192.168.1.1"},
			expInvokeCount: 2,
		},
		"skip non-forwardable event": {
			event: rasEventEngineDied,
			aps:   []string{"192.168.1.1"},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, buf)

			expNextSeq := uint64(tc.expInvokeCount + 1)

			mi := NewMockInvoker(log, &MockInvokerConfig{})
			if tc.nilClient {
				mi = nil
			}

			callCount := tc.expInvokeCount
			if callCount == 0 {
				callCount++ // call at least once
			}

			ef := NewEventForwarder(mi, tc.aps)
			for i := 0; i < callCount; i++ {
				ef.OnEvent(context.TODO(), tc.event)
			}

			test.AssertEqual(t, tc.expInvokeCount, mi.invokeCount,
				"unexpected number of rpc calls")
			test.AssertEqual(t, expNextSeq, <-ef.seq,
				"unexpected next forwarding sequence")
		})
	}
}

// In real syslog implementation we would see entries logged to logger specific
// to a given priority, here we just check the correct prefix (maps to severity)
// is printed which verifies the event was written to the correct logger.
func TestControl_EventLogger_OnEvent(t *testing.T) {
	var (
		evtIDMarker   = " id: "
		mockSyslogBuf *strings.Builder
	)

	mockNewSyslogger := func(prio syslog.Priority, flags int) (*log.Logger, error) {
		return log.New(mockSyslogBuf, fmt.Sprintf("prio%d ", prio), flags), nil
	}
	mockNewSysloggerFail := func(prio syslog.Priority, _ int) (*log.Logger, error) {
		return nil, errors.Errorf("failed to create new syslogger (prio %d)", prio)
	}

	rasEventEngineDied := mockEvtEngineDied(t).WithForwarded(false)
	rasEventEngineDiedFwded := mockEvtEngineDied(t).WithForwarded(true)

	for name, tc := range map[string]struct {
		event              *events.RASEvent
		newSyslogger       newSysloggerFn
		expShouldLog       bool
		expShouldLogSyslog bool
		expSyslogOut       string
	}{
		"nil event": {
			event: nil,
		},
		"forwarded event is not logged": {
			event: rasEventEngineDiedFwded,
		},
		"not forwarded error event gets logged": {
			event:              rasEventEngineDied,
			expShouldLog:       false,
			expShouldLogSyslog: true,
		},
		"not forwarded info event gets logged": {
			event: events.NewGenericEvent(events.RASID(math.MaxInt32-1),
				events.RASSeverityNotice, "DAOS generic test event",
				`{"people":["bill","steve","bob"]}`),
			expShouldLog:       false,
			expShouldLogSyslog: true,
		},
		"sysloggers not created": {
			event:              rasEventEngineDied,
			newSyslogger:       mockNewSysloggerFail,
			expShouldLog:       true,
			expShouldLogSyslog: false,
		},
		"exp syslog output": {
			event:              rasEventEngineDied,
			expShouldLog:       false,
			expShouldLogSyslog: true,
			expSyslogOut: `
prio27 id: [engine_died] ts: [%s] host: [foo] type: [STATE_CHANGE] sev: [ERROR] msg: [DAOS engine 0 exited unexpectedly: process exited with 0] pid: [1234] rank: [0]
`,
		},
	} {
		t.Run(name, func(t *testing.T) {
			logBasic, bufBasic := logging.NewTestLogger(t.Name())
			defer test.ShowBufferOnFailure(t, bufBasic)

			mockSyslogBuf = &strings.Builder{}

			if tc.newSyslogger == nil {
				tc.newSyslogger = mockNewSyslogger
			}

			el := newEventLogger(logBasic, tc.newSyslogger)
			el.OnEvent(context.TODO(), tc.event)

			// check event logged to control plane
			test.AssertEqual(t, tc.expShouldLog,
				strings.Contains(bufBasic.String(), evtIDMarker),
				"unexpected log output")

			slStr := mockSyslogBuf.String()
			t.Logf("syslog out: %s", slStr)
			if !tc.expShouldLogSyslog {
				test.AssertTrue(t, slStr == "", "expected syslog to be empty")
				return
			}
			prioStr := fmt.Sprintf("prio%d ", tc.event.Severity.SyslogPriority())
			sevOut := "sev: [" + tc.event.Severity.String() + "]"

			// check event logged to correct mock syslogger
			test.AssertEqual(t, 1, strings.Count(slStr, evtIDMarker),
				"unexpected number of events in syslog")
			test.AssertTrue(t, strings.Contains(slStr, sevOut),
				"syslog output missing severity")
			test.AssertTrue(t, strings.HasPrefix(slStr, prioStr),
				"syslog output missing syslog priority")

			if tc.expSyslogOut == "" {
				return
			}
			// hack to avoid mismatch on timestamp
			tc.expSyslogOut = fmt.Sprintf(tc.expSyslogOut, tc.event.Timestamp)

			if diff := cmp.Diff(strings.TrimLeft(tc.expSyslogOut, "\n"), slStr); diff != "" {
				t.Fatalf("Unexpected syslog output (-want, +got):\n%s\n", diff)
			}
		})
	}
}
