//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package support

import (
	"testing"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
)

func TestSupport_Display(t *testing.T) {
	progress := ProgressBar{
		Start:     1,
		Total:     7,
		NoDisplay: false,
	}

	for name, tc := range map[string]struct {
		Start     int
		Steps     int
		NoDisplay bool
		expResult string
	}{
		"Valid Step count progress": {
			Start:     2,
			Steps:     7,
			NoDisplay: false,
			expResult: "\r[=====================                                                                               ]        3/7",
		},
		"Valid progress end": {
			Start:     7,
			Steps:     7,
			NoDisplay: false,
			expResult: "\r[====================================================================================================]        7/7\n",
		},
		"No Progress Bar if JsonOutput is Enabled": {
			Start:     2,
			Steps:     7,
			NoDisplay: true,
			expResult: "",
		},
	} {
		t.Run(name, func(t *testing.T) {
			progress.Start = tc.Start
			progress.Steps = tc.Steps
			progress.NoDisplay = tc.NoDisplay
			gotOutput := progress.Display()
			test.AssertEqual(t, tc.expResult, gotOutput, "")
		})
	}
}

func TestSupport_checkEngineState(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	for name, tc := range map[string]struct {
		expResult bool
		expErr    error
	}{
		"Check Engine status when no engine process is running": {
			expResult: false,
			expErr:    errors.New("daos_engine is not running on server: exit status 1"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotOutput, gotErr := checkEngineState(log)
			test.AssertEqual(t, tc.expResult, gotOutput, "")
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}

func TestSupport_getRunningConf(t *testing.T) {
	log, buf := logging.NewTestLogger(t.Name())
	defer test.ShowBufferOnFailure(t, buf)

	for name, tc := range map[string]struct {
		expResult string
		expErr    error
	}{
		"get default server config": {
			expResult: "",
			expErr:    errors.New("daos_engine is not running on server: exit status 1"),
		},
	} {
		t.Run(name, func(t *testing.T) {
			gotOutput, gotErr := getRunningConf(log)
			test.AssertEqual(t, tc.expResult, gotOutput, "")
			test.CmpErr(t, tc.expErr, gotErr)
		})
	}
}
