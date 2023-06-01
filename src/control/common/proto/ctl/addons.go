//
// (C) Copyright 2022-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package ctl

import (
	"strconv"
	"strings"

	"github.com/pkg/errors"
)

func unmarshalState(data []byte, toName map[int32]string, toValue map[string]int32) (int32, error) {
	stateStr := strings.Trim(strings.ToUpper(string(data)), "\"")

	if si, err := strconv.ParseInt(stateStr, 0, 32); err == nil {
		state := int32(si)
		if _, ok := toName[state]; !ok {
			return 0, errors.Errorf("invalid state name lookup %q", stateStr)
		}
		return state, nil
	}

	// Try converting the string to an int32, to handle the conversion from control-plane native type.
	if si, ok := toValue[stateStr]; ok {
		return int32(si), nil
	}

	return 0, errors.Errorf("invalid state value lookup %q", stateStr)
}

func (nds *NvmeDevState) UnmarshalJSON(data []byte) error {
	state, err := unmarshalState(data, NvmeDevState_name, NvmeDevState_value)
	if err != nil {
		return err
	}
	*nds = NvmeDevState(state)

	return nil
}

func (vls *LedState) UnmarshalJSON(data []byte) error {
	state, err := unmarshalState(data, LedState_name, LedState_value)
	if err != nil {
		return err
	}
	*vls = LedState(state)

	return nil
}
