//
// (C) Copyright 2019-2023 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"fmt"
	"math"
	"os/user"
	"strconv"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
	"golang.org/x/net/context"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/common/proto"
	"github.com/daos-stack/daos/src/control/common/proto/convert"
	"github.com/daos-stack/daos/src/control/common/proto/ctl"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	msgFormatErr      = "instance %d: failure formatting storage, check RPC response for details"
	msgNvmeFormatSkip = "NVMe format skipped on instance %d as SCM format did not complete"
	// Storage size reserved for storing DAOS metadata stored on SCM device.
	//
	// NOTE This storage size value is larger than the minimal size observed (i.e. 36864B),
	// because some metadata files such as the control plane RDB (i.e. daos_system.db file) does
	// not have fixed size.  Indeed this last one will eventually grow along the life of the
	// DAOS file system.  However, with 16 MiB (i.e. 16777216 Bytes) of storage we should never
	// have out of space issue.  The size of the memory mapped VOS metadata file (i.e. rdb-pool
	// file) is not included.  This last one is configurable by the end user, and thus should be
	// defined at runtime.
	mdDaosScmBytes uint64 = 16 * humanize.MiByte

	// TODO DAOS-12750 : Add meaningful comment
	ext4MdBytes uint64 = humanize.MiByte
)

// newResponseState creates, populates and returns ResponseState.
func newResponseState(inErr error, badStatus ctlpb.ResponseStatus, infoMsg string) *ctlpb.ResponseState {
	rs := new(ctlpb.ResponseState)
	rs.Info = infoMsg

	if inErr != nil {
		rs.Status = badStatus
		rs.Error = inErr.Error()
	}

	return rs
}

// stripNvmeDetails removes all controller details leaving only PCI address and
// NUMA node/socket ID. Useful when scanning only device topology.
func stripNvmeDetails(pbc *ctlpb.NvmeController) {
	pbc.Serial = ""
	pbc.Model = ""
	pbc.FwRev = ""
}

// newScanBdevResp populates protobuf NVMe scan response with controller info
// including health statistics or metadata if requested.
func newScanNvmeResp(req *ctlpb.ScanNvmeReq, inResp *storage.BdevScanResponse, inErr error) (*ctlpb.ScanNvmeResp, error) {
	outResp := new(ctlpb.ScanNvmeResp)
	outResp.State = new(ctlpb.ResponseState)

	if inErr != nil {
		outResp.State = newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_NVME, "")
		return outResp, nil
	}

	pbCtrlrs := make(proto.NvmeControllers, 0, len(inResp.Controllers))
	if err := pbCtrlrs.FromNative(inResp.Controllers); err != nil {
		return nil, err
	}

	// trim unwanted fields so responses can be coalesced from hash map
	for _, pbc := range pbCtrlrs {
		if !req.GetHealth() {
			pbc.HealthStats = nil
		}
		if !req.GetMeta() {
			pbc.SmdDevices = nil
		}
		if req.GetBasic() {
			stripNvmeDetails(pbc)
		}
	}

	outResp.Ctrlrs = pbCtrlrs

	return outResp, nil
}

// scanBdevs updates transient details if health statistics or server metadata
// is requested otherwise just retrieves cached static controller details.
func (c *ControlService) scanBdevs(ctx context.Context, req *ctlpb.ScanNvmeReq, nsps []*ctlpb.ScmNamespace) (*ctlpb.ScanNvmeResp, error) {
	if req == nil {
		return nil, errors.New("nil bdev request")
	}

	var bdevsInCfg bool
	for _, ei := range c.harness.Instances() {
		if ei.GetStorage().HasBlockDevices() {
			bdevsInCfg = true
		}
	}
	if !bdevsInCfg {
		c.log.Debugf("no bdevs in cfg so scan all")
		// return details of all bdevs if none are assigned to engines
		resp, err := c.storage.ScanBdevs(storage.BdevScanRequest{})

		return newScanNvmeResp(req, resp, err)
	}

	c.log.Debugf("bdevs in cfg so scan only assigned")
	resp, err := c.scanAssignedBdevs(ctx, nsps, req.GetHealth() || req.GetMeta())

	return newScanNvmeResp(req, resp, err)
}

// newScanScmResp sets protobuf SCM scan response with module or namespace info.
func newScanScmResp(inResp *storage.ScmScanResponse, inErr error) (*ctlpb.ScanScmResp, error) {
	outResp := new(ctlpb.ScanScmResp)
	outResp.State = new(ctlpb.ResponseState)

	if inErr != nil {
		outResp.State = newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_SCM, "")
		return outResp, nil
	}

	if len(inResp.Namespaces) == 0 {
		outResp.Modules = make(proto.ScmModules, 0, len(inResp.Modules))
		if err := (*proto.ScmModules)(&outResp.Modules).FromNative(inResp.Modules); err != nil {
			return nil, err
		}

		return outResp, nil
	}

	outResp.Namespaces = make(proto.ScmNamespaces, 0, len(inResp.Namespaces))
	if err := (*proto.ScmNamespaces)(&outResp.Namespaces).FromNative(inResp.Namespaces); err != nil {
		return nil, err
	}

	return outResp, nil
}

// scanScm will return mount details and usage for either emulated RAM or real PMem.
func (c *ControlService) scanScm(ctx context.Context, req *ctlpb.ScanScmReq) (*ctlpb.ScanScmResp, error) {
	if req == nil {
		return nil, errors.New("nil scm request")
	}

	ssr, scanErr := c.ScmScan(storage.ScmScanRequest{})

	if scanErr != nil || !req.GetUsage() {
		return newScanScmResp(ssr, scanErr)
	}

	return newScanScmResp(c.getScmUsage(ssr))
}

func (c *ControlService) getEngineCfgFromNvmeCtl(nc *ctl.NvmeController) (*engine.Config, error) {
	var engineCfg *engine.Config
	pciAddr := nc.PciAddr
	for index := range c.srvCfg.Engines {
		if engineCfg != nil {
			break
		}

		for _, tierCfg := range c.srvCfg.Engines[index].Storage.Tiers {
			if engineCfg != nil {
				break
			}

			if !tierCfg.IsBdev() {
				continue
			}

			for _, devName := range tierCfg.Bdev.DeviceList.Devices() {
				if devName == pciAddr {
					engineCfg = c.srvCfg.Engines[index]
					break
				}

			}
		}
	}

	if engineCfg == nil {
		return nil, errors.Errorf("unknown PCI device %s", pciAddr)
	}

	return engineCfg, nil
}

func (c *ControlService) getEngineCfgFromNsp(nsp *ctl.ScmNamespace) (*engine.Config, error) {
	var engineCfg *engine.Config
	mountPoint := nsp.GetMount().Path
	for index := range c.srvCfg.Engines {
		if engineCfg != nil {
			break
		}

		for _, tierCfg := range c.srvCfg.Engines[index].Storage.Tiers {
			if tierCfg.IsSCM() && tierCfg.Scm.MountPoint == mountPoint {
				engineCfg = c.srvCfg.Engines[index]
				break
			}
		}
	}

	if engineCfg == nil {
		return nil, errors.Errorf("unknown SCM mount point %s", mountPoint)
	}

	return engineCfg, nil
}

// return the size of the RDB file used for managing SCM metadata
func (c *ControlService) getRdbSize(engineCfg *engine.Config) (uint64, error) {
	mdCapStr, err := engineCfg.GetEnvVar(daos.DaosMdCapEnv)
	if err != nil {
		c.log.Debugf("using default RDB file size with engine %d: %s (%d Bytes)",
			engineCfg.Index, humanize.Bytes(daos.DefaultDaosMdCapSize), daos.DefaultDaosMdCapSize)
		return uint64(daos.DefaultDaosMdCapSize), nil
	}

	rdbSize, err := strconv.ParseUint(mdCapStr, 10, 64)
	if err != nil {
		return 0, errors.Errorf("invalid RDB file size: %q does not define a plain int",
			mdCapStr)
	}
	rdbSize = rdbSize << 20
	c.log.Debugf("using custom RDB size with engine %d: %s (%d Bytes)",
		engineCfg.Index, humanize.Bytes(rdbSize), rdbSize)

	return rdbSize, nil
}

// Adjust the NVME available size to its real usable size.
func (c *ControlService) adjustNvmeSize(resp *ctlpb.ScanNvmeResp) {
	type deviceSizeStat struct {
		clusterPerTarget uint64 // Number of usable SPDK clusters for each target
		devices          []*ctl.SmdDevice
	}

	devicesToAdjust := make(map[uint32]*deviceSizeStat, 0)
	for _, ctlr := range resp.GetCtrlrs() {
		engineCfg, err := c.getEngineCfgFromNvmeCtl(ctlr)
		if err != nil {
			c.log.Noticef("Skipping NVME controller %s: %s",
				ctlr.GetPciAddr(), err.Error())
			continue
		}

		for _, dev := range ctlr.GetSmdDevices() {
			rank := dev.GetRank()

			if dev.GetDevState() != ctlpb.NvmeDevState_NORMAL {
				c.log.Debugf("SMD device %s (rank %d, ctlr %s) not usable: device state %q",
					dev.GetUuid(), rank, ctlr.GetPciAddr(), ctlpb.NvmeDevState_name[int32(dev.DevState)])
				dev.AvailBytes = 0
				dev.UsableBytes = 0
				continue
			}

			if dev.GetClusterSize() == 0 || len(dev.GetTgtIds()) == 0 {
				c.log.Noticef("SMD device %s (rank %d,  ctlr %s) not usable: missing storage info",
					dev.GetUuid(), rank, ctlr.GetPciAddr())
				continue
			}

			if dev.GetRoleBits() != 0 && (dev.GetRoleBits()&storage.BdevRoleData) == 0 {
				c.log.Debugf("SMD device %s (rank %d, ctrlr %s) not used to store data (Role bits 0x%X)",
					dev.GetUuid(), rank, dev.GetTrAddr(), dev.GetRoleBits())
				dev.AvailBytes = 0
				dev.UsableBytes = 0
				continue
			}

			c.log.Debugf("Initial availabe size of SMD device %s (rank %d, ctrlr %s): %s (%d bytes)",
				dev.GetUuid(), rank, dev.GetTrAddr(), humanize.Bytes(dev.GetAvailBytes()), dev.GetAvailBytes())

			clusterSize := uint64(dev.GetClusterSize())
			availBytes := (dev.GetAvailBytes() / clusterSize) * clusterSize
			if dev.GetAvailBytes() != availBytes {
				c.log.Debugf("Adjusting available size of SMD device %s (rank %d, ctrlr %s): from %s (%d Bytes) to %s (%d bytes)",
					dev.GetUuid(), rank, dev.GetTrAddr(),
					humanize.Bytes(dev.GetAvailBytes()), dev.GetAvailBytes(),
					humanize.Bytes(availBytes), availBytes)
				dev.AvailBytes = availBytes
			}

			dataClusterCount := dev.GetAvailBytes() / clusterSize
			if dev.GetRoleBits() == 0 {
				if devicesToAdjust[rank] == nil {
					devicesToAdjust[rank] = &deviceSizeStat{
						clusterPerTarget: math.MaxUint64,
					}
				}
				devicesToAdjust[rank].devices = append(devicesToAdjust[rank].devices, dev)
				targetCount := uint64(len(dev.GetTgtIds()))
				clusterPerTarget := dataClusterCount / targetCount
				if clusterPerTarget < devicesToAdjust[rank].clusterPerTarget {
					devicesToAdjust[rank].clusterPerTarget = clusterPerTarget
				}
				c.log.Debugf("No meta-data stored on SMD device %s (rank %d, ctrlr %s): dataClusterCount=%d, clusterPerTarget=%d",
					dev.GetUuid(), rank, dev.GetTrAddr(), dataClusterCount, clusterPerTarget)
				continue
			}

			engineTargetNb := uint64(engineCfg.TargetCount)
			if (dev.GetRoleBits() & storage.BdevRoleMeta) != 0 {
				metadataClusterCount := dev.GetMetaSize() / clusterSize
				if dev.GetMetaSize()%clusterSize != 0 {
					metadataClusterCount += 1
				}
				metadataClusterCount *= engineTargetNb
				c.log.Debugf("Removing %d Metadata clusters from the usable size of the SMD device %s (rank %d, ctrlr %s): ",
					metadataClusterCount, dev.GetUuid(), rank, dev.GetTrAddr())
				if metadataClusterCount >= dataClusterCount {
					c.log.Debugf("No more usable space in SMD device %s (rank %d, ctrlr %s)",
						dev.GetUuid(), rank, dev.GetTrAddr())
					dev.UsableBytes = 0
					continue
				}
				dataClusterCount -= metadataClusterCount
			}

			if (dev.GetRoleBits() & storage.BdevRoleWAL) != 0 {
				metaWalClusterCount := dev.GetMetaWalSize() / clusterSize
				if dev.GetMetaWalSize()%clusterSize != 0 {
					metaWalClusterCount += 1
				}
				metaWalClusterCount *= engineTargetNb
				c.log.Debugf("Removing %d Metadata WAL clusters from the usable size of the SMD device %s (rank %d, ctrlr %s): ",
					metaWalClusterCount, dev.GetUuid(), rank, dev.GetTrAddr())
				if metaWalClusterCount >= dataClusterCount {
					c.log.Debugf("No more usable space in SMD device %s (rank %d, ctrlr %s)",
						dev.GetUuid(), rank, dev.GetTrAddr())
					dev.UsableBytes = 0
					continue
				}
				dataClusterCount -= metaWalClusterCount
			}

			rdbSize := dev.GetRdbSize()
			if rdbSize > 0 && (dev.GetRoleBits()&storage.BdevRoleMeta) != 0 {
				rdbClusterCount := rdbSize / clusterSize
				if rdbSize%clusterSize != 0 {
					rdbClusterCount += 1
				}
				c.log.Debugf("Removing %d RDB clusters the usable size of the SMD device %s (rank %d, ctrlr %s)",
					rdbClusterCount, dev.GetUuid(), rank, dev.GetTrAddr())
				if rdbClusterCount >= dataClusterCount {
					c.log.Debugf("No more usable space in SMD device %s (rank %d, ctrlr %s)",
						dev.GetUuid(), rank, dev.GetTrAddr())
					dev.UsableBytes = 0
					continue
				}
				dataClusterCount -= rdbClusterCount
			}

			if rdbSize > 0 && (dev.GetRoleBits()&storage.BdevRoleWAL) != 0 {
				rdbWalClusterCount := dev.GetRdbWalSize() / clusterSize
				if dev.GetRdbWalSize()%clusterSize != 0 {
					rdbWalClusterCount += 1
				}
				c.log.Debugf("Removing %d RDB WAL clusters from the usable size of the SMD device %s (rank %d, ctrlr %s)",
					rdbWalClusterCount, dev.GetUuid(), rank, dev.GetTrAddr())
				if rdbWalClusterCount >= dataClusterCount {
					c.log.Debugf("No more usable space in SMD device %s (rank %d, ctrlr %s)",
						dev.GetUuid(), rank, dev.GetTrAddr())
					dev.UsableBytes = 0
					continue
				}
				dataClusterCount -= rdbWalClusterCount
			}

			if devicesToAdjust[rank] == nil {
				devicesToAdjust[rank] = &deviceSizeStat{
					clusterPerTarget: math.MaxUint64,
				}
			}
			devicesToAdjust[rank].devices = append(devicesToAdjust[rank].devices, dev)
			targetCount := uint64(len(dev.GetTgtIds()))
			clusterPerTarget := dataClusterCount / targetCount
			c.log.Debugf("Meta-data stored on SMD device %s (rank %d, ctrlr %s): dataClusterCount=%d, clusterPerTarget=%d",
				dev.GetUuid(), rank, dev.GetTrAddr(), dataClusterCount, clusterPerTarget)
			if clusterPerTarget < devicesToAdjust[rank].clusterPerTarget {
				devicesToAdjust[rank].clusterPerTarget = clusterPerTarget
			}
		}
	}

	for rank, item := range devicesToAdjust {
		for _, dev := range item.devices {
			targetCount := uint64(len(dev.GetTgtIds()))
			dev.UsableBytes = targetCount * item.clusterPerTarget * dev.GetClusterSize()
			c.log.Debugf("Defining usable size of the SMD device %s (rank %d, ctrlr %s) to %s (%d bytes)",
				dev.GetUuid(), rank, dev.GetTrAddr(),
				humanize.Bytes(dev.GetUsableBytes()), dev.GetUsableBytes())
		}
	}
}

// Adjust the SCM available size to the real usable size.
func (c *ControlService) adjustScmSize(resp *ctlpb.ScanScmResp) {
	for _, scmNamespace := range resp.GetNamespaces() {
		m := scmNamespace.GetMount()
		mountPath := m.GetPath()
		m.UsableBytes = m.GetAvailBytes()
		c.log.Debugf("Initial usable size of SCM %s: %s (%d bytes)", mountPath,
			humanize.Bytes(m.GetUsableBytes()), m.GetUsableBytes())

		engineCfg, err := c.getEngineCfgFromNsp(scmNamespace)
		if err != nil {
			c.log.Noticef("Adjusting usable size to 0 Bytes of SCM device %q: %s",
				mountPath, err.Error())
			m.UsableBytes = 0
			continue
		}

		mdBytes, err := c.getRdbSize(engineCfg)
		if err != nil {
			c.log.Noticef("Adjusting usable size to 0 Bytes of SCM device %q: %s",
				mountPath, err.Error())
			m.UsableBytes = 0
			continue
		}
		c.log.Debugf("Removing RDB (%s, %d bytes) from the usable size of the SCM device %q",
			humanize.Bytes(mdBytes), mdBytes, mountPath)
		if mdBytes >= m.GetUsableBytes() {
			c.log.Debugf("No more usable space in SCM device %s", mountPath)
			m.UsableBytes = 0
			continue
		}
		m.UsableBytes -= mdBytes

		if engineCfg.Storage.ControlMetadata.HasPath() {
			cmdPath := engineCfg.Storage.ControlMetadata.Path
			hasPrefix, err := common.HasPrefixPath(mountPath, cmdPath)
			if err != nil {
				c.log.Noticef("Adjusting usable size to 0 Bytes of SCM device %q: %s",
					mountPath, err.Error())
				m.UsableBytes = 0
				continue
			}
			if hasPrefix {
				c.log.Debugf("Removing control plane metadata (%s, %d bytes) from the usable size of the SCM device %q",
					humanize.Bytes(mdDaosScmBytes), mdDaosScmBytes, mountPath)
				if mdDaosScmBytes >= m.GetUsableBytes() {
					c.log.Debugf("No more usable space in SCM device %s", mountPath)
					m.UsableBytes = 0
					continue
				}
				m.UsableBytes -= mdDaosScmBytes
			}
		}

		c.log.Debugf("Removing Ext4 fs metadata (%s, %d bytes) from the usable size of the SCM device %q",
			humanize.Bytes(ext4MdBytes), ext4MdBytes, mountPath)
		m.UsableBytes -= ext4MdBytes

		usableBytes := scmNamespace.Mount.GetUsableBytes()
		c.log.Debugf("Usable size of SCM device %q: %s (%d bytes)",
			scmNamespace.Mount.GetPath(), humanize.Bytes(usableBytes), usableBytes)
	}
}

// StorageScan discovers non-volatile storage hardware on node.
func (c *ControlService) StorageScan(ctx context.Context, req *ctlpb.StorageScanReq) (*ctlpb.StorageScanResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	resp := new(ctlpb.StorageScanResp)

	respScm, err := c.scanScm(ctx, req.Scm)
	if err != nil {
		return nil, err
	}
	if req.Scm.GetUsage() {
		c.adjustScmSize(respScm)
	}
	resp.Scm = respScm

	respNvme, err := c.scanBdevs(ctx, req.Nvme, respScm.Namespaces)
	if err != nil {
		return nil, err
	}
	if req.Nvme.GetMeta() {
		c.adjustNvmeSize(respNvme)
	}
	resp.Nvme = respNvme

	mi, err := c.getMemInfo()
	if err != nil {
		return nil, err
	}
	if err := convert.Types(mi, &resp.MemInfo); err != nil {
		return nil, err
	}

	return resp, nil
}

// StorageFormat delegates to Storage implementation's Format methods to prepare
// storage for use by DAOS data plane.
//
// Errors returned will stop other servers from formatting, non-fatal errors
// specific to particular device should be reported within resp results instead.
//
// Send response containing multiple results of format operations on scm mounts
// and nvme controllers.
func (c *ControlService) StorageFormat(ctx context.Context, req *ctlpb.StorageFormatReq) (*ctlpb.StorageFormatResp, error) {
	instances := c.harness.Instances()
	resp := new(ctlpb.StorageFormatResp)
	resp.Mrets = make([]*ctlpb.ScmMountResult, 0, len(instances))
	resp.Crets = make([]*ctlpb.NvmeControllerResult, 0, len(instances))
	scmChan := make(chan *ctlpb.ScmMountResult, len(instances))
	mdFormatted := false

	// Format control metadata first, if needed
	if needs, err := c.storage.ControlMetadataNeedsFormat(); err != nil {
		return nil, errors.Wrap(err, "detecting if metadata format is needed")
	} else if needs || req.Reformat {
		engineIdxs := make([]uint, len(instances))
		for i, eng := range instances {
			engineIdxs[i] = uint(eng.Index())
		}

		c.log.Debug("formatting control metadata storage")
		if err := c.storage.FormatControlMetadata(engineIdxs); err != nil {
			return nil, errors.Wrap(err, "formatting control metadata storage")
		}
		mdFormatted = true
	} else {
		c.log.Debug("no control metadata format needed")
	}

	instanceErrored := make(map[uint32]string)
	instanceSkipped := make(map[uint32]bool)
	// TODO: enable per-instance formatting
	formatting := 0
	for _, ei := range instances {
		if needs, err := ei.GetStorage().ScmNeedsFormat(); err != nil {
			return nil, errors.Wrap(err, "detecting if SCM format is needed")
		} else if needs || req.Reformat {
			formatting++
			go func(e Engine) {
				scmChan <- e.StorageFormatSCM(ctx, req.Reformat)
			}(ei)
		} else {
			var mountpoint string
			if scmConfig, err := ei.GetStorage().GetScmConfig(); err != nil {
				c.log.Debugf("unable to get mountpoint: %v", err)
			} else {
				mountpoint = scmConfig.Scm.MountPoint
			}

			resp.Mrets = append(resp.Mrets, &ctlpb.ScmMountResult{
				Instanceidx: ei.Index(),
				Mntpoint:    mountpoint,
				State: &ctlpb.ResponseState{
					Info: "SCM is already formatted",
				},
			})

			instanceSkipped[ei.Index()] = true
		}
	}

	for formatting > 0 {
		select {
		case <-ctx.Done():
			return nil, ctx.Err()
		case scmResult := <-scmChan:
			formatting--
			state := scmResult.GetState()
			if state.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
				instanceErrored[scmResult.GetInstanceidx()] = state.GetError()
			}
			resp.Mrets = append(resp.Mrets, scmResult)
		}
	}

	// allow format to complete on one instance even if another fail
	// TODO: perform bdev format in parallel
	for _, ei := range instances {
		_, hasError := instanceErrored[ei.Index()]
		_, skipped := instanceSkipped[ei.Index()]
		if hasError || (skipped && !mdFormatted) {
			// if scm errored or was already formatted, indicate skipping bdev format
			ret := ei.newCret(storage.NilBdevAddress, nil)
			ret.State.Info = fmt.Sprintf(msgNvmeFormatSkip, ei.Index())
			resp.Crets = append(resp.Crets, ret)
			continue
		}

		// SCM formatted correctly on this instance, format NVMe
		cResults := ei.StorageFormatNVMe()
		if cResults.HasErrors() {
			instanceErrored[ei.Index()] = cResults.Errors()
			resp.Crets = append(resp.Crets, cResults...)
			continue
		}

		if err := ei.GetStorage().WriteNvmeConfig(ctx, c.log); err != nil {
			instanceErrored[ei.Index()] = err.Error()
			cResults = append(cResults, ei.newCret("", err))
		}

		resp.Crets = append(resp.Crets, cResults...)
	}

	// Notify storage ready for instances formatted without error.
	// Block until all instances have formatted NVMe to avoid
	// VFIO device or resource busy when starting I/O Engines
	// because devices have already been claimed during format.
	for _, ei := range instances {
		if msg, hasError := instanceErrored[ei.Index()]; hasError {
			c.log.Errorf("instance %d: %s", ei.Index(), msg)
			continue
		}
		ei.NotifyStorageReady()
	}

	return resp, nil
}

// StorageNvmeRebind rebinds SSD from kernel and binds to user-space to allow DAOS to use it.
func (c *ControlService) StorageNvmeRebind(ctx context.Context, req *ctlpb.NvmeRebindReq) (*ctlpb.NvmeRebindResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	cu, err := user.Current()
	if err != nil {
		return nil, errors.Wrap(err, "get username")
	}

	prepReq := storage.BdevPrepareRequest{
		// zero as hugepages already allocated on start-up
		HugepageCount: 0,
		TargetUser:    cu.Username,
		PCIAllowList:  req.PciAddr,
		Reset_:        false,
	}

	resp := new(ctlpb.NvmeRebindResp)
	if _, err := c.NvmePrepare(prepReq); err != nil {
		err = errors.Wrap(err, "nvme rebind")
		c.log.Error(err.Error())

		resp.State = &ctlpb.ResponseState{
			Error:  err.Error(),
			Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
		}

		return resp, nil // report prepare call result in response
	}

	return resp, nil
}

// StorageNvmeAddDevice adds a newly added SSD to a DAOS engine's NVMe config to allow it to be used.
//
// If StorageTierIndex is set to -1 in request, add the device to the first configured bdev tier.
func (c *ControlService) StorageNvmeAddDevice(ctx context.Context, req *ctlpb.NvmeAddDeviceReq) (resp *ctlpb.NvmeAddDeviceResp, err error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	engines := c.harness.Instances()
	engineIndex := req.GetEngineIndex()

	if len(engines) <= int(engineIndex) {
		return nil, errors.Errorf("engine with index %d not found", engineIndex)
	}
	defer func() {
		err = errors.Wrapf(err, "engine %d", engineIndex)
	}()

	var tierCfg *storage.TierConfig
	engineStorage := engines[engineIndex].GetStorage()
	tierIndex := req.GetStorageTierIndex()

	for _, tier := range engineStorage.GetBdevConfigs() {
		if tierIndex == -1 || int(tierIndex) == tier.Tier {
			tierCfg = tier
			break
		}
	}

	if tierCfg == nil {
		if tierIndex == -1 {
			return nil, errors.New("no bdev storage tiers in config")
		}
		return nil, errors.Errorf("bdev storage tier with index %d not found in config",
			tierIndex)
	}

	c.log.Debugf("bdev list to be updated: %+v", tierCfg.Bdev.DeviceList)
	if err := tierCfg.Bdev.DeviceList.AddStrings(req.PciAddr); err != nil {
		return nil, errors.Errorf("updating bdev list for tier %d", tierIndex)
	}
	c.log.Debugf("updated bdev list: %+v", tierCfg.Bdev.DeviceList)

	resp = new(ctlpb.NvmeAddDeviceResp)
	if err := engineStorage.WriteNvmeConfig(ctx, c.log); err != nil {
		err = errors.Wrapf(err, "write nvme config for engine %d", engineIndex)
		c.log.Error(err.Error())

		// report write conf call result in response
		resp.State = &ctlpb.ResponseState{
			Error:  err.Error(),
			Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
		}
	}

	return resp, nil
}
