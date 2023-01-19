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

func TestDisplay(t *testing.T) {
	progress := support.ProgressBar{
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
