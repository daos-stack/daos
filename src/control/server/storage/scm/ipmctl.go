//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"fmt"
	"os/exec"
	"regexp"
	"strconv"
	"sync"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	minNrNssPerSocket = 1
	maxNrNssPerSocket = 4

	sockAny = -1 // Select all socket IDs.

	alignmentBoundaryBytes = humanize.MiByte * 2
)

type (
	runCmdFn    func(string) (string, error)
	lookPathFn  func(string) (string, error)
	runCmdError struct {
		wrapped error
		stdout  string
	}
)

func (rce *runCmdError) Error() string {
	if ee, ok := rce.wrapped.(*exec.ExitError); ok {
		return fmt.Sprintf("%s: stdout: %s; stderr: %s", ee.ProcessState,
			rce.stdout, ee.Stderr)
	}

	return fmt.Sprintf("%s: stdout: %s", rce.wrapped.Error(), rce.stdout)
}

func run(cmd string) (string, error) {
	out, err := exec.Command("bash", "-c", cmd).Output()
	if err != nil {
		return "", &runCmdError{
			wrapped: err,
			stdout:  string(out),
		}
	}

	return string(out), nil
}

type cmdRunner struct {
	log       logging.Logger
	binding   ipmctl.IpmCtl
	runCmd    runCmdFn
	lookPath  lookPathFn
	checkOnce sync.Once
}

// getModules scans the storage host for PMem modules and returns a slice of them.
func (cr *cmdRunner) getModules(sockID int) (storage.ScmModules, error) {
	discovery, err := cr.binding.GetModules(cr.log)
	if err != nil {
		return nil, errors.Wrap(err, "failed to discover pmem modules")
	}
	cr.log.Debugf("discovered %d pmem modules", len(discovery))

	modules := make(storage.ScmModules, 0, len(discovery))
	for _, d := range discovery {
		if sockID != sockAny && int(d.Socket_id) != sockID {
			continue // Skip module not bound to socket specified.
		}

		modules = append(modules, &storage.ScmModule{
			ChannelID:        uint32(d.Channel_id),
			ChannelPosition:  uint32(d.Channel_pos),
			ControllerID:     uint32(d.Memory_controller_id),
			SocketID:         uint32(d.Socket_id),
			PhysicalID:       uint32(d.Physical_id),
			Capacity:         d.Capacity,
			UID:              d.Uid.String(),
			PartNumber:       d.Part_number.String(),
			FirmwareRevision: d.Fw_revision.String(),
		})
	}

	return modules, nil
}

func (cr *cmdRunner) getPMemState(sockID int) (*storage.ScmSocketState, error) {
	resp := &storage.ScmSocketState{
		State: storage.ScmStateUnknown,
	}
	if sockID >= 0 {
		s := uint(sockID)
		resp.SocketID = &s
	}

	regionsPerSocket, err := cr.getRegionDetails(sockID)
	if err != nil {
		switch err {
		case errNoPMemModules:
			resp.State = storage.ScmNoModules
			return resp, nil
		case errNoPMemRegions:
			resp.State = storage.ScmNoRegions
			return resp, nil
		default:
			return nil, err
		}
	}

	hasFreeCap := false
	for _, sid := range regionsPerSocket.keys() {
		r := regionsPerSocket[sid]
		cr.log.Debugf("region detail: %+v", r)
		state := getRegionState(r)

		switch state {
		case storage.ScmNotInterleaved, storage.ScmNotHealthy, storage.ScmPartFreeCap, storage.ScmUnknownMode:
			cr.log.Debugf("socket %d region in state %q", sid, state)
			s := uint(sid)
			resp.SocketID = &s
			resp.State = state
			return resp, nil

		case storage.ScmFreeCap:
			fc := humanize.Bytes(uint64(r.FreeCapacity))
			cr.log.Debugf("socket %d app-direct region has %s free", r.SocketID, fc)
			hasFreeCap = true

		case storage.ScmNoFreeCap:
			// Fall-through

		default:
			return nil, errors.New("unexpected state from getRegionState")
		}
	}

	// If any of the processed regions has full free capacity, return free-cap state.
	if hasFreeCap {
		resp.State = storage.ScmFreeCap
		return resp, nil
	}

	// If none of the processed regions has any free capacity, return no-free-cap state.
	resp.State = storage.ScmNoFreeCap
	return resp, nil
}

// For each region, create <nrNsPerSocket> namespaces.
func (cr *cmdRunner) createNamespaces(nrNsPerSocket uint, sockID int) (storage.ScmNamespaces, error) {
	if nrNsPerSocket < minNrNssPerSocket || nrNsPerSocket > maxNrNssPerSocket {
		return nil, errors.Errorf("unexpected number of namespaces requested per socket: want [%d-%d], got %d",
			minNrNssPerSocket, maxNrNssPerSocket, nrNsPerSocket)
	}

	regionsPerSocket, err := cr.getRegionDetails(sockID)
	if err != nil {
		return nil, err
	}
	if len(regionsPerSocket) == 0 {
		return nil, errors.New("unexpected number of pmem regions (0)")
	}

	for sid, region := range regionsPerSocket {
		// Check value is 2MiB aligned and (TODO) multiples of interleave width.
		pmemBytes := uint64(region.FreeCapacity) / uint64(nrNsPerSocket)

		if pmemBytes%alignmentBoundaryBytes != 0 {
			return nil, errors.Errorf("socket %d: free region size is not 2MiB aligned", sid)
		}

		// Create specified number of namespaces on a single region (NUMA node).
		for j := uint(0); j < nrNsPerSocket; j++ {
			if _, err := cr.createNamespace(int(region.ID), pmemBytes); err != nil {
				return nil, errors.WithMessagef(err, "socket %d: create namespace cmd", sid)
			}
		}
	}

	sockState, err := cr.getPMemState(sockID)
	if err != nil {
		return nil, errors.WithMessage(err, "getting state")
	}
	if sockState.State != storage.ScmNoFreeCap {
		return nil, errors.Errorf("unexpected state: want %s, got %s", storage.ScmNoFreeCap.String(),
			sockState.State.String())
	}

	return cr.getNamespaces(sockID)
}

func checkStateHasSock(sockState storage.ScmSocketState, faultFunc func(uint) *fault.Fault) error {
	if sockState.SocketID == nil {
		return errors.Errorf("expecting socket id with %s state", sockState.State)
	}
	return faultFunc(*sockState.SocketID)
}

const namespaceMajorMinorPattern = `namespace([0-9]).([0-9])`

// Verify that created namespaces' block device names match the socket ID of the underlying PMem
// region and that the the expected number of namespaces exist per region.
func (cr *cmdRunner) verifyPMem(nss storage.ScmNamespaces, nrNsPerSocket uint, sockID int) error {
	namespaces, err := cr.getNamespaces(sockID)
	if err != nil {
		return err
	}

	re := regexp.MustCompile(namespaceMajorMinorPattern)
	nsMajMinMap := make(map[uint64][]uint64)

	// Verify each namespace has valid NUMA node and region ID
	for _, ns := range namespaces {
		matches := re.FindStringSubmatch(ns.Name)
		if len(matches) != 3 {
			return errors.Errorf("unexpected format of namespace dev string: %q", ns.Name)
		}

		maj, err := strconv.ParseUint(matches[1], 16, 10)
		if err != nil {
			return errors.Wrapf(err, "parse major namespace version (%q)", ns.Name)
		}

		if uint32(maj) != ns.NumaNode {
			return errors.Errorf("expected namespace major version (%d) to equal numa node (%d)",
				maj, ns.NumaNode)
		}

		min, err := strconv.ParseUint(matches[2], 16, 10)
		if err != nil {
			return errors.Wrapf(err, "parse minor namespace version (%q)", ns.Name)
		}

		nsMajMinMap[maj] = append(nsMajMinMap[maj], min)
	}

	regions, err := cr.getRegionDetails(sockID)
	if err != nil {
		return err
	}

	if len(nsMajMinMap) != len(regions) {
		return errors.Errorf("expected num namespace major versions (%d) to equal num regions (%d)",
			len(nsMajMinMap), len(regions))
	}

	for maj, mins := range nsMajMinMap {
		if len(mins) != int(nrNsPerSocket) {
			return errors.Errorf("unexpected num namespaces on numa %d, want %d got %d",
				maj, nrNsPerSocket, len(mins))
		}
	}

	return nil
}

// prep executes commands to configure PMem modules into AppDirect interleaved
// regions (sets) hosting pmem block-device namespaces.
//
// State is established based on presence and free capacity of regions.
//
// Actions based on state:
// * regions exist but region not in interleaved mode -> return error
// * regions exist but region not in healthy state -> return error
// * regions exist but region has only partial capacity -> return error
// * regions exist but region has unsupported memory mode -> return error
// * modules exist and no regions -> create all regions (needs reboot)
// * regions exist and free capacity -> create all namespaces, return created
// * regions exist but no free capacity -> no-op, return namespaces
func (cr *cmdRunner) prep(req storage.ScmPrepareRequest, scanRes *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error) {
	sockState := scanRes.State
	resp := &storage.ScmPrepareResponse{
		State: sockState,
	}

	cr.log.Debugf("scm backend prep: req %+v, pmem state from scan %+v", req, sockState)

	// First identify any socket specific unexpected state that should trigger an immediate
	// error response.
	switch sockState.State {
	case storage.ScmNoModules:
		return resp, nil
	case storage.ScmNotInterleaved:
		// Non-interleaved AppDirect memory mode is unsupported.
		return nil, checkStateHasSock(sockState, storage.FaultScmNotInterleaved)
	case storage.ScmNotHealthy:
		// A PMem AppDirect region is not healthy.
		return nil, checkStateHasSock(sockState, storage.FaultScmNotHealthy)
	case storage.ScmPartFreeCap:
		// Only create namespaces if none already exist, partial state should not be
		// supported and user should reset PMem before trying again.
		return nil, checkStateHasSock(sockState, storage.FaultScmPartialCapacity)
	case storage.ScmUnknownMode:
		// A PMem AppDirect region is reporting unsupported value for persistent memory
		// type.
		return nil, checkStateHasSock(sockState, storage.FaultScmUnknownMemoryMode)
	}

	// If socket ID set in request, only scan devices attached to that socket.
	sockSelector := sockAny
	if req.SocketID != nil {
		sockSelector = int(*req.SocketID)
	}

	// Regardless of actionable state, remove any previously applied resource allocation goals.
	if err := cr.deleteGoals(sockSelector); err != nil {
		return nil, err
	}

	// Handle unspecified NrNamespacesPerSocket in request.
	if req.NrNamespacesPerSocket == 0 {
		req.NrNamespacesPerSocket = minNrNssPerSocket
	}

	// Now process actionable states.
	switch sockState.State {
	case storage.ScmNoRegions:
		// No regions exist, create interleaved AppDirect PMem regions.
		if err := cr.createRegions(sockSelector); err != nil {
			return nil, err
		}
		resp.RebootRequired = true
		return resp, nil

	case storage.ScmFreeCap:
		// Regions exist but no namespaces, create block devices on PMem regions and sanity
		// check details.
		namespaces, err := cr.createNamespaces(req.NrNamespacesPerSocket, sockSelector)
		if err != nil {
			return nil, err
		}
		resp.Namespaces = namespaces
		resp.State.State = storage.ScmNoFreeCap

	case storage.ScmNoFreeCap:
		// Regions and namespaces exist so sanity check details.
		resp.Namespaces = scanRes.Namespaces

	default:
		return nil, errors.Errorf("unhandled scm state %q", sockState.State)
	}

	return resp, cr.verifyPMem(resp.Namespaces, req.NrNamespacesPerSocket, sockSelector)
}

// prepReset executes commands to remove namespaces and regions on PMem modules.
//
// Returns indication of whether a reboot is required alongside error.
// Command output from external tools will be returned. State will be passed in.
func (cr *cmdRunner) prepReset(req storage.ScmPrepareRequest, scanRes *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error) {
	sockState := scanRes.State
	resp := &storage.ScmPrepareResponse{
		State: sockState,
	}

	cr.log.Debugf("scm backend prep reset: state %q", sockState.State)

	// If socket ID set in request, only scan devices attached to that socket.
	sockSelector := sockAny
	if req.SocketID != nil {
		sockSelector = int(*req.SocketID)
	}

	if err := cr.deleteGoals(sockSelector); err != nil {
		return nil, err
	}

	switch sockState.State {
	case storage.ScmNoModules, storage.ScmNoRegions:
		return resp, nil
	case storage.ScmFreeCap, storage.ScmNoFreeCap, storage.ScmNotInterleaved, storage.ScmNotHealthy,
		storage.ScmPartFreeCap, storage.ScmUnknownMode:
		// Continue to remove namespaces and regions.
		resp.RebootRequired = true
	default:
		return nil, errors.Errorf("unhandled scm state %q", sockState.State)
	}

	for _, dev := range scanRes.Namespaces {
		if err := cr.removeNamespace(dev.Name); err != nil {
			return nil, err
		}
	}

	cr.log.Infof("Resetting PMem memory allocations.")

	if err := cr.removeRegions(sockSelector); err != nil {
		return nil, err
	}

	return resp, nil
}

func defaultCmdRunner(log logging.Logger) *cmdRunner {
	cr, err := newCmdRunner(log, &ipmctl.NvmMgmt{}, run, exec.LookPath)
	if err != nil {
		panic(err)
	}

	return cr
}

func newCmdRunner(log logging.Logger, lib ipmctl.IpmCtl, runCmd runCmdFn, lookPath lookPathFn) (*cmdRunner, error) {
	if lib == nil {
		lib = &ipmctl.NvmMgmt{}
	}
	if err := lib.Init(log); err != nil {
		return nil, err
	}

	return &cmdRunner{
		log:      log,
		binding:  lib,
		runCmd:   runCmd,
		lookPath: lookPath,
	}, nil
}
