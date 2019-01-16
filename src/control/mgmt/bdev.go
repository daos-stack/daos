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

package mgmt

import (
	"bytes"
	"errors"
	"path/filepath"
	"text/template"
)

// BdClass enum specifing block device type for storage
type BdClass string

const (
	BD_NVME   BdClass = "nvme"
	BD_MALLOC BdClass = "malloc"
	BD_KDEV   BdClass = "kdev"
	BD_FILE   BdClass = "file"

	CONF_OUT   = "daos_nvme.conf"
	NVME_TEMPL = `[Nvme]
{{ range $i, $e := .BdevList }}    TransportID "trtype:PCIe traddr:{{$e}}" Nvme{{$i}}
{{ end }}    RetryCount 4
    TimeoutUsec 0
    ActionOnTimeout None
    AdminPollRate 100000
    HotplugEnable No
    HotplugPollRate 0
`
	// device block size hardcoded to 4096
	FILE_TEMPL = `[AIO]
{{ range $i, $e := .BdevList }}    AIO {{$e}} AIO{{$i}} 4096
{{ end }} `
	KDEV_TEMPL = `[AIO]
{{ range $i, $e := .BdevList }}    AIO {{$e}} AIO{{$i}}
{{ end }}`
	MALLOC_TEMPL = `[Malloc]
	NumberOfLuns {{.BdevNumber}}
	LunSizeInMB {{.BdevSize}}000
`
	GBYTE   = 1000000000
	BLKSIZE = 4096
)

// genFromNvme takes NVMe device PCI addresses and generates config content
// (output as string) from template.
func genFromTempl(server *server, templ string) (string, error) {
	t := template.Must(
		template.New(CONF_OUT).Parse(templ))
	var out bytes.Buffer
	if err := t.Execute(&out, server); err != nil {
		return "", err
	}
	return out.String(), nil
}

func createConf(ext External, server *server, templ string) error {
	out, err := genFromTempl(server, templ)
	if err != nil {
		return err
	}
	if out == "" {
		return errors.New("generated NVMe config unexpectedly empty")
	}
	confPath := filepath.Join(server.ScmMount, CONF_OUT)
	// write NVMe config file for this I/O Server located in
	// server-local SCM mount dir
	if err := ext.writeToFile(out, confPath); err != nil {
		return err
	}
	// set location of daos_nvme.conf to pass to I/O Server
	server.CliOpts = append(server.CliOpts, "-n", confPath)
	return nil
}

func (c *configuration) ParseNvme() error {
	for i, _ := range c.Servers {
		s := &c.Servers[i]
		switch s.BdevClass {
		case BD_NVME:
			if len(s.BdevList) == 0 {
				continue
			}
			// standard daos_nvme.conf, don't need to set VOS_BDEV_CLASS
			if err := createConf(c.ext, s, NVME_TEMPL); err != nil {
				return err
			}
		case BD_MALLOC:
			if s.BdevNumber == 0 {
				continue
			}
			if err := createConf(c.ext, s, MALLOC_TEMPL); err != nil {
				return err
			}
			s.EnvVars = append(s.EnvVars, "VOS_BDEV_CLASS=MALLOC")
		case BD_KDEV:
			if len(s.BdevList) == 0 {
				continue
			}
			if err := createConf(c.ext, s, KDEV_TEMPL); err != nil {
				return err
			}
			s.EnvVars = append(s.EnvVars, "VOS_BDEV_CLASS=AIO")
		case BD_FILE:
			if len(s.BdevList) == 0 {
				continue
			}
			// requested size aligned with block size
			size := (int64(s.BdevSize*GBYTE) / int64(BLKSIZE)) * int64(BLKSIZE)
			for _, path := range s.BdevList {
				err := c.ext.createEmpty(path, size)
				if err != nil {
					return err
				}
			}
			if err := createConf(c.ext, s, FILE_TEMPL); err != nil {
				return err
			}
			s.EnvVars = append(s.EnvVars, "VOS_BDEV_CLASS=AIO")
		}
	}
	return nil
}
