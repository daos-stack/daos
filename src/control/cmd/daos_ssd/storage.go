//
// (C) Copyright 2020-2022 Intel Corporation.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
// The Government's rights to use, modify, reproduce, release, perform, display,
// or disclose this software are subject to the terms of the Apache License as
// provided in Contract No. 8F-30005.
// Any reproduction of computer software, computer software documentation, or
// portions thereof marked with this legend must also reproduce the markings.
//

package main

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/server/config"
	"github.com/daos-stack/daos/src/control/server/storage"
	"github.com/daos-stack/daos/src/control/server/storage/bdev"
)

type formatSSDCmd struct {
	logCmd
	cfgCmd
	PCIAddrs []string `short:"p" long:"pci-addr" description:"Format specified PCI Address(es) (formats all in config by default)"`
}

func cfgBdevs(cfg *config.Server) (bdevList []string) {
	for _, e := range cfg.Engines {
		for _, tCfg := range e.Storage.Tiers.BdevConfigs() {
			bdevList = append(bdevList, tCfg.Bdev.DeviceList.Strings()...)
		}
	}

	return
}

func writeNvmeConfigs(bp storage.BdevProvider, sc *config.Server) error {
	if len(sc.Engines) == 0 {
		return errors.New("no engines in config")
	}

	hn, err := os.Hostname()
	if err != nil {
		return errors.Wrap(err, "failed to get hostname")
	}

	for _, ec := range sc.Engines {
		scmMount := ec.Storage.Tiers.ScmConfigs()[0].Scm.MountPoint
		cfgPath := filepath.Join(scmMount, storage.BdevOutConfName)
		req := storage.BdevWriteConfigRequest{
			ConfigOutputPath: cfgPath,
			OwnerUID:         os.Geteuid(),
			OwnerGID:         os.Getegid(),
			Hostname:         hn,
		}

		bdevTiers := ec.Storage.Tiers.BdevConfigs()
		for _, tier := range bdevTiers {
			tierProps := storage.BdevTierPropertiesFromConfig(tier)
			req.TierProps = append(req.TierProps, tierProps)
		}

		_, err = bp.WriteConfig(req)
		if err != nil {
			return err
		}
	}

	return nil
}

func (cmd *formatSSDCmd) Execute(_ []string) error {
	bp := bdev.DefaultProvider(cmd.log)

	formatList := cmd.PCIAddrs
	if len(cmd.PCIAddrs) == 0 {
		if err := cmd.loadConfig(); err != nil {
			return errors.Wrapf(err, "failed to load config from %s", cmd.configPath())
		}
		cmd.log.Infof("DAOS Server config loaded from %s", cmd.configPath())

		formatList = cfgBdevs(cmd.config)
	}

	if len(formatList) == 0 {
		return errors.New("no devices specified in format")
	}

	cmd.log.Infof("device(s) to format: %s", strings.Join(formatList, ", "))

	if !common.GetConsent(cmd.log) {
		return errors.New("try again and respond yes if you want to format")
	}

	fReq := storage.BdevFormatRequest{
		Properties: storage.BdevTierProperties{
			Class:      storage.ClassNvme,
			DeviceList: storage.MustNewBdevDeviceList(formatList...),
		},
	}
	fReq.Forwarded = true // hack; don't try to forward to daos_admin

	fResp, err := bp.Format(fReq)
	if err != nil {
		return err
	}

	var results strings.Builder
	for dev, resp := range fResp.DeviceResponses {
		var status string
		if resp.Formatted {
			status = "success"
		} else {
			status = resp.Error.Error()
		}
		fmt.Fprintf(&results, "  %s: %s\n", dev, status)
	}

	if err := writeNvmeConfigs(bp, cmd.config); err != nil {
		return errors.Wrap(err, "failed to write NVMe configs")
	}

	cmd.log.Info(results.String())

	return nil
}
