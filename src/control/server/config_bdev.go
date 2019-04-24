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
	"fmt"
	"path/filepath"
	"text/template"

	"github.com/daos-stack/daos/src/control/log"
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
{{ range $i, $e := .BdevList }}    AIO {{$e}} AIO{{$i}} 4096
{{ end }} `
	kdevTempl = `[AIO]
{{ range $i, $e := .BdevList }}    AIO {{$e}} AIO{{$i}}
{{ end }}`
	mallocTempl = `[Malloc]
	NumberOfLuns {{.BdevNumber}}
	LunSizeInMB {{.BdevSize}}000
`
	gbyte   = 1000000000
	blkSize = 4096

	bdNVMe   BdClass = "nvme"
	bdMalloc BdClass = "malloc"
	bdKdev   BdClass = "kdev"
	bdFile   BdClass = "file"

	msgBdevNone = "in config, no nvme.conf generated for server"
)

type Bdev struct {
	templ   string
	vosEnv  string
	isEmpty func(*server) string            // check no elements
	isValid func(*server) string            // check valid elements
	prep    func(int, *configuration) error // prerequisite actions
}

func nilValidate(srv *server) string { return "" }

func nilPrep(i int, c *configuration) error { return nil }

func isEmptyList(srv *server) string {
	if len(srv.BdevList) == 0 {
		return "bdev_list empty " + msgBdevNone
	}

	return ""
}

func isEmptyNumber(srv *server) string {
	if srv.BdevNumber == 0 {
		return "bdev_number == 0 " + msgBdevNone
	}

	return ""
}

func isValidList(srv *server) string {
	for i, elem := range srv.BdevList {
		if elem == "" {
			return fmt.Sprintf(
				"element %d in bdev_list is empty", i)
		}
	}

	return ""
}

func isValidSize(srv *server) string {
	if srv.BdevSize < 1 {
		return "bdev_size should be greater than 0"
	}

	return ""
}

func prepBdevFile(i int, c *configuration) error {
	srv := c.Servers[i]

	// truncate or create files for SPDK AIO emulation,
	// requested size aligned with block size
	size := (int64(srv.BdevSize*gbyte) / int64(blkSize)) *
		int64(blkSize)

	for _, path := range srv.BdevList {
		err := c.ext.createEmpty(path, size)
		if err != nil {
			return err
		}
	}

	return nil
}

var bdevMap = map[BdClass]Bdev{
	bdNVMe:   Bdev{nvmeTempl, "", isEmptyList, isValidList, nilPrep},
	bdMalloc: Bdev{mallocTempl, "MALLOC", isEmptyNumber, nilValidate, nilPrep},
	bdKdev:   Bdev{kdevTempl, "AIO", isEmptyList, isValidList, nilPrep},
	bdFile:   Bdev{fileTempl, "AIO", isEmptyList, isValidSize, prepBdevFile},
}

// rank represents a rank of an I/O server or a nil rank.
// genFromNvme takes NVMe device PCI addresses and generates config content
// (output as string) from template.
func genFromTempl(server *server, templ string) (out bytes.Buffer, err error) {
	t := template.Must(
		template.New(confOut).Parse(templ))

	err = t.Execute(&out, server)

	return
}

// createConf writes NVMe conf to persistent SCM mount at location "path"
// generated from io_server config at given index populating template "templ".
// Generated file is written to SCM mount specific to an io_server instance
// to be consumed by SPDK in that process.
func (c *configuration) createConf(srv *server, templ string, path string) (
	err error) {

	confBytes, err := genFromTempl(srv, templ)
	if err != nil {
		return
	}

	confStr := confBytes.String()
	if confStr == "" {
		return errors.New(
			"spdk: generated NVMe config unexpectedly empty")
	}

	if err = c.ext.writeToFile(confStr, path); err != nil {
		return
	}

	return
}

// parseNvme reads server config file, calls createConf when "create" == true
// and performs necessary file creation and sets environment variable for SPDK
// emulation if needed. Direct io_server to use generated NVMe conf by setting
//"-n" cli opt.
func (c *configuration) parseNvme(i int, create bool) (err error) {
	srv := &c.Servers[i]
	confPath := filepath.Join(srv.ScmMount, confOut)
	bdev := bdevMap[srv.BdevClass]

	if msg := bdev.isEmpty(srv); msg != "" {
		log.Debugf("spdk %s: %s (server %d)\n", srv.BdevClass, msg, i)
		return
	}

	if create {
		if err = bdev.prep(i, c); err != nil {
			return
		}

		if err = c.createConf(srv, bdev.templ, confPath); err != nil {
			return
		}
	}

	if bdev.vosEnv != "" {
		srv.EnvVars = append(srv.EnvVars, "VOS_BDEV_CLASS="+bdev.vosEnv)
	}

	// if we get here we can assume we have SPDK conf in SCM mount, set
	// location in io_server cli opts (assuming config hasn't changed
	// between restarts)
	srv.CliOpts = append(srv.CliOpts, "-n", confPath)

	return
}
