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
	clsEmptyTemplate = ``
	clsNvmeTemplate  = `[Nvme]
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
    LunSizeInMB {{.DeviceFileSize}}
`
	clsFileBlkSize = humanize.KiByte * 4
)

func createEmptyFile(log logging.Logger, path string, size uint64) error {
	if !filepath.IsAbs(path) {
		return errors.Errorf("expected absolute file path but got relative (%s)", path)
	}
	if size == 0 {
		return errors.New("expected non-zero file size")
	}

	if _, err := os.Stat(path); err != nil && !os.IsNotExist(err) {
		return errors.Wrapf(err, "stat %q", path)
	}

	// adjust file size to align with block size
	size = (size / clsFileBlkSize) * clsFileBlkSize

	log.Debugf("allocating blank file %s of size %s", path, humanize.Bytes(size))
	file, err := common.TruncFile(path)
	if err != nil {
		return errors.Wrapf(err, "open %q for truncate", path)
	}
	defer file.Close()

	if err := syscall.Fallocate(int(file.Fd()), 0, 0, int64(size)); err != nil {
		e, ok := err.(syscall.Errno)
		if ok && (e == syscall.ENOSYS || e == syscall.EOPNOTSUPP) {
			log.Debugf("warning: Fallocate not supported, attempting Truncate: ", e)

			return errors.Wrapf(file.Truncate(int64(size)), "truncate %q", path)
		}

		return errors.Wrapf(err, "fallocate %q", path)
	}

	return nil
}

// renderTemplate takes NVMe device PCI addresses and generates config content
// (output as string) from template.
func renderTemplate(req FormatRequest, templ string) (out bytes.Buffer, err error) {
	t := template.Must(template.New(req.ConfigPath).Parse(templ))
	err = t.Execute(&out, req)

	return
}

func writeConf(log logging.Logger, templ string, req FormatRequest) error {
	confBytes, err := renderTemplate(req, templ)
	if err != nil {
		return err
	}

	f, err := os.Create(req.ConfigPath)
	if err != nil {
		return errors.Wrap(err, "create")
	}
	defer func() {
		if err := f.Close(); err != nil {
			log.Errorf("closing %q: %s", req.ConfigPath, err)
		}
	}()

	if confBytes.Len() == 0 {
		return nil
	}

	if _, err := confBytes.WriteTo(f); err != nil {
		return errors.Wrap(err, "write")
	}

	return nil
}

// writeNvmeConf generates nvme config file for given bdev type to be consumed
// by spdk.
func (sb *spdkBackend) writeNvmeConfig(req FormatRequest) error {
	if req.ConfigPath == "" {
		return errors.New("no output config directory set in request")
	}

	if req.Class == storage.BdevClassNvme && len(req.DeviceList) == 0 {
		req.Class = storage.BdevClassNone
	}

	templ := map[storage.BdevClass]string{
		storage.BdevClassNone:   clsEmptyTemplate,
		storage.BdevClassNvme:   clsNvmeTemplate,
		storage.BdevClassMalloc: clsMallocTemplate,
		storage.BdevClassKdev:   clsKdevTemplate,
		storage.BdevClassFile:   clsFileTemplate,
	}[req.Class]

	// spdk ini file expects device size in MBs
	req.DeviceFileSize = req.DeviceFileSize / humanize.MiByte

	// special case template edit for class nvme
	if req.Class == storage.BdevClassNvme && !sb.IsVMDDisabled() {
		templ = `[Vmd]
    Enable True

` + templ
	}

	sb.log.Debugf("write nvme output config: %+v", req)
	return writeConf(sb.log, templ, req)
}
