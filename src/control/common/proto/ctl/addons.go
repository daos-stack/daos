//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ctl

import (
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

func (nds *NvmeDevState) UnmarshalJSON(data []byte) error {
	var state int32
	stateStr := strings.Trim(strings.ToUpper(string(data)), "\"")

	if si, err := strconv.ParseInt(stateStr, 0, 32); err == nil {
		state = int32(si)
		if _, ok := NvmeDevState_name[state]; !ok {
			return errors.Errorf("invalid vmd led state name lookup %q", stateStr)
		}
	} else {
		// Try converting the string to an int32, to handle the conversion from
		// control-plane native type.
		si, ok := NvmeDevState_value[stateStr]
		if !ok {
			return errors.Errorf("invalid vmd led state value lookup %q", stateStr)
		}
		state = int32(si)
	}
	*nds = NvmeDevState(state)

	return nil
}

func (vls *VmdLedState) UnmarshalJSON(data []byte) error {
	var state int32
	stateStr := strings.Trim(strings.ToUpper(string(data)), "\"")

	if si, err := strconv.ParseInt(stateStr, 0, 32); err == nil {
		state = int32(si)
		if _, ok := VmdLedState_name[state]; !ok {
			return errors.Errorf("invalid vmd led state name lookup %q", stateStr)
		}
	} else {
		// Try converting the string to an int32, to handle the conversion from
		// control-plane native type.
		si, ok := VmdLedState_value[stateStr]
		if !ok {
			return errors.Errorf("invalid vmd led state value lookup %q", stateStr)
		}
		state = int32(si)
	}
	*vls = VmdLedState(state)

	return nil
}
