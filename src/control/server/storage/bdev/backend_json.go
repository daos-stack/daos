//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/dustin/go-humanize"
)

// SPDK bdev subsystem configuration method name definitions.
const (
	BdevSetOptions           = "bdev_set_options"
	BdevNvmeSetOptions       = "bdev_nvme_set_options"
	BdevNvmeAttachController = "bdev_nvme_attach_controller"
	BdevNvmeSetHotplug       = "bdev_nvme_set_hotplug"
)

// SpdkSubsystemConfigParams is an interface that defines an object that
// contains details for a subsystem configuration method.
type SpdkSubsystemConfigParams interface {
	isSpdkSubsystemConfigParams()
}

// SetOptionsParms specifies details for a BdevSetOptions method.
type SetOptionsParams struct {
	BdevIoPoolSize  uint64 `json:"bdev_io_pool_size"`
	BdevIoCacheSize uint64 `json:"bdev_io_cache_size"`
}

func (sop SetOptionsParams) isSpdkSubsystemConfigParams() {}

// NvmeSetOptionsParms specifies details for a BdevNvmeSetOptions method.
type NvmeSetOptionsParams struct {
	RetryCount               uint32 `json:"retry_count"`
	TimeoutUsec              uint64 `json:"timeout_us"`
	NvmeAdminqPollPeriodUsec uint32 `json:"nvme_adminq_poll_period_us"`
	ActionOnTimeout          string `json:"action_on_timeout"`
	NvmeIoqPollPeriodUsec    uint32 `json:"nvme_ioq_poll_period_us"`
}

func (nsop NvmeSetOptionsParams) isSpdkSubsystemConfigParams() {}

// NvmeAttachControllerParams specifies details for a BdevNvmeAttachController
// method.
type NvmeAttachControllerParams struct {
	TransportType    string `json:"trtype"`
	DeviceName       string `json:"name"`
	TransportAddress string `json:"traddr"`
}

func (napp NvmeAttachControllerParams) isSpdkSubsystemConfigParams() {}

// NvmeSetHotplugParams specifies details for a BdevNvmeSetHotplug method.
type NvmeSetHotplugParams struct {
	Enable     bool   `json:"enable"`
	PeriodUsec uint64 `json:"period_us"`
}

func (nshp NvmeSetHotplugParams) isSpdkSubsystemConfigParams() {}

// SpdkSubsystemConfig entries apply to any SpdkSubsystem.
type SpdkSubsystemConfig struct {
	Params SpdkSubsystemConfigParams `json:"params"`
	Method string                    `json:"method"`
}

// SpdkSubsystem entries make up the Subsystems field of a SpdkConfig.
type SpdkSubsystem struct {
	Name    string                 `json:"subsystem"`
	Configs []*SpdkSubsystemConfig `json:"config"`
}

// SpdkConfig is used to indicate which devices are to be used by SPDK and the
// desired behaviour of SPDK subsystems.
type SpdkConfig struct {
	Subsystems []*SpdkSubsystem `json:"subsystems"`
}

func defaultSpdkConfig() *SpdkConfig {
	bdevSubsystemConfigs := []*SpdkSubsystemConfig{
		{
			Method: BdevSetOptions,
			Params: SetOptionsParams{
				BdevIoPoolSize:  humanize.KiByte * 64,
				BdevIoCacheSize: 256,
			},
		},
		{
			Method: BdevNvmeSetOptions,
			Params: NvmeSetOptionsParams{
				RetryCount:               4,
				NvmeAdminqPollPeriodUsec: 100 * 1000,
				ActionOnTimeout:          "none",
			},
		},
		{
			Method: BdevNvmeSetHotplug,
			Params: NvmeSetHotplugParams{
				PeriodUsec: 10 * 1000 * 1000,
			},
		},
	}

	subsystems := []*SpdkSubsystem{
		{
			Name:    "bdev",
			Configs: bdevSubsystemConfigs,
		},
	}

	return &SpdkConfig{
		Subsystems: subsystems,
	}
}

func getNvmeAttachMethods(devs []string, host string) (sscs []*SpdkSubsystemConfig) {
	for i, d := range devs {
		name := fmt.Sprintf("Nvme_%s_%d", host, i)
		sscs = append(sscs, &SpdkSubsystemConfig{
			Method: BdevNvmeAttachController,
			Params: NvmeAttachControllerParams{
				TransportType:    "PCIe",
				DeviceName:       name,
				TransportAddress: d,
			},
		})
	}

	return
}

func newNvmeSpdkConfig(log logging.Logger, enableVmd bool, req *FormatRequest) (*SpdkConfig, error) {
	sc := defaultSpdkConfig()

	// default spdk config will have only one bdev subsystem, append device
	// attach config method calls
	sc.Subsystems[0].Configs = append(sc.Subsystems[0].Configs,
		getNvmeAttachMethods(req.DeviceList, req.Hostname)...)

	return sc, nil
}
