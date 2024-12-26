//
// (C) Copyright 2020-2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package server

import (
	"context"
	"fmt"
	"sort"
	"strings"
	"time"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"
	"golang.org/x/sys/unix"

	"github.com/daos-stack/daos/src/control/build"
	"github.com/daos-stack/daos/src/control/common/proto"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/events"
	"github.com/daos-stack/daos/src/control/fault"
	"github.com/daos-stack/daos/src/control/lib/hardware"
	"github.com/daos-stack/daos/src/control/lib/hardware/pciutils"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var (
	// Function pointers to enable mocking.
	scanSmd       = listSmdDevices
	scanHealth    = getBioHealth
	linkStatsProv = pciutils.NewPCIeLinkStatsProvider()

	// Sentinel errors to enable comparison.
	errEngineBdevScanEmptyDevList = errors.New("empty device list for engine instance")
	errCtrlrHealthSkipped         = errors.New("controller health update was skipped")
)

// newMntRet creates and populates SCM mount result.
// Currently only used for format operations.
func (ei *EngineInstance) newMntRet(mountPoint string, inErr error) *ctlpb.ScmMountResult {
	var info string
	if fault.HasResolution(inErr) {
		info = fault.ShowResolutionFor(inErr)
	}
	return &ctlpb.ScmMountResult{
		Mntpoint:    mountPoint,
		State:       newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_SCM, info),
		Instanceidx: ei.Index(),
	}
}

// newCret creates and populates NVMe controller result and logs error
func (ei *EngineInstance) newCret(pciAddr string, inErr error) *ctlpb.NvmeControllerResult {
	var info string
	if pciAddr == "" {
		pciAddr = storage.NilBdevAddress
	}
	if inErr != nil && fault.HasResolution(inErr) {
		info = fault.ShowResolutionFor(inErr)
	}
	return &ctlpb.NvmeControllerResult{
		PciAddr: pciAddr,
		State:   newResponseState(inErr, ctlpb.ResponseStatus_CTL_ERR_NVME, info),
	}
}

func (ei *EngineInstance) logDuration(msg string, start time.Time) {
	ei.log.Infof("%v: %v\n", msg, time.Since(start))
}

// scmFormat will return either successful result or error.
func (ei *EngineInstance) scmFormat(force bool) (*ctlpb.ScmMountResult, error) {
	cfg, err := ei.storage.GetScmConfig()
	if err != nil {
		return nil, err
	}

	defer ei.logDuration(track(fmt.Sprintf(
		"Format of SCM storage for %s instance %d (reformat: %t)", build.DataPlaneName,
		ei.Index(), force)))

	err = ei.storage.FormatScm(force)
	if err != nil {
		return nil, err
	}

	return ei.newMntRet(cfg.Scm.MountPoint, nil), nil
}

func formatEngineBdevs(ei *EngineInstance, ctrlrs storage.NvmeControllers) (results proto.NvmeControllerResults) {
	// If no superblock exists, format NVMe and populate response with results.
	needsSuperblock, err := ei.needsSuperblock()
	if err != nil {
		ei.log.Errorf("engine storage for %s instance %d: needsSuperblock(): %s",
			build.DataPlaneName, ei.Index(), err)

		return proto.NvmeControllerResults{
			ei.newCret("", err),
		}
	}

	if !needsSuperblock {
		return
	}

	defer ei.logDuration(track(fmt.Sprintf(
		"Format of NVMe storage for %s instance %d", build.DataPlaneName, ei.Index())))

	for _, tr := range ei.storage.FormatBdevTiers(ctrlrs) {
		if tr.Error != nil {
			results = append(results, ei.newCret(fmt.Sprintf("tier %d", tr.Tier),
				tr.Error))
			continue
		}
		for devAddr, status := range tr.Result.DeviceResponses {
			ei.log.Debugf("instance %d: tier %d: device fmt of %s, status %+v, roles %q",
				ei.Index(), tr.Tier, devAddr, status, tr.DeviceRoles)

			// TODO DAOS-5828: passing status.Error directly triggers segfault
			var err error
			if status.Error != nil {
				err = status.Error
			}
			res := ei.newCret(devAddr, err)
			res.RoleBits = uint32(tr.DeviceRoles.OptionBits)
			results = append(results, res)
		}
	}

	return
}

// StorageFormatSCM performs format on SCM and identifies if superblock needs
// writing.
func (ei *EngineInstance) StorageFormatSCM(ctx context.Context, force bool) (mResult *ctlpb.ScmMountResult) {
	engineIdx := ei.Index()

	var scmErr error
	cfg, err := ei.storage.GetScmConfig()
	if err != nil {
		scmErr = err
		cfg = &storage.TierConfig{Scm: storage.ScmConfig{MountPoint: "unknown"}}
		return
	}

	defer func() {
		if scmErr != nil {
			ei.log.Errorf(msgFormatErr, engineIdx)
			mResult = ei.newMntRet(cfg.Scm.MountPoint, scmErr)
		}
	}()

	if ei.IsStarted() {
		if !force {
			scmErr = errors.Errorf("instance %d: can't format storage of running instance",
				engineIdx)
			return
		}

		ei.log.Infof("forcibly stopping instance %d prior to reformat", ei.Index())
		if scmErr = ei.Stop(unix.SIGKILL); scmErr != nil {
			return
		}

		ei.requestStart(ctx)
	}

	mResult, scmErr = ei.scmFormat(force)
	return
}

func addLinkInfoToHealthStats(prov hardware.PCIeLinkStatsProvider, pciCfg string, health *ctlpb.BioHealthResp) error {
	if health == nil {
		return errors.New("nil BioHealthResp")
	}

	// Convert byte-string to lspci-format.
	sb := new(strings.Builder)
	formatBytestring(pciCfg, sb)

	pciDev := &hardware.PCIDevice{}
	if err := prov.PCIeCapsFromConfig([]byte(sb.String()), pciDev); err != nil {
		return errors.Wrap(err, "pciutils lib")
	}

	// Copy link details from PCIDevice to health stats.
	health.LinkPortId = uint32(pciDev.LinkPortID)
	health.LinkMaxSpeed = pciDev.LinkMaxSpeed
	health.LinkMaxWidth = uint32(pciDev.LinkMaxWidth)
	health.LinkNegSpeed = pciDev.LinkNegSpeed
	health.LinkNegWidth = uint32(pciDev.LinkNegWidth)

	return nil
}

// Only raise events if speed or width state is:
// - Currently at maximum but was previously downgraded
// - Currently downgraded but is now at maximum
// - Currently downgraded and was previously at a different downgraded speed
func checkPublishEvent(engine Engine, id events.RASID, typ, pciAddr, maximum, negotiated, lastMax, lastNeg string, port uint32) {

	// Return early if previous and current stats are both in the expected state.
	if lastNeg == lastMax && negotiated == maximum {
		return
	}

	// Return if stats have not changed since when last seen.
	if negotiated == lastNeg && maximum == lastMax {
		return
	}

	// Otherwise publish event indicating link state change.

	engine.Debugf("link %s changed on %s, was %s (max %s) now %s (max %s)",
		typ, pciAddr, lastNeg, lastMax, negotiated, maximum)
	msg := fmt.Sprintf("NVMe PCIe device at %q port-%d: link %s changed to %s "+
		"(max %s)", pciAddr, port, typ, negotiated, maximum)

	sev := events.RASSeverityWarning
	if negotiated == maximum {
		sev = events.RASSeverityNotice
	}

	engine.Publish(events.NewGenericEvent(id, sev, msg, ""))
}

// Evaluate PCIe link state on NVMe SSD and raise events when negotiated speed or width changes in
// relation to last recorded stats for the given PCI address.
func publishLinkStatEvents(engine Engine, pciAddr string, stats *ctlpb.BioHealthResp) {
	lastStats := engine.GetLastHealthStats(pciAddr)
	engine.SetLastHealthStats(pciAddr, stats)

	lastMaxSpeedStr, lastSpeedStr, lastMaxWidthStr, lastWidthStr := "-", "-", "-", "-"
	if lastStats != nil {
		lastMaxSpeedStr = humanize.SI(float64(lastStats.LinkMaxSpeed), "T/s")
		lastSpeedStr = humanize.SI(float64(lastStats.LinkNegSpeed), "T/s")
		lastMaxWidthStr = fmt.Sprintf("x%d", lastStats.LinkMaxWidth)
		lastWidthStr = fmt.Sprintf("x%d", lastStats.LinkNegWidth)
	}

	checkPublishEvent(engine, events.RASNVMeLinkSpeedChanged, "speed", pciAddr,
		humanize.SI(float64(stats.LinkMaxSpeed), "T/s"),
		humanize.SI(float64(stats.LinkNegSpeed), "T/s"),
		lastMaxSpeedStr, lastSpeedStr, stats.LinkPortId)

	checkPublishEvent(engine, events.RASNVMeLinkWidthChanged, "width", pciAddr,
		fmt.Sprintf("x%d", stats.LinkMaxWidth),
		fmt.Sprintf("x%d", stats.LinkNegWidth),
		lastMaxWidthStr, lastWidthStr, stats.LinkPortId)
}

type ctrlrHealthReq struct {
	meta          bool
	engine        Engine
	bhReq         *ctlpb.BioHealthReq
	ctrlr         *ctlpb.NvmeController
	linkStatsProv hardware.PCIeLinkStatsProvider
}

// Retrieve NVMe controller health statistics for those in an acceptable state. Return nil health
// resp if in a bad state.
func getCtrlrHealth(ctx context.Context, req ctrlrHealthReq) (*ctlpb.BioHealthResp, error) {
	stateName := ctlpb.NvmeDevState_name[int32(req.ctrlr.DevState)]
	if !req.ctrlr.CanSupplyHealthStats() {
		req.engine.Debugf("skip fetching health stats on device %q in %q state",
			req.ctrlr.PciAddr, stateName)
		return nil, errCtrlrHealthSkipped
	}

	health, err := scanHealth(ctx, req.engine, req.bhReq)
	if err != nil {
		return nil, errors.Wrapf(err, "retrieve health stats for %q (state %q)", req.ctrlr,
			stateName)
	}

	return health, nil
}

// Add link state and capability information to input health statistics for the given controller
// then if successful publish events based on link statistic changes. Link updated health stats to
// controller.
func setCtrlrHealthWithLinkInfo(req ctrlrHealthReq, health *ctlpb.BioHealthResp) error {
	err := addLinkInfoToHealthStats(req.linkStatsProv, req.ctrlr.PciCfg, health)
	if err == nil {
		publishLinkStatEvents(req.engine, req.ctrlr.PciAddr, health)
	} else {
		if errors.Cause(err) != pciutils.ErrNoPCIeCaps {
			return errors.Wrapf(err, "add link stats for %q", req.ctrlr)
		}
		req.engine.Debugf("device %q not reporting PCIe capabilities", req.ctrlr.PciAddr)
	}

	return nil
}

// Update controller health statistics and include link info if required and available.
func populateCtrlrHealth(ctx context.Context, req ctrlrHealthReq) (bool, error) {
	health, err := getCtrlrHealth(ctx, req)
	if err != nil {
		if err == errCtrlrHealthSkipped {
			// Nothing to do.
			return false, nil
		}
		return false, errors.Wrap(err, "get ctrlr health")
	}

	if req.linkStatsProv == nil {
		req.engine.Debugf("device %q skip adding link stats; nil provider",
			req.ctrlr.PciAddr)
	} else if req.ctrlr.PciCfg == "" {
		req.engine.Debugf("device %q skip adding link stats; empty pci cfg",
			req.ctrlr.PciAddr)
	} else {
		if err = setCtrlrHealthWithLinkInfo(req, health); err != nil {
			return false, errors.Wrap(err, "set ctrlr health")
		}
	}

	req.ctrlr.HealthStats = health
	return true, nil
}

// Scan SMD devices over dRPC and reconstruct NVMe scan response from results.
func scanEngineBdevsOverDrpc(ctx context.Context, engine Engine, pbReq *ctlpb.ScanNvmeReq) (*ctlpb.ScanNvmeResp, error) {
	scanSmdResp, err := scanSmd(ctx, engine, &ctlpb.SmdDevReq{})
	if err != nil {
		return nil, errors.Wrap(err, "scan smd")
	}
	if scanSmdResp == nil {
		return nil, errors.New("nil smd scan resp")
	}
	engine.Tracef("smd scan devices: %+v", scanSmdResp.Devices)

	// Re-link SMD devices inside NVMe controller structures and populate scan response.

	pbResp := ctlpb.ScanNvmeResp{
		State: new(ctlpb.ResponseState),
	}
	seenCtrlrs := make(map[string]*ctlpb.NvmeController)

	for i, sd := range scanSmdResp.Devices {
		if sd.Ctrlr == nil {
			return nil, errors.Errorf("smd %q has no ctrlr ref", sd.Uuid)
		}

		addr := sd.Ctrlr.PciAddr
		if addr == "" {
			// Mock identifier for emulated NVMe mode where devices have no PCI-address.
			// Allows for 256 unique identifiers per-host and formatted string template
			// ensures no collisions with real device addresses. Note that this mock
			// identifier address is not used outside of this loop and is only used for
			// the purpose of mapping SMD records to NVMe (emulated) device details.
			addr = fmt.Sprintf("FFFF:00:%X.F", i)
		}

		if _, exists := seenCtrlrs[addr]; !exists {
			c := new(ctlpb.NvmeController)
			*c = *sd.Ctrlr
			c.SmdDevices = nil
			c.HealthStats = nil
			seenCtrlrs[addr] = c
		}

		c := seenCtrlrs[addr]

		engineRank, err := engine.GetRank()
		if err != nil {
			return nil, errors.Wrapf(err, "instance %d GetRank", engine.Index())
		}

		// Only provide minimal info in standard scan to enable result aggregation across
		// homogeneous hosts.
		nsd := &ctlpb.SmdDevice{
			RoleBits:         sd.RoleBits,
			CtrlrNamespaceId: sd.CtrlrNamespaceId,
			Rank:             engineRank.Uint32(),
		}

		if !sd.Ctrlr.IsScannable() {
			engine.Debugf("smd %q partial update of ctrlr %+v with bad state",
				sd.Uuid, sd.Ctrlr)
			continue
		}

		// Populate health if requested.
		healthUpdated := false
		if pbReq.Health && c.HealthStats == nil {
			bhReq := &ctlpb.BioHealthReq{DevUuid: sd.Uuid}
			if pbReq.Meta {
				bhReq.MetaSize = pbReq.MetaSize
				bhReq.RdbSize = pbReq.RdbSize
			}

			chReq := ctrlrHealthReq{
				engine: engine,
				bhReq:  bhReq,
				ctrlr:  c,
			}
			if pbReq.LinkStats {
				// Add link stats to health if flag set.
				chReq.linkStatsProv = linkStatsProv
			}

			healthUpdated, err = populateCtrlrHealth(ctx, chReq)
			if err != nil {
				return nil, err
			}
		}
		// Used to update health with link stats, now redundant.
		c.PciCfg = ""

		// Populate usage data if requested.
		if pbReq.Meta {
			*nsd = *sd
			nsd.Ctrlr = nil
			nsd.Rank = engineRank.Uint32()
			nsd.MetaSize = pbReq.MetaSize
			nsd.RdbSize = pbReq.RdbSize
			if healthUpdated {
				// Populate space usage for each SMD device from health stats.
				nsd.TotalBytes = c.HealthStats.TotalBytes
				nsd.AvailBytes = c.HealthStats.AvailBytes
				nsd.ClusterSize = c.HealthStats.ClusterSize
				nsd.MetaWalSize = c.HealthStats.MetaWalSize
				nsd.RdbWalSize = c.HealthStats.RdbWalSize
			}
		}

		c.SmdDevices = append(c.SmdDevices, nsd)
	}

	var keys []string
	for k := range seenCtrlrs {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	for _, k := range keys {
		c := seenCtrlrs[k]
		engine.Tracef("bdev discovered: %+v", c)
		pbResp.Ctrlrs = append(pbResp.Ctrlrs, c)
	}

	return &pbResp, nil
}

func bdevScanEngineAssigned(ctx context.Context, engine Engine, req *ctlpb.ScanNvmeReq, bdevCfgs storage.TierConfigs, isStarted *bool) (*ctlpb.ScanNvmeResp, error) {
	*isStarted = engine.IsStarted()
	if !*isStarted {
		engine.Debugf("scanning engine-%d bdevs while engine is down", engine.Index())
		if req.Meta {
			return nil, errors.New("meta smd usage info unavailable as engine stopped")
		}

		return bdevScanToProtoResp(engine.GetStorage().ScanBdevs, bdevCfgs)
	}

	engine.Debugf("scanning engine-%d bdevs while engine is up", engine.Index())

	// If engine is started but not ready, wait for ready state.
	pollFn := func(e Engine) bool { return e.IsReady() }
	if err := pollInstanceState(ctx, []Engine{engine}, pollFn); err != nil {
		return nil, errors.Wrapf(err, "waiting for engine %d to be ready to receive drpcs",
			engine.Index())
	}

	return scanEngineBdevsOverDrpc(ctx, engine, req)
}

// Accommodate for VMD backing devices and emulated NVMe (AIO).
func getEffCtrlrCount(ctrlrs []*ctlpb.NvmeController) (int, error) {
	pas := hardware.MustNewPCIAddressSet()
	for _, c := range ctrlrs {
		if err := pas.AddStrings(c.PciAddr); err != nil {
			return 0, err
		}
	}
	if pas.HasVMD() {
		if npas, err := pas.BackingToVMDAddresses(); err != nil {
			return 0, err
		} else {
			return npas.Len(), nil
		}
	}

	// Return inputted number of controllers rather than number of parsed addresses to cater for
	// the case of emulated NVMe where there will be no valid PCI address.
	return len(ctrlrs), nil
}

// bdevScanEngine calls either in to the private engine storage provider to scan bdevs if engine process
// is not started, otherwise dRPC is used to retrieve details from the online engine.
func bdevScanEngine(ctx context.Context, engine Engine, req *ctlpb.ScanNvmeReq) (*ctlpb.ScanNvmeResp, error) {
	if req == nil {
		return nil, errors.New("nil request")
	}

	bdevCfgs := storage.TierConfigs(engine.GetStorage().GetBdevConfigs())
	nrCfgBdevs := bdevCfgs.Bdevs().Len()

	if nrCfgBdevs == 0 {
		return nil, errEngineBdevScanEmptyDevList
	}

	var isStarted bool
	resp, err := bdevScanEngineAssigned(ctx, engine, req, bdevCfgs, &isStarted)
	if err != nil {
		return nil, err
	}

	// Compare number of VMD domain addresses rather than the number of backing devices found
	// behind it as the domain is what is specified in the server config file.
	nrBdevs, err := getEffCtrlrCount(resp.Ctrlrs)
	if err != nil {
		return nil, err
	}

	// Retry once if engine provider scan returns unexpected number of controllers in case
	// engines claimed devices between when started state was checked and scan was executed.
	if nrBdevs != nrCfgBdevs && !isStarted {
		engine.Debugf("retrying engine bdev scan as unexpected nr returned, want %d got %d",
			nrCfgBdevs, nrBdevs)

		resp, err = bdevScanEngineAssigned(ctx, engine, req, bdevCfgs, &isStarted)
		if err != nil {
			return nil, err
		}

		nrBdevs, err = getEffCtrlrCount(resp.Ctrlrs)
		if err != nil {
			return nil, err
		}
	}

	if nrBdevs != nrCfgBdevs {
		engine.Debugf("engine bdev scan returned unexpected nr, want %d got %d",
			nrCfgBdevs, nrBdevs)
	}

	// Filter devices in an unusable state from the response.
	outCtrlrs := make([]*ctlpb.NvmeController, 0, len(resp.Ctrlrs))
	for _, c := range resp.Ctrlrs {
		if c.IsScannable() {
			outCtrlrs = append(outCtrlrs, c)
		} else {
			engine.Tracef("excluding bdev from scan results: %+v", c)
		}
	}
	resp.Ctrlrs = outCtrlrs

	return resp, nil
}

func smdQueryEngine(ctx context.Context, engine Engine, pbReq *ctlpb.SmdQueryReq) (*ctlpb.SmdQueryResp_RankResp, error) {
	engineRank, err := engine.GetRank()
	if err != nil {
		return nil, errors.Wrapf(err, "instance %d GetRank", engine.Index())
	}

	scanSmdResp, err := scanSmd(ctx, engine, &ctlpb.SmdDevReq{})
	if err != nil {
		return nil, errors.Wrapf(err, "rank %d: scan smd", engineRank)
	}
	if scanSmdResp == nil {
		return nil, errors.Errorf("rank %d: nil scan smd response", engineRank)
	}

	rResp := new(ctlpb.SmdQueryResp_RankResp)
	rResp.Rank = engineRank.Uint32()
	if len(scanSmdResp.Devices) == 0 {
		rResp.Devices = nil
		return rResp, nil
	}

	for _, sd := range scanSmdResp.Devices {
		if sd != nil {
			rResp.Devices = append(rResp.Devices, sd)
		}
	}

	found := false
	for _, dev := range rResp.Devices {
		if pbReq.Uuid != "" && dev.Uuid != pbReq.Uuid {
			continue // Skip health query if UUID doesn't match requested.
		}
		if pbReq.IncludeBioHealth {
			chReq := ctrlrHealthReq{
				engine:        engine,
				bhReq:         &ctlpb.BioHealthReq{DevUuid: dev.Uuid},
				ctrlr:         dev.Ctrlr,
				linkStatsProv: linkStatsProv,
			}

			if _, err = populateCtrlrHealth(ctx, chReq); err != nil {
				return nil, err
			}
		}
		// Used to update health with link stats, now redundant.
		dev.Ctrlr.PciCfg = ""

		if pbReq.Uuid != "" && dev.Uuid == pbReq.Uuid {
			rResp.Devices = []*ctlpb.SmdDevice{dev}
			found = true
			break
		}
	}
	if pbReq.Uuid != "" && !found {
		rResp.Devices = nil
	}

	return rResp, nil
}
