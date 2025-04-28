//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package pretty

import (
	"fmt"
	"io"
	"sort"
	"strings"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/lib/control"
	"github.com/daos-stack/daos/src/control/lib/txtfmt"
	"github.com/daos-stack/daos/src/control/server/storage"
)

var (
	errNoMetaRole        = errors.New("no meta role detected")
	errInconsistentRoles = errors.New("roles inconsistent between hosts")
	errInsufficientScan  = errors.New("insufficient info in scan response")
)

// PrintHostStorageUsageMap generates a human-readable representation of the supplied
// HostStorageMap struct and writes utilization info to the supplied io.Writer.
func PrintHostStorageUsageMap(hsm control.HostStorageMap, out io.Writer) {
	if len(hsm) == 0 {
		return
	}

	hostsTitle := "Hosts"
	scmTitle := "SCM-Total"
	scmFreeTitle := "SCM-Free"
	scmUsageTitle := "SCM-Used"
	nvmeTitle := "NVMe-Total"
	nvmeFreeTitle := "NVMe-Free"
	nvmeUsageTitle := "NVMe-Used"

	tablePrint := txtfmt.NewTableFormatter(hostsTitle, scmTitle, scmFreeTitle,
		scmUsageTitle, nvmeTitle, nvmeFreeTitle, nvmeUsageTitle)
	tablePrint.InitWriter(out)
	table := []txtfmt.TableRow{}

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString())
		row := txtfmt.TableRow{hostsTitle: hosts}
		storage := hss.HostStorage

		sns := storage.ScmNamespaces
		row[scmTitle] = humanize.Bytes(sns.Total())
		scmFree := sns.Free()
		row[scmFreeTitle] = humanize.Bytes(scmFree)
		row[scmUsageTitle] = common.PercentageString(sns.Total()-scmFree, sns.Total())

		ncs := storage.NvmeDevices
		row[nvmeTitle] = humanize.Bytes(ncs.Total())
		nvmeFree := ncs.Free()
		row[nvmeFreeTitle] = humanize.Bytes(nvmeFree)
		row[nvmeUsageTitle] = common.PercentageString(ncs.Total()-nvmeFree, ncs.Total())

		table = append(table, row)
	}

	tablePrint.Format(table)
}

const (
	metaRole  = storage.BdevRoleMeta
	dataRole  = storage.BdevRoleData
	rankTitle = "Rank"
)

// Return role combinations for each tier that contains either a meta or data role.
func getTierRolesForHost(nvme storage.NvmeControllers, metaRolesLast, dataRolesLast *storage.BdevRoles) error {
	roles := make(map[int]*storage.BdevRoles)
	for _, c := range nvme {
		if c.Roles().HasMeta() {
			if _, exists := roles[metaRole]; !exists {
				roles[metaRole] = c.Roles()
			}
		} else if c.Roles().HasData() {
			if _, exists := roles[dataRole]; !exists {
				roles[dataRole] = c.Roles()
			}
		}
	}

	if roles[metaRole].IsEmpty() {
		return errNoMetaRole
	}

	if !metaRolesLast.IsEmpty() {
		// Indicates valid "last" values exist so check consistency.
		if *roles[metaRole] != *metaRolesLast {
			return errInconsistentRoles
		}
		if roles[dataRole].IsEmpty() {
			if !dataRolesLast.IsEmpty() {
				return errInconsistentRoles
			}
		} else {
			if *roles[dataRole] != *dataRolesLast {
				return errInconsistentRoles
			}
		}
	} else {
		*metaRolesLast = *roles[metaRole]
		if !roles[dataRole].IsEmpty() {
			*dataRolesLast = *roles[dataRole]
		}
	}

	return nil
}

// Print which roles each tier is assigned, only print tiers with meta or data roles.
// Currently tier-list hardcoded to (META/DATA) but this can be extended.
func printTierRolesTable(hsm control.HostStorageMap, out, dbg io.Writer) ([]storage.BdevRoles, error) {
	tierTitle := "Tier"
	rolesTitle := "Roles"

	tablePrint := txtfmt.NewTableFormatter(tierTitle, rolesTitle)
	tablePrint.InitWriter(out)
	table := []txtfmt.TableRow{}

	// Currently only tiers with meta and data are of interest so select implicitly.
	var metaRoles, dataRoles storage.BdevRoles
	for _, key := range hsm.Keys() {
		err := getTierRolesForHost(hsm[key].HostStorage.NvmeDevices, &metaRoles, &dataRoles)
		if err != nil {
			hSet := hsm[key].HostSet
			fmt.Fprintf(dbg, "scan resp for hosts %q: %+v\n", hSet, hsm[key].HostStorage)
			return nil, errors.Wrapf(err, "hosts %q", hSet)
		}
	}

	if metaRoles.IsEmpty() {
		fmt.Fprintf(dbg, "scan resp: %+v\n", hsm)
		return nil, errInsufficientScan
	}

	rolesToShow := []storage.BdevRoles{metaRoles}
	if !dataRoles.IsEmpty() {
		// Print data role row if assigned to a separate tier from meta role.
		rolesToShow = append(rolesToShow, dataRoles)
	}
	for i, roles := range rolesToShow {
		table = append(table, txtfmt.TableRow{
			// Starting tier index of 1.
			tierTitle:  fmt.Sprintf("T%d", i+1),
			rolesTitle: roles.String(),
		})
	}

	tablePrint.Format(table)
	return rolesToShow, nil
}

func getRowTierTitles(i int, showUsable bool) []string {
	totalTitle := fmt.Sprintf("T%d-Total", i)
	freeTitle := fmt.Sprintf("T%d-Free", i)
	if showUsable {
		freeTitle = fmt.Sprintf("T%d-Usable", i)
	}
	usageTitle := fmt.Sprintf("T%d-Usage", i)

	return []string{totalTitle, freeTitle, usageTitle}
}

type roleDevsMap map[storage.BdevRoles]storage.NvmeControllers
type rankRoleDevsMap map[int]roleDevsMap

func iterRankRoleDevs(nvme storage.NvmeControllers, tierRoles []storage.BdevRoles, dbg io.Writer, rankRoleDevs rankRoleDevsMap) error {
	for _, nd := range nvme {
		if len(nd.SmdDevices) == 0 || nd.SmdDevices[0] == nil {
			fmt.Fprintf(dbg, "no smd for %s\n", nd.PciAddr)
			continue
		}
		rank := int(nd.Rank())
		if _, exists := rankRoleDevs[rank]; !exists {
			rankRoleDevs[rank] = make(roleDevsMap)
		}
		roles := nd.Roles()
		if roles == nil {
			return errors.New("unexpected nil roles")
		}
		for _, rolesWant := range tierRoles {
			if *roles != rolesWant {
				continue
			}
			fmt.Fprintf(dbg, "add r%d-%s roles %q tot/avail/usabl %d/%d/%d\n", rank,
				nd.PciAddr, roles, nd.Total(), nd.Free(), nd.Usable())
			rankRoleDevs[rank][rolesWant] = append(
				rankRoleDevs[rank][rolesWant], nd)
			break
		}
	}

	return nil
}

func getRankRolesRow(rank int, tierRoles []storage.BdevRoles, roleDevs roleDevsMap, showUsable bool) txtfmt.TableRow {
	row := txtfmt.TableRow{rankTitle: fmt.Sprintf("%d", rank)}
	for i, roles := range tierRoles {
		titles := getRowTierTitles(i+1, showUsable)
		totalTitle, freeTitle, usageTitle := titles[0], titles[1], titles[2]
		devs, exists := roleDevs[roles]
		if !exists {
			row[totalTitle] = "-"
			row[freeTitle] = "-"
			row[usageTitle] = "-"
			continue
		}
		row[totalTitle] = humanize.Bytes(devs.Total())
		free := devs.Free()
		// Handle special case where SSDs with META but without DATA should show usable
		// space as bytes available in regards to META space. Usable bytes is only
		// calculated for SSDs with DATA role.
		if showUsable && !(roles.HasMeta() && !roles.HasData()) {
			free = devs.Usable()
		}
		row[freeTitle] = humanize.Bytes(free)
		row[usageTitle] = common.PercentageString(devs.Total()-free, devs.Total())
	}

	return row
}

// Print usage table with row for each rank and column for each tier.
func printTierUsageTable(hsm control.HostStorageMap, tierRoles []storage.BdevRoles, out, dbg io.Writer, showUsable bool) error {
	if len(tierRoles) == 0 {
		return errors.New("no table role data to show")
	}
	titles := []string{rankTitle}
	for i := range tierRoles {
		titles = append(titles, getRowTierTitles(i+1, showUsable)...)
	}

	tablePrint := txtfmt.NewTableFormatter(titles...)
	tablePrint.InitWriter(out)
	table := []txtfmt.TableRow{}

	// Build controllers-to-roles-to-rank map.
	rankRoleDevs := make(rankRoleDevsMap)
	for _, key := range hsm.Keys() {
		err := iterRankRoleDevs(hsm[key].HostStorage.NvmeDevices, tierRoles, dbg,
			rankRoleDevs)
		if err != nil {
			return errors.Wrapf(err, "host %q", hsm[key].HostSet)
		}
	}

	var ranks []int
	for rank := range rankRoleDevs {
		ranks = append(ranks, rank)
	}
	sort.Ints(ranks)

	for _, rank := range ranks {
		table = append(table,
			getRankRolesRow(rank, tierRoles, rankRoleDevs[rank], showUsable))
	}

	tablePrint.Format(table)
	return nil
}

// PrintHostStorageUsageMapMdOnSsd generates a human-readable representation of the supplied
// HostStorageMap struct and writes utilization info to the supplied io.Writer in a format
// relevant to MD-on-SSD mode.
func PrintHostStorageUsageMapMdOnSsd(hsm control.HostStorageMap, out, dbg io.Writer, showUsable bool) error {
	if len(hsm) == 0 {
		return nil
	}

	tierRoles, err := printTierRolesTable(hsm, out, dbg)
	if err != nil {
		return err
	}
	fmt.Fprintf(out, "\n")

	return printTierUsageTable(hsm, tierRoles, out, dbg, showUsable)
}

// NVMe controller namespace ID (NSID) should only be displayed if >= 1. Zero value should be
// ignored in display output.
func printSmdDevice(dev *storage.SmdDevice, iw io.Writer, opts ...PrintConfigOption) error {
	fc := getPrintConfig(opts...)

	trAddr := fmt.Sprintf("TrAddr:%s", dev.Ctrlr.PciAddr)
	nsID := fmt.Sprintf("NSID:%d", dev.CtrlrNamespaceID)
	uid := fmt.Sprintf("UUID:%s", dev.UUID)
	led := fmt.Sprintf("LED:%s", dev.Ctrlr.LedState)

	if fc.LEDInfoOnly {
		out := trAddr
		if dev.CtrlrNamespaceID > 0 {
			out = fmt.Sprintf("%s %s", out, nsID)
		}
		if dev.UUID != "" {
			out = fmt.Sprintf("%s [%s]", out, uid)
		}

		_, err := fmt.Fprintf(iw, "%s %s\n", out, led)
		return err
	}

	out := fmt.Sprintf("%s [%s", uid, trAddr)
	if dev.CtrlrNamespaceID > 0 {
		out = fmt.Sprintf("%s %s", out, nsID)
	}
	if _, err := fmt.Fprintf(iw, "%s]\n", out); err != nil {
		return err
	}

	var hasSysXS string
	if dev.HasSysXS {
		hasSysXS = "SysXS "
	}
	if _, err := fmt.Fprintf(txtfmt.NewIndentWriter(iw),
		"Roles:%s %sTargets:%+v Rank:%d State:%s LED:%s\n", &dev.Roles, hasSysXS,
		dev.TargetIDs, dev.Rank, dev.Ctrlr.NvmeState, dev.Ctrlr.LedState); err != nil {
		return err
	}

	return nil
}

func printSmdPool(pool *control.SmdPool, out io.Writer, opts ...PrintConfigOption) error {
	ew := txtfmt.NewErrWriter(out)
	fmt.Fprintf(ew, "Rank:%d Targets:%+v", pool.Rank, pool.TargetIDs)
	cfg := getPrintConfig(opts...)
	if cfg.Verbose {
		fmt.Fprintf(ew, " Blobs:%+v", pool.Blobs)
	}
	fmt.Fprintln(ew)
	return ew.Err
}

// PrintSmdInfoMap generates a human-readable representation of the supplied
// HostStorageMap, with a focus on presenting the per-server metadata (SMD) information.
func PrintSmdInfoMap(omitDevs, omitPools bool, hsm control.HostStorageMap, out io.Writer, opts ...PrintConfigOption) error {
	w := txtfmt.NewErrWriter(out)

	for _, key := range hsm.Keys() {
		hss := hsm[key]
		hosts := getPrintHosts(hss.HostSet.RangedString(), opts...)
		lineBreak := strings.Repeat("-", len(hosts))
		fmt.Fprintf(out, "%s\n%s\n%s\n", lineBreak, hosts, lineBreak)

		iw := txtfmt.NewIndentWriter(out)
		if hss.HostStorage.SmdInfo == nil {
			fmt.Fprintln(iw, "No SMD info returned")
			continue
		}

		if !omitDevs {
			if len(hss.HostStorage.SmdInfo.Devices) > 0 {
				fmt.Fprintln(iw, "Devices")

				for _, device := range hss.HostStorage.SmdInfo.Devices {
					iw1 := txtfmt.NewIndentWriter(iw)
					if err := printSmdDevice(device, iw1, opts...); err != nil {
						return err
					}
					if device.Ctrlr.HealthStats == nil {
						continue
					}
					if err := printNvmeHealth(device.Ctrlr.HealthStats,
						txtfmt.NewIndentWriter(iw1), opts...); err != nil {
						return err
					}
					fmt.Fprintln(out)
				}
			} else {
				fmt.Fprintln(iw, "No devices found")
			}
		}

		if !omitPools {
			if len(hss.HostStorage.SmdInfo.Pools) > 0 {
				fmt.Fprintln(iw, "Pools")

				for uuid, poolSet := range hss.HostStorage.SmdInfo.Pools {
					iw1 := txtfmt.NewIndentWriter(iw)
					fmt.Fprintf(iw1, "UUID:%s\n", uuid)
					iw2 := txtfmt.NewIndentWriter(iw1)
					for _, pool := range poolSet {
						if err := printSmdPool(pool, iw2, opts...); err != nil {
							return err
						}
					}
					fmt.Fprintln(out)
				}
			} else {
				fmt.Fprintln(iw, "No pools with NVMe found")
			}
		}
	}

	return w.Err
}

// PrintSmdManageResp generates a human-readable representation of the supplied response.
func PrintSmdManageResp(op control.SmdManageOpcode, resp *control.SmdResp, out, outErr io.Writer, opts ...PrintConfigOption) error {
	switch op {
	case control.SetFaultyOp, control.DevReplaceOp:
		if resp.ResultCount() > 1 {
			return errors.Errorf("smd-manage %s: unexpected number of results, "+
				"want %d got %d", op, 1, resp.ResultCount())
		}

		hem := resp.GetHostErrors()
		if len(hem) > 0 {
			for errStr, hostSet := range hem {
				fmt.Fprintln(outErr, fmt.Sprintf("%s operation failed on %s: %s",
					op, hostSet.HostSet, errStr))
			}
			return nil
		}

		return PrintHostStorageSuccesses(fmt.Sprintf("%s operation performed", op),
			resp.HostStorage, out)
	case control.LedCheckOp, control.LedBlinkOp, control.LedResetOp:
		if err := PrintResponseErrors(resp, outErr, opts...); err != nil {
			return err
		}

		return PrintSmdInfoMap(false, true, resp.HostStorage, out, opts...)
	default:
		return errors.Errorf("unsupported opcode %d", op)
	}
}
