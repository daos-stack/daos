//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"encoding/json"
	"fmt"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

// JSON tags should match decoding logic in src/bio/bio_config.c.

const (
	hotplugPeriod = 5 * time.Second
)

// SpdkSubsystemConfigParams is an interface that defines an object that
// contains details for a subsystem configuration method.
type SpdkSubsystemConfigParams interface {
	isSpdkSubsystemConfigParams()
}

// DaosConfigParams is an interface that defines an object that
// contains details for a DAOS configuration method.
type DaosConfigParams interface {
	isDaosConfigParams()
}

// SetOptionsParams specifies details for a storage.ConfBdevSetOptions method.
type SetOptionsParams struct {
	BdevIoPoolSize  uint64 `json:"bdev_io_pool_size"`
	BdevIoCacheSize uint64 `json:"bdev_io_cache_size"`
}

func (_ SetOptionsParams) isSpdkSubsystemConfigParams() {}

// NvmeSetOptionsParams specifies details for a storage.ConfBdevNvmeSetOptions method.
type NvmeSetOptionsParams struct {
	TransportRetryCount      uint32 `json:"transport_retry_count"`
	TimeoutUsec              uint64 `json:"timeout_us"`
	NvmeAdminqPollPeriodUsec uint32 `json:"nvme_adminq_poll_period_us"`
	ActionOnTimeout          string `json:"action_on_timeout"`
	NvmeIoqPollPeriodUsec    uint32 `json:"nvme_ioq_poll_period_us"`
}

func (_ NvmeSetOptionsParams) isSpdkSubsystemConfigParams() {}

// NvmeAttachControllerParams specifies details for a storage.ConfBdevNvmeAttachController
// method.
type NvmeAttachControllerParams struct {
	TransportType    string `json:"trtype"`
	DeviceName       string `json:"name"`
	TransportAddress string `json:"traddr"`
}

func (_ NvmeAttachControllerParams) isSpdkSubsystemConfigParams() {}

// NvmeSetHotplugParams specifies details for a storage.ConfBdevNvmeSetHotplug method.
type NvmeSetHotplugParams struct {
	Enable     bool   `json:"enable"`
	PeriodUsec uint64 `json:"period_us"`
}

func (_ NvmeSetHotplugParams) isSpdkSubsystemConfigParams() {}

// VmdEnableParams specifies details for a storage.ConfVmdEnable method.
type VmdEnableParams struct{}

func (vep VmdEnableParams) isSpdkSubsystemConfigParams() {}

// AioCreateParams specifies details for a storage.ConfAioCreate method.
type AioCreateParams struct {
	BlockSize  uint64 `json:"block_size"`
	DeviceName string `json:"name"`
	Filename   string `json:"filename"`
}

func (_ AioCreateParams) isSpdkSubsystemConfigParams() {}

// HotplugBusidRangeParams specifies details for a storage.ConfSetHotplugBusidRange method.
type HotplugBusidRangeParams struct {
	Begin uint8 `json:"begin"`
	End   uint8 `json:"end"`
}

func (_ HotplugBusidRangeParams) isDaosConfigParams() {}

// AccelPropsParams specifies details for a storage.ConfSetAccelProps method.
type AccelPropsParams storage.AccelProps

func (_ AccelPropsParams) isDaosConfigParams() {}

// SpdkRpcServerParams specifies details for a storage.ConfSetSpdkRpcServer method.
type SpdkRpcServerParams storage.SpdkRpcServer

func (_ SpdkRpcServerParams) isDaosConfigParams() {}

// AutoFaultyParams specifies details for a storage.ConfSetAutoFaultyProp method.
type AutoFaultyParams storage.BdevAutoFaulty

func (_ AutoFaultyParams) isDaosConfigParams() {}

// SpdkSubsystemConfig entries apply to any SpdkSubsystem.
type SpdkSubsystemConfig struct {
	Params SpdkSubsystemConfigParams `json:"params"`
	Method string                    `json:"method"`
}

func (ssc *SpdkSubsystemConfig) UnmarshalJSON(data []byte) error {
	var tmp struct {
		Params json.RawMessage `json:"params"`
		Method string          `json:"method"`
	}
	if err := strictJsonUnmarshalBuf(data, &tmp); err != nil {
		return err
	}
	ssc.Method = tmp.Method

	switch ssc.Method {
	case storage.ConfBdevSetOptions:
		ssc.Params = &SetOptionsParams{}
	case storage.ConfBdevNvmeSetOptions:
		ssc.Params = &NvmeSetOptionsParams{}
	case storage.ConfBdevNvmeAttachController:
		ssc.Params = &NvmeAttachControllerParams{}
	case storage.ConfBdevNvmeSetHotplug:
		ssc.Params = &NvmeSetHotplugParams{}
	case storage.ConfVmdEnable:
		ssc.Params = &VmdEnableParams{}
	case storage.ConfBdevAioCreate:
		ssc.Params = &AioCreateParams{}
	default:
		return errors.Errorf("unknown SPDK subsystem config method %q", ssc.Method)
	}

	return strictJsonUnmarshalBuf(tmp.Params, ssc.Params)
}

// SpdkSubsystem entries make up the Subsystems field of a SpdkConfig.
type SpdkSubsystem struct {
	Name    string                 `json:"subsystem"`
	Configs []*SpdkSubsystemConfig `json:"config"`
}

// DaosConfig entries apply to the DAOS entry.
type DaosConfig struct {
	Params DaosConfigParams `json:"params"`
	Method string           `json:"method"`
}

func (dc *DaosConfig) UnmarshalJSON(data []byte) error {
	var tmp struct {
		Params json.RawMessage `json:"params"`
		Method string          `json:"method"`
	}
	if err := strictJsonUnmarshalBuf(data, &tmp); err != nil {
		return err
	}
	dc.Method = tmp.Method

	switch dc.Method {
	case storage.ConfSetHotplugBusidRange:
		dc.Params = &HotplugBusidRangeParams{}
	case storage.ConfSetAccelProps:
		dc.Params = &AccelPropsParams{}
	case storage.ConfSetSpdkRpcServer:
		dc.Params = &SpdkRpcServerParams{}
	case storage.ConfSetAutoFaultyProps:
		dc.Params = &AutoFaultyParams{}
	default:
		return errors.Errorf("unknown DAOS config method %q", dc.Method)
	}

	return strictJsonUnmarshalBuf(tmp.Params, dc.Params)
}

// DaosData entries contain a number of DaosConfig entries and make up
// the DaosData field of a SpdkConfig.
type DaosData struct {
	Configs []*DaosConfig `json:"config"`
}

// SpdkConfig is used to indicate which devices are to be used by SPDK and the
// desired behavior of SPDK subsystems.
type SpdkConfig struct {
	DaosData   *DaosData        `json:"daos_data"`
	Subsystems []*SpdkSubsystem `json:"subsystems"`
}

func defaultSpdkConfig() *SpdkConfig {
	bdevSubsystemConfigs := []*SpdkSubsystemConfig{
		{
			Method: storage.ConfBdevSetOptions,
			Params: &SetOptionsParams{
				BdevIoPoolSize:  humanize.KiByte * 64,
				BdevIoCacheSize: 256,
			},
		},
		{
			Method: storage.ConfBdevNvmeSetOptions,
			Params: &NvmeSetOptionsParams{
				TransportRetryCount:      4,
				NvmeAdminqPollPeriodUsec: 100 * 1000,
				ActionOnTimeout:          "none",
			},
		},
	}

	subsystems := []*SpdkSubsystem{
		{
			Name:    "bdev",
			Configs: bdevSubsystemConfigs,
		},
	}

	daosData := &DaosData{
		Configs: make([]*DaosConfig, 0),
	}

	return &SpdkConfig{
		DaosData:   daosData,
		Subsystems: subsystems,
	}
}

type configMethodGetter func(string, string) *SpdkSubsystemConfig

func getNvmeAttachMethod(name, pci string) *SpdkSubsystemConfig {
	return &SpdkSubsystemConfig{
		Method: storage.ConfBdevNvmeAttachController,
		Params: &NvmeAttachControllerParams{
			TransportType:    "PCIe",
			DeviceName:       fmt.Sprintf("Nvme_%s", name),
			TransportAddress: pci,
		},
	}
}

func getAioFileCreateMethod(name, path string) *SpdkSubsystemConfig {
	return &SpdkSubsystemConfig{
		Method: storage.ConfBdevAioCreate,
		Params: &AioCreateParams{
			DeviceName: fmt.Sprintf("AIO_%s", name),
			Filename:   path,
			BlockSize:  aioBlockSize,
		},
	}
}

func getAioKdevCreateMethod(name, path string) *SpdkSubsystemConfig {
	return &SpdkSubsystemConfig{
		Method: storage.ConfBdevAioCreate,
		Params: &AioCreateParams{
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
			// Encode bdev tier info in RPC name field.
			name := fmt.Sprintf("%s_%d_%d_%d", req.Hostname, index, tier.Tier,
				tier.DeviceRoles.OptionBits)
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
				Method: storage.ConfVmdEnable,
				Params: &VmdEnableParams{},
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

// Add hotplug bus-ID range to DAOS config data for use by non-SPDK consumers in
// engine e.g. BIO or VOS.
func hotplugPropSet(req *storage.BdevWriteConfigRequest, data *DaosData) {
	data.Configs = append(data.Configs, &DaosConfig{
		Method: storage.ConfSetHotplugBusidRange,
		Params: &HotplugBusidRangeParams{
			Begin: req.HotplugBusidBegin,
			End:   req.HotplugBusidEnd,
		},
	})
}

// Add acceleration engine properties to DAOS config data if non-native implementation
// has been selected in config file.
func accelPropSet(req *storage.BdevWriteConfigRequest, data *DaosData) {
	props := req.AccelProps
	// Add config if acceleration engine and options have been selected.
	if props.Engine != storage.AccelEngineNone && !props.Options.IsEmpty() {
		data.Configs = append(data.Configs, &DaosConfig{
			Method: storage.ConfSetAccelProps,
			Params: (*AccelPropsParams)(&props),
		})
	}
}

// Add SPDK JSON-RPC server settings to DAOS config data.
func rpcSrvSet(req *storage.BdevWriteConfigRequest, data *DaosData) {
	props := req.SpdkRpcSrvProps
	// Add config if RPC server options have been selected.
	if props.Enable {
		data.Configs = append(data.Configs, &DaosConfig{
			Method: storage.ConfSetSpdkRpcServer,
			Params: (*SpdkRpcServerParams)(&props),
		})
	}
}

// Add NVMe auto-faulty settings to DAOS config data.
func autoFaultySet(req *storage.BdevWriteConfigRequest, data *DaosData) {
	props := req.AutoFaultyProps
	if props.Enable {
		data.Configs = append(data.Configs, &DaosConfig{
			Method: storage.ConfSetAutoFaultyProps,
			Params: (*AutoFaultyParams)(&props),
		})
	}
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

	accelPropSet(req, sc.DaosData)
	rpcSrvSet(req, sc.DaosData)
	autoFaultySet(req, sc.DaosData)
	sc.WithBdevConfigs(log, req)

	// SPDK-3370: Ensure hotplug config appears after attach directives to avoid race when VMD
	// with hotplug is enabled with multiple domains.
	hpParams := &NvmeSetHotplugParams{}
	if req.HotplugEnabled {
		hpParams.Enable = true
		hpParams.PeriodUsec = uint64(hotplugPeriod.Microseconds())
		hotplugPropSet(req, sc.DaosData)
	}
	var found bool
	for _, ss := range sc.Subsystems {
		if ss.Name != "bdev" {
			continue
		}
		found = true
		ss.Configs = append(ss.Configs, &SpdkSubsystemConfig{
			Method: storage.ConfBdevNvmeSetHotplug,
			Params: hpParams,
		})
		break
	}
	if !found {
		return nil, errors.New("no bdev subsystem section found in generated spdk config")
	}

	return sc, nil
}
