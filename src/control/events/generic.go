//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package events

import (
	"os"
	"time"

	"github.com/daos-stack/daos/src/control/common"
	sharedpb "github.com/daos-stack/daos/src/control/common/proto/shared"
)

// StrInfo contains opaque blob of type string to hold custom details for a
// generic RAS event to be forwarded through the control-plane from the
// data-plane to an external consumer e.g. syslog.
type StrInfo string

func (si *StrInfo) isExtendedInfo() {}

// GetStrInfo returns extended info if of type StrInfo.
func (evt *RASEvent) GetStrInfo() *StrInfo {
	if ei, ok := evt.ExtendedInfo.(*StrInfo); ok {
		return ei
	}

	return nil
}

// StrInfoFromProto converts event info from proto to native format.
func StrInfoFromProto(pbInfo *sharedpb.RASEvent_StrInfo) (*StrInfo, error) {
	si := StrInfo(pbInfo.StrInfo)

	return &si, nil
}

// StrInfoToProto converts event info from native to proto format.
func StrInfoToProto(si *StrInfo) (*sharedpb.RASEvent_StrInfo, error) {
	pbInfo := &sharedpb.RASEvent_StrInfo{
		StrInfo: string(*si),
	}

	return pbInfo, nil
}

// NewGenericEvent creates a generic RAS event from given inputs.
func NewGenericEvent(id RASID, typ RASTypeID, sev RASSeverityID, msg string,
	rank uint32, hwID, jobID, poolUUID, contUUID, objID, ctlOp, data string) (*RASEvent, error) {

	hn, err := os.Hostname()
	if err != nil {
		return nil, err
	}
	si := StrInfo(data)

	return &RASEvent{
		Timestamp:    common.FormatTime(time.Now()),
		Msg:          msg,
		ID:           id,
		Hostname:     hn,
		Rank:         rank,
		Type:         typ,
		Severity:     sev,
		HWID:         hwID,
		JobID:        jobID,
		PoolUUID:     poolUUID,
		ContUUID:     contUUID,
		ObjID:        objID,
		CtlOp:        ctlOp,
		ExtendedInfo: &si,
	}, nil
}
