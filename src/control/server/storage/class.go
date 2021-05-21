//
// (C) Copyright 2018-2021 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package storage

import (
	"bytes"
	"fmt"
	"os"
	"path/filepath"
	"syscall"
	"text/template"

	"github.com/dustin/go-humanize"
	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/common"
	"github.com/daos-stack/daos/src/control/logging"
)

const (
	confOut   = "daos_nvme.conf"
	nvmeTempl = `[Nvme]
{{ $host := .Bdev.Hostname }}{{ $tier := .Tier }}{{ range $i, $e := .Bdev.DeviceList }}    TransportID "trtype:PCIe traddr:{{$e}}" Nvme_{{$host}}_{{$i}}_{{$tier}}
{{ end }}    RetryCount 4
TimeoutUsec 0
ActionOnTimeout None
AdminPollRate 100000
HotplugEnable No
HotplugPollRate 0
`
	// device block size hardcoded to 4096
	fileTempl = `[AIO]
{{ $host := .Bdev.Hostname }}{{ $tier := .Tier }}{{ range $i, $e := .Bdev.DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}}_{{$tier}} 4096
{{ end }}`
	kdevTempl = `[AIO]
{{ $host := .Bdev.Hostname }}{{ $tier := .Tier }}{{ range $i, $e := .Bdev.DeviceList }}    AIO {{$e}} AIO_{{$host}}_{{$i}}_{{$tier}}
{{ end }}`
	mallocTempl = `[Malloc]
NumberOfLuns {{.Bdev.DeviceCount}}
LunSizeInMB {{.Bdev.FileSize}}000
`
	gbyte   = 1000000000
	blkSize = 4096

	msgBdevNone    = "in config, no nvme.conf generated for server"
	msgBdevEmpty   = "bdev device list entry empty"
	msgBdevBadSize = "backfile_size should be greater than 0"
)

// bdev describes parameters and behaviors for a particular bdev class.
type bdev struct {
	templ   string
	isEmpty func(*BdevConfig) string                // check no elements
	isValid func(*BdevConfig) string                // check valid elements
	init    func(logging.Logger, *BdevConfig) error // prerequisite actions
}

func nilValidate(_ *BdevConfig) string { return "" }

func nilInit(_ logging.Logger, _ *BdevConfig) error { return nil }

func isEmptyList(c *BdevConfig) string {
	if len(c.DeviceList) == 0 {
		return "bdev_list empty " + msgBdevNone
	}

	return ""
}

func isEmptyNumber(c *BdevConfig) string {
	if c.DeviceCount == 0 {
		return "bdev_number == 0 " + msgBdevNone
	}

	return ""
}

func isValidList(c *BdevConfig) string {
	for i, elem := range c.DeviceList {
		if elem == "" {
			return fmt.Sprintf("%s (index %d)", msgBdevEmpty, i)
		}
	}

	return ""
}

func isValidSize(c *BdevConfig) string {
	if c.FileSize < 1 {
		return msgBdevBadSize
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

func bdevFileInit(log logging.Logger, c *BdevConfig) error {
	// truncate or create files for SPDK AIO emulation,
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

// genFromNvme takes NVMe device PCI addresses and generates config content
// (output as string) from template.
func genFromTempl(cfg *Config, templ string) (out bytes.Buffer, err error) {
	t := template.Must(template.New(confOut).Parse(templ))
	err = t.Execute(&out, cfg)

	return
}

// ClassProvider implements functionality for a given bdev class
type ClassProvider struct {
	log     logging.Logger
	cfg     Configs
	cfgPath string
	bdev    []bdev
}

// NewClassProvider returns a new ClassProvider reference for given bdev type.
func NewClassProvider(log logging.Logger, cfgDir string, engineCfgs *StorageConfig) (*ClassProvider, error) {
	bdevCfgs := engineCfgs.Tiers.BdevConfigs()
	p := &ClassProvider{
		log: log,
		cfg: bdevCfgs,
	}
	p.bdev = make([]bdev, 0, len(bdevCfgs))

	for tierIdx, bdevCfg := range bdevCfgs {
		switch bdevCfg.Class {
		case ClassNone:
			engineCfgs.VosEnv = ""
			p.bdev = append(p.bdev, bdev{nvmeTempl, isEmptyList, isValidList, nilInit})
		case ClassNvme:
			engineCfgs.VosEnv = "NVME"
			p.bdev = append(p.bdev, bdev{nvmeTempl, isEmptyList, isValidList, nilInit})
			if !bdevCfg.Bdev.VmdDisabled {
				p.bdev[tierIdx].templ = `[Vmd]
	Enable True

	` + p.bdev[tierIdx].templ
			}
		case ClassMalloc:
			engineCfgs.VosEnv = "MALLOC"
			p.bdev = append(p.bdev, bdev{mallocTempl, isEmptyNumber, nilValidate, nilInit})
		case ClassKdev:
			engineCfgs.VosEnv = "AIO"
			p.bdev = append(p.bdev, bdev{kdevTempl, isEmptyList, isValidList, nilInit})
		case ClassFile:
			engineCfgs.VosEnv = "AIO"
			p.bdev = append(p.bdev, bdev{fileTempl, isEmptyList, isValidSize, bdevFileInit})
		default:
			return nil, errors.Errorf("unable to map %q to BdevClass", bdevCfgs[tierIdx].Class)
		}

		if msg := p.bdev[tierIdx].isEmpty(&p.cfg[tierIdx].Bdev); msg != "" {
			log.Debugf("spdk %s: %s", bdevCfgs[tierIdx].Class, msg)
			// No devices; no need to generate a config file
			return p, nil
		}

		if msg := p.bdev[tierIdx].isValid(&p.cfg[tierIdx].Bdev); msg != "" {
			log.Debugf("spdk %s: %s", bdevCfgs[tierIdx].Class, msg)
			// Bad config; don't generate a config file
			return nil, errors.Errorf("invalid nvme config: %s", msg)
		}
	}

	// Config file required; set this so it gets generated later
	p.cfgPath = filepath.Join(cfgDir, confOut)
	engineCfgs.ConfigPath = p.cfgPath
	log.Debugf("output bdev conf file set to %s", p.cfgPath)

	return p, nil
}

// GenConfigFile generates nvme config file for given bdev type to be consumed
// by spdk.
func (p *ClassProvider) GenConfigFile() error {
	if p.cfgPath == "" {
		p.log.Debug("skip bdev conf file generation as no path set")

		return nil
	}
	f, err := os.Create(p.cfgPath)
	defer func() {
		ce := f.Close()
		if err == nil {
			err = ce
		}
	}()

	if err != nil {
		return errors.Wrapf(err, "spdk: failed to create NVMe config file %s", p.cfgPath)
	}
	for tierIdx, _ := range p.bdev {
		if err := p.bdev[tierIdx].init(p.log, &p.cfg[tierIdx].Bdev); err != nil {
			return errors.Wrap(err, "bdev device init")
		}

		confBytes, err := genFromTempl(p.cfg[tierIdx], p.bdev[tierIdx].templ)
		if err != nil {
			return err
		}

		if confBytes.Len() == 0 {
			return errors.New("spdk: generated nvme config is unexpectedly empty")
		}

		p.log.Debugf("create %s with %v bdevs", p.cfgPath[tierIdx], p.cfg[tierIdx].Bdev.DeviceList)

		if _, err := confBytes.WriteTo(f); err != nil {
			return errors.Wrapf(err, "spdk: failed to write NVMe config to file %s", p.cfgPath)
		}
	}
	return nil
}
