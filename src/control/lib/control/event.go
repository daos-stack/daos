//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"log"
	"log/syslog"

	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
)

// EventNotifyReq contains the inputs for an event notify request.
type EventNotifyReq struct {
	unaryRequest
	msRequest
}

// EventNotifyResp contains the inputs for an event notify response.
type EventNotifyResp struct{}

// eventNotify will attempt to notify the DAOS system of a cluster event.
func eventNotify(ctx context.Context, rpcClient UnaryInvoker, seq uint64, evt *events.RASEvent, aps []string) error {
	switch {
	case evt == nil:
		return errors.New("nil event")
	case seq == 0:
		return errors.New("invalid sequence number in request")
	case rpcClient == nil:
		return errors.New("nil rpc client")
	}

	pbRASEvent, err := evt.ToProto()
	if err != nil {
		return errors.Wrap(err, "convert event to proto")
	}

	req := &EventNotifyReq{}
	req.SetHostList(aps)
	rpcClient.Debugf("forwarding %s event to MS access points %v (seq: %d)", evt.ID, aps, seq)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		msg, err := mgmtpb.NewMgmtSvcClient(conn).ClusterEvent(ctx,
			&sharedpb.ClusterEventReq{Sequence: seq, Event: pbRASEvent})
		if err == nil && msg != nil {
			rpcClient.Debugf("%s event forwarded to MS @ %s", evt.ID, conn.Target())
		}
		return msg, err
	})

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return convertMSResponse(ur, new(EventNotifyResp))
}

// EventForwarder implements the events.Handler interface, increments sequence
// number for each event forwarded and distributes requests to MS access points.
type EventForwarder struct {
	seq       <-chan uint64
	client    UnaryInvoker
	accessPts []string
}

// OnEvent implements the events.Handler interface.
func (ef *EventForwarder) OnEvent(ctx context.Context, evt *events.RASEvent) {
	switch {
	case evt == nil:
		ef.client.Debug("skip event forwarding, nil event")
		return
	case len(ef.accessPts) == 0:
		ef.client.Debug("skip event forwarding, missing access points")
		return
	case !evt.ShouldForward():
		ef.client.Debugf("forwarding disabled for %s event", evt.ID)
		return
	}

	if err := eventNotify(ctx, ef.client, <-ef.seq, evt, ef.accessPts); err != nil {
		ef.client.Debugf("failed to forward event to MS: %s", err)
	}
}

// NewEventForwarder returns an initialized EventForwarder.
func NewEventForwarder(rpcClient UnaryInvoker, accessPts []string) *EventForwarder {
	seqCh := make(chan uint64)
	go func(ch chan<- uint64) {
		for i := uint64(1); ; i++ {
			ch <- i
		}
	}(seqCh)

	return &EventForwarder{
		seq:       seqCh,
		client:    rpcClient,
		accessPts: accessPts,
	}
}

// EventLogger implements the events.Handler interface and logs RAS event to
// INFO using supplied logging.Logger. In addition syslog is written to at the
// priority level derived from the event severity.
type EventLogger struct {
	log        logging.Logger
	sysloggers map[events.RASSeverityID]*log.Logger
}

// OnEvent implements the events.Handler interface.
func (el *EventLogger) OnEvent(_ context.Context, evt *events.RASEvent) {
	switch {
	case evt == nil:
		el.log.Debug("skip event forwarding, nil event")
		return
	case evt.IsForwarded():
		return // event has already been logged at source
	}

	out := evt.PrintRAS()
	if sl := el.sysloggers[evt.Severity]; sl != nil {
		sl.Print(out)
		return
	}
	// no syslogger, write to default logger instead
	el.log.Info("&&& RAS " + out)
}

type newSysloggerFn func(syslog.Priority, int) (*log.Logger, error)

// newEventLogger returns an initialized EventLogger using the provided function
// to populate syslog endpoints which map to event severity identifiers.
func newEventLogger(logBasic logging.Logger, newSyslogger newSysloggerFn) *EventLogger {
	el := &EventLogger{
		log:        logBasic,
		sysloggers: make(map[events.RASSeverityID]*log.Logger),
	}

	// syslog writer will prepend timestamp in message header so don't add
	// duplicate timestamp in log entries
	flags := log.LstdFlags &^ (log.Ldate | log.Ltime)

	for _, sev := range []events.RASSeverityID{
		events.RASSeverityUnknown,
		events.RASSeverityError,
		events.RASSeverityWarning,
		events.RASSeverityNotice,
	} {
		sl, err := newSyslogger(sev.SyslogPriority(), flags)
		if err != nil {
			logBasic.Errorf("failed to create syslogger with priority %d (severity=%s,"+
				" facility=DAEMON): %s", sev.SyslogPriority(), sev, err)
			continue
		}
		el.sysloggers[sev] = sl
	}

	return el
}

// NewEventLogger returns an initialized EventLogger capable of writing to the
// supplied logger in addition to syslog.
func NewEventLogger(log logging.Logger) *EventLogger {
	return newEventLogger(log, syslog.NewLogger)
}
