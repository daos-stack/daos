//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package control

import (
	"context"
	"encoding/json"
	"fmt"
	"net"
	"strings"
	"time"

	"github.com/dustin/go-humanize/english"
	"github.com/pkg/errors"
	"google.golang.org/grpc"
	"google.golang.org/protobuf/proto"

	"github.com/daos-stack/daos/src/control/build"
	pbUtil "github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hostlist"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
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

// IsMSConnectionFailure checks whether the error is an MS connection failure.
func IsMSConnectionFailure(err error) bool {
	if err == nil {
		return false
	}
	return errors.Cause(err).Error() == errMSConnectionFailure.Error()
}

type sysRequest struct {
	Ranks ranklist.RankSet
	Hosts hostlist.HostSet
}

func (req *sysRequest) SetRanks(ranks *ranklist.RankSet) {
	req.Ranks.Replace(ranks)
}

func (req *sysRequest) SetHosts(hosts *hostlist.HostSet) {
	req.Hosts.Replace(hosts)
}

type sysResponse struct {
	AbsentRanks ranklist.RankSet `json:"-"`
	AbsentHosts hostlist.HostSet `json:"-"`
}

func (resp *sysResponse) getAbsentHostsRanks(inHosts, inRanks string) error {
	ahs, err := hostlist.CreateSet(inHosts)
	if err != nil {
		return err
	}
	ars, err := ranklist.CreateRankSet(inRanks)
	if err != nil {
		return err
	}
	resp.AbsentHosts.Replace(ahs)
	resp.AbsentRanks.Replace(ars)

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
	Rank        ranklist.Rank
	URI         string
	NumContexts uint32              `json:"Nctxs"`
	FaultDomain *system.FaultDomain `json:"SrvFaultDomain"`
	InstanceIdx uint32              `json:"Idx"`
	Incarnation uint64              `json:"Incarnation"`
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
	Rank       ranklist.Rank
	State      system.MemberState
	LocalJoin  bool
	MapVersion uint32 `json:"map_version"`
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
		case IsRetryableConnErr(err), system.IsNotReady(err):
			return true
		}
		return err == errNoMsResponse
	}
	rpcClient.Debugf("DAOS system join request: %+v", pbReq)

	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		rpcClient.Debugf("failed to invoke system join RPC: %s", err)
		return nil, err
	}

	resp := new(SystemJoinResp)
	if err := convertMSResponse(ur, resp); err != nil {
		rpcClient.Debugf("DAOS system join failed: %s", err)
		return nil, err
	}

	return resp, nil
}

// SystemQueryReq contains the inputs for the system query request.
type SystemQueryReq struct {
	unaryRequest
	msRequest
	sysRequest
	retryableRequest
	FailOnUnavailable bool               // Fail without retrying if the MS is unavailable
	NotOK             bool               // Only show engines not in a joined state
	WantedStates      system.MemberState // Bitmask of desired states
}

func (req *SystemQueryReq) getStateMask() (system.MemberState, error) {
	switch {
	case req.NotOK:
		return system.AllMemberFilter &^ system.MemberStateJoined, nil
	case req.WantedStates == 0:
		return system.AllMemberFilter, nil
	case req.WantedStates > 0 && req.WantedStates <= system.AllMemberFilter:
		return req.WantedStates, nil
	default:
		return system.MemberStateUnknown, errors.Errorf("invalid member states bitmask %x",
			int(req.WantedStates))
	}
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

	mask, err := req.getStateMask()
	if err != nil {
		return nil, errors.Wrap(err, "calculating member state bitmask")
	}
	pbReq.StateMask = uint32(mask)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemQuery(ctx, pbReq)
	})
	req.retryTestFn = func(err error, _ uint) bool {
		// In the case where the caller does not want the default
		// retry behavior, return true for specific errors in order
		// to implement our own retry behavior.
		return req.FailOnUnavailable &&
			(system.IsUnavailable(err) || IsRetryableConnErr(err) ||
				system.IsNotLeader(err) || system.IsNotReplica(err))
	}
	req.retryFn = func(_ context.Context, _ uint) error {
		if req.FailOnUnavailable {
			return system.ErrRaftUnavail
		}
		return nil
	}

	rpcClient.Debugf("DAOS system query request: %s", pbUtil.Debug(pbReq))
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

	rpcClient.Debugf("DAOS system start request: %s", pbUtil.Debug(pbReq))
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
	pbReq.Force = req.Force
	pbReq.Sys = req.getSystem(rpcClient)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemStop(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS system stop request: %s", pbUtil.Debug(pbReq))
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

// SystemExcludeReq contains the inputs for the system exclude request.
type SystemExcludeReq struct {
	unaryRequest
	msRequest
	sysRequest
	Clear bool
}

// SystemExcludeResp contains the request response.
type SystemExcludeResp struct {
	sysResponse
	Results system.MemberResults
}

// Errors returns a single error combining all error messages associated with a
// system exclude response.
func (resp *SystemExcludeResp) Errors() error {
	return concatSysErrs(resp.getAbsentHostsRanksErrors(), resp.Results.Errors())
}

// SystemExclude will mark the specified ranks as administratively excluded from the system.
func SystemExclude(ctx context.Context, rpcClient UnaryInvoker, req *SystemExcludeReq) (*SystemExcludeResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T request", req)
	}

	pbReq := &mgmtpb.SystemExcludeReq{
		Hosts: req.Hosts.String(),
		Ranks: req.Ranks.String(),
		Sys:   req.getSystem(rpcClient),
		Clear: req.Clear,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemExclude(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS system exclude request: %s", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemExcludeResp)
	return resp, convertMSResponse(ur, resp)
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

// Errors returns error if any of the results indicate a failure.
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

	aliveRanks, err := ranklist.CreateRankSet("")
	if err != nil {
		return err
	}
	for _, member := range resp.Members {
		if member.State&system.AvailableMemberFilter != 0 {
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

	rpcClient.Debugf("DAOS system-erase request: %s", pbUtil.Debug(pbReq))
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
	sysRequest
}

// LeaderQueryResp contains the status of the request and, if successful, the
// MS leader and set of replicas in the system.
type LeaderQueryResp struct {
	Leader       string   `json:"current_leader"`
	Replicas     []string `json:"replicas"`
	DownReplicas []string `json:"down_replicas"`
}

// LeaderQuery requests the current Management Service leader and the set of
// MS replicas.
func LeaderQuery(ctx context.Context, rpcClient UnaryInvoker, req *LeaderQueryReq) (*LeaderQueryResp, error) {
	pbReq := &mgmtpb.LeaderQueryReq{Sys: req.getSystem(rpcClient)}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).LeaderQuery(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS system leader-query request: %s", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(LeaderQueryResp)
	if err = convertMSResponse(ur, resp); err != nil {
		return nil, errors.Wrap(err, "converting MS to LeaderQuery resp")
	}

	req.SetHostList(resp.Replicas)
	ur, err = rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	for _, hostResp := range ur.Responses {
		if hostResp.Error != nil {
			resp.DownReplicas = append(resp.DownReplicas, hostResp.Addr)
			continue
		}
	}

	return resp, nil
}

// RanksReq contains the parameters for a system ranks request.
type RanksReq struct {
	unaryRequest
	respReportCb HostResponseReportFn
	Ranks        string
	Force        bool
}

func (r *RanksReq) reportResponse(resp *HostResponse) {
	if r.respReportCb != nil && resp != nil {
		r.respReportCb(resp)
	}
}

func (r *RanksReq) SetReportCb(cb HostResponseReportFn) {
	r.respReportCb = cb
}

// RanksResp contains the response from a system ranks request.
type RanksResp struct {
	HostErrorsResp // record unresponsive hosts
	RankResults    system.MemberResults
}

// addHostResponse is responsible for validating the given HostResponse
// and adding its results to the RanksResp.
func (srr *RanksResp) addHostResponse(hr *HostResponse) (err error) {
	if hr.Error != nil {
		if err = srr.addHostError(hr.Addr, hr.Error); err != nil {
			return
		}
		return
	}

	pbResp, ok := hr.Message.(interface{ GetResults() []*sharedpb.RankResult })
	if !ok {
		return errors.Errorf("unable to unpack message: %+v", hr.Message)
	}

	memberResults := make(system.MemberResults, 0)
	if err := convert.Types(pbResp.GetResults(), &memberResults); err != nil {
		return srr.addHostError(hr.Addr, errors.Wrap(err, "type conversion failed"))
	}

	srr.RankResults = append(srr.RankResults, memberResults...)

	return
}

// invokeRPCFanout invokes unary RPC across all hosts provided in the request
// parameter and unpacks host responses and errors into a RanksResp,
// returning RanksResp's reference.
func invokeRPCFanout(ctx context.Context, rpcClient UnaryInvoker, req *RanksReq) (*RanksResp, error) {
	resps, err := rpcClient.InvokeUnaryRPCAsync(ctx, req)
	if err != nil {
		return nil, err
	}

	rr := new(RanksResp)
	for {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case hr := <-resps:
			if hr == nil {
				return rr, nil
			}

			req.reportResponse(hr)
			if err := rr.addHostResponse(hr); err != nil {
				return nil, err
			}
		}
	}
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

	rpcClient.Debugf("DAOS system prep shutdown-ranks request: %s", pbUtil.Debug(pbReq))
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

	rpcClient.Debugf("DAOS system stop-ranks request: %s", pbUtil.Debug(pbReq))
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

	rpcClient.Debugf("DAOS system reset-format-ranks request: %s", pbUtil.Debug(pbReq))
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

	rpcClient.Debugf("DAOS system start-ranks request: %s", pbUtil.Debug(pbReq))
	return invokeRPCFanout(ctx, rpcClient, req)
}

// SystemCleanupReq contains the inputs for the system cleanup request.
type SystemCleanupReq struct {
	unaryRequest
	msRequest
	sysRequest
	Machine string `json:"machine"`
}

type CleanupResult struct {
	Status int32  `json:"status"`  // Status returned from this specific evict call
	Msg    string `json:"msg"`     // Error message if Status is not Success
	PoolID string `json:"pool_id"` // Unique identifier
	Count  uint32 `json:"count"`   // Number of pools reclaimed
}

// SystemCleanupResp contains the request response.
type SystemCleanupResp struct {
	Results []*CleanupResult `json:"results"`
}

// Errors returns a single error combining all error messages associated with a
// system cleanup response.
func (scr *SystemCleanupResp) Errors() error {
	out := new(strings.Builder)

	for _, r := range scr.Results {
		if r.Status != int32(daos.Success) {
			fmt.Fprintf(out, "%s\n", r.Msg)
		}
	}

	if out.String() != "" {
		return errors.New(out.String())
	}

	return nil
}

// SystemCleanup requests resources associated with a machine name be cleanedup.
func SystemCleanup(ctx context.Context, rpcClient UnaryInvoker, req *SystemCleanupReq) (*SystemCleanupResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T request", req)
	}

	if req.Machine == "" {
		return nil, errors.New("SystemCleanup requires a machine name.")
	}

	pbReq := new(mgmtpb.SystemCleanupReq)
	pbReq.Machine = req.Machine
	pbReq.Sys = req.getSystem(rpcClient)

	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemCleanup(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS system cleanup request: %s", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemCleanupResp)
	return resp, convertMSResponse(ur, resp)
}

// SystemSetAttrReq contains the inputs for the system set-attr request.
type SystemSetAttrReq struct {
	unaryRequest
	msRequest

	Attributes map[string]string
}

// SystemSetAttr sets system attributes.
func SystemSetAttr(ctx context.Context, rpcClient UnaryInvoker, req *SystemSetAttrReq) error {
	if req == nil {
		return errors.Errorf("nil %T request", req)
	}
	if len(req.Attributes) == 0 {
		return errors.New("attributes cannot be empty")
	}

	pbReq := &mgmtpb.SystemSetAttrReq{
		Sys:        req.getSystem(rpcClient),
		Attributes: req.Attributes,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemSetAttr(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS SystemSetAttr request: %s", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return ur.getMSError()
}

type (
	// SystemGetAttrReq contains the inputs for the system get-attr request.
	SystemGetAttrReq struct {
		unaryRequest
		msRequest

		Keys []string
	}

	// SystemGetAttrResp contains the request response.
	SystemGetAttrResp struct {
		Attributes map[string]string `json:"attributes"`
	}
)

// SystemGetAttr gets system attributes.
func SystemGetAttr(ctx context.Context, rpcClient UnaryInvoker, req *SystemGetAttrReq) (*SystemGetAttrResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T request", req)
	}

	pbReq := &mgmtpb.SystemGetAttrReq{
		Sys:  req.getSystem(rpcClient),
		Keys: req.Keys,
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemGetAttr(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS SystemGetAttr request: %s", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	resp := new(SystemGetAttrResp)
	return resp, convertMSResponse(ur, resp)
}

// SystemSetPropReq contains the inputs for the system set-prop request.
type SystemSetPropReq struct {
	unaryRequest
	msRequest

	Properties map[daos.SystemPropertyKey]daos.SystemPropertyValue
}

// SystemSetProp sets system properties.
func SystemSetProp(ctx context.Context, rpcClient UnaryInvoker, req *SystemSetPropReq) error {
	if req == nil {
		return errors.Errorf("nil %T request", req)
	}
	if len(req.Properties) == 0 {
		return errors.New("properties cannot be empty")
	}

	pbReq := &mgmtpb.SystemSetPropReq{
		Sys:        req.getSystem(rpcClient),
		Properties: make(map[string]string),
	}
	for k, v := range req.Properties {
		pbReq.Properties[k.String()] = v.String()
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemSetProp(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS SystemSetProp request: %s", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return err
	}

	return errors.Wrap(ur.getMSError(), "system set-prop failed")
}

type (
	// SystemGetPropReq contains the inputs for the system get-attr request.
	SystemGetPropReq struct {
		unaryRequest
		msRequest

		Keys []daos.SystemPropertyKey
	}

	// SystemGetPropResp contains the request response.
	SystemGetPropResp struct {
		Properties []*daos.SystemProperty `json:"properties"`
	}
)

// SystemGetProp gets system attributes.
func SystemGetProp(ctx context.Context, rpcClient UnaryInvoker, req *SystemGetPropReq) (*SystemGetPropResp, error) {
	if req == nil {
		return nil, errors.Errorf("nil %T request", req)
	}

	pbReq := &mgmtpb.SystemGetPropReq{
		Sys: req.getSystem(rpcClient),
	}
	for _, k := range req.Keys {
		pbReq.Keys = append(pbReq.Keys, k.String())
	}
	req.setRPC(func(ctx context.Context, conn *grpc.ClientConn) (proto.Message, error) {
		return mgmtpb.NewMgmtSvcClient(conn).SystemGetProp(ctx, pbReq)
	})

	rpcClient.Debugf("DAOS SystemGetProp request: %s", pbUtil.Debug(pbReq))
	ur, err := rpcClient.InvokeUnaryRPC(ctx, req)
	if err != nil {
		return nil, err
	}

	msg, err := ur.getMSResponse()
	if err != nil {
		return nil, err
	}

	pbResp, ok := msg.(*mgmtpb.SystemGetPropResp)
	if !ok {
		return nil, errors.Errorf("unexpected response type: %T", msg)
	}

	sysProps := daos.SystemProperties()
	resp := new(SystemGetPropResp)
	for k, v := range pbResp.Properties {
		prop, ok := sysProps.Get(k)
		if !ok {
			rpcClient.Debugf("skipping unknown system property: %s", k)
			continue
		}

		switch pv := prop.Value.(type) {
		case *daos.CompPropVal:
			// Convert this to a simple string so that we're
			// displaying the server-computed value.
			prop.Value = daos.NewStringPropVal(v)
		default:
			if err := pv.Handler(v); err != nil {
				return nil, errors.Wrapf(err, "failed to parse system property %s", k)
			}
		}

		resp.Properties = append(resp.Properties, prop)
	}

	return resp, nil
}
