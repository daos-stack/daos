//
// (C) Copyright 2019-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"encoding/json"
	"encoding/xml"
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
	regionID       uint32
	regionSocketID uint32
	regionType     ipmctl.PMemRegionType
	regionCapacity uint64
	regionHealth   ipmctl.PMemRegionHealth

	// RegionList struct contains all the PMemRegions.
	RegionList struct {
		XMLName xml.Name `xml:"RegionList"`
		Regions []Region `xml:"Region"`
	}

	// Region struct represents a PMem AppDirect region.
	Region struct {
		XMLName              xml.Name       `xml:"Region"`
		ID                   regionID       `xml:"RegionID"`
		SocketID             regionSocketID `xml:"SocketID"`
		PersistentMemoryType regionType     `xml:"PersistentMemoryType"`
		Capacity             regionCapacity `xml:"Capacity"`
		FreeCapacity         regionCapacity `xml:"FreeCapacity"`
		Health               regionHealth   `xml:"HealthState"`
	}
)

func (ri *regionID) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	var s string
	if err := d.DecodeElement(&s, &start); err != nil {
		return err
	}

	n, err := strconv.ParseUint(strings.Replace(s, "0x", "", -1), 16, 16) // 4-character hex field
	if err != nil {
		return errors.Wrapf(err, "region id %q could not be parsed", s)
	}

	*ri = regionID(n)

	return nil
}

func (rsi *regionSocketID) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	var s string
	if err := d.DecodeElement(&s, &start); err != nil {
		return err
	}

	n, err := strconv.ParseUint(strings.Replace(s, "0x", "", -1), 16, 16) // 4-character hex field
	if err != nil {
		return errors.Wrapf(err, "socket id %q could not be parsed", s)
	}

	*rsi = regionSocketID(n)

	return nil
}

func (rt *regionType) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	var s string
	if err := d.DecodeElement(&s, &start); err != nil {
		return err
	}

	*rt = regionType(ipmctl.PMemRegionTypeFromString(s))

	return nil
}

func (rc *regionCapacity) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	var s string
	if err := d.DecodeElement(&s, &start); err != nil {
		return err
	}

	fc, err := humanize.ParseBytes(s)
	if err != nil {
		return errors.Wrapf(err, "capacity %q could not be parsed", s)
	}

	*rc = regionCapacity(fc)

	return nil
}

func (rh *regionHealth) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	var s string
	if err := d.DecodeElement(&s, &start); err != nil {
		return err
	}

	*rh = regionHealth(ipmctl.PMemRegionHealthFromString(s))

	return nil
}

var (
	badIpmctlVers = []semVer{
		// https://github.com/intel/ipmctl/commit/9e3898cb15fa9eed3ef3e9de4488be1681d53ff4
		{"02", "00", "00", "3809"},
		{"02", "00", "00", "3814"},
		{"02", "00", "00", "3816"},
	}
)

type (
	semVer      []string
	runCmdFn    func(string) (string, error)
	lookPathFn  func(string) (string, error)
	runCmdError struct {
		wrapped error
		stdout  string
	}
)

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

// constants for ipmctl commandline calls
//
// Manage AppDirect/Interleaved memory allocation goals across all DCPMMs on a system.
const (
	cmdShowIpmctlVersion = "ipmctl version"
	cmdShowRegions       = "ipmctl show -o nvmxml -region" // returns region info in xml
	cmdCreateRegions     = "ipmctl create -f -goal PersistentMemoryType=AppDirect"
	cmdRemoveRegions     = "ipmctl create -f -goal MemoryMode=100"
	cmdDeleteGoals       = "ipmctl delete -goal"

	outNoCLIPerms    = "ipmctl command you have attempted to execute requires root privileges"
	outNoPMemModules = "No PMem modules in the system"
	outNoPMemRegions = "no Regions defined in the system"
)

var (
	errNoPMemModules = errors.New(outNoPMemModules)
	errNoPMemRegions = errors.New(outNoPMemRegions)
)

// constants for ndctl commandline calls
const (
	cmdCreateNamespace  = "ndctl create-namespace"  // returns ns info in json
	cmdListNamespaces   = "ndctl list -N -v"        // returns ns info in json
	cmdDisableNamespace = "ndctl disable-namespace" // expect device name param
	cmdDestroyNamespace = "ndctl destroy-namespace" // expect device name param
)

type cmdRunner struct {
	log       logging.Logger
	binding   ipmctl.IpmCtl
	runCmd    runCmdFn
	lookPath  lookPathFn
	checkOnce sync.Once
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

func (cr *cmdRunner) showRegions(sockID int) (string, error) {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return "", errors.WithMessage(err, "checkIpmctl")
	}

	cmd := cmdShowRegions
	if sockID != sockAny {
		cmd = fmt.Sprintf("%s --socket %d", cmd, sockID)
	}

	out, err := cr.runCmd(cmd)
	if err != nil {
		return "", errors.Wrapf(err, "cmd %q", cmd)
	}
	cr.log.Debugf("%q cmd returned: %q", cmd, out)

	return out, nil
}

func (cr *cmdRunner) createRegions(sockID int) error {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return errors.WithMessage(err, "checkIpmctl")
	}

	cr.log.Debug("set interleaved appdirect goal to create regions")

	cmd := cmdCreateRegions
	if sockID != sockAny {
		cmd = fmt.Sprintf("%s --socket %d", cmd, sockID)
	}

	out, err := cr.runCmd(cmd)
	if err != nil {
		return errors.Wrapf(err, "cmd %q", cmd)
	}
	cr.log.Debugf("%q cmd returned: %q", cmd, out)

	return nil
}

func (cr *cmdRunner) removeRegions(sockID int) error {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return errors.WithMessage(err, "checkIpmctl")
	}

	cr.log.Debug("set memory mode goal to remove regions")

	cmd := cmdRemoveRegions
	if sockID != sockAny {
		cmd = fmt.Sprintf("%s --socket %d", cmd, sockID)
	}

	out, err := cr.runCmd(cmd)
	if err != nil {
		return errors.Wrapf(err, "cmd %q", cmd)
	}
	cr.log.Debugf("%q cmd returned: %q", cmd, out)

	return nil
}

func (cr *cmdRunner) deleteGoals(sockID int) error {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return errors.WithMessage(err, "checkIpmctl")
	}

	cr.log.Debug("delete any existing memory allocation goals")

	cmd := cmdDeleteGoals
	if sockID != sockAny {
		cmd = fmt.Sprintf("%s --socket %d", cmd, sockID)
	}

	out, err := cr.runCmd(cmd)
	if err != nil {
		return errors.Wrapf(err, "cmd %q", cmd)
	}
	cr.log.Debugf("%q cmd returned: %q", cmd, out)

	return nil
}

func (cr *cmdRunner) createNamespace(regionID int, sizeBytes uint64) (string, error) {
	cmd := cmdCreateNamespace
	cmd = fmt.Sprintf("%s --region %d", cmd, regionID)
	cmd = fmt.Sprintf("%s --size %d", cmd, sizeBytes)

	return cr.runCmd(cmd)
}

func (cr *cmdRunner) listNamespaces(sockID int) (string, error) {
	cmd := cmdListNamespaces
	if sockID != sockAny {
		cmd = fmt.Sprintf("%s --numa-node %d", cmd, sockID)
	}

	return cr.runCmd(cmd)
}

func (cr *cmdRunner) disableNamespace(name string) (string, error) {
	return cr.runCmd(fmt.Sprintf("%s %s", cmdDisableNamespace, name))
}

func (cr *cmdRunner) destroyNamespace(name string) (string, error) {
	return cr.runCmd(fmt.Sprintf("%s %s", cmdDestroyNamespace, name))
}

// checkNdctl verifies ndctl application is installed.
func (cr *cmdRunner) checkNdctl() (errOut error) {
	cr.checkOnce.Do(func() {
		if _, err := cr.lookPath("ndctl"); err != nil {
			errOut = FaultMissingNdctl
		}
	})

	return
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

// socketRegionMap maps regions based on socket ID key.
type socketRegionMap map[int]Region

func (srm *socketRegionMap) keys() []int {
	if srm == nil || len(*srm) == 0 {
		return []int{}
	}

	keys := make([]int, len(*srm))
	for k := range *srm {
		keys = append(keys, k)
	}
	sort.Ints(keys)

	return keys
}

func (srm *socketRegionMap) isEmpty() bool {
	if srm == nil || len(*srm) == 0 {
		return true
	}

	return false
}

func (srm *socketRegionMap) fromXML(data string) error {
	// parseRegions takes nvmxml output from ipmctl tool and returns PMem region details.
	var rl RegionList
	if err := xml.Unmarshal([]byte(data), &rl); err != nil {
		return errors.Wrap(err, "parse show region cmd output")
	}

	regionsPerSocket := make(socketRegionMap)
	for _, region := range rl.Regions {
		sockID := int(region.SocketID)
		if _, exists := regionsPerSocket[sockID]; exists {
			return errors.Errorf("unexpected second region assigned to socket %d", sockID)
		}
		regionsPerSocket[sockID] = region
	}
	*srm = regionsPerSocket

	return nil
}

func (cr *cmdRunner) getRegionDetails(sockID int) (socketRegionMap, error) {
	out, err := cr.showRegions(sockID)
	if err != nil {
		return nil, errors.WithMessage(err, "show regions cmd")
	}

	cr.log.Debugf("show region output: %s\n", out)

	switch {
	case strings.Contains(out, outNoCLIPerms):
		return nil, errors.Errorf("insufficient permissions to run %q", cmdShowRegions)
	case strings.Contains(out, outNoPMemModules):
		return nil, errNoPMemModules
	case strings.Contains(out, outNoPMemRegions):
		return nil, errNoPMemRegions
	}

	var regionsPerSocket socketRegionMap
	if err := regionsPerSocket.fromXML(out); err != nil {
		return nil, errors.Wrap(err, "mapping regions to socket id")
	}
	if regionsPerSocket.isEmpty() {
		return nil, errors.New("no app-direct pmem regions parsed")
	}

	return regionsPerSocket, nil
}

func getRegionState(region Region) storage.ScmState {
	rt := ipmctl.PMemRegionType(region.PersistentMemoryType)

	switch rt {
	case ipmctl.RegionTypeNotInterleaved:
		return storage.ScmNotInterleaved
	case ipmctl.RegionTypeAppDirect:
		// Fall-through
	default:
		return storage.ScmUnknownMode
	}

	rh := ipmctl.PMemRegionHealth(region.Health)

	if rh == ipmctl.RegionHealthError {
		return storage.ScmNotHealthy
	}

	// Expecting free capacity to be equal to either zero or all of capacity.
	switch region.FreeCapacity {
	case region.Capacity:
		return storage.ScmFreeCap
	case 0:
		return storage.ScmNoFreeCap
	default:
		return storage.ScmPartCap
	}
}

func (cr *cmdRunner) getPMemState(sockID int) (*storage.ScmSocketState, error) {
	resp := &storage.ScmSocketState{
		SocketID: sockAny,
		State:    storage.ScmStateUnknown,
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
	for _, id := range regionsPerSocket.keys() {
		r := regionsPerSocket[id]
		cr.log.Debugf("region detail: %+v", r)
		state := getRegionState(r)

		switch state {
		case storage.ScmNotInterleaved, storage.ScmNotHealthy, storage.ScmPartCap, storage.ScmUnknownMode:
			cr.log.Debugf("socket %d region in state %q", id, state)
			resp.SocketID = id
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

func checkStateHasSock(sockState storage.ScmSocketState, faultFunc func(int) *fault.Fault) error {
	if sockState.SocketID == sockAny {
		return errors.Errorf("expecting socket id with %s state", sockState.State)
	}
	return faultFunc(sockState.SocketID)
}

const namespaceMajorMinorPattern = `namespace([0-9]).([0-9])`

// Verify that created namespaces' block device names match the socket ID of the underlying PMem
// region and that the the expected number of namespaces exist per region.
func (cr *cmdRunner) verifyNamespaces(nss storage.ScmNamespaces, nrNsPerSocket uint, sockID int) error {
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
	case storage.ScmPartCap:
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
		resp.State = storage.ScmSocketState{
			SocketID: sockSelector,
			State:    storage.ScmNoFreeCap,
		}

	case storage.ScmNoFreeCap:
		// Regions and namespaces exist so sanity check details.
		resp.Namespaces = scanRes.Namespaces

	default:
		return nil, errors.Errorf("unhandled scm state %q", sockState.State)
	}

	return resp, cr.verifyNamespaces(resp.Namespaces, req.NrNamespacesPerSocket, sockSelector)
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
		storage.ScmPartCap, storage.ScmUnknownMode:
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

func (cr *cmdRunner) removeNamespace(devName string) error {
	if err := cr.checkNdctl(); err != nil {
		return err
	}

	cr.log.Debugf("removing pmem namespace %q", devName)

	if _, err := cr.disableNamespace(devName); err != nil {
		return errors.WithMessagef(err, "disable namespace cmd (%s)", devName)
	}

	if _, err := cr.destroyNamespace(devName); err != nil {
		return errors.WithMessagef(err, "destroy namespace cmd (%s)", devName)
	}

	return nil
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

	for socketID, region := range regionsPerSocket {
		if region.FreeCapacity == 0 || region.FreeCapacity != region.Capacity {
			// Sanity check that we are working on a region with full free capacity.
			return nil, errors.Errorf("region for socket %d does not have full free capacity", socketID)
		}

		// Check value is 2MiB aligned and (TODO) multiples of interleave width.
		pmemBytes := uint64(region.FreeCapacity) / uint64(nrNsPerSocket)

		if pmemBytes%alignmentBoundaryBytes != 0 {
			return nil, errors.New("free region size is not 2MiB aligned")
		}

		// Create specified number of namespaces on a single region (NUMA node).
		for j := uint(0); j < nrNsPerSocket; j++ {
			if _, err := cr.createNamespace(int(region.ID), pmemBytes); err != nil {
				return nil, errors.WithMessage(err, "create namespace cmd")
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

func parseNamespaces(jsonData string) (storage.ScmNamespaces, error) {
	nss := storage.ScmNamespaces{}

	// turn single entries into arrays
	if !strings.HasPrefix(jsonData, "[") {
		jsonData = "[" + jsonData + "]"
	}

	if err := json.Unmarshal([]byte(jsonData), &nss); err != nil {
		return nil, err
	}

	return nss, nil
}

// getNamespaces calls ndctl to list pmem namespaces and returns converted
// native storage types.
func (cr *cmdRunner) getNamespaces(sockID int) (storage.ScmNamespaces, error) {
	if err := cr.checkNdctl(); err != nil {
		return nil, err
	}

	out, err := cr.listNamespaces(sockID)
	if err != nil {
		return nil, errors.WithMessage(err, "list namespaces cmd")
	}

	nss, err := parseNamespaces(out)
	if err != nil {
		return nil, err
	}

	return nss, nil
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
