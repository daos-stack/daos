//
// (C) Copyright 2019-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"fmt"
	"os/exec"
	"regexp"
	"sort"
	"strconv"
	"strings"
	"sync"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	pmemCmd struct {
		BinaryName string
		Args       []string
	}
	runCmdFn   func(logging.Logger, pmemCmd) (string, error)
	lookPathFn func(string) (string, error)
)

func (pc *pmemCmd) String() string {
	return fmt.Sprintf("%s %s", pc.BinaryName, strings.Join(pc.Args, " "))
}

const (
	minNrNssPerSocket = 1
	maxNrNssPerSocket = 4

	sockAny = -1 // Select all socket IDs.

	alignmentBoundaryBytes = humanize.MiByte * 2

	ipmctlName    = `ipmctl`
	outNoCLIPerms = `ipmctl command you have attempted to execute requires root privileges`
)

var (
	// Cmd structs for ipmctl commandline calls to manage AppDirect/Interleaved memory
	// allocation goals across PMem modules.
	cmdShowIpmctlVersion = pmemCmd{
		BinaryName: ipmctlName,
		Args:       []string{"version"},
	}

	badIpmctlVers = []semVer{
		// https://github.com/intel/ipmctl/commit/9e3898cb15fa9eed3ef3e9de4488be1681d53ff4
		{"02", "00", "00", "3809"},
		{"02", "00", "00", "3814"},
		{"02", "00", "00", "3816"},
	}
)

type cmdRunner struct {
	log         logging.Logger
	runInternal runCmdFn
	lookPath    lookPathFn
	checkOnce   sync.Once
}

func (cr *cmdRunner) runCmd(cmd pmemCmd) (string, error) {
	return cr.runInternal(cr.log, cmd)
}

type semVer []string

func (sv semVer) String() string {
	return strings.Join(sv, ".")
}

func validateSemVer(sv semVer, badList []semVer) error {
	for _, badVer := range badList {
		if sv.String() == badVer.String() {
			return FaultIpmctlBadVersion(sv.String())
		}
	}

	return nil
}

// checkIpmctl verifies ipmctl application version is acceptable.
func (cr *cmdRunner) checkIpmctl(badList []semVer) (errOut error) {
	cr.checkOnce.Do(func() {
		cmdOut, err := cr.runCmd(cmdShowIpmctlVersion)
		if err != nil {
			errOut = errors.WithMessage(err, "show version cmd")
			return
		}

		re := regexp.MustCompile(`(\d{2}).(\d{2}).(\d{2}).(\d{4})`)
		matched := re.FindStringSubmatch(cmdOut)

		if matched == nil {
			errOut = errors.Errorf("could not read ipmctl version (%s)", cmdOut)
			return
		}

		ipmctlBinVer := matched[1:]
		cr.log.Debugf("ipmctl binary semver: %v", ipmctlBinVer)

		errOut = validateSemVer(ipmctlBinVer, badList)
	})

	return
}

func (cr *cmdRunner) runSockAwareCmd(sockID int, cmd pmemCmd) (string, error) {
	if cmd.BinaryName == ipmctlName {
		if err := cr.checkIpmctl(badIpmctlVers); err != nil {
			return "", errors.WithMessage(err, "checkIpmctl")
		}
	}

	cmdTmp := cmd

	// Insert socket ID arg after -goal flag if present otherwise at end.
	if sockID != sockAny {
		sockArg := fmt.Sprintf("%d", sockID)
		for i, arg := range cmdTmp.Args {
			if i == len(cmdTmp.Args)-1 {
				cmdTmp.Args = append(cmdTmp.Args, "-socket", sockArg)
				break
			}
			// Special case for goal cmds as ipmctl is picky about opt order.
			if arg == "-goal" {
				// Extend slice by two.
				cmdTmp.Args = append(cmdTmp.Args, "", "")
				// Shift along elements after index found to add space.
				copy(cmdTmp.Args[i+3:], cmdTmp.Args[i+1:])
				// Insert new element into space between.
				cmdTmp.Args[i+1] = "-socket"
				cmdTmp.Args[i+2] = sockArg
				break
			}
		}
	}

	return cr.runCmd(cmdTmp)
}

func checkStateHasSock(sockState *storage.ScmSocketState, faultFunc func(uint) *fault.Fault) error {
	if sockState == nil {
		return errors.New("nil sockState arg")
	}
	if sockState.SocketID == nil {
		return errors.Errorf("expecting socket id with %s state", sockState.State)
	}

	return faultFunc(*sockState.SocketID)
}

func checkStateForErrors(sockState *storage.ScmSocketState) error {
	if sockState == nil {
		return errors.New("nil sockState arg")
	}

	switch sockState.State {
	case storage.ScmNotInterleaved:
		// Non-interleaved AppDirect memory mode is unsupported.
		return checkStateHasSock(sockState, storage.FaultScmNotInterleaved)
	case storage.ScmNotHealthy:
		// A PMem AppDirect region is not healthy.
		return checkStateHasSock(sockState, storage.FaultScmNotHealthy)
	case storage.ScmPartFreeCap:
		// Only create namespaces if none already exist, partial state should not be
		// supported and user should reset PMem before trying again.
		return checkStateHasSock(sockState, storage.FaultScmPartialCapacity)
	case storage.ScmUnknownMode:
		// A PMem AppDirect region is reporting unsupported value for persistent memory
		// type.
		return checkStateHasSock(sockState, storage.FaultScmUnknownMemoryMode)
	case storage.ScmStateUnknown:
		return errors.New("unknown scm state")
	}

	return nil
}

func (cr *cmdRunner) handleFreeCapacity(sockSelector int, nrNsPerSock uint, regions Regions) (storage.ScmNamespaces, *storage.ScmSocketState, error) {
	regionPerSocket, err := mapRegionsToSocket(regions)
	if err != nil {
		return nil, nil, errors.Wrap(err, "mapRegionsToSocket")
	}

	numaIDs, err := cr.createNamespaces(regionPerSocket, nrNsPerSock)
	if err != nil {
		return nil, nil, errors.Wrap(err, "createNamespaces")
	}

	if len(numaIDs) == 0 {
		return nil, nil, errors.New("no namespaces created on regions with free capacity")
	}
	if len(numaIDs) > 1 && sockSelector != sockAny {
		return nil, nil, errors.Errorf("unexpected number of numa nodes processed, want 1 got %d",
			len(numaIDs))
	}
	cr.log.Tracef("namespaces created on %v numa-nodes, fetching updated region details", numaIDs)

	nss, err := cr.getNamespaces(sockSelector)
	if err != nil {
		return nil, nil, errors.Wrap(err, "getNamespaces")
	}

	rs, err := cr.getRegions(sockSelector)
	if err != nil {
		return nil, nil, errors.Wrap(err, "getRegions")
	}
	if len(rs) == 0 {
		return nil, nil, errors.New("getRegions: expected a nonzero number of regions")
	}
	regions = rs

	// Retrieve new state from up-to-date region details.
	ss, err := getPMemState(cr.log, regions)
	if err != nil {
		return nil, nil, errors.Wrap(err, "getPMemState")
	}

	if ss.State != storage.ScmNoFreeCap {
		return nil, nil, errors.Errorf("unexpected state from getPMemState, want %s got %s",
			storage.ScmNoFreeCap, ss.State)
	}

	return nss, ss, nil
}

func (cr *cmdRunner) processActionableState(req storage.ScmPrepareRequest, state storage.ScmState, namespaces storage.ScmNamespaces, regions Regions) (*storage.ScmPrepareResponse, error) {
	cr.log.Debugf("process actionable state %s", state)

	// If socket ID set in request, only process PMem attached to that socket.
	sockSelector := sockAny
	if req.SocketID != nil {
		sockSelector = int(*req.SocketID)
	}

	// Regardless of actionable state, remove any previously applied resource allocation goals.
	if err := cr.deleteGoals(sockSelector); err != nil {
		return nil, err
	}

	resp := &storage.ScmPrepareResponse{
		Namespaces: namespaces,
		Socket: &storage.ScmSocketState{
			State:    state,
			SocketID: req.SocketID,
		},
	}

	switch state {
	case storage.ScmNoRegions:
		// No regions exist, create interleaved AppDirect PMem regions.
		// PMem mode should be changed for all sockets as changing just for a single socket
		// can be determined as an incorrect configuration on some platforms.
		cr.log.Info("Creating PMem regions...")
		cr.log.Debug("set interleaved appdirect goal to create regions")
		if _, err := cr.runSockAwareCmd(sockSelector, cmdCreateRegions); err != nil {
			return nil, errors.Wrap(err, "createRegions")
		}
		resp.RebootRequired = true
	case storage.ScmFreeCap:
		// At least one region exists without a namespace so create block devices on those PMem regions
		// with available capacity and populate response with namespace details.
		cr.log.Info("Creating PMem namespaces...")
		nss, sockState, err := cr.handleFreeCapacity(sockSelector, req.NrNamespacesPerSocket, regions)
		if err != nil {
			return nil, errors.Wrap(err, "handleFreeCapacity")
		}
		resp.Namespaces = nss // Return only namespaces created.
		resp.Socket = sockState
	case storage.ScmNoFreeCap:
		// Regions and namespaces exist so no changes to response necessary.
		cr.log.Info("PMem namespaces already exist.")
	default:
		return nil, errors.Errorf("unhandled scm state %q (%d)", state, state)
	}

	return resp, nil
}

// Verify state is as expected and that created namespaces' block device names match the socket ID
// of the underlying PMem region and that the the expected number of namespaces exist per region.
func verifyPMem(log logging.Logger, resp *storage.ScmPrepareResponse, regions Regions, nrNsPerSock uint) error {
	if resp == nil {
		return errors.New("verifyPMem received nil ScmScanResponse")
	}

	switch resp.Socket.State {
	case storage.ScmNoRegions:
		return nil
	case storage.ScmNoFreeCap:
	default:
		return errors.Errorf("unexpected state in response, want %s|%s got %s",
			storage.ScmNoRegions, storage.ScmNoFreeCap, resp.Socket.State)
	}

	nsMajMinMap := make(map[int][]int)

	// Verify each namespace has valid NUMA node and region ID
	for _, ns := range resp.Namespaces {
		matches := nsMajMinRegex.FindStringSubmatch(ns.Name)
		if len(matches) != 3 {
			return errors.Errorf("unexpected format of namespace dev string: %q", ns.Name)
		}

		maj, err := strconv.Atoi(matches[1])
		if err != nil {
			return errors.Wrapf(err, "parse major namespace version (%q)", ns.Name)
		}

		if uint32(maj) != ns.NumaNode {
			log.Noticef("expected namespace major version (%d) to equal numa node (%d)",
				maj, ns.NumaNode)
		}

		min, err := strconv.Atoi(matches[2])
		if err != nil {
			return errors.Wrapf(err, "parse minor namespace version (%q)", ns.Name)
		}

		if maj < 0 || min < 0 {
			return errors.Errorf("unexpected negative value in regex matches %v", matches)
		}

		log.Debugf("found namespace %d.%d on numa %d", maj, min, ns.NumaNode)

		nsMajMinMap[maj] = append(nsMajMinMap[maj], min)
		sort.Ints(nsMajMinMap[maj])
	}

	if len(nsMajMinMap) != len(regions) {
		return errors.Errorf("expected num namespace major versions (%d) to equal num regions (%d)",
			len(nsMajMinMap), len(regions))
	}

	var keys []int
	for k := range nsMajMinMap {
		keys = append(keys, k)
	}
	sort.Ints(keys)

	for _, maj := range keys {
		mins := nsMajMinMap[maj]
		if len(mins) != int(nrNsPerSock) {
			return errors.Errorf("unexpected num namespaces on numa %d, want %d got %d",
				maj, nrNsPerSock, len(mins))
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
	if scanRes == nil {
		return nil, errors.New("nil scan response")
	}
	if len(scanRes.Modules) == 0 {
		cr.log.Info("Skip SCM prepare as no PMem in system")
		return &storage.ScmPrepareResponse{
			Namespaces: storage.ScmNamespaces{},
			Socket: &storage.ScmSocketState{
				State: storage.ScmNoModules,
			},
		}, nil
	}

	// If socket ID set in request, only process PMem attached to that socket.
	sockSelector := sockAny
	if req.SocketID != nil {
		sockSelector = int(*req.SocketID)
	}

	// Handle unspecified NrNamespacesPerSocket in request.
	if req.NrNamespacesPerSocket == 0 {
		req.NrNamespacesPerSocket = minNrNssPerSocket
	}

	cr.log.Info("Reading PMem configuration...")

	regions, err := cr.getRegions(sockSelector)
	if err != nil {
		return nil, errors.Wrap(err, "getRegions")
	}

	sockState, err := getPMemState(cr.log, regions)
	if err != nil {
		return nil, errors.Wrap(err, "getPMemState")
	}

	cr.log.Debugf("scm backend prep: req %+v, pmem state %+v", req, sockState)

	// First identify any socket specific unexpected state that should trigger an immediate
	// error response.
	if err := checkStateForErrors(sockState); err != nil {
		return nil, errors.Wrap(err, "checkStateForErrors after getPMemState")
	}

	// After initial validation, process actionable states.
	resp, err := cr.processActionableState(req, sockState.State, scanRes.Namespaces, regions)
	if err != nil {
		return nil, errors.Wrap(err, "processActionableState")
	}

	cr.log.Info("Verifying that PMem is in a valid state...")

	if err := verifyPMem(cr.log, resp, regions, req.NrNamespacesPerSocket); err != nil {
		return nil, storage.FaultScmInvalidPMem(err.Error())
	}

	cr.log.Info("Finished. If prompted then reboot and rerun command.")

	return resp, nil
}

// prepReset executes commands to remove namespaces and regions on PMem modules.
//
// Returns indication of whether a reboot is required alongside error.
// Command output from external tools will be returned. State will be passed in.
func (cr *cmdRunner) prepReset(req storage.ScmPrepareRequest, scanRes *storage.ScmScanResponse) (*storage.ScmPrepareResponse, error) {
	if scanRes == nil {
		return nil, errors.New("nil scan response")
	}
	resp := &storage.ScmPrepareResponse{
		Namespaces: storage.ScmNamespaces{},
		Socket:     &storage.ScmSocketState{},
	}
	if len(scanRes.Modules) == 0 {
		cr.log.Info("Skip SCM reset as no PMem in system")
		resp.Socket.State = storage.ScmNoModules
		return resp, nil
	}

	// If socket ID set in request, only process PMem attached to that socket.
	sockSelector := sockAny
	if req.SocketID != nil {
		sockSelector = int(*req.SocketID)
	}

	regions, err := cr.getRegions(sockSelector)
	if err != nil {
		return nil, errors.Wrap(err, "getRegions")
	}

	sockState, err := getPMemState(cr.log, regions)
	if err != nil {
		sockState = &storage.ScmSocketState{
			State: storage.ScmUnknownMode,
		}
		cr.log.Noticef("getPMemState: %s", err.Error()) // Continue to reset regions.
	}
	resp.Socket = sockState
	if sockState.SocketID == nil {
		resp.Socket.SocketID = req.SocketID
	}

	cr.log.Debugf("scm backend prep reset: req %+v, pmem state %+v", req, resp.Socket)

	if err := cr.deleteGoals(sockSelector); err != nil {
		return nil, errors.Wrapf(err, "deleteGoals")
	}

	switch sockState.State {
	case storage.ScmNoRegions:
		cr.log.Info("SCM is already reset as there are no PMem regions.")
		return resp, nil
	case storage.ScmFreeCap, storage.ScmNoFreeCap, storage.ScmNotInterleaved,
		storage.ScmNotHealthy, storage.ScmPartFreeCap, storage.ScmUnknownMode:
		// Continue to remove namespaces and regions.
		resp.RebootRequired = true
	default:
		return nil, errors.Errorf("unhandled scm state %q", sockState.State)
	}

	cr.log.Info("Removing PMem namespaces...")

	for _, dev := range scanRes.Namespaces {
		if err := cr.removeNamespace(dev.Name); err != nil {
			return nil, errors.Wrap(err, "removeNamespace")
		}
	}

	cr.log.Info("Re-creating PMem regions...")
	cr.log.Debug("set interleaved appdirect goal to re-create regions")
	if _, err := cr.runSockAwareCmd(sockSelector, cmdCreateRegions); err != nil {
		return nil, errors.Wrap(err, "createRegions")
	}

	cr.log.Info("Finished")

	return resp, nil
}

func run(log logging.Logger, cmd pmemCmd) (string, error) {
	var bytes []byte
	var err error

	if cmd.BinaryName == "" || strings.Contains(cmd.BinaryName, " ") {
		return "", errors.Errorf("invalid binary name %q", cmd.BinaryName)
	} else if len(cmd.Args) == 0 {
		bytes, err = exec.Command(cmd.BinaryName).Output()
	} else {
		bytes, err = exec.Command(cmd.BinaryName, cmd.Args...).Output()
	}
	out := string(bytes)

	if err != nil {
		return "", errors.Wrap(&system.RunCmdError{
			Wrapped: err,
			Stdout:  out,
		}, cmd.String())
	}
	log.Debugf("%s returned: %q", cmd.String(), out)

	return out, nil
}

func defaultCmdRunner(log logging.Logger) *cmdRunner {
	cr, err := newCmdRunner(log, run, exec.LookPath)
	if err != nil {
		panic(err)
	}

	return cr
}

func newCmdRunner(log logging.Logger, runCmd runCmdFn, lookPath lookPathFn) (*cmdRunner, error) {
	if runCmd == nil {
		// If no commandline call function is provided, assume no calls should be
		// expected and create a mock runner that will error if called.
		runCmd = func(_ logging.Logger, cmd pmemCmd) (string, error) {
			return "", errors.New("unexpected ipmctl commandline call")
		}
	}

	return &cmdRunner{
		log:         log,
		runInternal: runCmd,
		lookPath:    lookPath,
	}, nil
}
