//
// (C) Copyright 2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"bytes"
	"encoding/json"
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
func renderTemplate(req *FormatRequest, templ string) (out bytes.Buffer, err error) {
	t := template.Must(template.New(req.ConfigPath).Parse(templ))
	err = t.Execute(&out, req)

	return
}

func writeConfFile(log logging.Logger, buf *bytes.Buffer, req *FormatRequest) error {
	if buf.Len() == 0 {
		return errors.New("generated file is unexpectedly empty")
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

	if _, err := buf.WriteTo(f); err != nil {
		return errors.Wrap(err, "write")
	}

	return errors.Wrapf(os.Chown(req.ConfigPath, req.OwnerUID, req.OwnerGID),
		"failed to set ownership of %q to %d.%d", req.ConfigPath,
		req.OwnerUID, req.OwnerGID)
}

func writeIniConfig(log logging.Logger, enableVmd bool, req FormatRequest) error {
	templ := map[storage.BdevClass]string{
		storage.BdevClassNvme: clsNvmeTemplate,
		storage.BdevClassKdev: clsKdevTemplate,
		storage.BdevClassFile: clsFileTemplate,
	}[req.Class]

	// special handling for class nvme
	if req.Class == storage.BdevClassNvme && enableVmd {
		templ = `[Vmd]
    Enable True

` + templ
	}

	// spdk ini file expects device size in MBs
	req.DeviceFileSize = req.DeviceFileSize / humanize.MiByte

	buf, err := renderTemplate(&req, templ)
	if err != nil {
		return err
	}
	if err := writeConfFile(log, &buf, &req); err != nil {
		return err
	}

	return nil
}

func writeJsonConfig(log logging.Logger, enableVmd bool, req FormatRequest) error {
	nsc := newNvmeSpdkConfig(req.DeviceList, req.Hostname)

	buf, err := json.MarshalIndent(nsc, "", "  ")
	if err != nil {
		return err
	}

	if err := writeConfFile(log, bytes.NewBuffer(buf), &req); err != nil {
		return err
	}

	return nil
}

// writeNvmeConf generates nvme config file for given bdev type to be consumed
// by spdk.
func (sb *spdkBackend) writeNvmeConfig(req *FormatRequest) error {
	if req.ConfigPath == "" {
		return errors.New("no output config directory set in request")
	}
	if req.Class == storage.BdevClassNvme && len(req.DeviceList) == 0 {
		sb.log.Debug("skip write nvme conf for empty device list")
		return nil
	}

	enableVmd := !sb.IsVMDDisabled()

	sb.log.Debugf("write nvme output ini config: %+v", req)
	// pass request by value to restrict the scope of side effects
	if err := writeIniConfig(sb.log, enableVmd, *req); err != nil {
		return err
	}

	if req.Class != storage.BdevClassNvme {
		// aio json spdk config not supported yet
		sb.log.Info("Skipping JSON SPDK config creation for non-NVMe bdev class")
		return nil
	}
	if enableVmd {
		// vmd support in json spdk config not supported yet
		sb.log.Info("Skipping JSON SPDK config creation for VMD enabled hosts")
		return nil
	}

	req.ConfigPath = req.ConfigPath + ".json"
	sb.log.Debugf("write nvme output json config: %+v", req)

	return writeJsonConfig(sb.log, enableVmd, *req)
}
