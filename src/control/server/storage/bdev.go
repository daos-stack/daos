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
	"path/filepath"
	"text/template"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/logging"
	"github.com/daos-stack/daos/src/control/server/ioserver"
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
{{ end }} `
	kdevTempl = `[AIO]
{{ $host := .Hostname }}{{ range $i, $e := .DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}}
{{ end }}`
	mallocTempl = `[Malloc]
	NumberOfLuns {{.DeviceCount}}
	LunSizeInMB {{.FileSize}}000
`
	gbyte   = 1000000000
	blkSize = 4096

	msgBdevNone    = "in config, no nvme.conf generated for server"
	msgBdevEmpty   = "bdev device list entry empty"
	msgBdevBadSize = "backfile_size should be greater than 0"
)

// specify functions to be mocked out
type ext interface {
	CreateEmpty(string, int64) error
	WriteToFile(string, string) error
}

// bdev describes parameters and behaviours for a particular bdev class.
type bdev struct {
	templ   string
	vosEnv  string
	isEmpty func(ioserver.BdevConfig) string     // check no elements
	isValid func(ioserver.BdevConfig) string     // check valid elements
	prep    func(ext, ioserver.BdevConfig) error // prerequisite actions
}

func nilValidate(c ioserver.BdevConfig) string { return "" }

func nilPrep(e ext, c ioserver.BdevConfig) error { return nil }

func isEmptyList(c ioserver.BdevConfig) string {
	if len(c.DeviceList) == 0 {
		return "bdev_list empty " + msgBdevNone
	}

	return ""
}

func isEmptyNumber(c ioserver.BdevConfig) string {
	if c.DeviceCount == 0 {
		return "bdev_number == 0 " + msgBdevNone
	}

	return ""
}

func isValidList(c ioserver.BdevConfig) string {
	for i, elem := range c.DeviceList {
		if elem == "" {
			return fmt.Sprintf("%s (index %d)", msgBdevEmpty, i)
		}
	}

	return ""
}

func isValidSize(c ioserver.BdevConfig) string {
	if c.FileSize < 1 {
		return msgBdevBadSize
	}

	return ""
}

func prepBdevFile(e ext, c ioserver.BdevConfig) error {
	// truncate or create files for SPDK AIO emulation,
	// requested size aligned with block size
	size := (int64(c.FileSize*gbyte) / int64(blkSize)) * int64(blkSize)

	for _, path := range c.DeviceList {
		err := e.CreateEmpty(path, size)
		if err != nil {
			return err
		}
	}

	return nil
}

// genFromNvme takes NVMe device PCI addresses and generates config content
// (output as string) from template.
func genFromTempl(cfg ioserver.BdevConfig, templ string) (out bytes.Buffer, err error) {
	t := template.Must(template.New(confOut).Parse(templ))
	err = t.Execute(&out, cfg)
	return
}

type BdevProvider struct {
	ext     ext
	log     logging.Logger
	cfg     ioserver.BdevConfig
	cfgPath string
	bdev    bdev
}

func NewBdevProvider(e ext, log logging.Logger, cfgDir string, cfg *ioserver.BdevConfig) (*BdevProvider, error) {
	p := &BdevProvider{
		ext: e,
		log: log,
		cfg: *cfg,
	}

	switch BdevClass(cfg.Class) {
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
	return p.bdev.prep(p.ext, p.cfg)
}

func (p *BdevProvider) GenConfigFile() error {
	if p.cfgPath == "" {
		return nil
	}

	confBytes, err := genFromTempl(p.cfg, p.bdev.templ)
	if err != nil {
		return err
	}

	confStr := confBytes.String()
	if confStr == "" {
		return errors.New("spdk: generated NVMe config is unexpectedly empty")
	}

	if err := p.ext.WriteToFile(confStr, p.cfgPath); err != nil {
		return err
	}

	return nil
}
