//
// (C) Copyright 2021-2023 Intel Corporation.
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

	"github.com/daos-stack/daos/src/control/common/test"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// TestBackend_createAioFile verifies AIO files are created (or not) as expected.
func TestBackend_createAioFile(t *testing.T) {
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

			req := &storage.BdevFormatRequest{
				OwnerUID: os.Getuid(),
				OwnerGID: os.Getgid(),
				Properties: storage.BdevTierProperties{
					DeviceFileSize: tc.size,
				},
			}

			gotResp := createAioFile(log, tc.path, req)
			if tc.expErr != nil {
				if gotResp.Error == nil {
					t.Fatal("expected non-nil error in response")
				}
				test.CmpErr(t, tc.expErr, gotResp.Error)
				if _, err := os.Stat(tc.path); err == nil {
					t.Fatalf("%s file was not removed on error", tc.path)
				}

				return
			}
			if gotResp.Error != nil {
				t.Fatal("expected nil error in response")
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
		accelEngine       string
		accelOptMask      storage.AccelOptionBits
		rpcSrvEnable      bool
		rpcSrvSockAddr    string
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
            "name": "Nvme_hostfoo_0_84_0",
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
					DeviceRoles: storage.BdevRoles{
						OptionBits: storage.OptionBits(storage.BdevRoleAll),
					},
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
            "name": "Nvme_hostfoo_0_84_7",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_1_84_7",
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
            "name": "Nvme_hostfoo_0_84_0",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_1_84_0",
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
            "name": "Nvme_hostfoo_0_84_0",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_1_84_0",
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
            "name": "Nvme_hostfoo_0_84_0",
            "traddr": "0000:01:00.0"
          },
          "method": "bdev_nvme_attach_controller"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_1_84_0",
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
		"nvme; single controller; acceleration set to none; move and crc opts specified": {
			confIn: storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1)...),
				},
			},
			// Verify default "none" acceleration setting is ignored.
			accelEngine:  storage.AccelEngineNone,
			accelOptMask: storage.AccelOptCRCFlag | storage.AccelOptMoveFlag,
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
            "name": "Nvme_hostfoo_0_84_0",
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
		"nvme; single controller; acceleration set to spdk; no opts specified": {
			confIn: storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1)...),
				},
			},
			// Verify default "spdk" acceleration setting with no enable options is ignored.
			accelEngine: storage.AccelEngineSPDK,
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
            "name": "Nvme_hostfoo_0_84_0",
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
		"nvme; single controller; accel set with opts; rpc srv set": {
			confIn: storage.TierConfig{
				Tier:  tierID,
				Class: storage.ClassNvme,
				Bdev: storage.BdevConfig{
					DeviceList: storage.MustNewBdevDeviceList(test.MockPCIAddrs(1)...),
				},
			},
			accelEngine:    storage.AccelEngineSPDK,
			accelOptMask:   storage.AccelOptCRCFlag | storage.AccelOptMoveFlag,
			rpcSrvEnable:   true,
			rpcSrvSockAddr: "/tmp/spdk.sock",
			expOut: `
{
  "daos_data": {
    "config": [
      {
        "params": {
          "accel_engine": "spdk",
          "accel_opts": 3
        },
        "method": "accel_props"
      },
      {
        "params": {
          "enable": true,
          "sock_addr": "/tmp/spdk.sock"
        },
        "method": "spdk_rpc_srv"
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
            "enable": false,
            "period_us": 0
          },
          "method": "bdev_nvme_set_hotplug"
        },
        {
          "params": {
            "trtype": "PCIe",
            "name": "Nvme_hostfoo_0_84_0",
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
				WithStorageEnableHotplug(tc.enableHotplug).
				WithStorageAccelProps(tc.accelEngine, tc.accelOptMask).
				WithStorageSpdkRpcSrvProps(tc.rpcSrvEnable, tc.rpcSrvSockAddr)

			req, err := storage.BdevWriteConfigRequestFromConfig(test.Context(t), log,
				&engineConfig.Storage, tc.enableVmd, storage.MockGetTopology)
			if err != nil {
				t.Fatal(err)
			}

			gotErr := writeJsonConfig(log, req)
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
