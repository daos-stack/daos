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

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/storage"
)

const (
	// device block size hardcoded to 4096
	aioBlockSize = humanize.KiByte * 4
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
	size = (size / aioBlockSize) * aioBlockSize

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

func writeJsonConfig(log logging.Logger, enableVmd bool, req *FormatRequest) error {
	nsc, err := newSpdkConfig(log, enableVmd, req)
	if err != nil {
		return err
	}

	buf, err := json.MarshalIndent(nsc, "", "  ")
	if err != nil {
		return err
	}

	if err := writeConfFile(log, bytes.NewBuffer(buf), req); err != nil {
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

	sb.log.Debugf("write nvme output json config: %+v", req)
	return writeJsonConfig(sb.log, !sb.IsVMDDisabled(), req)
}
