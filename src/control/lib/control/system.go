//
// (C) Copyright 2020-2021 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package control

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"time"

	"github.com/dustin/go-humanize/english"
	"github.com/golang/protobuf/proto"
	"github.com/pkg/errors"
	"google.golang.org/grpc"

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

type sysRequest struct {
	Ranks system.RankSet
	Hosts hostlist.HostSet
}

type sysResponse struct {
	AbsentRanks system.RankSet
	AbsentHosts hostlist.HostSet
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

func (resp *sysResponse) DisplayAbsentHostsRanks() string {
	switch {
	case resp.AbsentHosts.Count() > 0:
		return fmt.Sprintf("\nUnknown %s: %s",
			english.Plural(resp.AbsentHosts.Count(), "host", "hosts"),
			resp.AbsentHosts.String())
	case resp.AbsentRanks.Count() > 0:
		return fmt.Sprintf("\nUnknown %s: %s",
			english.Plural(resp.AbsentRanks.Count(), "rank", "ranks"),
			resp.AbsentRanks.String())
	default:
		return ""
	}
}

// SystemJoinReq contains the inputs for the system join request.
// TODO: Unify this with system.JoinRequest
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
func (fwdr *EventForwarder) OnEvent(ctx context.Context, evt *events.RASEvent) {
	switch {
	case evt == nil:
		fwdr.client.Debug("skip event forwarding, nil event")
		return
	case len(fwdr.accessPts) == 0:
		fwdr.client.Debug("skip event forwarding, missing access points")
		return
	}

	req := &SystemNotifyReq{
		Sequence: <-fwdr.seq,
		Event:    evt.WithIsForwarded(true),
	}
	req.SetHostList(fwdr.accessPts)
	fwdr.client.Debugf("forwarding %s event to MS access points %v (seq: %d)",
		evt.ID, fwdr.accessPts, req.Sequence)

	if _, err := SystemNotify(ctx, fwdr.client, req); err != nil {
		fwdr.client.Debugf("failed to forward event to MS: %s", err)
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
// INFO.
type EventLogger struct {
	log logging.Logger
}

// OnEvent implements the events.Handler interface.
func (el *EventLogger) OnEvent(_ context.Context, evt *events.RASEvent) {
	if evt.IsForwarded() {
		return // event has already been logged at source
	}
	el.log.Info(evt.PrintRAS())
}

// NewEventLogger returns an initialized EventLogger.
func NewEventLogger(log logging.Logger) *EventLogger {
	return &EventLogger{log: log}
}

// SystemQueryReq contains the inputs for the system query request.
type SystemQueryReq struct {
	unaryRequest
	msRequest
	sysRequest
}

// SystemQueryResp contains the request response.
type SystemQueryResp struct {
	sysResponse
	Members system.Members
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
	pbReq.Sys = req.getSystem()

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemQuery(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system query request: %s", req)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemQueryResp)
	return resp, convertMSResponse(ur, resp)
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
	pbReq.Sys = req.getSystem()

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
	pbReq.Sys = req.getSystem()

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

// SystemResetFormatReq contains the inputs for the request.
type SystemResetFormatReq struct {
	unaryRequest
	msRequest
}

// SystemResetFormatResp contains the request response.
type SystemResetFormatResp struct {
	Results system.MemberResults
}

// SystemReformat will reformat and start rank after a controlled shutdown of DAOS system.
//
// First phase trigger format reset on each rank in membership registry, if
// successful, putting selected harness managed instances in "awaiting format"
// state (but not proceeding to starting the io_server process runner).
//
// Second phase is to perform storage format on each host which, if successful,
// will reformat storage, un-block "awaiting format" state and start the
// io_server process. SystemReformat() will only return when relevant io_server
// processes are running and ready.
//
// This method handles request sent from management client app e.g. 'dmg'.
//
// The SystemResetFormat and StorageFormat control API requests are sent to
// mgmt_system.go method of the same name. The triggered method uses the control
// API to fanout to (selection or all) gRPC servers listening as part of the
// DAOS system and retrieve results from the selected ranks hosted there.
func SystemReformat(ctx context.Context, rpcClient UnaryInvoker, resetReq *SystemResetFormatReq) (*StorageFormatResp, error) {
	if resetReq == nil {
		return nil, errors.Errorf("nil %T request", resetReq)
	}

	pbReq := new(mgmtpb.SystemResetFormatReq)
	pbReq.Sys = resetReq.getSystem()

	resetReq.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemResetFormat(ctx, pbReq)
	})
	rpcClient.Debugf("DAOS system-reset-format request: %s", resetReq)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, resetReq)
	if err != nil {
		return nil, err
	}

	// MS response will contain collated results for all ranks
	resetResp := new(SystemResetFormatResp)
	if err = convertMSResponse(ur, resetResp); err != nil {
		return nil, errors.WithMessage(err, "converting MS to reformat resp")
	}

	resetRankErrors, hostList, err := getResetRankErrors(resetResp.Results)
	if err != nil {
		return nil, err
	}

	if len(resetRankErrors) > 0 {
		reformatResp := new(StorageFormatResp)

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
				if err := reformatResp.HostErrorsResp.addHostError(addr, err); err != nil {
					return nil, err
				}
			}
		}

		return reformatResp, nil
	}

	// all requested ranks in AwaitFormat state, trigger format
	formatReq := &StorageFormatReq{Reformat: true}
	formatReq.SetHostList(hostList)

	rpcClient.Debugf("DAOS storage-format request: %s", formatReq)

	return StorageFormat(ctx, rpcClient, formatReq)
}

// LeaderQueryReq contains the inputs for the leader query request.
type LeaderQueryReq struct {
	unaryRequest
	msRequest
	System string
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
			Sys: req.System,
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
			Sys: req.getSystem(),
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
