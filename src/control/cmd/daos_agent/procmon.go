//
// (C) Copyright 2020 Intel Corporation.
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

package main

import (
	"context"
	"fmt"
	"os"
	"syscall"
	"time"

	"github.com/daos-stack/daos/src/control/drpc"
	"github.com/daos-stack/daos/src/control/logging"
)

type procMonRequest struct {
	// Pid of the process that the request is for
	pid int32
	// Whether the message is an attach or disconnect. Uses drpc method identifiers.
	action drpc.MgmtMethod
}

type procMonResponse struct {
	// Pid the response is coming from
	pid int32
	// Error indicating why the process "died"
	err error
}

type procInfo struct {
	log       logging.Logger
	pid       int32
	cancelCtx func()
	response  chan *procMonResponse
}

func getProcPidInode(pid int32) (uint64, error) {
	pidPath := fmt.Sprintf("/proc/%d", pid)
	info, err := os.Stat(pidPath)
	if err != nil {
		return 0, err
	}

	// Make sure info's underlying interface is correct.
	stat, ok := info.Sys().(*syscall.Stat_t)
	if !ok {
		return 0, fmt.Errorf("Underlying type of FileInfo.Sys() is not Stat_t")
	}
	return stat.Ino, nil
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
	Ino, err := getProcPidInode(p.pid)
	if err != nil {
		p.sendResponse(ctx, p.pid, err)
		return
	}

	p.log.Debugf("Monitoring pid:%d\n", p.pid)
	for {
		select {
		case <-ctx.Done():
			return
		case <-time.After(MonWaitTime):
			newIno, newErr := getProcPidInode(p.pid)
			if newErr != nil {
				if os.IsNotExist(newErr) {
					p.sendResponse(ctx, p.pid, fmt.Errorf("Pid %d terminated unexpectedly", p.pid))
				} else {
					p.sendResponse(ctx, p.pid, newErr)
				}
				return
			}
			if newIno != Ino {
				p.sendResponse(ctx, p.pid, fmt.Errorf("Pid %d terminated but another processes took its place %d:%d", p.pid, Ino, newIno))
				return
			}
		}
	}
}

// procMon is the top level process monitoring struct which accepts requests to
// monitor and disconnect processes. Once created it is started by passing a
// context into the startMonitoring call.
type procMon struct {
	log      logging.Logger
	procs    map[int32]*procInfo
	request  chan *procMonRequest
	response chan *procMonResponse
}

// NewProcMon creates a new process monitor struct setting initializing the
// internal process map and the request channel.
func NewProcMon(logger logging.Logger) *procMon {
	return &procMon{
		log:      logger,
		procs:    make(map[int32]*procInfo),
		request:  make(chan *procMonRequest),
		response: make(chan *procMonResponse),
	}
}

func (p *procMon) RegisterProcess(ctx context.Context, Pid int32) {
	p.submitRequest(ctx, Pid, drpc.MethodGetAttachInfo)
}

func (p *procMon) UnregisterProcess(ctx context.Context, Pid int32) {
	p.submitRequest(ctx, Pid, drpc.MethodDisconnect)
}

func (p *procMon) submitRequest(ctx context.Context, Pid int32, Action drpc.MgmtMethod) {
	req := &procMonRequest{
		pid:    Pid,
		action: Action,
	}
	select {
	case <-ctx.Done():
	case p.request <- req:
	}
}

func (p *procMon) handleProcessAttach(ctx context.Context, request *procMonRequest) {
	info, found := p.procs[request.pid]

	if found {
		p.log.Errorf("Attempted to monitor process %d more than once", request.pid)
		return
	}

	child, cancel := context.WithCancel(ctx)
	info = &procInfo{
		log:       p.log,
		pid:       request.pid,
		cancelCtx: cancel,
		response:  p.response,
	}

	p.procs[request.pid] = info
	go info.monitorProcess(child)
}

func (p *procMon) handleProcessDisconnect(request *procMonRequest) {
	info, found := p.procs[request.pid]

	p.log.Debugf("Received request to disconnect pid:%d\n", request.pid)
	if !found {
		p.log.Errorf("Attempted to disconnect process %d but process is missing", request.pid)
	}

	info.cancelCtx()
	delete(p.procs, request.pid)
	p.log.Debugf("Process %d has disconnected", info.pid)
}

func (p *procMon) handleRequests(ctx context.Context) {
	for {
		select {
		case <-ctx.Done():
			return
		case request := <-p.request:
			switch request.action {
			case drpc.MethodGetAttachInfo:
				p.handleProcessAttach(ctx, request)
			case drpc.MethodDisconnect:
				p.handleProcessDisconnect(request)
			default:
				p.log.Errorf("Received request with invalid action type %s", request.action)
			}
		case resp := <-p.response:
			p.log.Debugf("Received response from Process %d, terminated with %s", resp.pid, resp.err)
		}
	}
}

// startMonitoring is the main driver which starts the process monitor. The
// passed in context is used to terminate all monitoring in the event of shutdown.
func (p *procMon) startMonitoring(ctx context.Context) {
	go p.handleRequests(ctx)
}
