//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"
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

// TestBackend_writeJsonConfig verifies config parameters for bdev get
// converted into nvme config files that can be consumed by spdk.
func TestBackend_writeJsonConfig(t *testing.T) {
	mockMntpt := "/mock/mnt/daos"
	host, _ := os.Hostname()

	tests := map[string]struct {
		class              storage.BdevClass
		fileSizeGB         int
		devList            []string
		enableVmd          bool
		vosEnv             string
		expExtraBdevCfgs   []*SpdkSubsystemConfig
		expExtraSubsystems []*SpdkSubsystem
		expValidateErr     error
		expErr             error
	}{
		"config validation failure": {
			class:          storage.BdevClassNvme,
			devList:        []string{"not a pci address"},
			expValidateErr: errors.New("unexpected pci address"),
		},
		"multiple controllers": {
			class:   storage.BdevClassNvme,
			devList: []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			expExtraBdevCfgs: []*SpdkSubsystemConfig{
				{
					Method: SpdkBdevNvmeAttachController,
					Params: NvmeAttachControllerParams{
						TransportType:    "PCIe",
						DeviceName:       fmt.Sprintf("Nvme_%s_0", host),
						TransportAddress: common.MockPCIAddr(1),
					},
				},
				{
					Method: SpdkBdevNvmeAttachController,
					Params: NvmeAttachControllerParams{
						TransportType:    "PCIe",
						DeviceName:       fmt.Sprintf("Nvme_%s_1", host),
						TransportAddress: common.MockPCIAddr(2),
					},
				},
			},
			vosEnv: "NVME",
		},
		"multiple controllers; vmd enabled": {
			class:     storage.BdevClassNvme,
			enableVmd: true,
			devList:   []string{common.MockPCIAddr(1), common.MockPCIAddr(2)},
			expExtraBdevCfgs: []*SpdkSubsystemConfig{
				{
					Method: SpdkBdevNvmeAttachController,
					Params: NvmeAttachControllerParams{
						TransportType:    "PCIe",
						DeviceName:       fmt.Sprintf("Nvme_%s_0", host),
						TransportAddress: common.MockPCIAddr(1),
					},
				},
				{
					Method: SpdkBdevNvmeAttachController,
					Params: NvmeAttachControllerParams{
						TransportType:    "PCIe",
						DeviceName:       fmt.Sprintf("Nvme_%s_1", host),
						TransportAddress: common.MockPCIAddr(2),
					},
				},
			},
			expExtraSubsystems: []*SpdkSubsystem{
				{
					Name: "vmd",
					Configs: []*SpdkSubsystemConfig{
						{
							Method: SpdkVmdEnable,
							Params: VmdEnableParams{},
						},
					},
				},
			},
			vosEnv: "NVME",
		},
		"AIO file class; multiple files; zero file size": {
			class:          storage.BdevClassFile,
			devList:        []string{"/path/to/myfile", "/path/to/myotherfile"},
			expValidateErr: errors.New("requires non-zero bdev_size"),
		},
		"AIO file class; multiple files; non-zero file size": {
			class:      storage.BdevClassFile,
			fileSizeGB: 1,
			devList:    []string{"/path/to/myfile", "/path/to/myotherfile"},
			expExtraBdevCfgs: []*SpdkSubsystemConfig{
				{
					Method: SpdkBdevAioCreate,
					Params: AioCreateParams{
						BlockSize:  humanize.KiByte * 4,
						DeviceName: fmt.Sprintf("AIO_%s_0", host),
						Filename:   "/path/to/myfile",
					},
				},
				{
					Method: SpdkBdevAioCreate,
					Params: AioCreateParams{
						BlockSize:  humanize.KiByte * 4,
						DeviceName: fmt.Sprintf("AIO_%s_1", host),
						Filename:   "/path/to/myotherfile",
					},
				},
			},
			vosEnv: "AIO",
		},
		"AIO kdev class; multiple devices": {
			class:   storage.BdevClassKdev,
			devList: []string{"/dev/sdb", "/dev/sdc"},
			expExtraBdevCfgs: []*SpdkSubsystemConfig{
				{
					Method: SpdkBdevAioCreate,
					Params: AioCreateParams{
						DeviceName: fmt.Sprintf("AIO_%s_0", host),
						Filename:   "/dev/sdb",
					},
				},
				{
					Method: SpdkBdevAioCreate,
					Params: AioCreateParams{
						DeviceName: fmt.Sprintf("AIO_%s_1", host),
						Filename:   "/dev/sdc",
					},
				},
			},
			vosEnv: "AIO",
		},
	}

	for name, tc := range tests {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			cfg := storage.BdevConfig{
				DeviceList: tc.devList,
				FileSize:   tc.fileSizeGB,
			}
			if tc.class != "" {
				cfg.Class = tc.class
			}

			engineConfig := engine.NewConfig().
				WithFabricProvider("test"). // valid enough to pass "not-blank" test
				WithFabricInterface("test").
				WithFabricInterfacePort(42).
				WithScmClass("dcpm").
				WithScmDeviceList("foo").
				WithScmMountPoint(mockMntpt)
			engineConfig.Storage.Bdev = cfg

			gotValidateErr := engineConfig.Validate() // populate output path
			common.CmpErr(t, tc.expValidateErr, gotValidateErr)
			if tc.expValidateErr != nil {
				return
			}
			cfg = engineConfig.Storage.Bdev // refer to validated config

			req := FormatRequestFromConfig(log, &cfg)

			gotCfg, gotErr := newSpdkConfig(log, tc.enableVmd, &req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			// currently we are only expecting nvme config with bdev subsystem
			expCfg := defaultSpdkConfig()
			for _, ec := range tc.expExtraBdevCfgs {
				expCfg.Subsystems[0].Configs = append(expCfg.Subsystems[0].Configs, ec)
			}
			for _, ess := range tc.expExtraSubsystems {
				expCfg.Subsystems = append(expCfg.Subsystems, ess)
			}

			if diff := cmp.Diff(expCfg, gotCfg); diff != "" {
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

			expSize := (tc.size / aioBlockSize) * aioBlockSize

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

func TestBackend_createJsonFile(t *testing.T) {
	host, _ := os.Hostname()

	tests := map[string]struct {
		confIn    storage.BdevConfig
		enableVmd bool
		expErr    error
		expOut    string
	}{
		"nvme; single ssds": {
			confIn: storage.BdevConfig{
				Class:      storage.BdevClassNvme,
				DeviceList: common.MockPCIAddrs(1),
			},
			expOut: `
{
  "subsystems": [
    {
      "subsystem": "bdev",
      "config": [
        {
          "params": {
            "bdev_io_pool_size": 65536,
            "bdev_io_cache_size": 256
          },
          "method": "bdev_set_options"
        },
        {
          "params": {
            "retry_count": 4,
            "timeout_us": 0,
            "nvme_adminq_poll_period_us": 100000,
            "action_on_timeout": "none",
            "nvme_ioq_poll_period_us": 0
          },
          "method": "bdev_nvme_set_options"
        },
        {
          "params": {
            "enable": false,
            "period_us": 10000000
          },
          "method": "bdev_nvme_set_hotplug"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_0",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        }
      ]
    }
  ]
}
`,
		},
		"nvme; multiple ssds": {
			confIn: storage.BdevConfig{
				Class:      storage.BdevClassNvme,
				DeviceList: common.MockPCIAddrs(1, 2),
			},
			expOut: `
{
  "subsystems": [
    {
      "subsystem": "bdev",
      "config": [
        {
          "params": {
            "bdev_io_pool_size": 65536,
            "bdev_io_cache_size": 256
          },
          "method": "bdev_set_options"
        },
        {
          "params": {
            "retry_count": 4,
            "timeout_us": 0,
            "nvme_adminq_poll_period_us": 100000,
            "action_on_timeout": "none",
            "nvme_ioq_poll_period_us": 0
          },
          "method": "bdev_nvme_set_options"
        },
        {
          "params": {
            "enable": false,
            "period_us": 10000000
          },
          "method": "bdev_nvme_set_hotplug"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_0",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_1",
            "traddr": "0000:02:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        }
      ]
    }
  ]
}
`,
		},
		"nvme; multiple ssds; vmd enabled": {
			confIn: storage.BdevConfig{
				Class:      storage.BdevClassNvme,
				DeviceList: common.MockPCIAddrs(1, 2),
			},
			enableVmd: true,
			expOut: `
{
  "subsystems": [
    {
      "subsystem": "bdev",
      "config": [
        {
          "params": {
            "bdev_io_pool_size": 65536,
            "bdev_io_cache_size": 256
          },
          "method": "bdev_set_options"
        },
        {
          "params": {
            "retry_count": 4,
            "timeout_us": 0,
            "nvme_adminq_poll_period_us": 100000,
            "action_on_timeout": "none",
            "nvme_ioq_poll_period_us": 0
          },
          "method": "bdev_nvme_set_options"
        },
        {
          "params": {
            "enable": false,
            "period_us": 10000000
          },
          "method": "bdev_nvme_set_hotplug"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_0",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_1",
            "traddr": "0000:02:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        }
      ]
    },
    {
      "subsystem": "vmd",
      "config": [
        {
          "params": {},
          "method": "enable_vmd"
        }
      ]
    }
  ]
}
`,
		},
	}

	for name, tc := range tests {
		t.Run(name, func(t *testing.T) {
			log, buf := logging.NewTestLogger(t.Name())
			defer common.ShowBufferOnFailure(t, buf)

			testDir, clean := common.CreateTestDir(t)
			defer clean()

			req := FormatRequestFromConfig(log, &tc.confIn)
			req.ConfigPath = filepath.Join(testDir, "outfile")

			gotErr := writeJsonConfig(log, tc.enableVmd, &req)
			common.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotOut, err := ioutil.ReadFile(req.ConfigPath)
			if err != nil {
				t.Fatal(err)
			}

			// replace hostname in wantOut
			tc.expOut = strings.ReplaceAll(tc.expOut, "hostfoo", host)
			tc.expOut = strings.TrimSpace(tc.expOut)

			if diff := cmp.Diff(tc.expOut, string(gotOut)); diff != "" {
				t.Fatalf("(-want, +got):\n%s", diff)
			}
		})
	}
}
