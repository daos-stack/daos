//
// (C) Copyright 2020-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"encoding/json"
	"log"
	"log/syslog"
	"net"
	"strings"
	"time"

	"github.com/dustin/go-humanize/english"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/system"
)

const (
	// SystemJoinTimeout defines the overall amount of time a system join attempt
	// can take. It should be an extremely generous value, in order to
	// accommodate (re)joins during leadership upheaval and other scenarios
	// that are expected to eventually resolve.
	SystemJoinTimeout = 1 * time.Hour // effectively "forever", just keep retrying
	// SystemJoinRetryTimeout defines the amount of time a retry attempt can take. It
	// should be set low in order to ensure that individual join attempts retry quickly.
	SystemJoinRetryTimeout = 10 * time.Second
)

var (
	errMSConnectionFailure = errors.Errorf("unable to contact the %s", build.ManagementServiceName)
)

type sysRequest struct {
	Ranks system.RankSet
	Hosts hostlist.HostSet
}

type sysResponse struct {
	AbsentRanks system.RankSet   `json:"-"`
	AbsentHosts hostlist.HostSet `json:"-"`
}

func (resp *sysResponse) getAbsentHostsRanks(inHosts, inRanks string) error {
	ahs, err := hostlist.CreateSet(inHosts)
	if err != nil {
		return err
	}
	ars, err := system.CreateRankSet(inRanks)
	if err != nil {
		return err
	}
	resp.AbsentHosts.ReplaceSet(ahs)
	resp.AbsentRanks.ReplaceSet(ars)

	return nil
}

func (resp *sysResponse) getAbsentHostsRanksErrors() error {
	var errMsgs []string

	if resp.AbsentHosts.Count() > 0 {
		errMsgs = append(errMsgs, "non-existent hosts "+resp.AbsentHosts.String())
	}
	if resp.AbsentRanks.Count() > 0 {
		errMsgs = append(errMsgs, "non-existent ranks "+resp.AbsentRanks.String())
	}

	if len(errMsgs) > 0 {
		return errors.New(strings.Join(errMsgs, ", "))
	}

	return nil
}

// SystemJoinReq contains the inputs for the system join request.
type SystemJoinReq struct {
	unaryRequest
	msRequest
	retryableRequest
	ControlAddr *net.TCPAddr
	UUID        string
	Rank        system.Rank
	URI         string
	NumContexts uint32              `json:"Nctxs"`
	FaultDomain *system.FaultDomain `json:"SrvFaultDomain"`
	InstanceIdx uint32              `json:"Idx"`
}

// MarshalJSON packs SystemJoinResp struct into a JSON message.
func (req *SystemJoinReq) MarshalJSON() ([]byte, error) {
	// use a type alias to leverage the default marshal for
	// most fields
	type toJSON SystemJoinReq
	return json.Marshal(&struct {
		Addr           string
		SrvFaultDomain string
		*toJSON
	}{
		Addr:           req.ControlAddr.String(),
		SrvFaultDomain: req.FaultDomain.String(),
		toJSON:         (*toJSON)(req),
	})
}

// SystemJoinResp contains the request response.
type SystemJoinResp struct {
	Rank      system.Rank
	State     system.MemberState
	LocalJoin bool
}

// SystemJoin will attempt to join a new member to the DAOS system.
func SystemJoin(ctx context.Context, rpcClient UnaryInvoker, req *SystemJoinReq) (*SystemJoinResp, error) {
	pbReq := new(mgmtpb.JoinReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, err
	}
	pbReq.Sys = req.getSystem(rpcClient)
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).Join(ctx, pbReq)
	})
	req.SetTimeout(SystemJoinTimeout)
	req.retryTimeout = SystemJoinRetryTimeout
	req.retryTestFn = func(err error, _ uint) bool {
		switch {
		case IsConnectionError(err), system.IsUnavailable(err):
			return true
		}
		return false
	}
	rpcClient.Debugf("DAOS system join request: %+v", pbReq)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemJoinResp)
	if err := convertMSResponse(ur, resp); err != nil {
		rpcClient.Debugf("DAOS system join failed: %s", err)
		return nil, err
	}

	rpcClient.Debugf("DAOS system join response: %+v", resp)
	return resp, nil
}

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

// SystemQueryReq contains the inputs for the system query request.
type SystemQueryReq struct {
	unaryRequest
	msRequest
	sysRequest
	retryableRequest
	FailOnUnavailable bool // Fail without retrying if the MS is unavailable.
}

// SystemQueryResp contains the request response.
type SystemQueryResp struct {
	sysResponse
	Members system.Members `json:"members"`
}

// UnmarshalJSON unpacks JSON message into SystemQueryResp struct.
func (resp *SystemQueryResp) UnmarshalJSON(data []byte) error {
	type Alias SystemQueryResp
	aux := &struct {
		AbsentHosts string
		AbsentRanks string
		*Alias
	}{
		Alias: (*Alias)(resp),
	}
	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}
	if err := resp.getAbsentHostsRanks(aux.AbsentHosts, aux.AbsentRanks); err != nil {
		return err
	}

	return nil
}

// Errors returns a single error combining all error messages associated with a
// system query response.
func (resp *SystemQueryResp) Errors() error {
	return resp.getAbsentHostsRanksErrors()
}

// SystemQuery requests DAOS system status.
//
// Handles MS requests sent from management client app e.g. 'dmg' and calls into
// mgmt_system.go method of the same name. The triggered method uses the control
// API to fanout to (selection or all) gRPC servers listening as part of the
// DAOS system and retrieve results from the selected ranks hosted there.
func SystemQuery(ctx context.Context, rpcClient UnaryInvoker, req *SystemQueryReq) (*SystemQueryResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T request", req)
	}

	pbReq := new(mgmtpb.SystemQueryReq)
	pbReq.Hosts = req.Hosts.String()
	pbReq.Ranks = req.Ranks.String()
	pbReq.Sys = req.getSystem(rpcClient)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemQuery(ctx, pbReq)
	})
	req.retryTestFn = func(err error, _ uint) bool {
		// In the case where the caller does not want the default
		// retry behavior, return true for specific errors in order
		// to implement our own retry behavior.
		return req.FailOnUnavailable &&
			(system.IsUnavailable(err) || IsConnectionError(err) ||
				system.IsNotLeader(err) || system.IsNotReplica(err))
	}
	req.retryFn = func(_ context.Context, _ uint) error {
		if req.FailOnUnavailable {
			return system.ErrRaftUnavail
		}
		return nil
	}
	rpcClient.Debugf("DAOS system query request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemQueryResp)
	return resp, convertMSResponse(ur, resp)
}

func concatSysErrs(errSys, errRes error) error {
	var errMsgs []string

	if errSys != nil {
		errMsgs = append(errMsgs, errSys.Error())
	}
	if errRes != nil {
		errMsgs = append(errMsgs, "check results for "+errRes.Error())
	}

	if len(errMsgs) > 0 {
		return errors.New(strings.Join(errMsgs, ", "))
	}

	return nil
}

// SystemStartReq contains the inputs for the system start request.
type SystemStartReq struct {
	unaryRequest
	msRequest
	sysRequest
}

// SystemStartResp contains the request response.
type SystemStartResp struct {
	sysResponse
	Results system.MemberResults // resulting from harness starts
}

// UnmarshalJSON unpacks JSON message into SystemStartResp struct.
func (resp *SystemStartResp) UnmarshalJSON(data []byte) error {
	type Alias SystemStartResp
	aux := &struct {
		AbsentHosts string
		AbsentRanks string
		*Alias
	}{
		Alias: (*Alias)(resp),
	}
	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}
	if err := resp.getAbsentHostsRanks(aux.AbsentHosts, aux.AbsentRanks); err != nil {
		return err
	}

	return nil
}

// Errors returns a single error combining all error messages associated with a
// system start response.
func (resp *SystemStartResp) Errors() error {
	return concatSysErrs(resp.getAbsentHostsRanksErrors(), resp.Results.Errors())
}

// SystemStart will perform a start after a controlled shutdown of DAOS system.
//
// Handles MS requests sent from management client app e.g. 'dmg' and calls into
// mgmt_system.go method of the same name. The triggered method uses the control
// API to fanout to (selection or all) gRPC servers listening as part of the
// DAOS system and retrieve results from the selected ranks hosted there.
func SystemStart(ctx context.Context, rpcClient UnaryInvoker, req *SystemStartReq) (*SystemStartResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T request", req)
	}

	pbReq := new(mgmtpb.SystemStartReq)
	pbReq.Hosts = req.Hosts.String()
	pbReq.Ranks = req.Ranks.String()
	pbReq.Sys = req.getSystem(rpcClient)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemStart(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system start request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemStartResp)
	return resp, convertMSResponse(ur, resp)
}

// SystemStopReq contains the inputs for the system stop command.
type SystemStopReq struct {
	unaryRequest
	msRequest
	sysRequest
	Prep  bool
	Kill  bool
	Force bool
}

// SystemStopResp contains the request response.
type SystemStopResp struct {
	sysResponse
	Results system.MemberResults
}

// UnmarshalJSON unpacks JSON message into SystemStopResp struct.
func (resp *SystemStopResp) UnmarshalJSON(data []byte) error {
	type Alias SystemStopResp
	aux := &struct {
		AbsentHosts string
		AbsentRanks string
		*Alias
	}{
		Alias: (*Alias)(resp),
	}
	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}
	if err := resp.getAbsentHostsRanks(aux.AbsentHosts, aux.AbsentRanks); err != nil {
		return err
	}

	return nil
}

// Errors returns a single error combining all error messages associated with a
// system stop response.
func (resp *SystemStopResp) Errors() error {
	return concatSysErrs(resp.getAbsentHostsRanksErrors(), resp.Results.Errors())
}

// SystemStop will perform a two-phase controlled shutdown of DAOS system and a
// list of remaining system members on failure.
//
// Handles MS requests sent from management client app e.g. 'dmg' and calls into
// mgmt_system.go method of the same name. The triggered method uses the control
// API to fanout to (selection or all) gRPC servers listening as part of the
// DAOS system and retrieve results from the selected ranks hosted there.
func SystemStop(ctx context.Context, rpcClient UnaryInvoker, req *SystemStopReq) (*SystemStopResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T request", req)
	}

	pbReq := new(mgmtpb.SystemStopReq)
	pbReq.Hosts = req.Hosts.String()
	pbReq.Ranks = req.Ranks.String()
	pbReq.Prep = req.Prep
	pbReq.Kill = req.Kill
	pbReq.Force = req.Force
	pbReq.Sys = req.getSystem(rpcClient)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemStop(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system stop request: %+v", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemStopResp)
	return resp, convertMSResponse(ur, resp)
}

// getResetHostErrors maps rank error messages to hosts that experience them.
func getResetRankErrors(results system.MemberResults) (map[string][]string, []string, error) {
	rankErrors := make(map[string][]string) // hosts that experience a specific rank err
	hosts := make(map[string]struct{})
	for _, result := range results {
		if result.Addr == "" {
			return nil, nil,
				errors.Errorf("host address missing for rank %d result", result.Rank)
		}
		if !result.Errored {
			hosts[result.Addr] = struct{}{}
			continue
		}
		if result.Msg == "" {
			result.Msg = "error message missing for rank result"
		}

		rankErrors[result.Msg] = append(rankErrors[result.Msg], result.Addr)
	}

	goodHosts := make([]string, 0) // hosts that have >0 successful rank results
	for host := range hosts {
		goodHosts = append(goodHosts, host)
	}

	return rankErrors, goodHosts, nil
}

// SystemEraseReq contains the inputs for a system erase request.
type SystemEraseReq struct {
	msRequest
	unaryRequest
	retryableRequest
}

// SystemEraseResp contains the results of a system erase request.
type SystemEraseResp struct {
	HostErrorsResp
	Results system.MemberResults
}

func (resp *SystemEraseResp) Errors() error {
	return resp.Results.Errors()
}

// checkSystemErase queries system to interrogate membership before deciding
// whether a system erase is appropriate.
func checkSystemErase(ctx context.Context, rpcClient UnaryInvoker) error {
	resp, err := SystemQuery(ctx, rpcClient, &SystemQueryReq{FailOnUnavailable: true})
	if err != nil {
		// If the AP hasn't been started, it will respond as if it
		// is not a replica.
		if system.IsNotReplica(err) || system.IsUnavailable(err) {
			return nil
		}
		return errors.Wrap(err, "System-Query command failed")
	}

	if len(resp.Members) == 0 {
		return nil
	}

	aliveRanks, err := system.CreateRankSet("")
	if err != nil {
		return err
	}
	for _, member := range resp.Members {
		if member.State()&system.AvailableMemberFilter != 0 {
			aliveRanks.Add(member.Rank)
		}
	}
	if aliveRanks.Count() > 0 {
		return errors.Errorf(
			"system erase requires the following %s to be stopped: %s",
			english.Plural(aliveRanks.Count(), "rank", "ranks"),
			aliveRanks.String())
	}

	return nil
}

// SystemErase initiates a wipe of system metadata prior to reformatting storage.
func SystemErase(ctx context.Context, rpcClient UnaryInvoker, req *SystemEraseReq) (*SystemEraseResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T request", req)
	}

	if err := checkSystemErase(ctx, rpcClient); err != nil {
		return nil, err
	}

	pbReq := new(mgmtpb.SystemEraseReq)
	pbReq.Sys = req.getSystem(rpcClient)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemErase(ctx, pbReq)
	})
	req.retryTestFn = func(err error, _ uint) bool {
		return system.IsUnavailable(err)
	}
	req.retryFn = func(_ context.Context, _ uint) error {
		return system.ErrRaftUnavail
	}
	rpcClient.Debugf("DAOS system-erase request: %s", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	// MS response will contain collated results for all ranks
	resp := new(SystemEraseResp)
	if err = convertMSResponse(ur, resp); err != nil {
		return nil, errors.Wrap(err, "converting MS to erase resp")
	}

	resetRankErrors, _, err := getResetRankErrors(resp.Results)
	if err != nil {
		return nil, err
	}

	if len(resetRankErrors) > 0 {
		// create "X ranks failed: err..." error entries for each host address
		// a single host maybe associated with multiple error entries in HEM
		for msg, addrs := range resetRankErrors {
			hostOccurrences := make(map[string]int)
			for _, addr := range addrs {
				hostOccurrences[addr]++
			}
			for addr, occurrences := range hostOccurrences {
				err := errors.Errorf("%s failed: %s",
					english.Plural(occurrences, "rank", "ranks"), msg)
				if err := resp.HostErrorsResp.addHostError(addr, err); err != nil {
					return nil, err
				}
			}
		}
	}

	return resp, nil
}

// LeaderQueryReq contains the inputs for the leader query request.
type LeaderQueryReq struct {
	unaryRequest
	msRequest
}

// LeaderQueryResp contains the status of the request and, if successful, the
// MS leader and set of replicas in the system.
type LeaderQueryResp struct {
	Leader   string `json:"CurrentLeader"`
	Replicas []string
}

// LeaderQuery requests the current Management Service leader and the set of
// MS replicas.
func LeaderQuery(ctx context.Context, rpcClient UnaryInvoker, req *LeaderQueryReq) (*LeaderQueryResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).LeaderQuery(ctx, &mgmtpb.LeaderQueryReq{
			Sys: req.getSystem(rpcClient),
		})
	})
	rpcClient.Debugf("DAOS system leader-query request: %s", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(LeaderQueryResp)
	return resp, convertMSResponse(ur, resp)
}

// ListPoolsReq contains the inputs for the list pools command.
type ListPoolsReq struct {
	unaryRequest
	msRequest
}

// ListPoolsResp contains the status of the request and, if successful, the list
// of pools in the system.
type ListPoolsResp struct {
	Status int32
	Pools  []*common.PoolDiscovery `json:"pools"`
}

// ListPools fetches the list of all pools and their service replicas from the
// system.
func ListPools(ctx context.Context, rpcClient UnaryInvoker, req *ListPoolsReq) (*ListPoolsResp, error) {
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).ListPools(ctx, &mgmtpb.ListPoolsReq{
			Sys: req.getSystem(rpcClient),
		})
	})
	rpcClient.Debugf("DAOS system list-pools request: %s", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(ListPoolsResp)
	return resp, convertMSResponse(ur, resp)
}

// RanksReq contains the parameters for a system ranks request.
type RanksReq struct {
	unaryRequest
	Ranks string
	Force bool
}

// RanksResp contains the response from a system ranks request.
type RanksResp struct {
	HostErrorsResp // record unresponsive hosts
	RankResults    system.MemberResults
}

// addHostResponse is responsible for validating the given HostResponse
// and adding its results to the RanksResp.
func (srr *RanksResp) addHostResponse(hr *HostResponse) (err error) {
	pbResp, ok := hr.Message.(interface{ GetResults() []*sharedpb.RankResult })
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	memberResults := make(system.MemberResults, 0)
	if err := convert.Types(pbResp.GetResults(), &memberResults); err != nil {
		if srr.HostErrors == nil {
			srr.HostErrors = make(HostErrorsMap)
		}
		return srr.HostErrors.Add(hr.Addr, err)
	}

	srr.RankResults = append(srr.RankResults, memberResults...)

	return
}

// invokeRPCFanout invokes unary RPC across all hosts provided in the request
// parameter and unpacks host responses and errors into a RanksResp,
// returning RanksResp's reference.
func invokeRPCFanout(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	rr := new(RanksResp)
	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			if err := rr.addHostError(hostResp.Addr, hostResp.Error); err != nil {
				return nil, err
			}
			continue
		}

		if err := rr.addHostResponse(hostResp); err != nil {
			return nil, err
		}
	}

	return rr, nil
}

// PrepShutdownRanks concurrently performs prep shutdown ranks across all hosts
// supplied in the request's hostlist.
//
// This is called from method of the same name in server/ctl_system.go with a
// populated host list in the request parameter and blocks until all results
// (successful or otherwise) are received after invoking fan-out.
// Returns a single response structure containing results generated with
// request responses from each selected rank.
func PrepShutdownRanks(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	pbReq := new(ctlpb.RanksReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, errors.Wrapf(err, "convert request type %T->%T", req, pbReq)
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).PrepShutdownRanks(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system prep shutdown-ranks request: %+v", req)

	return invokeRPCFanout(ctx, rpcClient, req)
}

// StopRanks concurrently performs stop ranks across all hosts supplied in the
// request's hostlist.
//
// This is called from method of the same name in server/ctl_system.go with a
// populated host list in the request parameter and blocks until all results
// (successful or otherwise) are received after invoking fan-out.
// Returns a single response structure containing results generated with
// request responses from each selected rank.
func StopRanks(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	pbReq := new(ctlpb.RanksReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, errors.Wrapf(err, "convert request type %T->%T", req, pbReq)
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).StopRanks(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system stop-ranks request: %+v", req)

	return invokeRPCFanout(ctx, rpcClient, req)
}

// ResetFormatRanks concurrently resets format state on ranks across all hosts
// supplied in the request's hostlist.
//
// This is called from SystemResetFormat in server/ctl_system.go with a
// populated host list in the request parameter and blocks until all results
// (successful or otherwise) are received after invoking fan-out.
// Returns a single response structure containing results generated with
// request responses from each selected rank.
func ResetFormatRanks(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	pbReq := new(ctlpb.RanksReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, errors.Wrapf(err, "convert request type %T->%T", req, pbReq)
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).ResetFormatRanks(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system reset-format-ranks request: %+v", req)

	return invokeRPCFanout(ctx, rpcClient, req)
}

// StartRanks concurrently performs start ranks across all hosts
// supplied in the request's hostlist.
//
// This is called from SystemStart in server/ctl_system.go with a
// populated host list in the request parameter and blocks until all results
// (successful or otherwise) are received after invoking fan-out.
// Returns a single response structure containing results generated with
// request responses from each selected rank.
func StartRanks(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	pbReq := new(ctlpb.RanksReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, errors.Wrapf(err, "convert request type %T->%T", req, pbReq)
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).StartRanks(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system start-ranks request: %+v", req)

	return invokeRPCFanout(ctx, rpcClient, req)
}

// PingRanks concurrently performs ping on ranks across all hosts
// supplied in the request's hostlist.
//
// This is called from SystemQuery in server/ctl_system.go with a
// populated host list in the request parameter and blocks until all results
// (successful or otherwise) are received after invoking fan-out.
// Returns a single response structure containing results generated with
// request responses from each selected rank.
func PingRanks(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	pbReq := new(ctlpb.RanksReq)
	if err := convert.Types(req, pbReq); err != nil {
		return nil, errors.Wrapf(err, "convert request type %T->%T", req, pbReq)
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return ctlpb.NewCtlSvcClient(conn).PingRanks(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system ping-ranks request: %+v", req)

	return invokeRPCFanout(ctx, rpcClient, req)
}
