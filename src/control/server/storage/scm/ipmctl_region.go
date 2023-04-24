//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"encoding/xml"
	"fmt"
	"regexp"
	"sort"
	"strconv"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/ipmctl"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

type (
	regionID       uint32
	regionSocketID uint32
	regionType     ipmctl.PMemRegionType
	regionCapacity uint64
	regionHealth   ipmctl.PMemRegionHealth
	regionISetID   uint64

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
		ISetID               regionISetID   `xml:"ISetID"`
	}

	// Regions is an alias for a Region slice.
	Regions []Region
)

func xmlStrToHex(d *xml.Decoder, start xml.StartElement) (uint64, error) {
	var s string
	if err := d.DecodeElement(&s, &start); err != nil {
		return 0, err
	}

	if strings.Contains(s, "0x") {
		return strconv.ParseUint(strings.Replace(s, "0x", "", -1), 16, 64)
	}
	return strconv.ParseUint(s, 10, 64)
}

func (ri *regionID) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	n, err := xmlStrToHex(d, start)
	if err != nil {
		return errors.Wrap(err, "region id could not be parsed")
	}
	*ri = regionID(n)

	return nil
}

func (rsi *regionSocketID) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	n, err := xmlStrToHex(d, start)
	if err != nil {
		return errors.Wrap(err, "region socket id could not be parsed")
	}
	*rsi = regionSocketID(n)

	return nil
}

func (rii *regionISetID) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	n, err := xmlStrToHex(d, start)
	if err != nil {
		return errors.Wrap(err, "region iset id could not be parsed")
	}
	*rii = regionISetID(n)

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

func (rt regionType) MarshalXML(e *xml.Encoder, start xml.StartElement) error {
	return e.EncodeElement(ipmctl.PMemRegionType(rt).String(), start)
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

func (rh regionHealth) MarshalXML(e *xml.Encoder, start xml.StartElement) error {
	return e.EncodeElement(ipmctl.PMemRegionHealth(rh).String(), start)
}

const (
	ipmctlName = "ipmctl"

	outNoCLIPerms    = "ipmctl command you have attempted to execute requires root privileges"
	outNoPMemModules = "No PMem modules in the system"
	outNoPMemRegions = "no Regions defined in the system"
	// Command output when specifying socket in cmd and no regions for that socket.
	outNoPMemRegionResults = "<Results>\n<Result>\n</Result>\n</Results>"
)

var (
	// Cmd structs for ipmctl commandline calls to manage AppDirect/Interleaved memory
	// allocation goals across PMem modules.

	cmdShowIpmctlVersion = pmemCmd{
		BinaryName: ipmctlName,
		Args:       []string{"version"},
	}
	cmdCreateRegions = pmemCmd{
		BinaryName: ipmctlName,
		Args:       []string{"create", "-f", "-goal", "PersistentMemoryType=AppDirect"},
	}
	cmdRemoveRegions = pmemCmd{
		BinaryName: ipmctlName,
		Args:       []string{"create", "-f", "-goal", "MemoryMode=100"},
	}
	cmdDeleteGoals = pmemCmd{
		BinaryName: ipmctlName,
		Args:       []string{"delete", "-goal"},
	}
	// returns region info in xml
	cmdShowRegions = pmemCmd{
		BinaryName: ipmctlName,
		Args:       []string{"show", "-o nvmxml", "-region"},
	}

	errNoPMemModules = errors.New(outNoPMemModules)
	errNoPMemRegions = errors.New(outNoPMemRegions)

	badIpmctlVers = []semVer{
		// https://github.com/intel/ipmctl/commit/9e3898cb15fa9eed3ef3e9de4488be1681d53ff4
		{"02", "00", "00", "3809"},
		{"02", "00", "00", "3814"},
		{"02", "00", "00", "3816"},
	}
)

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
		cmd := cmdShowIpmctlVersion
		cmdOut, err := cr.runCmd(cr.log, cmd)
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

func (cr *cmdRunner) runRegionCmd(sockID int, cmd pmemCmd) (string, error) {
	cmdTmp := cmd

	// Insert socket ID arg after -goal flag if present otherwise at end.
	if sockID != sockAny {
		sockArg := fmt.Sprintf("%d", sockID)
		for i, arg := range cmdTmp.Args {
			if i == len(cmdTmp.Args)-1 {
				cmdTmp.Args = append(cmdTmp.Args, "-socket", sockArg)
				break
			}
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

	return cr.runCmd(cr.log, cmdTmp)
}

func (cr *cmdRunner) createRegions(sockID int) error {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return errors.WithMessage(err, "checkIpmctl")
	}
	cr.log.Debug("set interleaved appdirect goal to create regions")

	_, err := cr.runRegionCmd(sockID, cmdCreateRegions)
	return err
}

func (cr *cmdRunner) removeRegions(sockID int) error {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return errors.WithMessage(err, "checkIpmctl")
	}
	cr.log.Debug("set memory mode goal to remove regions")

	_, err := cr.runRegionCmd(sockID, cmdRemoveRegions)
	return err
}

func (cr *cmdRunner) deleteGoals(sockID int) error {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return errors.WithMessage(err, "checkIpmctl")
	}
	cr.log.Debug("delete any existing memory allocation goals")

	_, err := cr.runRegionCmd(sockID, cmdDeleteGoals)
	return err
}

// socketRegionMap maps regions based on socket ID key.
type socketRegionMap map[int]Region

func (srm socketRegionMap) keys() []int {
	keys := make([]int, 0, len(srm))
	for k := range srm {
		keys = append(keys, k)
	}
	sort.Ints(keys)

	return keys
}

func mapRegionsToSocket(regions Regions) (socketRegionMap, error) {
	srm := make(socketRegionMap)
	for _, region := range regions {
		sockID := int(region.SocketID)
		if _, exists := srm[sockID]; exists {
			return nil, errors.Errorf("multiple regions assigned to the same socket (%d)", sockID)
		}
		srm[sockID] = region
	}

	return srm, nil
}

// getRegions takes nvmxml output from ipmctl tool and returns PMem region details.
func (cr *cmdRunner) getRegions(sockID int) (Regions, error) {
	if err := cr.checkIpmctl(badIpmctlVers); err != nil {
		return nil, errors.WithMessage(err, "checkIpmctl")
	}

	out, err := cr.runRegionCmd(sockID, cmdShowRegions)
	if err != nil {
		return nil, err
	}

	switch {
	case strings.Contains(out, outNoCLIPerms):
		return nil, errors.Errorf("insufficient permissions to run %s", cmdShowRegions)
	case strings.Contains(out, outNoPMemModules):
		return nil, errNoPMemModules
	case strings.Contains(out, outNoPMemRegions):
		return Regions{}, nil
	case strings.Contains(out, outNoPMemRegionResults):
		return Regions{}, nil
	}

	var rl RegionList
	if err := xml.Unmarshal([]byte(out), &rl); err != nil {
		return nil, errors.Wrap(err, "parse show region cmd output")
	}

	if len(rl.Regions) == 0 {
		return nil, errors.New("no app-direct pmem regions parsed")
	}

	return Regions(rl.Regions), nil
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
		return storage.ScmPartFreeCap
	}
}

func getPMemState(log logging.Logger, regions Regions) (*storage.ScmSocketState, error) {
	resp := &storage.ScmSocketState{
		State: storage.ScmStateUnknown,
	}

	switch len(regions) {
	case 0:
		resp.State = storage.ScmNoRegions
		return resp, nil
	case 1:
		s := uint(regions[0].SocketID)
		resp.SocketID = &s
	}

	regionPerSocket, err := mapRegionsToSocket(regions)
	if err != nil {
		return nil, errors.Wrap(err, "mapRegionsToSocket")
	}

	hasFreeCap := false
	for _, sid := range regionPerSocket.keys() {
		r := regionPerSocket[sid]
		log.Debugf("region detail: %+v", r)
		state := getRegionState(r)

		switch state {
		case storage.ScmNotInterleaved, storage.ScmNotHealthy, storage.ScmPartFreeCap, storage.ScmUnknownMode:
			log.Debugf("socket %d region in state %q", sid, state)
			if resp.SocketID == nil {
				// Indicate state for a specific socket.
				s := uint(sid)
				resp.SocketID = &s
			}
			resp.State = state
			return resp, nil
		case storage.ScmFreeCap:
			log.Debugf("socket %d app-direct region has %s free", r.SocketID,
				humanize.Bytes(uint64(r.FreeCapacity)))
			hasFreeCap = true
		case storage.ScmNoFreeCap:
			// Fall-through
		default:
			return nil, errors.Errorf("unexpected state %s (%d)", state, state)
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
