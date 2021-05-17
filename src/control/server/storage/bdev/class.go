//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"bytes"
	"os"
	"path/filepath"
	"syscall"
	"text/template"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	// OutConfName is the name of the output configuration file to be
	// consumed by the bdev provider backend.
	OutConfName     = "daos_nvme.conf"
	clsNvmeTemplate = `[Nvme]
{{ $host := .Hostname }}{{ range $i, $e := .DeviceList }}    TransportID "trtype:PCIe traddr:{{$e}}" Nvme_{{$host}}_{{$i}}
{{ end }}    RetryCount 4
    TimeoutUsec 0
    ActionOnTimeout None
    AdminPollRate 100000
    HotplugEnable No
    HotplugPollRate 0
`
	// device block size hardcoded to 4096
	clsFileTemplate = `[AIO]
{{ $host := .Hostname }}{{ range $i, $e := .DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}} 4096
{{ end }}`
	clsKdevTemplate = `[AIO]
{{ $host := .Hostname }}{{ range $i, $e := .DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}}
{{ end }}`
	clsMallocTemplate = `[Malloc]
    NumberOfLuns {{.DeviceCount}}
    LunSizeInMB {{.FileSize}}000
`
	gbyte   = 1000000000
	blkSize = 4096
)

func createEmptyFile(log logging.Logger, path string, size int64) error {
	if !filepath.IsAbs(path) {
		return errors.Errorf("please specify absolute path (%s)", path)
	}

	if _, err := os.Stat(path); !os.IsNotExist(err) {
		return err
	}

	log.Debugf("allocating new file %s of size %s", path,
		humanize.Bytes(uint64(size)))
	file, err := common.TruncFile(path)
	if err != nil {
		return err
	}
	defer file.Close()

	if err := syscall.Fallocate(int(file.Fd()), 0, 0, size); err != nil {
		e, ok := err.(syscall.Errno)
		if ok && (e == syscall.ENOSYS || e == syscall.EOPNOTSUPP) {
			log.Debugf("warning: Fallocate not supported, attempting Truncate: ", e)

			if err := file.Truncate(size); err != nil {
				return err
			}
		} else {
			return err
		}
	}

	return nil
}

// clsFileInit truncates or creates files for SPDK AIO emulation.
func clsFileInit(log logging.Logger, c *storage.BdevConfig) error {
	// requested size aligned with block size
	size := (int64(c.FileSize*gbyte) / int64(blkSize)) * int64(blkSize)

	for _, path := range c.DeviceList {
		err := createEmptyFile(log, path, size)
		if err != nil {
			return err
		}
	}

	return nil
}

// renderTemplate takes NVMe device PCI addresses and generates config content
// (output as string) from template.
func renderTemplate(cfg *storage.BdevConfig, templ string) (out bytes.Buffer, err error) {
	t := template.Must(template.New(OutConfName).Parse(templ))
	err = t.Execute(&out, cfg)

	return
}

func writeConfig(templ string, cfg *storage.BdevConfig) error {
	confBytes, err := renderTemplate(cfg, templ)
	if err != nil {
		return err
	}

	if confBytes.Len() == 0 {
		return errors.New("spdk: generated nvme config is unexpectedly empty")
	}

	f, err := os.Create(cfg.OutputPath)
	if err != nil {
		return errors.Wrapf(err, "bdev create output config file")
	}

	defer func() {
		ce := f.Close()
		if err == nil {
			err = ce
		}
	}()

	if _, err := confBytes.WriteTo(f); err != nil {
		return errors.Wrapf(err, "bdev write to %q", cfg.OutputPath)
	}

	return nil
}

// GenConfigFile generates nvme config file for given bdev type to be consumed
// by backend.
func (p *Provider) GenConfigFile(cfg *storage.BdevConfig) error {
	if cfg.OutputPath == "" {
		p.log.Debug("skip bdev conf file generation as no path set")

		return nil
	}

	// special case init for class aio-file
	if cfg.Class == storage.BdevClassFile {
		if err := clsFileInit(p.log, cfg); err != nil {
			return err
		}
	}

	templ := map[storage.BdevClass]string{
		storage.BdevClassNone:   clsNvmeTemplate,
		storage.BdevClassNvme:   clsNvmeTemplate,
		storage.BdevClassMalloc: clsMallocTemplate,
		storage.BdevClassKdev:   clsKdevTemplate,
		storage.BdevClassFile:   clsFileTemplate,
	}[cfg.Class]

	// special case template edit for class nvme
	if !cfg.VmdDisabled {
		templ = `[Vmd]
    Enable True

` + templ
	}

	cfg.VosEnv = map[storage.BdevClass]string{
		storage.BdevClassNone:   "",
		storage.BdevClassNvme:   "NVME",
		storage.BdevClassMalloc: "MALLOC",
		storage.BdevClassKdev:   "AIO",
		storage.BdevClassFile:   "AIO",
	}[cfg.Class]

	p.log.Debugf("write %q with %v bdevs", cfg.OutputPath, cfg.DeviceList)
	return writeConfig(templ, cfg)
}
