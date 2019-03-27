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

package main

import (
	"bytes"
	"errors"
	"path/filepath"
	"text/template"
)

const (
	confOut   = "daos_nvme.conf"
	nvmeTempl = `[Nvme]
{{ range $i, $e := .BdevList }}    TransportID "trtype:PCIe traddr:{{$e}}" Nvme{{$i}}
{{ end }}    RetryCount 4
    TimeoutUsec 0
    ActionOnTimeout None
    AdminPollRate 100000
    HotplugEnable No
    HotplugPollRate 0
`
	// device block size hardcoded to 4096
	fileTempl = `[AIO]
{{ $host := .Hostname }}{{ range $i, $e := .BdevList }}    AIO {{$e}} AIO_{{$host}}_{{$i}} 4096
{{ end }} `
	kdevTempl = `[AIO]
{{ $host := .Hostname }}{{ range $i, $e := .BdevList }}    AIO {{$e}} AIO_{{$host}}_{{$i}}
{{ end }}`
	mallocTempl = `[Malloc]
	NumberOfLuns {{.BdevNumber}}
	LunSizeInMB {{.BdevSize}}000
`
	gbyte   = 1000000000
	blkSize = 4096
)

// genFromNvme takes NVMe device PCI addresses and generates config content
// (output as string) from template.
func genFromTempl(server *server, templ string) (string, error) {
	t := template.Must(
		template.New(confOut).Parse(templ))
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
	confPath := filepath.Join(server.ScmMount, confOut)
	// write NVMe config file for this I/O Server located in
	// server-local SCM mount dir
	if err := ext.writeToFile(out, confPath); err != nil {
		return err
	}
	// set location of daos_nvme.conf to pass to I/O Server
	server.CliOpts = append(server.CliOpts, "-n", confPath)
	return nil
}

func (c *configuration) parseNvme(i int) error {
	srv := &c.Servers[i]

	switch srv.BdevClass {
	case bdNVMe:
		if len(srv.BdevList) == 0 {
			break
		}
		// standard daos_nvme.conf, don't need to set VOS_BDEV_CLASS
		if err := createConf(c.ext, srv, nvmeTempl); err != nil {
			return err
		}
	case bdMalloc:
		if srv.BdevNumber == 0 {
			break
		}
		if err := createConf(c.ext, srv, mallocTempl); err != nil {
			return err
		}
		srv.EnvVars = append(srv.EnvVars, "VOS_BDEV_CLASS=MALLOC")
	case bdKdev:
		if len(srv.BdevList) == 0 {
			break
		}
		if err := createConf(c.ext, srv, kdevTempl); err != nil {
			return err
		}
		srv.EnvVars = append(srv.EnvVars, "VOS_BDEV_CLASS=AIO")
	case bdFile:
		if len(srv.BdevList) == 0 {
			break
		}
		// requested size aligned with block size
		size := (int64(srv.BdevSize*gbyte) / int64(blkSize)) * int64(blkSize)
		for _, path := range srv.BdevList {
			err := c.ext.createEmpty(path, size)
			if err != nil {
				return err
			}
		}
		if err := createConf(c.ext, srv, fileTempl); err != nil {
			return err
		}
		srv.EnvVars = append(srv.EnvVars, "VOS_BDEV_CLASS=AIO")
	}

	return nil
}
