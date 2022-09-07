//
// (C) Copyright 2022 Intel Corporation.
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

	// Regions is an alias for a Region slice.
	Regions []Region
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

// constants for ipmctl commandline calls
//
// Manage AppDirect/Interleaved memory allocation goals across all DCPMMs on a system.
const (
	cmdShowIpmctlVersion = "ipmctl version"
	cmdShowRegions       = "ipmctl show -o nvmxml -region" // returns region info in xml
	cmdCreateRegions     = "ipmctl create -f -goal%sPersistentMemoryType=AppDirect"
	cmdRemoveRegions     = "ipmctl create -f -goal%sMemoryMode=100"
	cmdDeleteGoals       = "ipmctl delete -goal"

	outNoCLIPerms    = "ipmctl command you have attempted to execute requires root privileges"
	outNoPMemModules = "No PMem modules in the system"
	outNoPMemRegions = "no Regions defined in the system"
)

var (
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
		cmd = fmt.Sprintf("%s -socket %d", cmd, sockID)
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
	opt := " "
	if sockID != sockAny {
		opt = fmt.Sprintf(" -socket %d ", sockID)
	}
	cmd = fmt.Sprintf(cmd, opt)

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
	opt := " "
	if sockID != sockAny {
		opt = fmt.Sprintf(" -socket %d ", sockID)
	}
	cmd = fmt.Sprintf(cmd, opt)

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
		cmd = fmt.Sprintf("%s -socket %d", cmd, sockID)
	}

	if _, err := cr.runCmd(cmd); err != nil {
		return errors.Wrapf(err, "cmd %q", cmd)
	}
	cr.log.Debugf("%q cmd was run", cmd)

	return nil
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
	out, err := cr.showRegions(sockID)
	if err != nil {
		return nil, errors.WithMessage(err, "showRegions")
	}

	switch {
	case strings.Contains(out, outNoCLIPerms):
		return nil, errors.Errorf("insufficient permissions to run %q", cmdShowRegions)
	case strings.Contains(out, outNoPMemModules):
		return nil, errNoPMemModules
	case strings.Contains(out, outNoPMemRegions):
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
			s := uint(sid)
			resp.SocketID = &s
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
