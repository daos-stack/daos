//
// (C) Copyright 2018-2019 Intel Corporation.
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

package storage

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"syscall"
	"text/template"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
	. "github.com/daos-stack/daos/src/control/server/storage/config"
)

const (
	confOut   = "daos_nvme.conf"
	nvmeTempl = `[Nvme]
{{ $host := .Hostname }}{{ range $i, $e := .DeviceList }}    TransportID "trtype:PCIe traddr:{{$e}}" Nvme_{{$host}}_{{$i}}
{{ end }}    RetryCount 4
    TimeoutUsec 0
    ActionOnTimeout None
    AdminPollRate 100000
    HotplugEnable No
    HotplugPollRate 0
`
	// device block size hardcoded to 4096
	fileTempl = `[AIO]
{{ $host := .Hostname }}{{ range $i, $e := .DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}} 4096
{{ end }}`
	kdevTempl = `[AIO]
{{ $host := .Hostname }}{{ range $i, $e := .DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}}
{{ end }}`
	mallocTempl = `[Malloc]
    NumberOfLuns {{.DeviceCount}}
    LunSizeInMB {{.FileSize}}000
`
	gbyte   = 1000000000
	blkSize = 4096

	MsgBdevNone    = "in config, no nvme.conf generated for server"
	MsgBdevEmpty   = "bdev device list entry empty"
	MsgBdevBadSize = "backfile_size should be greater than 0"
)

// bdev describes parameters and behaviours for a particular bdev class.
type bdev struct {
	templ   string
	vosEnv  string
	isEmpty func(BdevConfig) string                // check no elements
	isValid func(BdevConfig) string                // check valid elements
	prep    func(logging.Logger, BdevConfig) error // prerequisite actions
}

func nilValidate(c BdevConfig) string { return "" }

func nilPrep(l logging.Logger, c BdevConfig) error { return nil }

func isEmptyList(c BdevConfig) string {
	if len(c.DeviceList) == 0 {
		return "bdev_list empty " + MsgBdevNone
	}

	return ""
}

func isEmptyNumber(c BdevConfig) string {
	if c.DeviceCount == 0 {
		return "bdev_number == 0 " + MsgBdevNone
	}

	return ""
}

func isValidList(c BdevConfig) string {
	for i, elem := range c.DeviceList {
		if elem == "" {
			return fmt.Sprintf("%s (index %d)", MsgBdevEmpty, i)
		}
	}

	return ""
}

func isValidSize(c BdevConfig) string {
	if c.FileSize < 1 {
		return MsgBdevBadSize
	}

	return ""
}

func createEmptyFile(log logging.Logger, path string, size int64) error {
	if !filepath.IsAbs(path) {
		return errors.Errorf("please specify absolute path (%s)", path)
	}

	if _, err := os.Stat(path); !os.IsNotExist(err) {
		return err
	}

	file, err := common.TruncFile(path)
	if err != nil {
		return err
	}
	defer file.Close()

	if err := syscall.Fallocate(int(file.Fd()), 0, 0, size); err != nil {
		e, ok := err.(syscall.Errno)
		if ok && (e == syscall.ENOSYS || e == syscall.EOPNOTSUPP) {
			log.Debugf(
				"Warning: Fallocate not supported, attempting Truncate: ", e)

			if err := file.Truncate(size); err != nil {
				return err
			}
		}
	}

	return nil
}

func prepBdevFile(l logging.Logger, c BdevConfig) error {
	// truncate or create files for SPDK AIO emulation,
	// requested size aligned with block size
	size := (int64(c.FileSize*gbyte) / int64(blkSize)) * int64(blkSize)

	for _, path := range c.DeviceList {
		err := createEmptyFile(l, path, size)
		if err != nil {
			return err
		}
	}

	return nil
}

// genFromNvme takes NVMe device PCI addresses and generates config content
// (output as string) from template.
func genFromTempl(cfg BdevConfig, templ string) (out bytes.Buffer, err error) {
	t := template.Must(template.New(confOut).Parse(templ))
	err = t.Execute(&out, cfg)
	return
}

type BdevProvider struct {
	log     logging.Logger
	cfg     BdevConfig
	cfgPath string
	bdev    bdev
}

func NewBdevProvider(log logging.Logger, cfgDir string, cfg *BdevConfig) (*BdevProvider, error) {
	p := &BdevProvider{
		log: log,
		cfg: *cfg,
	}

	switch cfg.Class {
	case BdevClassNone, BdevClassNvme:
		p.bdev = bdev{nvmeTempl, "", isEmptyList, isValidList, nilPrep}
	case BdevClassMalloc:
		p.bdev = bdev{mallocTempl, "MALLOC", isEmptyNumber, nilValidate, nilPrep}
	case BdevClassKdev:
		p.bdev = bdev{kdevTempl, "AIO", isEmptyList, isValidList, nilPrep}
	case BdevClassFile:
		p.bdev = bdev{fileTempl, "AIO", isEmptyList, isValidSize, prepBdevFile}
	default:
		return nil, errors.Errorf("unable to map %q to BdevClass", cfg.Class)
	}

	if msg := p.bdev.isEmpty(p.cfg); msg != "" {
		log.Debugf("spdk %s: %s", cfg.Class, msg)
		// No devices; no need to generate a config file
		return p, nil
	}

	if msg := p.bdev.isValid(p.cfg); msg != "" {
		log.Debugf("spdk %s: %s", cfg.Class, msg)
		// Bad config; don't generate a config file
		return nil, errors.Errorf("invalid NVMe config: %s", msg)
	}

	// Config file required; set this so it gets generated later
	p.cfgPath = filepath.Join(cfgDir, confOut)

	// FIXME: Not really happy with having side-effects here, but trying
	// not to change too much at once.
	cfg.VosEnv = p.bdev.vosEnv
	cfg.ConfigPath = p.cfgPath

	return p, nil
}

func (p *BdevProvider) PrepareDevices() error {
	return p.bdev.prep(p.log, p.cfg)
}

func (p *BdevProvider) GenConfigFile() error {
	if p.cfgPath == "" {
		return nil
	}

	confBytes, err := genFromTempl(p.cfg, p.bdev.templ)
	if err != nil {
		return err
	}

	if confBytes.Len() == 0 {
		return errors.New("spdk: generated NVMe config is unexpectedly empty")
	}

	f, err := os.Create(p.cfgPath)
	defer f.Close()
	if err != nil {
		return errors.Wrapf(err, "spdk: failed to create NVMe config file %s", p.cfgPath)
	}
	if _, err := confBytes.WriteTo(f); err != nil {
		return errors.Wrapf(err, "spdk: failed to write NVMe config to file %s", p.cfgPath)
	}

	return nil
}
