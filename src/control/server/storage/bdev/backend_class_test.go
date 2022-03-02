//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"context"
	"io/ioutil"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/dustin/go-humanize"
	"github.com/google/go-cmp/cmp"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

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
			defer test.ShowBufferOnFailure(t, buf)

			testDir, clean := test.CreateTestDir(t)
			defer clean()

			if !tc.pathImmutable {
				tc.path = filepath.Join(testDir, tc.path)
			}

			gotErr := createEmptyFile(log, tc.path, tc.size)
			test.CmpErr(t, tc.expErr, gotErr)
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

func TestBackend_writeJSONFile(t *testing.T) {
	tierID := 84
	host, _ := os.Hostname()

	tests := map[string]struct {
		confIn            storage.TierConfig
		enableVmd         bool
		enableHotplug     bool
		hotplugBusidRange string
		expErr            error
		expOut            string
	}{
		"nvme; single ssds": {
			confIn: storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1)...),
				},
			},
			expOut: `
{
  "daos_data": {
    "config": []
  },
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
            "period_us": 0
          },
          "method": "bdev_nvme_set_hotplug"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_0_84",
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
			confIn: storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1, 2)...),
				},
			},
			expOut: `
{
  "daos_data": {
    "config": []
  },
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
            "period_us": 0
          },
          "method": "bdev_nvme_set_hotplug"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_0_84",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_1_84",
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
		"nvme; multiple ssds; vmd enabled; bus-id range": {
			confIn: storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1, 2)...),
					BusidRange: storage.MustNewBdevBusRange("0x80-0x8f"),
				},
			},
			enableVmd: true,
			expOut: `
{
  "daos_data": {
    "config": []
  },
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
            "period_us": 0
          },
          "method": "bdev_nvme_set_hotplug"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_0_84",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_1_84",
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
		"nvme; multiple ssds; hotplug enabled; bus-id range": {
			confIn: storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1, 2)...),
					BusidRange: storage.MustNewBdevBusRange("0x80-0x8f"),
				},
			},
			enableHotplug: true,
			expOut: `
{
  "daos_data": {
    "config": [
      {
        "params": {
          "begin": 128,
          "end": 143
        },
        "method": "hotplug_busid_range"
      }
    ]
  },
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
            "enable": true,
            "period_us": 5000000
          },
          "method": "bdev_nvme_set_hotplug"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_0_84",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_1_84",
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
		"nvme; multiple ssds; vmd and hotplug enabled": {
			confIn: storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1, 2)...),
				},
			},
			enableHotplug: true,
			enableVmd:     true,
			expOut: `
{
  "daos_data": {
    "config": [
      {
        "params": {
          "begin": 0,
          "end": 255
        },
        "method": "hotplug_busid_range"
      }
    ]
  },
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
            "enable": true,
            "period_us": 5000000
          },
          "method": "bdev_nvme_set_hotplug"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_0_84",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_1_84",
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
			defer test.ShowBufferOnFailure(t, buf)

			testDir, clean := test.CreateTestDir(t)
			defer clean()

			cfgOutputPath := filepath.Join(testDir, "outfile")
			engineConfig := engine.MockConfig().
				WithFabricProvider("test"). // valid enough to pass "not-blank" test
				WithFabricInterface("test").
				WithFabricInterfacePort(42).
				WithStorage(
					storage.NewTierConfig().
						WithStorageClass("dcpm").
						WithScmDeviceList("foo").
						WithScmMountPoint("scmmnt"),
					&tc.confIn,
				).
				WithStorageConfigOutputPath(cfgOutputPath).
				WithStorageEnableHotplug(tc.enableHotplug)

			req, err := storage.BdevWriteConfigRequestFromConfig(context.TODO(), log,
				&engineConfig.Storage, tc.enableVmd, storage.MockGetTopology)
			if err != nil {
				t.Fatal(err)
			}

			gotErr := writeJsonConfig(log, &req)
			test.CmpErr(t, tc.expErr, gotErr)
			if tc.expErr != nil {
				return
			}

			gotOut, err := ioutil.ReadFile(cfgOutputPath)
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
