//
// (C) Copyright 2021-2024 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package bdev

import (
	"bytes"
	"encoding/json"
	"io"
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
	aioBlockSize       = humanize.KiByte * 4 // device block size hardcoded to 4096 bytes
	defaultAioFileMode = 0600                // AIO file permissions set to owner +rw
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

	log.Debugf("allocating blank file %s of size %s", path, humanize.IBytes(size))
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

func createAioFile(log logging.Logger, path string, req *storage.BdevFormatRequest) (devResp *storage.BdevDeviceFormatResponse) {
	devResp = new(storage.BdevDeviceFormatResponse)
	defer func() {
		if devResp.Error != nil {
			os.Remove(path)
		}
	}()

	if err := createEmptyFile(log, path, req.Properties.DeviceFileSize); err != nil {
		devResp.Error = FaultFormatError(path, err)
		return
	}
	if err := os.Chown(path, req.OwnerUID, req.OwnerGID); err != nil {
		devResp.Error = FaultFormatError(path, errors.Wrapf(err,
			"failed to set ownership of %q to %d.%d", path,
			req.OwnerUID, req.OwnerGID))
		return
	}
	if err := os.Chmod(path, defaultAioFileMode); err != nil {
		devResp.Error = FaultFormatError(path, errors.Wrapf(err,
			"failed to set file mode of %q to %s", path,
			os.FileMode(defaultAioFileMode)))
		return
	}

	return
}

func writeConfigFile(log logging.Logger, buf *bytes.Buffer, req *storage.BdevWriteConfigRequest) error {
	if buf.Len() == 0 {
		return errors.New("generated file is unexpectedly empty")
	}

	f, err := os.Create(req.ConfigOutputPath)
	if err != nil {
		return errors.Wrap(err, "create")
	}

	defer func() {
		if err := f.Close(); err != nil {
			log.Errorf("closing %q: %s", req.ConfigOutputPath, err)
		}
	}()

	if _, err := buf.WriteTo(f); err != nil {
		return errors.Wrap(err, "write")
	}

	return errors.Wrapf(os.Chown(req.ConfigOutputPath, req.OwnerUID, req.OwnerGID),
		"failed to set ownership of %q to %d.%d", req.ConfigOutputPath,
		req.OwnerUID, req.OwnerGID)
}

// writeJsonConfig generates nvme config file for given bdev type to be consumed
// by spdk.
func writeJsonConfig(log logging.Logger, req *storage.BdevWriteConfigRequest) error {
	if req == nil {
		return errors.Errorf("nil %T request", req)
	}
	log.Debugf("writing json nvme conf file from req: %+v", req)

	if len(req.TierProps) == 0 {
		return nil
	}
	if req.ConfigOutputPath == "" {
		return errors.New("no output config directory set in request")
	}
	hasBdevs := false
	for _, tierProp := range req.TierProps {
		if tierProp.Class != storage.ClassNvme || tierProp.DeviceList.Len() > 0 {
			hasBdevs = true
			break
		}
	}
	if !hasBdevs {
		log.Debug("skip write nvme conf for empty device list")
		return nil
	}

	nsc, err := newSpdkConfig(log, req)
	if err != nil {
		return err
	}

	buf, err := json.MarshalIndent(nsc, "", "  ")
	if err != nil {
		return err
	}

	if err := writeConfigFile(log, bytes.NewBuffer(buf), req); err != nil {
		return err
	}

	return nil
}

func strictJsonUnmarshal(r io.Reader, v any) error {
	decoder := json.NewDecoder(r)
	decoder.DisallowUnknownFields()

	return decoder.Decode(v)
}

func strictJsonUnmarshalBuf(data []byte, v any) error {
	return strictJsonUnmarshal(bytes.NewReader(data), v)
}

func readSpdkConfig(r io.Reader) (*SpdkConfig, error) {
	cfg := &SpdkConfig{}
	if err := strictJsonUnmarshal(r, cfg); err != nil {
		return nil, storage.FaultInvalidSPDKConfig(err)
	}

	return cfg, nil
}
