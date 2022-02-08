//
// (C) Copyright 2021-2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"
	"time"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	hotplugPeriod = 5 * time.Second
)

// SPDK bdev subsystem configuration method name definitions.
const (
	SpdkBdevSetOptions           = "bdev_set_options"
	SpdkBdevNvmeSetOptions       = "bdev_nvme_set_options"
	SpdkBdevNvmeAttachController = "bdev_nvme_attach_controller"
	SpdkBdevNvmeSetHotplug       = "bdev_nvme_set_hotplug"
	SpdkVmdEnable                = "enable_vmd"
	SpdkBdevAioCreate            = "bdev_aio_create"
	SpdkHotplugBusidRange        = "hotplug_busid_range"
)

// SpdkSubsystemConfigParams is an interface that defines an object that
// contains details for a subsystem configuration method.
type SpdkSubsystemConfigParams interface {
	isSpdkSubsystemConfigParams()
}

// SpdkDaosConfigParams is an interface that defines an object that
// contains details for a DAOS configuration method.
type SpdkDaosConfigParams interface {
	isSpdkDaosConfigParams()
}

// SetOptionsParams specifies details for a SpdkBdevSetOptions method.
type SetOptionsParams struct {
	BdevIoPoolSize  uint64 `json:"bdev_io_pool_size"`
	BdevIoCacheSize uint64 `json:"bdev_io_cache_size"`
}

func (sop SetOptionsParams) isSpdkSubsystemConfigParams() {}

// NvmeSetOptionsParams specifies details for a SpdkBdevNvmeSetOptions method.
type NvmeSetOptionsParams struct {
	RetryCount               uint32 `json:"retry_count"`
	TimeoutUsec              uint64 `json:"timeout_us"`
	NvmeAdminqPollPeriodUsec uint32 `json:"nvme_adminq_poll_period_us"`
	ActionOnTimeout          string `json:"action_on_timeout"`
	NvmeIoqPollPeriodUsec    uint32 `json:"nvme_ioq_poll_period_us"`
}

func (nsop NvmeSetOptionsParams) isSpdkSubsystemConfigParams() {}

// NvmeAttachControllerParams specifies details for a SpdkBdevNvmeAttachController
// method.
type NvmeAttachControllerParams struct {
	TransportType    string `json:"trtype"`
	DeviceName       string `json:"name"`
	TransportAddress string `json:"traddr"`
}

func (napp NvmeAttachControllerParams) isSpdkSubsystemConfigParams() {}

// NvmeSetHotplugParams specifies details for a SpdkBdevNvmeSetHotplug method.
type NvmeSetHotplugParams struct {
	Enable     bool   `json:"enable"`
	PeriodUsec uint64 `json:"period_us"`
}

func (nshp NvmeSetHotplugParams) isSpdkSubsystemConfigParams() {}

// VmdEnableParams specifies details for a SpdkVmdEnable method.
type VmdEnableParams struct{}

func (vep VmdEnableParams) isSpdkSubsystemConfigParams() {}

// AioCreateParams specifies details for a SpdkAioCreate method.
type AioCreateParams struct {
	BlockSize  uint64 `json:"block_size"`
	DeviceName string `json:"name"`
	Filename   string `json:"filename"`
}

func (acp AioCreateParams) isSpdkSubsystemConfigParams() {}

// HotplugBusidRangeParams specifies details for a SpdkHotplugBusidRange method.
type HotplugBusidRangeParams struct {
	Begin uint8 `json:"begin"`
	End   uint8 `json:"end"`
}

func (hbrp HotplugBusidRangeParams) isSpdkDaosConfigParams() {}

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

// SpdkDaosConfig entries apply to the SpdkDaos entry.
type SpdkDaosConfig struct {
	Params SpdkDaosConfigParams `json:"params"`
	Method string               `json:"method"`
}

// SpdkDaos entries make up the DaosData field of a SpdkConfig.
type SpdkDaos struct {
	Configs []*SpdkDaosConfig `json:"config"`
}

// SpdkConfig is used to indicate which devices are to be used by SPDK and the
// desired behavior of SPDK subsystems.
type SpdkConfig struct {
	DaosData   *SpdkDaos        `json:"daos_data"`
	Subsystems []*SpdkSubsystem `json:"subsystems"`
}

func defaultSpdkConfig() *SpdkConfig {
	bdevSubsystemConfigs := []*SpdkSubsystemConfig{
		{
			Method: SpdkBdevSetOptions,
			Params: SetOptionsParams{
				BdevIoPoolSize:  humanize.KiByte * 64,
				BdevIoCacheSize: 256,
			},
		},
		{
			Method: SpdkBdevNvmeSetOptions,
			Params: NvmeSetOptionsParams{
				RetryCount:               4,
				NvmeAdminqPollPeriodUsec: 100 * 1000,
				ActionOnTimeout:          "none",
			},
		},
		{
			Method: SpdkBdevNvmeSetHotplug,
			Params: NvmeSetHotplugParams{},
		},
	}

	subsystems := []*SpdkSubsystem{
		{
			Name:    "bdev",
			Configs: bdevSubsystemConfigs,
		},
	}

	daosData := &SpdkDaos{
		Configs: make([]*SpdkDaosConfig, 0),
	}

	return &SpdkConfig{
		DaosData:   daosData,
		Subsystems: subsystems,
	}
}

type configMethodGetter func(string, string) *SpdkSubsystemConfig

func getNvmeAttachMethod(name, pci string) *SpdkSubsystemConfig {
	return &SpdkSubsystemConfig{
		Method: SpdkBdevNvmeAttachController,
		Params: NvmeAttachControllerParams{
			TransportType:    "PCIe",
			DeviceName:       fmt.Sprintf("Nvme_%s", name),
			TransportAddress: pci,
		},
	}
}

func getAioFileCreateMethod(name, path string) *SpdkSubsystemConfig {
	return &SpdkSubsystemConfig{
		Method: SpdkBdevAioCreate,
		Params: AioCreateParams{
			DeviceName: fmt.Sprintf("AIO_%s", name),
			Filename:   path,
			BlockSize:  aioBlockSize,
		},
	}
}

func getAioKdevCreateMethod(name, path string) *SpdkSubsystemConfig {
	return &SpdkSubsystemConfig{
		Method: SpdkBdevAioCreate,
		Params: AioCreateParams{
			DeviceName: fmt.Sprintf("AIO_%s", name),
			Filename:   path,
		},
	}
}

func getSpdkConfigMethods(req *storage.BdevWriteConfigRequest) (sscs []*SpdkSubsystemConfig) {
	for _, tier := range req.TierProps {
		var f configMethodGetter

		switch tier.Class {
		case storage.ClassNvme:
			f = getNvmeAttachMethod
		case storage.ClassFile:
			f = getAioFileCreateMethod
		case storage.ClassKdev:
			f = getAioKdevCreateMethod
		}

		for index, dev := range tier.DeviceList.Devices() {
			name := fmt.Sprintf("%s_%d_%d", req.Hostname, index, tier.Tier)
			sscs = append(sscs, f(name, dev))
		}
	}

	return
}

// WithVMDEnabled adds vmd subsystem with enable method to an SpdkConfig.
func (sc *SpdkConfig) WithVMDEnabled() *SpdkConfig {
	sc.Subsystems = append(sc.Subsystems, &SpdkSubsystem{
		Name: "vmd",
		Configs: []*SpdkSubsystemConfig{
			{
				Method: SpdkVmdEnable,
				Params: VmdEnableParams{},
			},
		},
	})

	return sc
}

// WithBdevConfigs adds config methods derived from the input
// BdevWriteConfigRequest to the bdev subsystem of an SpdkConfig.
func (sc *SpdkConfig) WithBdevConfigs(log logging.Logger, req *storage.BdevWriteConfigRequest) *SpdkConfig {
	for _, ss := range sc.Subsystems {
		if ss.Name != "bdev" {
			continue
		}

		ss.Configs = append(ss.Configs, getSpdkConfigMethods(req)...)

		return sc
	}

	log.Error("no bdev subsystem found in spdk config")
	return sc
}

func newSpdkConfig(log logging.Logger, req *storage.BdevWriteConfigRequest) (*SpdkConfig, error) {
	sc := defaultSpdkConfig()

	if req.VMDEnabled {
		for _, tp := range req.TierProps {
			if tp.Class == storage.ClassNvme {
				sc.WithVMDEnabled()
				break
			}
		}
	}

	if req.HotplugEnabled {
		var found bool
		for _, ss := range sc.Subsystems {
			if ss.Name != "bdev" {
				continue
			}

			for _, bsc := range ss.Configs {
				if bsc.Method == SpdkBdevNvmeSetHotplug {
					bsc.Params = NvmeSetHotplugParams{
						Enable:     true,
						PeriodUsec: uint64(hotplugPeriod.Microseconds()),
					}
					found = true

					break
				}
			}

			if found {
				break
			}
		}

		sc.DaosData.Configs = append(sc.DaosData.Configs, &SpdkDaosConfig{
			Method: SpdkHotplugBusidRange,
			Params: HotplugBusidRangeParams{
				Begin: req.HotplugBusidBegin,
				End:   req.HotplugBusidEnd,
			},
		})
	}

	return sc.WithBdevConfigs(log, req), nil
}
