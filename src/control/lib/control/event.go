//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"log"
	"log/syslog"

	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/logging"
)

// SystemNotifyReq contains the inputs for the system notify request.
type SystemNotifyReq struct {
	unaryRequest
	msRequest
	Event    *events.RASEvent
	Sequence uint64
}

// toClusterEventReq converts the system notify request to a cluster event
// request. Resolve control address to a hostname if possible.
func (req *SystemNotifyReq) toClusterEventReq() (*sharedpb.ClusterEventReq, error) {
	if req.Event == nil {
		return nil, errors.New("nil event in request")
	}

	pbRASEvent, err := req.Event.ToProto()
	if err != nil {
		return nil, errors.Wrap(err, "convert event to proto")
	}

	return &sharedpb.ClusterEventReq{Sequence: req.Sequence, Event: pbRASEvent}, nil
}

// SystemNotifyResp contains the request response.
type SystemNotifyResp struct{}

// SystemNotify will attempt to notify the DAOS system of a cluster event.
func SystemNotify(ctx context.Context, rpcClient UnaryInvoker, req *SystemNotifyReq) (*SystemNotifyResp, error) {
	switch {
	case req == nil:
		return nil, errors.New("nil request")
	case common.InterfaceIsNil(req.Event):
		return nil, errors.New("nil event in request")
	case req.Sequence == 0:
		return nil, errors.New("invalid sequence number in request")
	case rpcClient == nil:
		return nil, errors.New("nil rpc client")
	}

	pbReq, err := req.toClusterEventReq()
	if err != nil {
		return nil, errors.Wrap(err, "decoding system notify request")
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).ClusterEvent(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS cluster event request: %+v", pbReq)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemNotifyResp)
	return resp, convertMSResponse(ur, resp)
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

	req := &SystemNotifyReq{
		Sequence: <-ef.seq,
		Event:    evt,
	}
	req.SetHostList(ef.accessPts)
	ef.client.Debugf("forwarding %s event to MS access points %v (seq: %d)",
		evt.ID, ef.accessPts, req.Sequence)

	if _, err := SystemNotify(ctx, ef.client, req); err != nil {
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
	el.log.Info(out)
	if sl := el.sysloggers[evt.Severity]; sl != nil {
		sl.Print(out)
	}
}

type newSysloggerFn func(syslog.Priority, int) (*log.Logger, error)

// newEventLogger returns an initialized EventLogger using the provided function
// to populate syslog endpoints which map to event severity identifiers.
func newEventLogger(logBasic logging.Logger, newSyslogger newSysloggerFn) *EventLogger {
	el := &EventLogger{
		log:        logBasic,
		sysloggers: make(map[events.RASSeverityID]*log.Logger),
	}

	for _, sev := range []events.RASSeverityID{
		events.RASSeverityUnknown,
		events.RASSeverityError,
		events.RASSeverityWarning,
		events.RASSeverityNotice,
	} {
		sl, err := newSyslogger(sev.SyslogPriority(), log.LstdFlags)
		if err != nil {
			logBasic.Errorf("failed to create syslogger with priority %s: %s",
				sev.SyslogPriority(), err)
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
