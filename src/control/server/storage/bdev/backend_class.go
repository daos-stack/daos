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
    LunSizeInMB {{.DeviceFileSize}}000
`
	gbyte   = 1000000000
	blkSize = 4096
)

func createEmptyFile(log logging.Logger, path string, size int64) error {
	if !filepath.IsAbs(path) {
		return errors.Errorf("expected absolute file path but got relative (%s)", path)
	}
	if size == 0 {
		return errors.New("expected non-zero file size")
	}

	if _, err := os.Stat(path); err != nil && !os.IsNotExist(err) {
		return errors.Wrapf(err, "stat %q", path)
	}

	log.Debugf("allocating blank file %s of size %s", path, humanize.Bytes(uint64(size)))
	file, err := common.TruncFile(path)
	if err != nil {
		return errors.Wrapf(err, "open %q for truncate", path)
	}
	defer file.Close()

	if err := syscall.Fallocate(int(file.Fd()), 0, 0, size); err != nil {
		e, ok := err.(syscall.Errno)
		if ok && (e == syscall.ENOSYS || e == syscall.EOPNOTSUPP) {
			log.Debugf("warning: Fallocate not supported, attempting Truncate: ", e)

			return errors.Wrapf(file.Truncate(size), "truncate %q", path)
		}

		return errors.Wrapf(err, "fallocate %q", path)
	}

	return nil
}

// clsFileInit truncates or creates files for SPDK AIO emulation.
func clsFileInit(log logging.Logger, req *FormatRequest) error {
	// requested size aligned with block size
	size := (int64(req.DeviceFileSize*gbyte) / int64(blkSize)) * int64(blkSize)

	for _, path := range req.DeviceList {
		if err := createEmptyFile(log, path, size); err != nil {
			return err
		}
	}

	return nil
}

// renderTemplate takes NVMe device PCI addresses and generates config content
// (output as string) from template.
func renderTemplate(req *FormatRequest, templ string) (out bytes.Buffer, err error) {
	t := template.Must(template.New(req.ConfigPath).Parse(templ))
	err = t.Execute(&out, req)

	return
}

func writeConf(templ string, req *FormatRequest) error {
	confBytes, err := renderTemplate(req, templ)
	if err != nil {
		return err
	}

	if confBytes.Len() == 0 {
		return errors.New("generated file is unexpectedly empty")
	}

	f, err := os.Create(req.ConfigPath)
	if err != nil {
		return errors.Wrap(err, "create")
	}

	defer func() {
		ce := f.Close()
		if err == nil {
			err = ce
		}
	}()

	if _, err := confBytes.WriteTo(f); err != nil {
		return errors.Wrap(err, "write")
	}

	return nil
}

// writeNvmeConf generates nvme config file for given bdev type to be consumed
// by spdk.
func (sb *spdkBackend) writeNvmeConfig(req *FormatRequest) error {
	if req.ConfigPath == "" {
		return errors.New("no output config directory set in request")
	}

	// special case init for class aio-file
	if req.Class == storage.BdevClassFile {
		if err := clsFileInit(sb.log, req); err != nil {
			return err
		}
	}

	templ := map[storage.BdevClass]string{
		storage.BdevClassNone:   clsNvmeTemplate,
		storage.BdevClassNvme:   clsNvmeTemplate,
		storage.BdevClassMalloc: clsMallocTemplate,
		storage.BdevClassKdev:   clsKdevTemplate,
		storage.BdevClassFile:   clsFileTemplate,
	}[req.Class]

	// special case template edit for class nvme
	if req.Class == storage.BdevClassNvme && !sb.IsVMDDisabled() {
		templ = `[Vmd]
    Enable True

` + templ
	}

	sb.log.Debugf("write nvme output config: %+v", req)
	return writeConf(templ, req)
}
