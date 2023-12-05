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
	"github.com/dustin/go-humanize/english"
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

// Package-local function variables for mocking in unit tests.
var (
	// Use to stub bdev scan response in StorageScan() unit tests.
	scanBdevs       = bdevScan
	scanEngineBdevs = bdevScanEngine
)

type scanBdevsFn func(storage.BdevScanRequest) (*storage.BdevScanResponse, error)

// Convert bdev scan results to protobuf response.
func bdevScanToProtoResp(scan scanBdevsFn, req storage.BdevScanRequest) (*ctlpb.ScanNvmeResp, error) {
	resp, err := scan(req)
	if err != nil {
		return nil, err
	}

	pbCtrlrs := make(proto.NvmeControllers, 0, len(resp.Controllers))

	if err := pbCtrlrs.FromNative(resp.Controllers); err != nil {
		return nil, err
	}

	return &ctlpb.ScanNvmeResp{
		State:  new(ctlpb.ResponseState),
		Ctrlrs: pbCtrlrs,
	}, nil
}

// Scan bdevs through harness's ControlService (not per-engine).
func bdevScanGlobal(cs *ControlService, cfgBdevs *storage.BdevDeviceList) (*ctlpb.ScanNvmeResp, error) {
	req := storage.BdevScanRequest{DeviceList: cfgBdevs}
	return bdevScanToProtoResp(cs.storage.ScanBdevs, req)
}

// Scan bdevs through each engine and collate response results.
func bdevScanEngines(ctx context.Context, cs *ControlService, req *ctlpb.ScanNvmeReq, nsps []*ctlpb.ScmNamespace) (*ctlpb.ScanNvmeResp, error) {
	var errLast error
	instances := cs.harness.Instances()
	resp := &ctlpb.ScanNvmeResp{}

	for _, ei := range instances {
		eReq := new(ctlpb.ScanNvmeReq)
		*eReq = *req
		ms, rs, err := cs.computeMetaRdbSize(ei, nsps)
		if err != nil {
			return nil, errors.Wrap(err, "computing meta and rdb size")
		}
		eReq.MetaSize, eReq.RdbSize = ms, rs

		respEng, err := scanEngineBdevs(ctx, ei, eReq)
		if err != nil {
			err = errors.Wrapf(err, "instance %d", ei.Index())
			if errLast == nil && len(instances) > 1 {
				errLast = err // Save err to preserve partial results.
				cs.log.Error(err.Error())
				continue
			}
			return nil, err // No partial results to save so fail.
		}
		resp.Ctrlrs = append(resp.Ctrlrs, respEng.Ctrlrs...)
	}

	// If one engine succeeds and one other fails, error is embedded in the response.
	resp.State = newResponseState(errLast, ctlpb.ResponseStatus_CTL_ERR_NVME, "")

	return resp, nil
}

// Trim unwanted fields so responses can be coalesced from hash map when returned from server.
func bdevScanTrimResults(req *ctlpb.ScanNvmeReq, resp *ctlpb.ScanNvmeResp) *ctlpb.ScanNvmeResp {
	if resp == nil {
		return nil
	}
	for _, pbc := range resp.Ctrlrs {
		if !req.GetHealth() {
			pbc.HealthStats = nil
		}
		if !req.GetMeta() {
			pbc.SmdDevices = nil
		}
		if req.GetBasic() {
			pbc.Serial = ""
			pbc.Model = ""
			pbc.FwRev = ""
		}
	}

	return resp
}

func engineHasStarted(instances []Engine) bool {
	for _, ei := range instances {
		if ei.IsStarted() {
			return true
		}
	}

	return false
}

func bdevScanAssigned(ctx context.Context, cs *ControlService, req *ctlpb.ScanNvmeReq, nsps []*ctlpb.ScmNamespace, hasStarted *bool, cfgBdevs *storage.BdevDeviceList) (*ctlpb.ScanNvmeResp, error) {
	*hasStarted = engineHasStarted(cs.harness.Instances())
	if !*hasStarted {
		cs.log.Debugf("scan bdevs from control service as no engines started")
		return bdevScanGlobal(cs, cfgBdevs)
	}

	// Delegate scan to engine instances as soon as one engine with assigned bdevs has started.
	cs.log.Debugf("scan assigned bdevs through engine instances as some are started")
	return bdevScanEngines(ctx, cs, req, nsps)
}

// Return NVMe device details. The scan method employed depends on whether the engines are running
// or not. If running, scan over dRPC. If not running then use engine's storage provider.
func bdevScan(ctx context.Context, cs *ControlService, req *ctlpb.ScanNvmeReq, nsps []*ctlpb.ScmNamespace) (resp *ctlpb.ScanNvmeResp, err error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	cfgBdevs := getBdevCfgsFromSrvCfg(cs.srvCfg).Bdevs()

	if cfgBdevs.Len() == 0 {
		cs.log.Debugf("scan bdevs from control service as no bdevs in cfg")

		// No bdevs configured for engines to claim so scan through control service.
		resp, err = bdevScanGlobal(cs, cfgBdevs)
		if err != nil {
			return nil, err
		}
		return bdevScanTrimResults(req, resp), nil
	}

	// Note the potential window where engines are started but not yet ready to respond. In this
	// state there is a possibility that neither scan mechanism will work because devices have
	// been claimed by SPDK but details are not yet available over dRPC.

	var hasStarted bool
	resp, err = bdevScanAssigned(ctx, cs, req, nsps, &hasStarted, cfgBdevs)
	if err != nil {
		return nil, err
	}

	// Retry once if global scan returns unexpected number of controllers in case engines
	// claimed devices between when started state was checked and scan was executed.
	if !hasStarted && len(resp.Ctrlrs) != cfgBdevs.Len() {
		cs.log.Debugf("retrying bdev scan as unexpected nr returned, want %d got %d",
			cfgBdevs.Len(), len(resp.Ctrlrs))

		resp, err = bdevScanAssigned(ctx, cs, req, nsps, &hasStarted, cfgBdevs)
		if err != nil {
			return nil, err
		}
	}

	if len(resp.Ctrlrs) != cfgBdevs.Len() {
		cs.log.Noticef("bdev scan returned unexpected nr, want %d got %d",
			cfgBdevs.Len(), len(resp.Ctrlrs))
	}

	return bdevScanTrimResults(req, resp), nil
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
		for _, tierCfg := range c.srvCfg.Engines[index].Storage.Tiers {
			if !tierCfg.IsBdev() {
				continue
			}
			for _, devName := range tierCfg.Bdev.DeviceList.Devices() {
				if devName == ctlrAddr {
					return c.srvCfg.Engines[index], nil
				}
			}
		}
	}

	return nil, errors.Errorf("unknown PCI device %q", pciAddr)
}

// Returns the engine configuration managing the given SCM name-space
func (c *ControlService) getEngineCfgFromScmNsp(nsp *ctl.ScmNamespace) (*engine.Config, error) {
	mountPoint := nsp.GetMount().Path
	for index := range c.srvCfg.Engines {
		for _, tierCfg := range c.srvCfg.Engines[index].Storage.Tiers {
			if tierCfg.IsSCM() && tierCfg.Scm.MountPoint == mountPoint {
				return c.srvCfg.Engines[index], nil
			}
		}
	}

	return nil, errors.Errorf("unknown SCM mount point %s", mountPoint)
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

// Compute the maximal size of the metadata to allow the engine to fill the WallMeta field
// response.  The maximal metadata (i.e. VOS index file) size should be equal to the SCM available
// size divided by the number of targets of the engine.
func (cs *ControlService) computeMetaRdbSize(ei Engine, nsps []*ctlpb.ScmNamespace) (md_size, rdb_size uint64, errOut error) {
	for _, nsp := range nsps {
		mp := nsp.GetMount()
		if mp == nil {
			continue
		}
		if r, err := ei.GetRank(); err != nil || uint32(r) != mp.GetRank() {
			continue
		}

		// NOTE DAOS-14223: This metadata size calculation won't necessarily match
		//                  the meta blob size on SSD if --meta-size is specified in
		//                  pool create command.
		md_size = mp.GetUsableBytes() / uint64(ei.GetTargetCount())

		engineCfg, err := cs.getEngineCfgFromScmNsp(nsp)
		if err != nil {
			errOut = errors.Wrap(err, "Engine with invalid configuration")
			return
		}
		rdb_size, errOut = cs.getRdbSize(engineCfg)
		if errOut != nil {
			return
		}
		break
	}

	if md_size == 0 {
		cs.log.Noticef("instance %d: no SCM space available for metadata", ei.Index)
	}

	return
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
		// TODO DAOS-14223: GetMetaSize() should reflect custom values set through pool
		//                  create --meta-size option.
		clusterCount := getClusterCount(dev.GetMetaSize(), engineTargetNb, clusterSize)
		c.log.Tracef("Removing %d Metadata clusters (cluster size: %d) from the usable size of the SMD device %s (rank %d, ctlr %s): ",
			clusterCount, clusterSize, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	if dev.GetRoleBits()&storage.BdevRoleWAL != 0 {
		clusterCount := getClusterCount(dev.GetMetaWalSize(), engineTargetNb, clusterSize)
		c.log.Tracef("Removing %d Metadata WAL clusters (cluster size: %d) from the usable size of the SMD device %s (rank %d, ctlr %s): ",
			clusterCount, clusterSize, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	if dev.GetRdbSize() == 0 {
		return
	}

	if dev.GetRoleBits()&storage.BdevRoleMeta != 0 {
		clusterCount := getClusterCount(dev.GetRdbSize(), 1, clusterSize)
		c.log.Tracef("Removing %d RDB clusters (cluster size: %d) the usable size of the SMD device %s (rank %d, ctlr %s)",
			clusterCount, clusterSize, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	if dev.GetRoleBits()&storage.BdevRoleWAL != 0 {
		clusterCount := getClusterCount(dev.GetRdbWalSize(), 1, clusterSize)
		c.log.Tracef("Removing %d RDB WAL clusters (cluster size: %d) from the usable size of the SMD device %s (rank %d, ctlr %s)",
			clusterCount, clusterSize, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
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

			if ctlr.GetDevState() != ctlpb.NvmeDevState_NORMAL {
				c.log.Debugf("SMD device %s (rank %d, ctlr %s) not usable: device state %q",
					dev.GetUuid(), rank, ctlr.GetPciAddr(), ctlpb.NvmeDevState_name[int32(ctlr.DevState)])
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
		nrInstances := len(c.harness.Instances())
		readyRanks := c.harness.readyRanks()
		if len(readyRanks) != nrInstances {
			return nil, errors.Wrapf(errEngineNotReady, "%s, ready: %v",
				english.Plural(nrInstances, "engine", "engines"),
				readyRanks)
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

	respNvme, err := scanBdevs(ctx, c, req.Nvme, respScm.Namespaces)
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
	emptyTmpfs := make(map[int]bool)
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

		// If the tmpfs was already mounted but empty, record that fact for later usage.
		if scmCfg.Class == storage.ClassRam && !needs {
			info, err := ei.GetStorage().GetScmUsage()
			if err != nil {
				return nil, nil, errors.Wrapf(err, "failed to check SCM usage for instance %d", idx)
			}
			emptyTmpfs[idx] = info.TotalBytes-info.AvailBytes == 0
		}
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

		// In the normal case, where SCM wasn't already mounted, we want
		// to trigger NVMe format. In the case where SCM was mounted and
		// wasn't empty, we want to skip NVMe format, as we're using
		// mountedness as a proxy for already-formatted. In the special
		// case where tmpfs was already mounted but empty, we will treat it
		// as an indication that the NVMe format needs to occur.
		if !emptyTmpfs[idx] {
			skipped[idx] = true
		}
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
			// if scm failed to format or was already formatted, indicate skipping bdev format
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
