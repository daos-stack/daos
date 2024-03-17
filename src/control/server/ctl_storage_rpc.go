//
// (C) Copyright 2019-2024 Intel Corporation.
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
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/ranklist"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/engine"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	msgFormatErr             = "instance %d: failure formatting storage, check RPC response for details"
	msgNvmeFormatSkip        = "NVMe format skipped on instance %d"
	msgNvmeFormatSkipHPD     = msgNvmeFormatSkip + ", use of hugepages disabled in config"
	msgNvmeFormatSkipFail    = msgNvmeFormatSkip + ", SCM format failed"
	msgNvmeFormatSkipNotDone = msgNvmeFormatSkip + ", SCM was not formatted"
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

var (
	errNoSrvCfg = errors.New("ControlService has no server config")
	errNilReq   = errors.New("nil request")
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
	scanBdevs        = bdevScan         // StorageScan() unit tests
	scanEngineBdevs  = bdevScanEngine   // bdevScan() unit tests
	computeMetaRdbSz = metaRdbComputeSz // TODO unit tests
)

type scanBdevsFn func(storage.BdevScanRequest) (*storage.BdevScanResponse, error)

func ctrlrToPciStr(nc *ctlpb.NvmeController) (string, error) {
	pciAddr, err := hardware.NewPCIAddress(nc.GetPciAddr())
	if err != nil {
		return "", errors.Wrapf(err, "Invalid PCI address")
	}
	if pciAddr.IsVMDBackingAddress() {
		if pciAddr, err = pciAddr.BackingToVMDAddress(); err != nil {
			return "", errors.Wrapf(err, "Invalid VMD address")
		}
	}

	return pciAddr.String(), nil
}

func findBdevTier(pciAddr string, tcs storage.TierConfigs) *storage.TierConfig {
	for _, tc := range tcs {
		if !tc.IsBdev() {
			continue
		}
		for _, name := range tc.Bdev.DeviceList.Devices() {
			if pciAddr == name {
				return tc
			}
		}
	}

	return nil
}

// Convert bdev scan results to protobuf response.
func bdevScanToProtoResp(scan scanBdevsFn, bdevCfgs storage.TierConfigs) (*ctlpb.ScanNvmeResp, error) {
	req := storage.BdevScanRequest{DeviceList: bdevCfgs.Bdevs()}

	resp, err := scan(req)
	if err != nil {
		return nil, err
	}

	pbCtrlrs := make(proto.NvmeControllers, 0, len(resp.Controllers))

	if err := pbCtrlrs.FromNative(resp.Controllers); err != nil {
		return nil, err
	}

	if bdevCfgs.HaveRealNVMe() {
		// Update proto Ctrlrs with role info and normal (DAOS) state for off-line display.
		for _, c := range pbCtrlrs {
			pciAddrStr, err := ctrlrToPciStr(c)
			if err != nil {
				return nil, err
			}
			bc := findBdevTier(pciAddrStr, bdevCfgs)
			if bc == nil {
				return nil, errors.Errorf("unknown PCI device, scanned ctrlr %q "+
					"not found in cfg", pciAddrStr)
			}
			if len(c.SmdDevices) != 0 {
				return nil, errors.Errorf("scanned ctrlr %q has unexpected smd",
					pciAddrStr)
			}
			c.SmdDevices = append(c.SmdDevices, &ctlpb.SmdDevice{
				RoleBits: uint32(bc.Bdev.DeviceRoles.OptionBits),
				Rank:     uint32(ranklist.NilRank),
			})
			c.DevState = ctlpb.NvmeDevState_NORMAL
		}
	}

	return &ctlpb.ScanNvmeResp{
		State:  new(ctlpb.ResponseState),
		Ctrlrs: pbCtrlrs,
	}, nil
}

// Scan bdevs through each engine and collate response results.
func bdevScanEngines(ctx context.Context, cs *ControlService, req *ctlpb.ScanNvmeReq, nsps []*ctlpb.ScmNamespace) (*ctlpb.ScanNvmeResp, error) {
	var errLast error
	instances := cs.harness.Instances()
	resp := &ctlpb.ScanNvmeResp{}

	for _, engine := range instances {
		eReq := new(ctlpb.ScanNvmeReq)
		*eReq = *req
		if req.Meta {
			ms, rs, err := computeMetaRdbSz(cs, engine, nsps)
			if err != nil {
				return nil, errors.Wrap(err, "computing meta and rdb size")
			}
			eReq.MetaSize, eReq.RdbSize = ms, rs
		}

		// If partial number of engines return results, indicate errors for non-ready
		// engines whilst returning successful scanmresults.
		respEng, err := scanEngineBdevs(ctx, engine, eReq)
		if err != nil {
			err = errors.Wrapf(err, "instance %d", engine.Index())
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
		if req.GetBasic() {
			pbc.SmdDevices = nil
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

func bdevScanAssigned(ctx context.Context, cs *ControlService, req *ctlpb.ScanNvmeReq, nsps []*ctlpb.ScmNamespace, hasStarted *bool, bdevCfgs storage.TierConfigs) (*ctlpb.ScanNvmeResp, error) {
	*hasStarted = engineHasStarted(cs.harness.Instances())
	if !*hasStarted {
		cs.log.Debugf("scan bdevs from control service as no engines started")
		if req.Meta {
			return nil, errors.New("meta smd usage info unavailable as engines stopped")
		}

		return bdevScanToProtoResp(cs.storage.ScanBdevs, bdevCfgs)
	}

	// Delegate scan to engine instances as soon as one engine with assigned bdevs has started.
	cs.log.Debugf("scan assigned bdevs through engine instances as some are started")
	return bdevScanEngines(ctx, cs, req, nsps)
}

// Return NVMe device details. The scan method employed depends on whether the engines are running
// or not. If running, scan over dRPC. If not running then use engine's storage provider.
func bdevScan(ctx context.Context, cs *ControlService, req *ctlpb.ScanNvmeReq, nsps []*ctlpb.ScmNamespace) (resp *ctlpb.ScanNvmeResp, err error) {
	if req == nil {
		return nil, errNilReq
	}
	if cs.srvCfg != nil && cs.srvCfg.DisableHugepages {
		return nil, errors.New("cannot scan bdevs if hugepages have been disabled")
	}

	defer func() {
		if err == nil && req.Meta {
			cs.adjustNvmeSize(resp)
		}
	}()

	bdevCfgs := getBdevCfgsFromSrvCfg(cs.srvCfg)
	nrCfgBdevs := bdevCfgs.Bdevs().Len()

	if nrCfgBdevs == 0 {
		cs.log.Debugf("scan bdevs from control service as no bdevs in cfg")

		// No bdevs configured for engines to claim so scan through control service.
		resp, err = bdevScanToProtoResp(cs.storage.ScanBdevs, bdevCfgs)
		if err != nil {
			return nil, err
		}

		return bdevScanTrimResults(req, resp), nil
	}

	// Note the potential window where engines are started but not yet ready to respond. In this
	// state there is a possibility that neither scan mechanism will work because devices have
	// been claimed by SPDK but details are not yet available over dRPC.

	var hasStarted bool
	resp, err = bdevScanAssigned(ctx, cs, req, nsps, &hasStarted, bdevCfgs)
	if err != nil {
		return nil, err
	}

	nrScannedBdevs, err := getEffCtrlrCount(resp.Ctrlrs)
	if err != nil {
		return nil, err
	}
	if nrScannedBdevs == nrCfgBdevs {
		return bdevScanTrimResults(req, resp), nil
	}

	// Retry once if harness scan returns unexpected number of controllers in case engines
	// claimed devices between when started state was checked and scan was executed.
	if !hasStarted {
		cs.log.Debugf("retrying harness bdev scan as unexpected nr returned, want %d got %d",
			nrCfgBdevs, nrScannedBdevs)

		resp, err = bdevScanAssigned(ctx, cs, req, nsps, &hasStarted, bdevCfgs)
		if err != nil {
			return nil, err
		}

		nrScannedBdevs, err := getEffCtrlrCount(resp.Ctrlrs)
		if err != nil {
			return nil, err
		}
		if nrScannedBdevs == nrCfgBdevs {
			return bdevScanTrimResults(req, resp), nil
		}
	}

	cs.log.Noticef("harness bdev scan returned unexpected nr, want %d got %d", nrCfgBdevs,
		nrScannedBdevs)

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
func (cs *ControlService) scanScm(ctx context.Context, req *ctlpb.ScanScmReq) (*ctlpb.ScanScmResp, error) {
	if req == nil {
		return nil, errors.New("nil scm request")
	}

	ssr, err := cs.ScmScan(storage.ScmScanRequest{})
	if err != nil || !req.GetUsage() {
		return newScanScmResp(ssr, err)
	}

	ssr, err = cs.getScmUsage(ssr)
	if err != nil {
		return nil, err
	}

	resp, err := newScanScmResp(ssr, nil)
	if err != nil {
		return nil, err
	}

	cs.adjustScmSize(resp)

	return resp, nil
}

// Returns the engine configuration managing the given NVMe controller
func (cs *ControlService) getEngineCfgFromNvmeCtl(nc *ctlpb.NvmeController) (*engine.Config, error) {
	pciAddrStr, err := ctrlrToPciStr(nc)
	if err != nil {
		return nil, err
	}

	for index := range cs.srvCfg.Engines {
		if findBdevTier(pciAddrStr, cs.srvCfg.Engines[index].Storage.Tiers) != nil {
			return cs.srvCfg.Engines[index], nil
		}
	}

	return nil, errors.Errorf("unknown PCI device, scanned ctrlr %q not found in cfg",
		pciAddrStr)
}

// Returns the engine configuration managing the given SCM name-space
func (cs *ControlService) getEngineCfgFromScmNsp(nsp *ctlpb.ScmNamespace) (*engine.Config, error) {
	mountPoint := nsp.GetMount().Path
	for index := range cs.srvCfg.Engines {
		for _, tierCfg := range cs.srvCfg.Engines[index].Storage.Tiers {
			if tierCfg.IsSCM() && tierCfg.Scm.MountPoint == mountPoint {
				return cs.srvCfg.Engines[index], nil
			}
		}
	}

	return nil, errors.Errorf("unknown SCM mount point %s", mountPoint)
}

// return the size of the RDB file used for managing SCM metadata
func (cs *ControlService) getRdbSize(engineCfg *engine.Config) (uint64, error) {
	mdCapStr, err := engineCfg.GetEnvVar(daos.DaosMdCapEnv)
	if err != nil {
		cs.log.Debugf("using default RDB file size with engine %d: %s (%d Bytes)",
			engineCfg.Index, humanize.Bytes(daos.DefaultDaosMdCapSize),
			daos.DefaultDaosMdCapSize)
		return uint64(daos.DefaultDaosMdCapSize), nil
	}

	rdbSize, err := strconv.ParseUint(mdCapStr, 10, 64)
	if err != nil {
		return 0, errors.Errorf("invalid RDB file size: %q does not define a plain int",
			mdCapStr)
	}
	rdbSize = rdbSize << 20
	cs.log.Debugf("using custom RDB size with engine %d: %s (%d Bytes)",
		engineCfg.Index, humanize.Bytes(rdbSize), rdbSize)

	return rdbSize, nil
}

// Compute the maximal size of the metadata to allow the engine to fill the WallMeta field
// response.  The maximal metadata (i.e. VOS index file) size should be equal to the SCM available
// size divided by the number of targets of the engine.
func metaRdbComputeSz(cs *ControlService, ei Engine, nsps []*ctlpb.ScmNamespace) (md_size, rdb_size uint64, errOut error) {
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
	ctlr *ctlpb.NvmeController
	idx  int
	rank uint32
}

type deviceSizeStat struct {
	clusterPerTarget uint64 // Number of usable SPDK clusters for each target
	devs             []*deviceToAdjust
}

// Add a device to the input map of device to which the usable size have to be adjusted
func (cs *ControlService) addDeviceToAdjust(devsStat map[uint32]*deviceSizeStat, devToAdjust *deviceToAdjust, dataClusterCount uint64) {
	dev := devToAdjust.ctlr.GetSmdDevices()[devToAdjust.idx]
	if devsStat[devToAdjust.rank] == nil {
		devsStat[devToAdjust.rank] = &deviceSizeStat{
			clusterPerTarget: math.MaxUint64,
		}
	}
	devsStat[devToAdjust.rank].devs = append(devsStat[devToAdjust.rank].devs, devToAdjust)
	targetCount := uint64(len(dev.GetTgtIds()))
	clusterPerTarget := dataClusterCount / targetCount
	cs.log.Tracef("SMD device %s (rank %d, ctlr %s) added to the list of device to adjust",
		dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
	if clusterPerTarget < devsStat[devToAdjust.rank].clusterPerTarget {
		cs.log.Tracef("Updating number of clusters per target of rank %d: old=%d new=%d",
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

func (cs *ControlService) getMetaClusterCount(engineCfg *engine.Config, devToAdjust deviceToAdjust) (subtrClusterCount uint64) {
	dev := devToAdjust.ctlr.GetSmdDevices()[devToAdjust.idx]
	clusterSize := uint64(dev.GetClusterSize())
	engineTargetNb := uint64(engineCfg.TargetCount)

	if dev.GetRoleBits()&storage.BdevRoleMeta != 0 {
		// TODO DAOS-14223: GetMetaSize() should reflect custom values set through pool
		//                  create --meta-size option.
		clusterCount := getClusterCount(dev.GetMetaSize(), engineTargetNb, clusterSize)
		cs.log.Tracef("Removing %d Metadata clusters (cluster size: %d) from the usable size of the SMD device %s (rank %d, ctlr %s): ",
			clusterCount, clusterSize, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	if dev.GetRoleBits()&storage.BdevRoleWAL != 0 {
		clusterCount := getClusterCount(dev.GetMetaWalSize(), engineTargetNb, clusterSize)
		cs.log.Tracef("Removing %d Metadata WAL clusters (cluster size: %d) from the usable size of the SMD device %s (rank %d, ctlr %s): ",
			clusterCount, clusterSize, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	if dev.GetRdbSize() == 0 {
		return
	}

	if dev.GetRoleBits()&storage.BdevRoleMeta != 0 {
		clusterCount := getClusterCount(dev.GetRdbSize(), 1, clusterSize)
		cs.log.Tracef("Removing %d RDB clusters (cluster size: %d) the usable size of the SMD device %s (rank %d, ctlr %s)",
			clusterCount, clusterSize, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	if dev.GetRoleBits()&storage.BdevRoleWAL != 0 {
		clusterCount := getClusterCount(dev.GetRdbWalSize(), 1, clusterSize)
		cs.log.Tracef("Removing %d RDB WAL clusters (cluster size: %d) from the usable size of the SMD device %s (rank %d, ctlr %s)",
			clusterCount, clusterSize, dev.GetUuid(), devToAdjust.rank, devToAdjust.ctlr.GetPciAddr())
		subtrClusterCount += clusterCount
	}

	return
}

// Adjust the NVME available size to its real usable size.
func (cs *ControlService) adjustNvmeSize(resp *ctlpb.ScanNvmeResp) {
	devsStat := make(map[uint32]*deviceSizeStat, 0)
	for _, ctlr := range resp.GetCtrlrs() {
		engineCfg, err := cs.getEngineCfgFromNvmeCtl(ctlr)
		if err != nil {
			cs.log.Noticef("Skipping NVME controller %s: %s", ctlr.GetPciAddr(), err.Error())
			continue
		}

		for idx, dev := range ctlr.GetSmdDevices() {
			rank := dev.GetRank()

			if dev.GetRoleBits() != 0 && (dev.GetRoleBits()&storage.BdevRoleData) == 0 {
				cs.log.Debugf("SMD device %s (rank %d, ctlr %s) not used to store data (Role bits 0x%X)",
					dev.GetUuid(), rank, ctlr.GetPciAddr(), dev.GetRoleBits())
				dev.TotalBytes = 0
				dev.AvailBytes = 0
				dev.UsableBytes = 0
				continue
			}

			if ctlr.GetDevState() != ctlpb.NvmeDevState_NORMAL {
				cs.log.Debugf("SMD device %s (rank %d, ctlr %s) not usable: device state %q",
					dev.GetUuid(), rank, ctlr.GetPciAddr(), ctlpb.NvmeDevState_name[int32(ctlr.DevState)])
				dev.AvailBytes = 0
				dev.UsableBytes = 0
				continue
			}

			if dev.GetClusterSize() == 0 || len(dev.GetTgtIds()) == 0 {
				cs.log.Noticef("SMD device %s (rank %d,  ctlr %s) not usable: missing storage info",
					dev.GetUuid(), rank, ctlr.GetPciAddr())
				dev.AvailBytes = 0
				dev.UsableBytes = 0
				continue
			}

			cs.log.Tracef("Initial available size of SMD device %s (rank %d, ctlr %s): %s (%d bytes)",
				dev.GetUuid(), rank, ctlr.GetPciAddr(), humanize.Bytes(dev.GetAvailBytes()), dev.GetAvailBytes())

			clusterSize := uint64(dev.GetClusterSize())
			availBytes := (dev.GetAvailBytes() / clusterSize) * clusterSize
			if dev.GetAvailBytes() != availBytes {
				cs.log.Tracef("Adjusting available size of SMD device %s (rank %d, ctlr %s): from %s (%d Bytes) to %s (%d bytes)",
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
				cs.log.Tracef("No meta-data stored on SMD device %s (rank %d, ctlr %s)",
					dev.GetUuid(), rank, ctlr.GetPciAddr())
				cs.addDeviceToAdjust(devsStat, &devToAdjust, dataClusterCount)
				continue
			}

			subtrClusterCount := cs.getMetaClusterCount(engineCfg, devToAdjust)
			if subtrClusterCount >= dataClusterCount {
				cs.log.Debugf("No more usable space in SMD device %s (rank %d, ctlr %s)",
					dev.GetUuid(), rank, ctlr.GetPciAddr())
				dev.UsableBytes = 0
				continue
			}
			dataClusterCount -= subtrClusterCount
			cs.addDeviceToAdjust(devsStat, &devToAdjust, dataClusterCount)
		}
	}

	for rank, item := range devsStat {
		for _, dev := range item.devs {
			smdDev := dev.ctlr.GetSmdDevices()[dev.idx]
			targetCount := uint64(len(smdDev.GetTgtIds()))
			smdDev.UsableBytes = targetCount * item.clusterPerTarget * smdDev.GetClusterSize()
			cs.log.Debugf("Defining usable size of the SMD device %s (rank %d, ctlr %s) to %s (%d bytes)",
				smdDev.GetUuid(), rank, dev.ctlr.GetPciAddr(),
				humanize.Bytes(smdDev.GetUsableBytes()), smdDev.GetUsableBytes())
		}
	}
}

// Adjust the SCM available size to the real usable size.
func (cs *ControlService) adjustScmSize(resp *ctlpb.ScanScmResp) {
	for _, scmNamespace := range resp.GetNamespaces() {
		mnt := scmNamespace.GetMount()
		mountPath := mnt.GetPath()
		mnt.UsableBytes = mnt.GetAvailBytes()
		cs.log.Debugf("Initial usable size of SCM %s: %s (%d bytes)", mountPath,
			humanize.Bytes(mnt.GetUsableBytes()), mnt.GetUsableBytes())

		engineCfg, err := cs.getEngineCfgFromScmNsp(scmNamespace)
		if err != nil {
			cs.log.Noticef("Adjusting usable size to 0 Bytes of SCM device %q: %s",
				mountPath, err.Error())
			mnt.UsableBytes = 0
			continue
		}

		mdBytes, err := cs.getRdbSize(engineCfg)
		if err != nil {
			cs.log.Noticef("Adjusting usable size to 0 Bytes of SCM device %q: %s",
				mountPath, err.Error())
			mnt.UsableBytes = 0
			continue
		}
		cs.log.Tracef("Removing RDB (%s, %d bytes) from the usable size of the SCM device %q",
			humanize.Bytes(mdBytes), mdBytes, mountPath)
		if mdBytes >= mnt.GetUsableBytes() {
			cs.log.Debugf("No more usable space in SCM device %s", mountPath)
			mnt.UsableBytes = 0
			continue
		}
		mnt.UsableBytes -= mdBytes

		removeControlPlaneMetadata := func(m *ctlpb.ScmNamespace_Mount) {
			mountPath := m.GetPath()

			cs.log.Tracef("Removing control plane metadata (%s, %d bytes) from the usable size of the SCM device %q",
				humanize.Bytes(mdDaosScmBytes), mdDaosScmBytes, mountPath)
			if mdDaosScmBytes >= m.GetUsableBytes() {
				cs.log.Debugf("No more usable space in SCM device %s", mountPath)
				m.UsableBytes = 0
				return
			}
			m.UsableBytes -= mdDaosScmBytes
		}
		if !engineCfg.Storage.Tiers.HasBdevRoleMeta() {
			removeControlPlaneMetadata(mnt)
		} else {
			if !engineCfg.Storage.ControlMetadata.HasPath() {
				cs.log.Noticef("Adjusting usable size to 0 Bytes of SCM device %q: %s",
					mountPath,
					"MD on SSD feature enabled without path for Control Metadata")
				mnt.UsableBytes = 0
				continue
			}

			cmdPath := engineCfg.Storage.ControlMetadata.Path
			if hasPrefix, err := common.HasPrefixPath(mountPath, cmdPath); hasPrefix || err != nil {
				if err != nil {
					cs.log.Noticef("Invalid SCM mount path or Control Metadata path: %q", err.Error())
				}
				if hasPrefix {
					removeControlPlaneMetadata(mnt)
				}
			}
		}

		cs.log.Tracef("Removing (%s, %d bytes) of usable size from the SCM device %q: space used by the file system metadata",
			humanize.Bytes(mdFsScmBytes), mdFsScmBytes, mountPath)
		mnt.UsableBytes -= mdFsScmBytes

		usableBytes := scmNamespace.Mount.GetUsableBytes()
		cs.log.Debugf("Usable size of SCM device %q: %s (%d bytes)",
			scmNamespace.Mount.GetPath(), humanize.Bytes(usableBytes), usableBytes)
	}
}

// StorageScan discovers non-volatile storage hardware on node.
func (cs *ControlService) StorageScan(ctx context.Context, req *ctlpb.StorageScanReq) (*ctlpb.StorageScanResp, error) {
	if req == nil {
		return nil, errNilReq
	}
	if cs.srvCfg == nil {
		return nil, errNoSrvCfg
	}
	resp := new(ctlpb.StorageScanResp)

	respScm, err := cs.scanScm(ctx, req.Scm)
	if err != nil {
		return nil, err
	}
	resp.Scm = respScm

	if cs.srvCfg.DisableHugepages {
		cs.log.Notice("bdev scan skipped as use of hugepages disabled in config")
		resp.Nvme = &ctlpb.ScanNvmeResp{
			State: new(ctlpb.ResponseState),
		}
	} else {
		respNvme, err := scanBdevs(ctx, cs, req.Nvme, respScm.Namespaces)
		if err != nil {
			return nil, err
		}
		resp.Nvme = respNvme
	}

	mi, err := cs.getMemInfo()
	if err != nil {
		return nil, err
	}
	if err := convert.Types(mi, &resp.MemInfo); err != nil {
		return nil, err
	}

	return resp, nil
}

func (cs *ControlService) formatMetadata(instances []Engine, reformat bool) (bool, error) {
	// Format control metadata first, if needed
	if needs, err := cs.storage.ControlMetadataNeedsFormat(); err != nil {
		return false, errors.Wrap(err, "detecting if metadata format is needed")
	} else if needs || reformat {
		engineIdxs := make([]uint, len(instances))
		for i, eng := range instances {
			engineIdxs[i] = uint(eng.Index())
		}

		cs.log.Debug("formatting control metadata storage")
		if err := cs.storage.FormatControlMetadata(engineIdxs); err != nil {
			return false, errors.Wrap(err, "formatting control metadata storage")
		}

		return true, nil
	}

	cs.log.Debug("no control metadata format needed")
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

func formatNvme(ctx context.Context, req formatNvmeReq, resp *ctlpb.StorageFormatResp) error {
	// Allow format to complete on one instance even if another fails
	for idx, engine := range req.instances {
		_, hasError := req.errored[idx]
		_, skipped := req.skipped[idx]

		// Skip NVMe format if scm was already formatted or failed to format.
		skipReason := ""
		if hasError {
			skipReason = msgNvmeFormatSkipFail
		} else if skipped && !req.mdFormatted {
			skipReason = msgNvmeFormatSkipNotDone
		}
		if skipReason != "" {
			ret := engine.newCret(storage.NilBdevAddress, nil)
			ret.State.Info = fmt.Sprintf(skipReason, engine.Index())
			resp.Crets = append(resp.Crets, ret)
			continue
		}

		respBdevs, err := scanEngineBdevs(ctx, engine, new(ctlpb.ScanNvmeReq))
		if err != nil {
			if errors.Is(err, errEngineBdevScanEmptyDevList) {
				// No controllers assigned in config, continue.
				continue
			}
			req.errored[idx] = err.Error()
			resp.Crets = append(resp.Crets, engine.newCret("", err))
			continue
		}

		// Convert proto ctrlr scan results to native when calling into storage provider.
		pbCtrlrs := proto.NvmeControllers(respBdevs.Ctrlrs)
		ctrlrs, err := pbCtrlrs.ToNative()
		if err != nil {
			return errors.Wrapf(err, "convert %T to %T", pbCtrlrs, ctrlrs)
		}

		ei, ok := engine.(*EngineInstance)
		if !ok {
			return errors.New("Engine interface obj is not an EngineInstance")
		}

		// SCM formatted correctly on this instance, format NVMe
		cResults := formatEngineBdevs(ei, ctrlrs)

		if cResults.HasErrors() {
			req.errored[idx] = cResults.Errors()
			resp.Crets = append(resp.Crets, cResults...)
			continue
		}

		if err := engine.GetStorage().WriteNvmeConfig(ctx, req.log, ctrlrs); err != nil {
			req.errored[idx] = err.Error()
			cResults = append(cResults, engine.newCret("", err))
		}

		resp.Crets = append(resp.Crets, cResults...)
	}

	return nil
}

// StorageFormat delegates to Storage implementation's Format methods to prepare
// storage for use by DAOS data plane.
//
// Errors returned will stop other servers from formatting, non-fatal errors
// specific to particular device should be reported within resp results instead.
//
// Send response containing multiple results of format operations on scm mounts
// and nvme controllers.
func (cs *ControlService) StorageFormat(ctx context.Context, req *ctlpb.StorageFormatReq) (*ctlpb.StorageFormatResp, error) {
	if req == nil {
		return nil, errNilReq
	}
	if cs.srvCfg == nil {
		return nil, errNoSrvCfg
	}

	instances := cs.harness.Instances()
	resp := new(ctlpb.StorageFormatResp)
	resp.Mrets = make([]*ctlpb.ScmMountResult, 0, len(instances))
	resp.Crets = make([]*ctlpb.NvmeControllerResult, 0, len(instances))
	mdFormatted := false

	if len(instances) == 0 {
		return resp, nil
	}

	mdFormatted, err := cs.formatMetadata(instances, req.Reformat)
	if err != nil {
		return nil, err
	}

	fsr := formatScmReq{
		log:        cs.log,
		reformat:   req.Reformat,
		instances:  instances,
		getMemInfo: cs.getMemInfo,
	}
	cs.log.Tracef("formatScmReq: %+v", fsr)
	instanceErrors, instanceSkips, err := formatScm(ctx, fsr, resp)
	if err != nil {
		return nil, err
	}

	hugepagesDisabled := false
	if cs.srvCfg.DisableHugepages {
		cs.log.Debug("skipping bdev format as use of hugepages disabled in config")
		hugepagesDisabled = true
	} else {
		fnr := formatNvmeReq{
			log:         cs.log,
			instances:   instances,
			errored:     instanceErrors,
			skipped:     instanceSkips,
			mdFormatted: mdFormatted,
		}
		cs.log.Tracef("formatNvmeReq: %+v", fnr)
		formatNvme(ctx, fnr, resp)
	}

	cs.log.Tracef("StorageFormatResp: %+v", resp)

	// Notify storage ready for instances formatted without error.
	// Block until all instances have formatted NVMe to avoid
	// VFIO device or resource busy when starting I/O Engines
	// because devices have already been claimed during format.
	for idx, engine := range instances {
		if hugepagesDisabled {
			// Populate skip NVMe format results for all engines.
			ret := engine.newCret(storage.NilBdevAddress, nil)
			ret.State.Info = fmt.Sprintf(msgNvmeFormatSkipHPD, engine.Index())
			resp.Crets = append(resp.Crets, ret)
		}
		if msg, hasError := instanceErrors[idx]; hasError {
			cs.log.Errorf("instance %d: %s", idx, msg)
			continue
		}
		engine.NotifyStorageReady()
	}

	return resp, nil
}

// StorageNvmeRebind rebinds SSD from kernel and binds to user-space to allow DAOS to use it.
func (cs *ControlService) StorageNvmeRebind(ctx context.Context, req *ctlpb.NvmeRebindReq) (*ctlpb.NvmeRebindResp, error) {
	if req == nil {
		return nil, errNilReq
	}
	if cs.srvCfg == nil {
		return nil, errNoSrvCfg
	}
	if cs.srvCfg.DisableHugepages {
		return nil, FaultHugepagesDisabled
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
	if _, err := cs.NvmePrepare(prepReq); err != nil {
		err = errors.Wrap(err, "nvme rebind")
		cs.log.Error(err.Error())

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
func (cs *ControlService) StorageNvmeAddDevice(ctx context.Context, req *ctlpb.NvmeAddDeviceReq) (resp *ctlpb.NvmeAddDeviceResp, err error) {
	if req == nil {
		return nil, errNilReq
	}
	if cs.srvCfg == nil {
		return nil, errNoSrvCfg
	}
	if cs.srvCfg.DisableHugepages {
		return nil, FaultHugepagesDisabled
	}

	engines := cs.harness.Instances()
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

	cs.log.Debugf("bdev list to be updated: %+v", tierCfg.Bdev.DeviceList)
	if err := tierCfg.Bdev.DeviceList.AddStrings(req.PciAddr); err != nil {
		return nil, errors.Errorf("updating bdev list for tier %d", tierIndex)
	}
	cs.log.Debugf("updated bdev list: %+v", tierCfg.Bdev.DeviceList)

	// TODO: Supply scan results for VMD backing device address mapping.
	resp = new(ctlpb.NvmeAddDeviceResp)
	if err := engineStorage.WriteNvmeConfig(ctx, cs.log, nil); err != nil {
		err = errors.Wrapf(err, "write nvme config for engine %d", engineIndex)
		cs.log.Error(err.Error())

		// report write conf call result in response
		resp.State = &ctlpb.ResponseState{
			Error:  err.Error(),
			Status: ctlpb.ResponseStatus_CTL_ERR_NVME,
		}
	}

	return resp, nil
}
