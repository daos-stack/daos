//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package main

import (
	"context"
	"fmt"
	"os"
	"time"

	"github.com/daos-stack/daos/src/control/common"
	mgmtpb "github.com/daos-stack/daos/src/control/common/proto/mgmt"
	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/security/auth"
)

const (
	// Agent-internal methods not linked to engine handlers.
	flushAllHandles drpc.MgmtMethod = drpc.MgmtMethod(^uint32(0) >> 1)
)

// dbgId returns a truncated representation of the UUID string.
func dbgId(uuidStr string) string {
	return uuidStr[:8]
}

type procMonRequest struct {
	// Pid of the process that the request is for
	pid int32
	// Whether the message is an attach or disconnect. Uses drpc method identifiers.
	action drpc.MgmtMethod
	// The UUID of the pool if action is poolconnect/disconnect
	poolUUID string
	// The UUID of the pool handle associated with this request
	poolHandleUUID string
	// If the request should be blocking, the caller should
	// supply a channel to be closed when the request is
	// complete.
	doneChan chan struct{}
}

type procMonResponse struct {
	// Pid the response is coming from
	pid int32
	// Error indicating why the process "died"
	err error
}

type poolHandleMap map[string]common.StringSet

func (phm poolHandleMap) add(poolUUID, handleUUID string) {
	if _, found := phm[poolUUID]; !found {
		phm[poolUUID] = common.NewStringSet()
	}
	phm[poolUUID].Add(handleUUID)
}

type procInfo struct {
	log       logging.Logger
	pid       int32
	name      string
	cancelCtx func()
	response  chan *procMonResponse
	handles   poolHandleMap
}

func checkProcPidExists(pid int32) error {
	pidPath := fmt.Sprintf("/proc/%d", pid)
	_, err := os.Stat(pidPath)
	return err
}

func (p *procInfo) sendResponse(ctx context.Context, pid int32, err error) {
	response := &procMonResponse{
		pid: pid,
		err: err,
	}
	select {
	case <-ctx.Done():
	case p.response <- response:
	}
}

const MonWaitTime = 3 * time.Second

// monitorProcess is used by procMon to kick off monitoring individual processes
// under their own child context to allow for terminating individual monitoring routines.
func (p *procInfo) monitorProcess(ctx context.Context) {
	monTicker := time.NewTicker(MonWaitTime)
	defer monTicker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-monTicker.C:
			if err := checkProcPidExists(p.pid); err != nil {
				if os.IsNotExist(err) {
					p.sendResponse(ctx, p.pid, fmt.Errorf("%s terminated unexpectedly", p))
				} else {
					p.sendResponse(ctx, p.pid, err)
				}
				return
			}
		}
	}
}

func (p *procInfo) String() string {
	var name string
	if p.name != "" {
		name = fmt.Sprintf(" (%s)", p.name)
	}
	return fmt.Sprintf("pid:%d%s", p.pid, name)
}

// procMon is the top level process monitoring struct which accepts requests to
// monitor and disconnect processes. Once created it is started by passing a
// context into the startMonitoring call.
type procMon struct {
	log        logging.Logger
	procs      map[int32]*procInfo
	request    chan *procMonRequest
	response   chan *procMonResponse
	ctlInvoker control.Invoker
	systemName string
}

// NewProcMon creates a new process monitor struct setting initializing the
// internal process map and the request channel.
func NewProcMon(logger logging.Logger, ctlInvoker control.Invoker, systemName string) *procMon {
	return &procMon{
		log:        logger,
		procs:      make(map[int32]*procInfo),
		request:    make(chan *procMonRequest),
		response:   make(chan *procMonResponse),
		ctlInvoker: ctlInvoker,
		systemName: systemName,
	}
}

func (p *procMon) AddPoolHandle(ctx context.Context, Pid int32, poolReq *mgmtpb.PoolMonitorReq) {
	req := &procMonRequest{
		pid:            Pid,
		action:         drpc.MethodNotifyPoolConnect,
		poolUUID:       poolReq.PoolUUID,
		poolHandleUUID: poolReq.PoolHandleUUID,
	}
	p.submitRequest(ctx, req)
}

func (p *procMon) RemovePoolHandle(ctx context.Context, Pid int32, poolReq *mgmtpb.PoolMonitorReq) {
	req := &procMonRequest{
		pid:            Pid,
		action:         drpc.MethodNotifyPoolDisconnect,
		poolUUID:       poolReq.PoolUUID,
		poolHandleUUID: poolReq.PoolHandleUUID,
	}
	p.submitRequest(ctx, req)
}

func (p *procMon) NotifyExit(ctx context.Context, Pid int32) {
	req := &procMonRequest{
		pid:    Pid,
		action: drpc.MethodNotifyExit,
	}
	p.submitRequest(ctx, req)
}

// FlushAllHandles submits a request to flush (evict and remove) all known
// open pool handles for local DAOS client processes. Blocks until the
// request has been completely processed.
func (p *procMon) FlushAllHandles(ctx context.Context) {
	var onShutdown string
	if agentIsShuttingDown(ctx) {
		onShutdown = " on shutdown"
	}
	p.log.Infof("flushing all open local pool handles%s", onShutdown)

	done := make(chan struct{})
	p.submitRequest(ctx, &procMonRequest{
		action:   flushAllHandles,
		doneChan: done,
	})

	<-done
}

func (p *procMon) submitRequest(ctx context.Context, request *procMonRequest) {
	select {
	case <-ctx.Done():
	case p.request <- request:
	}
}

func (p *procMon) handleNotifyPoolConnect(ctx context.Context, request *procMonRequest) {
	info, found := p.procs[request.pid]
	if !found {
		procName, err := common.GetProcName(int(request.pid))
		if err != nil {
			p.log.Errorf("failed to get process name for pid %d: %s", request.pid, err)
			return
		}

		child, cancel := context.WithCancel(ctx)
		info = &procInfo{
			log:       p.log,
			pid:       request.pid,
			name:      procName,
			cancelCtx: cancel,
			response:  p.response,
			handles:   make(poolHandleMap),
		}

		p.procs[request.pid] = info
		go info.monitorProcess(child)
	}

	p.log.Debugf("%s, connect %s/%s", info, dbgId(request.poolUUID), dbgId(request.poolHandleUUID))
	info.handles.add(request.poolUUID, request.poolHandleUUID)
}

func (p *procMon) handleNotifyPoolDisconnect(request *procMonRequest) {
	info, found := p.procs[request.pid]
	if !found || len(info.handles) == 0 {
		return
	}

	_, found = info.handles[request.poolUUID][request.poolHandleUUID]
	if found {
		p.log.Debugf("%s, disconnect %s/%s", info, dbgId(request.poolUUID), dbgId(request.poolHandleUUID))
		delete(info.handles[request.poolUUID], request.poolHandleUUID)
		if len(info.handles[request.poolUUID]) == 0 {
			delete(info.handles, request.poolUUID)
		}
		if len(info.handles) == 0 {
			info.cancelCtx()
			delete(p.procs, info.pid)
		}
	}
}

// A process will leak handles when it either dies illegally or exits without
// calling daos_pool_disconnect on the handles it has open. This will be called
// if we detect a process terminating without disconnect, or if during
// disconnect we still have a list of open pool handles for the process.
func (p *procMon) cleanupLeakedHandles(ctx context.Context, info *procInfo) {
	if len(info.handles) == 0 {
		return
	}

	for poolUUID, handleMap := range info.handles {
		if len(handleMap) == 0 {
			continue
		}

		var fromPid string
		if info.pid != 0 {
			fromPid = fmt.Sprintf(" from %s", info)
		}
		ctxStr := "leaked handles"
		if agentIsShuttingDown(ctx) {
			ctxStr = "handles on shutdown"
		}
		p.log.Infof("pool %s: cleaning up %d %s%s", poolUUID, len(handleMap), ctxStr, fromPid)

		req := &control.PoolEvictReq{ID: poolUUID, Handles: handleMap.ToSlice()}
		req.SetSystem(p.systemName)

		err := control.PoolEvict(ctx, p.ctlInvoker, req)
		if err != nil {
			p.log.Errorf("pool %s: failed to evict %d handles: %s", poolUUID, len(handleMap), err)
		}
	}

	delete(p.procs, info.pid)
}

func (p *procMon) handleNotifyExit(ctx context.Context, request *procMonRequest) {
	info, found := p.procs[request.pid]
	if found {
		info.cancelCtx()
		p.cleanupLeakedHandles(ctx, info)
	}
}

func (p *procMon) flushAllHandles(ctx context.Context) {
	// create a single map of open handles to reduce the number of RPCs
	allPoolHandles := make(poolHandleMap)

	for _, info := range p.procs {
		for pool, handles := range info.handles {
			for handle := range handles {
				allPoolHandles.add(pool, handle)
			}
		}

		// NB: This is best-effort cleanup, so if something fails we can't
		// retry it.
		delete(p.procs, info.pid)
	}

	p.cleanupLeakedHandles(ctx, &procInfo{handles: allPoolHandles})
}

func (p *procMon) handleRequests(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case request := <-p.request:
			switch request.action {
			case drpc.MethodNotifyPoolConnect:
				p.handleNotifyPoolConnect(ctx, request)
			case drpc.MethodNotifyPoolDisconnect:
				p.handleNotifyPoolDisconnect(request)
			case drpc.MethodNotifyExit:
				p.handleNotifyExit(ctx, request)
			case flushAllHandles:
				p.flushAllHandles(ctx)
			default:
				p.log.Errorf("failed to handle request with invalid action type %s", request.action)
			}

			if request.doneChan != nil {
				close(request.doneChan)
			}
		case resp := <-p.response:
			info, found := p.procs[resp.pid]
			if found {
				p.cleanupLeakedHandles(ctx, info)
			}
		}
	}
}

// startMonitoring is the main driver which starts the process monitor. The
// passed in context is used to terminate all monitoring in the event of shutdown.
func (p *procMon) startMonitoring(ctx context.Context, cleanOnStart bool) {
	if cleanOnStart {
		p.cleanupServerHandles(ctx)
	}
	go p.handleRequests(ctx)
}

// cleanupServerHandles can be run to revoke all pool handles associated with a given machine/host.
// DAOS server will be instructed to cleanup handles associated with the source machine/host.
func (p *procMon) cleanupServerHandles(ctx context.Context) {
	machineName, err := auth.GetMachineName()
	if err != nil {
		p.log.Errorf("hostname lookup: %s, cannot cleanup handles", err)
		return
	}
	if machineName == "" {
		p.log.Errorf("empty machine name is invalid, cannot cleanup handles", err)
		return
	}

	req := &control.SystemCleanupReq{Machine: machineName}
	req.SetSystem(p.systemName)

	msg := fmt.Sprintf("machine %q", machineName)
	resp, err := control.SystemCleanup(ctx, p.ctlInvoker, req)
	if err != nil {
		p.log.Errorf("%s: failed to run system cleanup: %s", msg, err)
		return
	}
	err = resp.Errors()
	if err != nil {
		p.log.Errorf("%s: system cleanup ran with error: %s", msg, err)
		return
	}

	msg = fmt.Sprintf("Running system cleanup on this %s", msg)
	if len(resp.Results) == 0 {
		p.log.Debugf("%s: no pool handles revoked", msg)
		return
	}
	msgRvkd := "Pool handles revoked: "
	for i, r := range resp.Results {
		msgRvkd = fmt.Sprintf("%s %s (%d)", msgRvkd, r.PoolID, r.Count)
		if i != len(resp.Results)-1 {
			msgRvkd += ", "
		}
	}
	p.log.Infof("%s: %s", msg, msgRvkd)
}
