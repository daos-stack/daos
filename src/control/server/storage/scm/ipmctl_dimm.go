//
// (C) Copyright 2022-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package scm

import (
	"encoding/xml"
	"fmt"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/provider/system"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// <DimmList>
//  <Dimm>
//   <DimmID>0x0001</DimmID>
//   <Capacity>502.599 GiB</Capacity>
//   <HealthState>Healthy</HealthState>
//   <FWVersion>01.00.00.5127</FWVersion>
//   <PhysicalID>0x001e</PhysicalID>
//   <DimmUID>8089-a2-1839-000010ce</DimmUID>
//   <SocketID>0x0000</SocketID>
//   <MemControllerID>0x0000</MemControllerID>
//   <ChannelID>0x0000</ChannelID>
//   <ChannelPos>1</ChannelPos>
//   <PartNumber>NMA1XXD512GQS</PartNumber>
//  </Dimm>
// </DimmList>

type (
	hexShort    uint32
	stringSize  uint64
	stringPlain string

	// DIMM struct represents a PMem AppDirect region.
	DIMM struct {
		XMLName          xml.Name    `xml:"Dimm",json:"-"`
		ID               hexShort    `xml:"DimmID",json:"dimm_id"`
		Capacity         stringSize  `xml:"Capacity",json:"capacity"`
		HealthState      stringPlain `xml:"HealthState",json:"health_state"`
		FirmwareRevision stringPlain `xml:"FWVersion",json:"fw_version"`
		PhysicalID       hexShort    `xml:"PhysicalID",json:"physical_id"`
		UID              stringPlain `xml:"DimmUID",json:"dimm_uid"`
		SocketID         hexShort    `xml:"SocketID",json:"socket_id"`
		ControllerID     hexShort    `xml:"MemControllerID",json:"mem_controller_id"`
		ChannelID        hexShort    `xml:"ChannelID",json:"channel_id"`
		ChannelPosition  uint32      `xml:"ChannelPos",json:"channel_pos"`
		PartNumber       stringPlain `xml:"PartNumber",json:"part_number"`
	}
)

func (hs *hexShort) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	n, err := xmlStrToHex(d, start)
	if err != nil {
		return errors.Wrap(err, "hex str could not be parsed")
	}
	*hs = hexShort(n)

	return nil
}

func (ss *stringSize) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	var s string
	if err := d.DecodeElement(&s, &start); err != nil {
		return err
	}

	val, err := humanize.ParseBytes(s)
	if err != nil {
		return errors.Wrapf(err, "string size %q could not be parsed", s)
	}

	*ss = stringSize(val)

	return nil
}

func (sp *stringPlain) UnmarshalXML(d *xml.Decoder, start xml.StartElement) error {
	var s string
	if err := d.DecodeElement(&s, &start); err != nil {
		return err
	}

	*sp = stringPlain(s)

	return nil
}

type (
	// DIMMs is an alias for a slice of DIMM structs
	DIMMs []DIMM

	// DIMMList struct contains all the PMem DIMMs.
	DIMMList struct {
		XMLName xml.Name `xml:"DimmList"`
		DIMMs   `xml:"Dimm"`
	}
)

const outNoPMemDIMMs = `No PMem modules in the system`

var (
	dimmFields = []string{
		"DimmID", "ChannelID", "ChannelPos", "MemControllerID", "SocketID", "PhysicalID",
		"Capacity", "DimmUID", "PartNumber", "FWVersion", "HealthState",
	}
	cmdShowDIMMs = pmemCmd{
		BinaryName: ipmctlName,
		Args: []string{
			"show", "-o nvmxml", "-d " + strings.Join(dimmFields, ","), "-dimm",
		},
	}
	errNoPMemDIMMs = errors.New(outNoPMemDIMMs)
)

// dimmInfoFromXML uses XML output from `ipmctl show -o nvmxml -dimm [-socket X]` to gather PMem
// DIMM details.
func (cr *cmdRunner) dimmInfoFromXML(sockID int) (DIMMs, error) {
	out, err := cr.runSockAwareCmd(sockID, cmdShowDIMMs)
	if err != nil {
		if rce, ok := errors.Cause(err).(*system.RunCmdError); ok {
			if strings.Contains(rce.Stdout, outNoPMemDIMMs) {
				return DIMMs{}, nil // No DIMMs shouldn't return error.
			}
		}
		return nil, err
	}

	var dl DIMMList
	if err := xml.Unmarshal([]byte(out), &dl); err != nil {
		return nil, errors.Wrap(err, "parse show dimm cmd output")
	}

	if len(dl.DIMMs) == 0 {
		return nil, errors.New("no dimms parsed")
	}

	return dl.DIMMs, nil
}

// getModules scans the storage host for PMem modules and returns a slice.
func (cr *cmdRunner) getModules(sockID int) (storage.ScmModules, error) {
	dimms, err := cr.dimmInfoFromXML(sockID)
	if err != nil {
		return nil, err
	}

	msg := fmt.Sprintf("discovered %d pmem modules", len(dimms))
	if sockID != sockAny {
		msg = fmt.Sprintf("%s on sock %d", msg, sockID)
	}
	cr.log.Debugf(msg)

	modules := storage.ScmModules{}
	if err := convert.Types(dimms, &modules); err != nil {
		return nil, err
	}
	cr.log.Tracef("discovered pmem modules details: %+v", modules)

	return modules, nil
}
