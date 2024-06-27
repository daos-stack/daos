//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pciutils_test

import (
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/pciutils"
)

func TestAPI_PCIeCapsFromConfig(t *testing.T) {
	mockBytes := []byte(`00: 86 80 53 09 06 04 10 00 01 02 08 01 00 00 00 00
10: 04 00 00 bc 00 00 00 00 00 00 00 00 00 00 00 00
20: 00 00 00 00 00 00 00 00 00 00 00 00 90 15 a8 00
30: 00 00 00 00 40 00 00 00 00 00 00 00 00 01 00 00
40: 01 50 03 00 08 00 00 00 00 00 00 00 00 00 00 00
50: 11 60 1f 00 00 20 00 00 00 30 00 00 00 00 00 00
60: 10 00 02 00 a1 85 00 10 10 29 09 00 43 6c 41 00
70: 00 00 43 00 00 00 00 00 00 00 00 00 00 00 00 00
80: 00 00 00 00 1f 00 00 00 00 00 00 00 0e 00 00 00
90: 03 00 1f 00 00 00 00 00 00 00 00 00 00 00 00 00
a0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
b0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
c0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
d0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
e0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
f0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
100: 01 00 01 15 00 00 00 00 00 00 00 00 30 20 06 00
`)

	for name, tc := range map[string]struct {
		cfgBytes []byte
		expDev   *hardware.PCIDevice
		expErr   error
	}{
		"empty config": {
			expErr: pciutils.ErrCfgMissing,
		},
		"incomplete config; no pci-express caps": {
			cfgBytes: append(mockBytes[:10], byte('\n')),
			expErr:   errors.New("no pci-express capabilities"),
		},
		"multiple devices input": {
			cfgBytes: append(append(mockBytes, []byte("\n\n")...), mockBytes...),
			expErr:   pciutils.ErrMultiDevices,
		},
		"no final new-line char": {
			cfgBytes: mockBytes[:len(mockBytes)-1],
			expErr:   pciutils.ErrCfgNotTerminated,
		},
		"success": {
			cfgBytes: mockBytes,
			expDev: &hardware.PCIDevice{
				LinkMaxSpeed: 8e+9,
				LinkMaxWidth: 4,
				LinkNegSpeed: 8e+9,
				LinkNegWidth: 4,
			},
		},
	} {
		t.Run(name, func(t *testing.T) {
			ap := pciutils.NewPCIeLinkStatsProvider()
			dev := &hardware.PCIDevice{}

			err := ap.PCIeCapsFromConfig(tc.cfgBytes, dev)
			test.CmpErr(t, tc.expErr, err)
			if err != nil {
				return
			}

			if diff := cmp.Diff(tc.expDev, dev); diff != "" {
				t.Fatalf("(-want, +got)\n%s\n", diff)
			}

			// Try again with updated values for negotiated speed and width to verify
			// multiple consecutive library calls.
			newCfgBytes := strings.Replace(string(tc.cfgBytes), `70: 00 00 43`,
				`70: 00 00 82`, 1)
			dev = &hardware.PCIDevice{}

			if err = ap.PCIeCapsFromConfig([]byte(newCfgBytes), dev); err != nil {
				t.Fatal(err)
			}

			tc.expDev.LinkNegWidth = 8
			tc.expDev.LinkNegSpeed = 5e+9

			if diff := cmp.Diff(tc.expDev, dev); diff != "" {
				t.Fatalf("2nd try: (-want +got)\n%s\n", diff)
			}
		})
	}
}
