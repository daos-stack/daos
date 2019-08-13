package main

import (
	"fmt"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/server"
)

type storageCmd struct {
	Scan     storageScanCmd     `command:"scan" description:"Scan SCM and NVMe storage attached to local server"`
	PrepNvme storagePrepNvmeCmd `command:"prep-nvme" description:"Prep NVMe devices for use with SPDK as current user"`
	PrepScm  storagePrepScmCmd  `command:"prep-scm" description:"Prep SCM modules into interleaved AppDirect and create the relevant namespace kernel devices"`
}

type storageScanCmd struct {
	cfgCmd
	logCmd
}

func (cmd *storageScanCmd) Execute(args []string) error {
	srv, err := server.NewControlService(cmd.config)
	if err != nil {
		return errors.WithMessage(err, "failed to init ControlService")
	}

	cmd.log.Info("Scanning locally-attached storage...")
	scanErrors := make([]error, 0, 2)
	controllers, err := srv.ScanNVMe()
	if err != nil {
		scanErrors = append(scanErrors, err)
	} else {
		str, err := common.StructsToString(controllers)
		if err != nil {
			scanErrors = append(scanErrors, err)
		} else {
			cmd.log.Infof("NVMe: %s", str)
		}
	}

	modules, err := srv.ScanSCM()
	if err != nil {
		scanErrors = append(scanErrors, err)
	} else {
		str, err := common.StructsToString(modules)
		if err != nil {
			scanErrors = append(scanErrors, err)
		} else {
			cmd.log.Infof("SCM: %s", str)
		}
	}

	if len(scanErrors) > 0 {
		errStr := "scan error(s):\n"
		for _, err := range scanErrors {
			errStr += fmt.Sprintf("  %s\n", err.Error())
		}
		return errors.New(errStr)
	}

	return nil
}

type storagePrepNvmeCmd struct {
	cfgCmd
	PCIWhiteList string `short:"w" long:"pci-whitelist" description:"Whitespace separated list of PCI devices (by address) to be unbound from Kernel driver and used with SPDK (default is all PCI devices)."`
	NrHugepages  int    `short:"p" long:"hugepages" description:"Number of hugepages to allocate (in MB) for use by SPDK (default 1024)"`
	TargetUser   string `short:"u" long:"target-user" description:"User that will own hugepage mountpoint directory and vfio groups."`
	Reset        bool   `short:"r" long:"reset" description:"Reset SPDK returning devices to kernel modules"`
}

func (cmd *storagePrepNvmeCmd) Execute(args []string) error {
	ok, usr := common.CheckSudo()
	if !ok {
		return errors.New("subcommand must be run as root or sudo")
	}

	// falls back to sudoer or root if TargetUser is unspecified
	tUsr := usr
	if cmd.TargetUser != "" {
		tUsr = cmd.TargetUser
	}

	srv, err := server.NewControlService(cmd.config)
	if err != nil {
		return errors.WithMessage(err, "initialising ControlService")
	}

	return srv.PrepNvme(server.PrepNvmeRequest{
		HugePageCount: cmd.NrHugepages,
		TargetUser:    tUsr,
		PCIWhitelist:  cmd.PCIWhiteList,
		ResetOnly:     cmd.Reset,
	})
}

type storagePrepScmCmd struct {
	cfgCmd
	logCmd
	Reset bool `short:"r" long:"reset" description:"Reset modules to memory mode after removing namespaces"`
}

func (cmd *storagePrepScmCmd) Execute(args []string) error {
	ok, _ := common.CheckSudo()
	if !ok {
		return errors.New("subcommand must be run as root or sudo")
	}

	srv, err := server.NewControlService(cmd.config)
	if err != nil {
		return errors.WithMessage(err, "initialising ControlService")
	}

	rebootStr, devices, err := srv.PrepScm(server.PrepScmRequest{
		Reset: cmd.Reset,
	})
	if err != nil {
		return err
	}

	// Nothing to do after reset
	if cmd.Reset {
		return nil
	}

	if rebootStr != "" {
		cmd.log.Info(rebootStr)
	} else {
		cmd.log.Infof("persistent memory kernel devices:\n\t%+v\n", devices)
	}

	return nil
}
