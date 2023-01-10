//
// (C) Copyright 2020-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package support_test

import (
	"testing"

	// "github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/support"
)

func TestPrintProgress(t *testing.T) {
	progress := support.ProgressBar{1, 7, 0, false}

	for name, tc := range map[string]struct {
		Start      int
		Steps      int
		jsonOutput bool
		expResult  string
	}{
		"Valid Step count progress": {
			Start:      2,
			Steps:      7,
			jsonOutput: false,
			expResult:  "\r[==============                                                                                      ]        2/7",
		},
		"No Progress Bar if JsonOutput is Enabled": {
			Start:      2,
			Steps:      7,
			jsonOutput: true,
			expResult:  "",
		},
	} {
		t.Run(name, func(t *testing.T) {
			progress.Start = tc.Start
			progress.Steps = tc.Steps
			progress.JsonOutput = tc.jsonOutput
			gotOutput := support.PrintProgress(&progress)
			test.AssertEqual(t, tc.expResult, gotOutput, "")
		})
	}
}

func TestPrintProgressEnd(t *testing.T) {
	progress := support.ProgressBar{1, 7, 0, false}

	for name, tc := range map[string]struct {
		jsonOutput bool
		expResult  string
	}{
		"Valid Progress Bar for Non JsonOutput": {
			jsonOutput: false,
			expResult:  "\r[====================================================================================================]        7/7\n",
		},
		"No Progress Bar if JsonOutput is Enabled": {
			jsonOutput: true,
			expResult:  "",
		},
	} {
		t.Run(name, func(t *testing.T) {
			progress.JsonOutput = tc.jsonOutput
			gotOutput := support.PrintProgressEnd(&progress)
			test.AssertEqual(t, tc.expResult, gotOutput, "")
		})
	}
}