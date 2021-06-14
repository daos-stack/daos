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
{{ $host := .Hostname }}{{ $tier := .Tier }}{{ range $i, $e := .DeviceList }}    TransportID "trtype:PCIe traddr:{{$e}}" Nvme_{{$host}}_{{$i}}_{{$tier}}
{{ end }}    RetryCount 4
    TimeoutUsec 0
    ActionOnTimeout None
    AdminPollRate 100000
    HotplugEnable No
    HotplugPollRate 0
`
	// device block size hardcoded to 4096
	clsFileTemplate = `[AIO]
{{ $host := .Hostname }}{{ $tier := .Tier }}{{ range $i, $e := .DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}}_{{$tier}} 4096
{{ end }}`
	clsKdevTemplate = `[AIO]
{{ $host := .Hostname }}{{ $tier := .Tier }}{{ range $i, $e := .DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}}_{{$tier}}
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
func renderTemplate(req *storage.BdevTierProperties, templ string) (out bytes.Buffer, err error) {
	t := template.Must(template.New(req.Class.String()).Parse(templ))
	err = t.Execute(&out, req)

	return
}

func appendConf(log logging.Logger, templ string, tier *storage.BdevTierProperties, f *os.File) error {
	confBytes, err := renderTemplate(tier, templ)
	if err != nil {
		return err
	}

	if confBytes.Len() == 0 {
		return errors.New("generated file is unexpectedly empty")
	}

	if _, err := confBytes.WriteTo(f); err != nil {
		return errors.Wrap(err, "write")
	}
	return nil
}

// writeNvmeConf generates nvme config file for given bdev type to be consumed
// by spdk.
func (sb *spdkBackend) writeNvmeConfig(req *storage.BdevWriteNvmeConfigRequest) error {
	if len(req.TierProps) == 0 {
		return nil
	}
	if req.ConfigOutputPath == "" {
		return errors.New("no output config directory set in request")
	}

	f, err := os.Create(req.ConfigOutputPath)
	if err != nil {
		return errors.Wrap(err, "create")
	}

	defer func() {
		if err := f.Close(); err != nil {
			sb.log.Errorf("closing %q: %s", req.ConfigOutputPath, err)
		}
	}()

	for _, tier := range req.TierProps {
		if tier.Class == storage.ClassNvme && !sb.IsVMDDisabled() {
			templ := `[Vmd]
    Enable True

`
			if _, err := f.WriteString(templ); err != nil {
				return errors.Wrap(err, "write")
			}
			break
		}
	}

	for _, tier := range req.TierProps {
		templ := map[storage.Class]string{
			storage.ClassNvme: clsNvmeTemplate,
			storage.ClassKdev: clsKdevTemplate,
			storage.ClassFile: clsFileTemplate,
		}[tier.Class]

		// special handling for class nvme
		if tier.Class == storage.ClassNvme {
			if len(tier.DeviceList) == 0 {
				sb.log.Debug("skip write nvme conf for empty device list")
				continue
			}
		}

		// spdk ini file expects device size in MBs
		tier.DeviceFileSize = tier.DeviceFileSize / humanize.MiByte

		sb.log.Debugf("write nvme output config: %+v", req)
		if err := appendConf(sb.log, templ, &tier, f); err != nil {
			return err
		}
	}

	return errors.Wrapf(os.Chown(req.ConfigOutputPath, req.OwnerUID, req.OwnerGID),
		"failed to set ownership of %q to %d.%d", req.ConfigOutputPath,
		req.OwnerUID, req.OwnerGID)
}
