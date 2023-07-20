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
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/logging"
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

	// NOTE DAOS-12750 Define an arbitrary storage space reserved of the filesystem used for
	// mounting an SCM device: ext4 for DCPM and tmpfs for RAM.
	mdFsScmBytes uint64 = humanize.MiByte
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

// Returns the engine configuration managing the given NVMe controller
func (c *ControlService) getEngineCfgFromNvmeCtl(nc *ctl.NvmeController) (*engine.Config, error) {
	var engineCfg *engine.Config

	pciAddr, err := hardware.NewPCIAddress(nc.GetPciAddr())
	if err != nil {
		return nil, errors.Errorf("Invalid PCI address: %s", err)
	}
	if pciAddr.IsVMDBackingAddress() {
		if pciAddr, err = pciAddr.BackingToVMDAddress(); err != nil {
			return nil, errors.Errorf("Invalid VMD address: %s", err)
		}
	}
	ctlrAddr := pciAddr.String()

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
				if devName == ctlrAddr {
					engineCfg = c.srvCfg.Engines[index]
					break
				}

			}
		}
	}

	if engineCfg == nil {
		return nil, errors.Errorf("unknown PCI device %q", pciAddr)
	}

	return engineCfg, nil
}

// Returns the engine configuration managing the given SCM name-space
func (c *ControlService) getEngineCfgFromScmNsp(nsp *ctl.ScmNamespace) (*engine.Config, error) {
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

type deviceToAdjust struct {
	ctlr *ctl.NvmeController
	idx  int
	rank uint32
}

type deviceSizeStat struct {
	clusterPerTarget uint64 // Number of usable SPDK clusters for each target
	devs             []*deviceToAdjust
}

// Add a device to the input map of device to which the usable size have to be adjusted
func (c *ControlService) addDeviceToAdjust(devsStat map[uint32]*deviceSizeStat, devToAdjust *deviceToAdjust, dataClusterCount uint64) {
	dev := devToAdjust.ctlr.GetSmdDevices()[devToAdjust.idx]
	if devsStat[devToAdjust.rank] == nil {
		devsStat[devToAdjust.rank] = &deviceSizeStat{
			clusterPerTarget: math.MaxUint64,
		}
	}
	devsStat[devToAdjust.rank].devs = append(devsStat[devToAdjust.rank].devs, devToAdjust)
	targetCount := uint64(len(dev.GetTgtIds()))
	clusterPerTarget := dataClusterCount / targetCount
	c.log.Tracef("SMD device %s (rank %d, ctlr %s) added to the list of device to adjust",
		dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
	if clusterPerTarget < devsStat[devToAdjust.rank].clusterPerTarget {
		c.log.Tracef("Updating number of clusters per target of rank %d: old=%d new=%d",
			devToAdjust.rank, devsStat[devToAdjust.rank].clusterPerTarget, clusterPerTarget)
		devsStat[devToAdjust.rank].clusterPerTarget = clusterPerTarget
	}
}

// For a given size in bytes, returns the total number of SPDK clusters needed for a given number of targets
func getClusterCount(sizeBytes uint64, targetNb uint64, clusterSize uint64) uint64 {
	clusterCount := sizeBytes / clusterSize
	if sizeBytes%clusterSize != 0 {
		clusterCount += 1
	}
	return clusterCount * targetNb
}

func (c *ControlService) getMetaClusterCount(engineCfg *engine.Config, devToAdjust deviceToAdjust) (subtrClusterCount uint64) {
	dev := devToAdjust.ctlr.GetSmdDevices()[devToAdjust.idx]
	clusterSize := uint64(dev.GetClusterSize())
	engineTargetNb := uint64(engineCfg.TargetCount)

	if dev.GetRoleBits()&storage.BdevRoleMeta != 0 {
		clusterCount := getClusterCount(dev.GetMetaSize(), engineTargetNb, clusterSize)
		c.log.Tracef("Removing %d Metadata clusters from the usable size of the SMD device %s (rank %d, ctlr %s): ",
			clusterCount, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	if dev.GetRoleBits()&storage.BdevRoleWAL != 0 {
		clusterCount := getClusterCount(dev.GetMetaWalSize(), engineTargetNb, clusterSize)
		c.log.Tracef("Removing %d Metadata WAL clusters from the usable size of the SMD device %s (rank %d, ctlr %s): ",
			clusterCount, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	if dev.GetRdbSize() == 0 {
		return
	}

	if dev.GetRoleBits()&storage.BdevRoleMeta != 0 {
		clusterCount := getClusterCount(dev.GetRdbSize(), 1, clusterSize)
		c.log.Tracef("Removing %d RDB clusters the usable size of the SMD device %s (rank %d, ctlr %s)",
			clusterCount, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	if dev.GetRoleBits()&storage.BdevRoleWAL != 0 {
		clusterCount := getClusterCount(dev.GetRdbWalSize(), 1, clusterSize)
		c.log.Tracef("Removing %d RDB WAL clusters from the usable size of the SMD device %s (rank %d, ctlr %s)",
			clusterCount, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	return
}

// Adjust the NVME available size to its real usable size.
func (c *ControlService) adjustNvmeSize(resp *ctlpb.ScanNvmeResp) {
	devsStat := make(map[uint32]*deviceSizeStat, 0)
	for _, ctlr := range resp.GetCtrlrs() {
		engineCfg, err := c.getEngineCfgFromNvmeCtl(ctlr)
		if err != nil {
			c.log.Noticef("Skipping NVME controller %s: %s", ctlr.GetPciAddr(), err.Error())
			continue
		}

		for idx, dev := range ctlr.GetSmdDevices() {
			rank := dev.GetRank()

			if dev.GetRoleBits() != 0 && (dev.GetRoleBits()&storage.BdevRoleData) == 0 {
				c.log.Debugf("SMD device %s (rank %d, ctlr %s) not used to store data (Role bits 0x%X)",
					dev.GetUuid(), rank, ctlr.GetPciAddr(), dev.GetRoleBits())
				dev.TotalBytes = 0
				dev.AvailBytes = 0
				dev.UsableBytes = 0
				continue
			}

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
				dev.AvailBytes = 0
				dev.UsableBytes = 0
				continue
			}

			c.log.Tracef("Initial available size of SMD device %s (rank %d, ctlr %s): %s (%d bytes)",
				dev.GetUuid(), rank, ctlr.GetPciAddr(), humanize.Bytes(dev.GetAvailBytes()), dev.GetAvailBytes())

			clusterSize := uint64(dev.GetClusterSize())
			availBytes := (dev.GetAvailBytes() / clusterSize) * clusterSize
			if dev.GetAvailBytes() != availBytes {
				c.log.Tracef("Adjusting available size of SMD device %s (rank %d, ctlr %s): from %s (%d Bytes) to %s (%d bytes)",
					dev.GetUuid(), rank, ctlr.GetPciAddr(),
					humanize.Bytes(dev.GetAvailBytes()), dev.GetAvailBytes(),
					humanize.Bytes(availBytes), availBytes)
				dev.AvailBytes = availBytes
			}

			devToAdjust := deviceToAdjust{
				ctlr: ctlr,
				idx:  idx,
				rank: rank,
			}
			dataClusterCount := dev.GetAvailBytes() / clusterSize
			if dev.GetRoleBits() == 0 {
				c.log.Tracef("No meta-data stored on SMD device %s (rank %d, ctlr %s)",
					dev.GetUuid(), rank, ctlr.GetPciAddr())
				c.addDeviceToAdjust(devsStat, &devToAdjust, dataClusterCount)
				continue
			}

			subtrClusterCount := c.getMetaClusterCount(engineCfg, devToAdjust)
			if subtrClusterCount >= dataClusterCount {
				c.log.Debugf("No more usable space in SMD device %s (rank %d, ctlr %s)",
					dev.GetUuid(), rank, ctlr.GetPciAddr())
				dev.UsableBytes = 0
				continue
			}
			dataClusterCount -= subtrClusterCount
			c.addDeviceToAdjust(devsStat, &devToAdjust, dataClusterCount)
		}
	}

	for rank, item := range devsStat {
		for _, dev := range item.devs {
			smdDev := dev.ctlr.GetSmdDevices()[dev.idx]
			targetCount := uint64(len(smdDev.GetTgtIds()))
			smdDev.UsableBytes = targetCount * item.clusterPerTarget * smdDev.GetClusterSize()
			c.log.Debugf("Defining usable size of the SMD device %s (rank %d, ctlr %s) to %s (%d bytes)",
				smdDev.GetUuid(), rank, dev.ctlr.GetPciAddr(),
				humanize.Bytes(smdDev.GetUsableBytes()), smdDev.GetUsableBytes())
		}
	}
}

// Adjust the SCM available size to the real usable size.
func (c *ControlService) adjustScmSize(resp *ctlpb.ScanScmResp) {
	for _, scmNamespace := range resp.GetNamespaces() {
		mnt := scmNamespace.GetMount()
		mountPath := mnt.GetPath()
		mnt.UsableBytes = mnt.GetAvailBytes()
		c.log.Debugf("Initial usable size of SCM %s: %s (%d bytes)", mountPath,
			humanize.Bytes(mnt.GetUsableBytes()), mnt.GetUsableBytes())

		engineCfg, err := c.getEngineCfgFromScmNsp(scmNamespace)
		if err != nil {
			c.log.Noticef("Adjusting usable size to 0 Bytes of SCM device %q: %s",
				mountPath, err.Error())
			mnt.UsableBytes = 0
			continue
		}

		mdBytes, err := c.getRdbSize(engineCfg)
		if err != nil {
			c.log.Noticef("Adjusting usable size to 0 Bytes of SCM device %q: %s",
				mountPath, err.Error())
			mnt.UsableBytes = 0
			continue
		}
		c.log.Tracef("Removing RDB (%s, %d bytes) from the usable size of the SCM device %q",
			humanize.Bytes(mdBytes), mdBytes, mountPath)
		if mdBytes >= mnt.GetUsableBytes() {
			c.log.Debugf("No more usable space in SCM device %s", mountPath)
			mnt.UsableBytes = 0
			continue
		}
		mnt.UsableBytes -= mdBytes

		removeControlPlaneMetadata := func(m *ctl.ScmNamespace_Mount) {
			mountPath := m.GetPath()

			c.log.Tracef("Removing control plane metadata (%s, %d bytes) from the usable size of the SCM device %q",
				humanize.Bytes(mdDaosScmBytes), mdDaosScmBytes, mountPath)
			if mdDaosScmBytes >= m.GetUsableBytes() {
				c.log.Debugf("No more usable space in SCM device %s", mountPath)
				m.UsableBytes = 0
				return
			}
			m.UsableBytes -= mdDaosScmBytes
		}
		if !engineCfg.Storage.Tiers.HasBdevRoleMeta() {
			removeControlPlaneMetadata(mnt)
		} else {
			if !engineCfg.Storage.ControlMetadata.HasPath() {
				c.log.Noticef("Adjusting usable size to 0 Bytes of SCM device %q: %s",
					mountPath,
					"MD on SSD feature enabled without path for Control Metadata")
				mnt.UsableBytes = 0
				continue
			}

			cmdPath := engineCfg.Storage.ControlMetadata.Path
			if hasPrefix, err := common.HasPrefixPath(mountPath, cmdPath); hasPrefix || err != nil {
				if err != nil {
					c.log.Noticef("Invalid SCM mount path or Control Metadata path: %q", err.Error())
				}
				if hasPrefix {
					removeControlPlaneMetadata(mnt)
				}
			}
		}

		c.log.Tracef("Removing (%s, %d bytes) of usable size from the SCM device %q: space used by the file system metadata",
			humanize.Bytes(mdFsScmBytes), mdFsScmBytes, mountPath)
		mnt.UsableBytes -= mdFsScmBytes

		usableBytes := scmNamespace.Mount.GetUsableBytes()
		c.log.Debugf("Usable size of SCM device %q: %s (%d bytes)",
			scmNamespace.Mount.GetPath(), humanize.Bytes(usableBytes), usableBytes)
	}
}

func checkEnginesReady(instances []Engine) error {
	for _, inst := range instances {
		if !inst.IsReady() {
			var err error = FaultDataPlaneNotStarted
			if inst.IsStarted() {
				err = errEngineNotReady
			}

			return errors.Wrapf(err, "instance %d", inst.Index())
		}
	}

	return nil
}

// StorageScan discovers non-volatile storage hardware on node.
func (c *ControlService) StorageScan(ctx context.Context, req *ctlpb.StorageScanReq) (*ctlpb.StorageScanResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}
	resp := new(ctlpb.StorageScanResp)

	// In the case that usage stats are being requested, relevant flags for both SCM and NVMe
	// will be set and so fail if engines are not ready for comms. This restriction should not
	// be applied if only the Meta flag is set in the NVMe component of the request to continue
	// to support off-line storage scan functionality which uses cached stats (e.g. dmg storage
	// scan --nvme-meta).
	if req.Scm.Usage && req.Nvme.Meta {
		if err := checkEnginesReady(c.harness.Instances()); err != nil {
			return nil, err
		}
	}

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

func (c *ControlService) formatMetadata(instances []Engine, reformat bool) (bool, error) {
	// Format control metadata first, if needed
	if needs, err := c.storage.ControlMetadataNeedsFormat(); err != nil {
		return false, errors.Wrap(err, "detecting if metadata format is needed")
	} else if needs || reformat {
		engineIdxs := make([]uint, len(instances))
		for i, eng := range instances {
			engineIdxs[i] = uint(eng.Index())
		}

		c.log.Debug("formatting control metadata storage")
		if err := c.storage.FormatControlMetadata(engineIdxs); err != nil {
			return false, errors.Wrap(err, "formatting control metadata storage")
		}

		return true, nil
	}

	c.log.Debug("no control metadata format needed")
	return false, nil
}

func checkTmpfsMem(log logging.Logger, scmCfgs map[int]*storage.TierConfig, getMemInfo func() (*common.MemInfo, error)) error {
	if scmCfgs[0].Class != storage.ClassRam {
		return nil
	}

	var memRamdisks uint64
	for _, sc := range scmCfgs {
		memRamdisks += uint64(sc.Scm.RamdiskSize) * humanize.GiByte
	}

	mi, err := getMemInfo()
	if err != nil {
		return errors.Wrap(err, "retrieving system meminfo")
	}
	memAvail := uint64(mi.MemAvailableKiB) * humanize.KiByte

	if err := checkMemForRamdisk(log, memRamdisks, memAvail); err != nil {
		return errors.Wrap(err, "check ram available for all tmpfs")
	}

	return nil
}

type formatScmReq struct {
	log        logging.Logger
	reformat   bool
	instances  []Engine
	getMemInfo func() (*common.MemInfo, error)
}

func formatScm(ctx context.Context, req formatScmReq, resp *ctlpb.StorageFormatResp) (map[int]string, map[int]bool, error) {
	needFormat := make(map[int]bool)
	scmCfgs := make(map[int]*storage.TierConfig)
	allNeedFormat := true

	for idx, ei := range req.instances {
		needs, err := ei.GetStorage().ScmNeedsFormat()
		if err != nil {
			return nil, nil, errors.Wrap(err, "detecting if SCM format is needed")
		}
		if !needs {
			allNeedFormat = false
		}
		needFormat[idx] = needs

		scmCfg, err := ei.GetStorage().GetScmConfig()
		if err != nil || scmCfg == nil {
			return nil, nil, errors.Wrap(err, "retrieving SCM config")
		}
		scmCfgs[idx] = scmCfg
	}

	if allNeedFormat {
		// Check available RAM is sufficient before formatting SCM on engines.
		if err := checkTmpfsMem(req.log, scmCfgs, req.getMemInfo); err != nil {
			return nil, nil, err
		}
	}

	scmChan := make(chan *ctlpb.ScmMountResult, len(req.instances))
	errored := make(map[int]string)
	skipped := make(map[int]bool)
	formatting := 0

	for idx, ei := range req.instances {
		if needFormat[idx] || req.reformat {
			formatting++
			go func(e Engine) {
				scmChan <- e.StorageFormatSCM(ctx, req.reformat)
			}(ei)

			continue
		}

		resp.Mrets = append(resp.Mrets, &ctlpb.ScmMountResult{
			Instanceidx: uint32(idx),
			Mntpoint:    scmCfgs[idx].Scm.MountPoint,
			State: &ctlpb.ResponseState{
				Info: "SCM is already formatted",
			},
		})

		skipped[idx] = true
	}

	for formatting > 0 {
		select {
		case <-ctx.Done():
			return nil, nil, ctx.Err()
		case scmResult := <-scmChan:
			formatting--
			state := scmResult.GetState()
			if state.GetStatus() != ctlpb.ResponseStatus_CTL_SUCCESS {
				errored[int(scmResult.GetInstanceidx())] = state.GetError()
			}
			resp.Mrets = append(resp.Mrets, scmResult)
		}
	}

	return errored, skipped, nil
}

type formatNvmeReq struct {
	log         logging.Logger
	instances   []Engine
	errored     map[int]string
	skipped     map[int]bool
	mdFormatted bool
}

func formatNvme(ctx context.Context, req formatNvmeReq, resp *ctlpb.StorageFormatResp) {
	// Allow format to complete on one instance even if another fails
	// TODO: perform bdev format in parallel
	for idx, ei := range req.instances {
		_, hasError := req.errored[idx]
		_, skipped := req.skipped[idx]
		if hasError || (skipped && !req.mdFormatted) {
			// if scm errored or was already formatted, indicate skipping bdev format
			ret := ei.newCret(storage.NilBdevAddress, nil)
			ret.State.Info = fmt.Sprintf(msgNvmeFormatSkip, ei.Index())
			resp.Crets = append(resp.Crets, ret)
			continue
		}

		// SCM formatted correctly on this instance, format NVMe
		cResults := ei.StorageFormatNVMe()
		if cResults.HasErrors() {
			req.errored[idx] = cResults.Errors()
			resp.Crets = append(resp.Crets, cResults...)
			continue
		}

		if err := ei.GetStorage().WriteNvmeConfig(ctx, req.log); err != nil {
			req.errored[idx] = err.Error()
			cResults = append(cResults, ei.newCret("", err))
		}

		resp.Crets = append(resp.Crets, cResults...)
	}
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
	mdFormatted := false

	if len(instances) == 0 {
		return resp, nil
	}

	mdFormatted, err := c.formatMetadata(instances, req.Reformat)
	if err != nil {
		return nil, err
	}

	fsr := formatScmReq{
		log:        c.log,
		reformat:   req.Reformat,
		instances:  instances,
		getMemInfo: c.getMemInfo,
	}
	instanceErrors, instanceSkips, err := formatScm(ctx, fsr, resp)
	if err != nil {
		return nil, err
	}

	fnr := formatNvmeReq{
		log:         c.log,
		instances:   instances,
		errored:     instanceErrors,
		skipped:     instanceSkips,
		mdFormatted: mdFormatted,
	}
	formatNvme(ctx, fnr, resp)

	// Notify storage ready for instances formatted without error.
	// Block until all instances have formatted NVMe to avoid
	// VFIO device or resource busy when starting I/O Engines
	// because devices have already been claimed during format.
	for idx, ei := range instances {
		if msg, hasError := instanceErrors[idx]; hasError {
			c.log.Errorf("instance %d: %s", idx, msg)
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
