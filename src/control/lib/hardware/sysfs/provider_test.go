//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package sysfs

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"testing"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/hardware"
)

func TestSysfs_NewProvider(t *testing.T) {
	p := NewProvider()

	if p == nil {
		t.Fatal("nil provider returned")
	}

	common.AssertEqual(t, "/sys", p.root, "")
}

func TestSysfs_Provider_GetNetDevClass(t *testing.T) {
	testDir, cleanupTestDir := common.CreateTestDir(t)
	defer cleanupTestDir()

	devs := map[string]uint32{
		"lo":   uint32(hardware.Loopback),
		"eth1": uint32(hardware.Ether),
	}

	for dev, class := range devs {
		path := filepath.Join(testDir, "class", "net", dev)
		os.MkdirAll(path, 0755)

		f, err := os.Create(filepath.Join(path, "type"))
		if err != nil {
			t.Fatal(err.Error())
		}

		_, err = f.WriteString(fmt.Sprintf("%d\n", class))
		f.Close()
		if err != nil {
			t.Fatal(err.Error())
		}
	}

	for name, tc := range map[string]struct {
		in        string
		expResult hardware.NetDevClass
		expErr    error
	}{
		"empty": {
			expErr: errors.New("device name required"),
		},
		"no such device": {
			in:     "fakedevice",
			expErr: errors.New("no such file"),
		},
		"loopback": {
			in:        "lo",
			expResult: hardware.NetDevClass(devs["lo"]),
		},
		"ether": {
			in:        "eth1",
			expResult: hardware.NetDevClass(devs["eth1"]),
		},
	} {
		t.Run(name, func(t *testing.T) {
			p := NewProvider()
			p.root = testDir

			result, err := p.GetNetDevClass(tc.in)

			common.CmpErr(t, tc.expErr, err)
			common.AssertEqual(t, tc.expResult, result, "")
		})
	}
}
