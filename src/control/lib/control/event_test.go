//
// (C) Copyright 2021 Intel Corporation.
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
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestControl_SystemNotify(t *testing.T) {
	rasEventEngineDied := events.NewEngineDiedEvent("foo", 0, 0, common.NormalExit)

	for name, tc := range map[string]struct {
		req     *SystemNotifyReq
		uErr    error
		uResp   *UnaryResponse
		expResp *SystemNotifyResp
		expErr  error
	}{
		"nil req": {
			req:    nil,
			expErr: errors.New("nil request"),
		},
		"nil event": {
			req:    &SystemNotifyReq{},
			expErr: errors.New("nil event in request"),
		},
		"zero sequence number": {
			req:    &SystemNotifyReq{Event: rasEventEngineDied},
			expErr: errors.New("invalid sequence"),
		},
		"local failure": {
			req: &SystemNotifyReq{
				Event:    rasEventEngineDied,
				Sequence: 1,
			},
			uErr:   errors.New("local failed"),
			expErr: errors.New("local failed"),
		},
		"remote failure": {
			req: &SystemNotifyReq{
				Event:    rasEventEngineDied,
				Sequence: 1,
			},
			uResp:  MockMSResponse("host1", errors.New("remote failed"), nil),
			expErr: errors.New("remote failed"),
		},
		"empty response": {
			req: &SystemNotifyReq{
				Event:    rasEventEngineDied,
				Sequence: 1,
			},
			uResp:   MockMSResponse("10.0.0.1:10001", nil, &sharedpb.ClusterEventResp{}),
			expResp: &SystemNotifyResp{},
		},
	} {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			rpcClient := NewMockInvoker(log, &MockInvokerConfig{
				UnaryError:    tc.uErr,
				UnaryResponse: tc.uResp,
			})

			gotResp, gotErr := SystemNotify(context.TODO(), rpcClient, tc.req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			if diff := cmp.Diff(tc.expResp, gotResp, defResCmpOpts()...); diff != "" {
				t.Fatalf("unexpected response (-want, +got):\n%s\n", diff)
			}
		})
	}
}

func TestControl_EventForwarder_OnEvent(t *testing.T) {
	rasEventEngineDiedFwdable := events.NewEngineDiedEvent("foo", 0, 0, common.NormalExit)
	rasEventEngineDied := events.NewEngineDiedEvent("foo", 0, 0, common.NormalExit).
		WithForwardable(false)

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
			defer common.ShowBufferOnFailure(t, buf)

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

			common.AssertEqual(t, tc.expInvokeCount, mi.invokeCount,
				"unexpected number of rpc calls")
			common.AssertEqual(t, expNextSeq, <-ef.seq,
				"unexpected next forwarding sequence")
		})
	}
}

// In real syslog implementation we would see entries logged to logger specific
// to a given priority, here we just check the correct prefix (maps to severity)
// is printed which verifies the event was written to the correct logger.
func TestControl_EventLogger_OnEvent(t *testing.T) {
	var mockSyslogBuf *strings.Builder
	mockNewSyslogger := func(prio syslog.Priority, _ int) (*log.Logger, error) {
		return log.New(mockSyslogBuf, fmt.Sprintf("prio%d ", prio), log.LstdFlags), nil
	}
	mockNewSysloggerFail := func(prio syslog.Priority, _ int) (*log.Logger, error) {
		return nil, errors.Errorf("failed to create new syslogger (prio %d)", prio)
	}

	rasEventEngineDied := events.NewEngineDiedEvent("foo", 0, 0, common.NormalExit)
	rasEventEngineDiedFwded := events.NewEngineDiedEvent("foo", 0, 0, common.NormalExit).
		WithForwarded(true)

	for name, tc := range map[string]struct {
		event           *events.RASEvent
		newSyslogger    newSysloggerFn
		expShouldLog    bool
		expShouldLogSys bool
	}{
		"nil event": {
			event: nil,
		},
		"forwarded event is not logged": {
			event: rasEventEngineDiedFwded,
		},
		"not forwarded error event gets logged": {
			event:           rasEventEngineDied,
			expShouldLog:    true,
			expShouldLogSys: true,
		},
		"not forwarded info event gets logged": {
			event: events.NewGenericEvent(events.RASID(math.MaxInt32-1),
				events.RASSeverityNotice, "DAOS generic test event",
				`{"people":["bill","steve","bob"]}`),
			expShouldLog:    true,
			expShouldLogSys: true,
		},
		"sysloggers not created": {
			event:           rasEventEngineDied,
			newSyslogger:    mockNewSysloggerFail,
			expShouldLog:    true,
			expShouldLogSys: false,
		},
	} {
		t.Run(name, func(t *testing.T) {
			logBasic, bufBasic := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, bufBasic)

			mockSyslogBuf = &strings.Builder{}

			if tc.newSyslogger == nil {
				tc.newSyslogger = mockNewSyslogger
			}

			el := newEventLogger(logBasic, tc.newSyslogger)
			el.OnEvent(context.TODO(), tc.event)

			// check event logged to control plane
			common.AssertEqual(t, tc.expShouldLog,
				strings.Contains(bufBasic.String(), "RAS "),
				"unexpected log output")

			slStr := mockSyslogBuf.String()
			t.Logf("syslog out: %s", slStr)
			if !tc.expShouldLogSys {
				common.AssertTrue(t, slStr == "",
					"expected syslog to be empty")
				return
			}
			prioStr := fmt.Sprintf("prio%d ", tc.event.Severity.SyslogPriority())
			sevOut := "sev: [" + tc.event.Severity.String() + "]"

			// check event logged to correct mock syslogger
			common.AssertEqual(t, 1, strings.Count(slStr, "RAS EVENT"),
				"unexpected number of events in syslog")
			common.AssertTrue(t, strings.Contains(slStr, sevOut),
				"syslog output missing severity")
			common.AssertTrue(t, strings.HasPrefix(slStr, prioStr),
				"syslog output missing syslog priority")
		})
	}
}
