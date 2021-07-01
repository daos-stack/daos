//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"fmt"

	"github.com/dustin/go-humanize"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// SPDK bdev subsystem configuration method name definitions.
const (
	SpdkBdevSetOptions           = "bdev_set_options"
	SpdkBdevNvmeSetOptions       = "bdev_nvme_set_options"
	SpdkBdevNvmeAttachController = "bdev_nvme_attach_controller"
	SpdkBdevNvmeSetHotplug       = "bdev_nvme_set_hotplug"
	SpdkVmdEnable                = "enable_vmd"
	SpdkBdevAioCreate            = "bdev_aio_create"
)

// SpdkSubsystemConfigParams is an interface that defines an object that
// contains details for a subsystem configuration method.
type SpdkSubsystemConfigParams interface {
	isSpdkSubsystemConfigParams()
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

//=======
//	clsNvmeTemplate = `[Nvme]
//{{ $host := .Hostname }}{{ $tier := .Tier }}{{ range $i, $e := .DeviceList }}    TransportID "trtype:PCIe traddr:{{$e}}" Nvme_{{$host}}_{{$i}}_{{$tier}}
//{{ end }}    RetryCount 4
//    TimeoutUsec 0
//    ActionOnTimeout None
//    AdminPollRate 100000
//    HotplugEnable No
//    HotplugPollRate 0
//`
//	// device block size hardcoded to 4096
//	clsFileTemplate = `[AIO]
//{{ $host := .Hostname }}{{ $tier := .Tier }}{{ range $i, $e := .DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}}_{{$tier}} 4096
//{{ end }}`
//	clsKdevTemplate = `[AIO]
//{{ $host := .Hostname }}{{ $tier := .Tier }}{{ range $i, $e := .DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}}_{{$tier}}
//{{ end }}`
//	clsFileBlkSize = humanize.KiByte * 4
//>>>>>>> 247657339daf862e9fffd949f6bef225851011ac

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
// desired behavior of SPDK subsystems.
type SpdkConfig struct {
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

func getSpdkConfigMethods(req *storage.BdevWriteNvmeConfigRequest) (sscs []*SpdkSubsystemConfig) {
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

		for index, dev := range tier.DeviceList {
			name := fmt.Sprintf("%s_%d_%d", req.Hostname, index, tier.Tier)
			sscs = append(sscs, f(name, dev))
		}
	}

	return
}

//	for _, tier := range req.TierProps {
//		if tier.Class == storage.ClassNvme && !sb.IsVMDDisabled() {
//			templ := `[Vmd]
//    Enable True
//
//`
//			if _, err := f.WriteString(templ); err != nil {
//				return errors.Wrap(err, "write")
//			}
//			break
//		}
//	}

//	for _, tier := range req.TierProps {
//		templ := map[storage.Class]string{
//			storage.ClassNvme: clsNvmeTemplate,
//			storage.ClassKdev: clsKdevTemplate,
//			storage.ClassFile: clsFileTemplate,
//		}[tier.Class]

//		// special handling for class nvme
//		if tier.Class == storage.ClassNvme {
//			if len(tier.DeviceList) == 0 {
//				sb.log.Debug("skip write nvme conf for empty device list")
//				continue
//			}
//		}

//		// spdk ini file expects device size in MBs
//		tier.DeviceFileSize = tier.DeviceFileSize / humanize.MiByte

//		sb.log.Debugf("write nvme output config: %+v", req)
//		if err := appendConf(sb.log, templ, &tier, f); err != nil {
//			return err
//		}

// WithVmdEnabled adds vmd subsystem with enable method to an SpdkConfig.
func (sc *SpdkConfig) WithVmdEnabled() *SpdkConfig {
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
// BdevWriteNvmeConfigRequest to the bdev subsystem of an SpdkConfig.
func (sc *SpdkConfig) WithBdevConfigs(log logging.Logger, req *storage.BdevWriteNvmeConfigRequest) *SpdkConfig {
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

func newSpdkConfig(log logging.Logger, enableVmd bool, req *storage.BdevWriteNvmeConfigRequest) (*SpdkConfig, error) {
	sc := defaultSpdkConfig()

	for _, tp := range req.TierProps {
		if enableVmd && tp.Class == storage.ClassNvme {
			sc.WithVmdEnabled()
			break
		}
	}

	return sc.WithBdevConfigs(log, req), nil
}
