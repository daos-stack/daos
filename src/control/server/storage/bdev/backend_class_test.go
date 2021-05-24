//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// TestBackend_writeNvmeConfig verifies config parameters for bdev get
// converted into nvme config files that can be consumed by spdk.
func TestBackend_writeNvmeConfig(t *testing.T) {
	tests := map[string]struct {
		class           storage.BdevClass
		devList         []string
		bdevVmdDisabled bool
		fileSize        int // relevant for FILE
		vosEnv          string
		wantBuf         []string
		expValidateErr  error
		expWriteErr     error
	}{
		"config validation failure": {
			class:          storage.BdevClassNvme,
			devList:        []string{"not a pci address"},
			expValidateErr: errors.New("unexpected pci address"),
		},
		"multiple controllers": {
			class:           storage.BdevClassNvme,
			bdevVmdDisabled: true,
			devList:         []string{"0000:81:00.0", "0000:81:00.1"},
			wantBuf: []string{
				`[Nvme]`,
				`    TransportID "trtype:PCIe traddr:0000:81:00.0" Nvme_hostfoo_0`,
				`    TransportID "trtype:PCIe traddr:0000:81:00.1" Nvme_hostfoo_1`,
				`    RetryCount 4`,
				`    TimeoutUsec 0`,
				`    ActionOnTimeout None`,
				`    AdminPollRate 100000`,
				`    HotplugEnable No`,
				`    HotplugPollRate 0`,
				``,
			},
			vosEnv: "NVME",
		},
		"VMD devices": {
			class:   storage.BdevClassNvme,
			devList: []string{"5d0505:01:00.0", "5d0505:03:00.0"},
			wantBuf: []string{
				`[Vmd]`,
				`    Enable True`,
				``,
				`[Nvme]`,
				`    TransportID "trtype:PCIe traddr:5d0505:01:00.0" Nvme_hostfoo_0`,
				`    TransportID "trtype:PCIe traddr:5d0505:03:00.0" Nvme_hostfoo_1`,
				`    RetryCount 4`,
				`    TimeoutUsec 0`,
				`    ActionOnTimeout None`,
				`    AdminPollRate 100000`,
				`    HotplugEnable No`,
				`    HotplugPollRate 0`,
				``,
			},
			vosEnv: "NVME",
		},
		"multiple VMD and NVMe controllers": {
			class:   storage.BdevClassNvme,
			devList: []string{"0000:81:00.0", "5d0505:01:00.0", "5d0505:03:00.0"},
			wantBuf: []string{
				`[Vmd]`,
				`    Enable True`,
				``,
				`[Nvme]`,
				`    TransportID "trtype:PCIe traddr:0000:81:00.0" Nvme_hostfoo_0`,
				`    TransportID "trtype:PCIe traddr:5d0505:01:00.0" Nvme_hostfoo_1`,
				`    TransportID "trtype:PCIe traddr:5d0505:03:00.0" Nvme_hostfoo_2`,
				`    RetryCount 4`,
				`    TimeoutUsec 0`,
				`    ActionOnTimeout None`,
				`    AdminPollRate 100000`,
				`    HotplugEnable No`,
				`    HotplugPollRate 0`,
				``,
			},
			vosEnv: "NVME",
		},
		"AIO file": {
			class:    storage.BdevClassFile,
			devList:  []string{"myfile", "myotherfile"},
			fileSize: 1, // GB/file
			wantBuf: []string{
				`[AIO]`,
				`    AIO myfile AIO_hostfoo_0 4096`,
				`    AIO myotherfile AIO_hostfoo_1 4096`,
				``,
			},
			vosEnv: "AIO",
		},
		"AIO kdev": {
			class:   storage.BdevClassKdev,
			devList: []string{"sdb", "sdc"},
			wantBuf: []string{
				`[AIO]`,
				`    AIO sdb AIO_hostfoo_0`,
				`    AIO sdc AIO_hostfoo_1`,
				``,
			},
			vosEnv: "AIO",
		},
	}

	for name, tc := range tests {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			testDir, clean := common.CreateTestDir(t)
			defer clean()

			cfg := storage.BdevConfig{}
			if tc.class != "" {
				cfg.Class = tc.class
			}

			if len(tc.devList) != 0 {
				switch tc.class {
				case storage.BdevClassFile, storage.BdevClassKdev:
					for _, devFile := range tc.devList {
						absPath := filepath.Join(testDir, devFile)
						cfg.DeviceList = append(cfg.DeviceList, absPath)
						// clunky...
						for idx, line := range tc.wantBuf {
							if strings.Contains(line, devFile) {
								tc.wantBuf[idx] = strings.Replace(line, devFile,
									absPath, -1)
							}
						}
					}
				default:
					cfg.DeviceList = tc.devList
				}
			}

			if tc.fileSize != 0 {
				cfg.FileSize = tc.fileSize
			}

			engineConfig := engine.NewConfig().
				WithFabricProvider("test"). // valid enough to pass "not-blank" test
				WithFabricInterface("test").
				WithFabricInterfacePort(42).
				WithScmClass("dcpm").
				WithScmDeviceList("foo").
				WithScmMountPoint(testDir)
			engineConfig.Storage.Bdev = cfg

			gotValidateErr := engineConfig.Validate() // populate output path
			common.CmpErr(t, tc.expValidateErr, gotValidateErr)
			if tc.expValidateErr != nil {
				return
			}
			cfg = engineConfig.Storage.Bdev // refer to validated config

			req := FormatRequestFromConfig(log, &cfg)

			sb := defaultBackend(log)
			// VMD state will be set on the backend during
			// provider format request forwarding
			if tc.bdevVmdDisabled {
				sb.DisableVMD()
			}

			gotWriteErr := sb.writeNvmeConfig(&req)
			common.CmpErr(t, tc.expWriteErr, gotWriteErr)
			if tc.expWriteErr != nil {
				return
			}

			if req.ConfigPath == "" {
				if len(req.DeviceList) == 0 {
					return
				}
				t.Fatal("request config path empty but device list isn't")
			}

			gotBuf, err := ioutil.ReadFile(req.ConfigPath)
			if err != nil {
				t.Fatal(err)
			}

			// replace hostname in wantBuf
			hn, _ := os.Hostname()
			for i := range tc.wantBuf {
				tc.wantBuf[i] = strings.ReplaceAll(tc.wantBuf[i], "hostfoo", hn)
			}

			if diff := cmp.Diff(strings.Join(tc.wantBuf, "\n"), string(gotBuf)); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}

			if cfg.VosEnv != tc.vosEnv {
				t.Fatalf("expected VosEnv to be %q, but it was %q", tc.vosEnv, cfg.VosEnv)
			}
		})
	}
}

// TestBackend_createEmptyFile verifies empty files are created as expected.
func TestBackend_createEmptyFile(t *testing.T) {
	tests := map[string]struct {
		path          string
		pathImmutable bool // avoid adjusting path in test if set
		size          uint64
		expErr        error
	}{
		"relative path": {
			path:          "somewhere/bad",
			pathImmutable: true,
			expErr:        errors.New("got relative"),
		},
		"zero size": {
			size:   0,
			expErr: errors.New("zero"),
		},
		"non-existent path": {
			path:   "/timbuk/tu",
			size:   humanize.MiByte,
			expErr: errors.New("no such file or directory"),
		},
		"successful create": {
			path: "/outfile",
			size: humanize.MiByte,
		},
	}

	for name, tc := range tests {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			testDir, clean := common.CreateTestDir(t)
			defer clean()

			if !tc.pathImmutable {
				tc.path = filepath.Join(testDir, tc.path)
			}

			gotErr := createEmptyFile(log, tc.path, tc.size)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			expSize := (tc.size / clsFileBlkSize) * clsFileBlkSize

			st, err := os.Stat(tc.path)
			if err != nil {
				t.Fatal(err)
			}
			gotSize := st.Size()
			if gotSize != int64(expSize) {
				t.Fatalf("expected %s size to be %d, but got %d",
					tc.path, expSize, gotSize)
			}
		})
	}
}
