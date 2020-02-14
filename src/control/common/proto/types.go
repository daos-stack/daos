//
// (C) Copyright 2019 Intel Corporation.
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
package proto

import (
	"bytes"
	"encoding/json"
	"fmt"
	"sort"
	"time"

	bytesize "github.com/inhies/go-bytesize"

	"github.com/daos-stack/daos/src/control/common"
	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func convertTypes(in interface{}, out interface{}) error {
	data, err := json.Marshal(in)
	if err != nil {
		return err
	}

	return json.Unmarshal(data, out)
}

type NvmeDeviceHealth ctlpb.NvmeController_Health

func (pb *NvmeDeviceHealth) FromNative(native *storage.NvmeDeviceHealth) error {
	return convertTypes(native, pb)
}

func (pb *NvmeDeviceHealth) ToNative() (*storage.NvmeDeviceHealth, error) {
	native := new(storage.NvmeDeviceHealth)
	return native, convertTypes(pb, native)
}

func (pb *NvmeDeviceHealth) AsProto() *ctlpb.NvmeController_Health {
	return (*ctlpb.NvmeController_Health)(pb)
}

type NvmeNamespace ctlpb.NvmeController_Namespace

func (pb *NvmeNamespace) FromNative(native *storage.NvmeNamespace) error {
	return convertTypes(native, pb)
}

func (pb *NvmeNamespace) ToNative() (*storage.NvmeNamespace, error) {
	native := new(storage.NvmeNamespace)
	return native, convertTypes(pb, native)
}

func (pb *NvmeNamespace) AsProto() *ctlpb.NvmeController_Namespace {
	return (*ctlpb.NvmeController_Namespace)(pb)
}

// NvmeNamespaces is an alias for protobuf NvmeController_Namespace message slice
// representing namespaces existing on a NVMe SSD.
type NvmeNamespaces []*ctlpb.NvmeController_Namespace

type NvmeController ctlpb.NvmeController

func (pb *NvmeController) FromNative(native *storage.NvmeController) error {
	return convertTypes(native, pb)
}

func (pb *NvmeController) ToNative() (*storage.NvmeController, error) {
	native := new(storage.NvmeController)
	return native, convertTypes(pb, native)
}

func (pb *NvmeController) AsProto() *ctlpb.NvmeController {
	return (*ctlpb.NvmeController)(pb)
}

// NvmeControllers is an alias for protobuf NvmeController message slice
// representing a number of NVMe SSD controllers installed on a storage node.
type NvmeControllers []*ctlpb.NvmeController

func (pb *NvmeControllers) FromNative(native storage.NvmeControllers) error {
	return convertTypes(native, pb)
}

func (pb NvmeControllers) ToNative() (storage.NvmeControllers, error) {
	native := make(storage.NvmeControllers, 0, len(pb))
	return native, convertTypes(pb, native)
}

func healthDetail(buf *bytes.Buffer, c *ctlpb.NvmeController) {
	stat := c.GetHealthstats()

	if stat == nil {
		fmt.Fprintf(buf, "\t\tHealth Stats Unavailable\n")
		return
	}

	fmt.Fprintf(buf, "\t\tHealth Stats:\n\t\t\tTemperature:%dK(%dC)\n", stat.Temp, stat.Temp-273)

	if stat.Tempwarntime > 0 {
		fmt.Fprintf(buf, "\t\t\t\tTemperature Warning Duration:%s\n",
			time.Duration(stat.Tempwarntime)*time.Minute)
	}
	if stat.Tempcrittime > 0 {
		fmt.Fprintf(buf, "\t\t\t\tTemperature Critical Duration:%s\n",
			time.Duration(stat.Tempcrittime)*time.Minute)
	}

	fmt.Fprintf(buf, "\t\t\tController Busy Time:%s\n", time.Duration(stat.Ctrlbusytime)*time.Minute)
	fmt.Fprintf(buf, "\t\t\tPower Cycles:%d\n", uint64(stat.Powercycles))
	fmt.Fprintf(buf, "\t\t\tPower On Duration:%s\n", time.Duration(stat.Poweronhours)*time.Hour)
	fmt.Fprintf(buf, "\t\t\tUnsafe Shutdowns:%d\n", uint64(stat.Unsafeshutdowns))
	fmt.Fprintf(buf, "\t\t\tMedia Errors:%d\n", uint64(stat.Mediaerrors))
	fmt.Fprintf(buf, "\t\t\tError Log Entries:%d\n", uint64(stat.Errorlogentries))

	fmt.Fprintf(buf, "\t\t\tCritical Warnings:\n")
	fmt.Fprintf(buf, "\t\t\t\tTemperature: ")
	if stat.Tempwarn {
		fmt.Fprintf(buf, "WARNING\n")
	} else {
		fmt.Fprintf(buf, "OK\n")
	}
	fmt.Fprintf(buf, "\t\t\t\tAvailable Spare: ")
	if stat.Availsparewarn {
		fmt.Fprintf(buf, "WARNING\n")
	} else {
		fmt.Fprintf(buf, "OK\n")
	}
	fmt.Fprintf(buf, "\t\t\t\tDevice Reliability: ")
	if stat.Reliabilitywarn {
		fmt.Fprintf(buf, "WARNING\n")
	} else {
		fmt.Fprintf(buf, "OK\n")
	}
	fmt.Fprintf(buf, "\t\t\t\tRead Only: ")
	if stat.Readonlywarn {
		fmt.Fprintf(buf, "WARNING\n")
	} else {
		fmt.Fprintf(buf, "OK\n")
	}
	fmt.Fprintf(buf, "\t\t\t\tVolatile Memory Backup: ")
	if stat.Volatilewarn {
		fmt.Fprintf(buf, "WARNING\n")
	} else {
		fmt.Fprintf(buf, "OK\n")
	}
}

// ctrlrDetail provides custom string representation for Controller type
// defined outside this package.
func ctrlrDetail(buf *bytes.Buffer, c *ctlpb.NvmeController) {
	tCap := bytesize.New(0)
	for _, n := range c.Namespaces {
		tCap += bytesize.GB * bytesize.New(float64(n.Size))
	}

	fmt.Fprintf(buf, "\t\tPCI:%s Model:%s FW:%s Socket:%d Capacity:%s\n",
		c.Pciaddr, c.Model, c.Fwrev, c.Socketid, tCap)
}

func (ncs NvmeControllers) String() string {
	buf := bytes.NewBufferString("NVMe controllers and namespaces:\n")

	if len(ncs) == 0 {
		fmt.Fprint(buf, "\t\tnone\n")
		return buf.String()
	}

	sort.Slice(ncs, func(i, j int) bool { return ncs[i].Pciaddr < ncs[j].Pciaddr })

	for _, ctrlr := range ncs {
		ctrlrDetail(buf, ctrlr)
	}

	return buf.String()
}

// StringHealthStats returns full string representation including NVMe health
// statistics as well as controller and namespace details.
func (ncs NvmeControllers) StringHealthStats() string {
	buf := bytes.NewBufferString(
		"NVMe controllers and namespaces detail with health statistics:\n")

	if len(ncs) == 0 {
		fmt.Fprint(buf, "\t\tnone\n")
		return buf.String()
	}

	for _, ctrlr := range ncs {
		ctrlrDetail(buf, ctrlr)
		healthDetail(buf, ctrlr)
	}

	return buf.String()
}

// Summary reports accumulated storage space and the number of controllers.
func (ncs NvmeControllers) Summary() string {
	tCap := bytesize.New(0)
	for _, c := range ncs {
		for _, n := range c.Namespaces {
			tCap += bytesize.GB * bytesize.New(float64(n.Size))
		}
	}

	return fmt.Sprintf("%s (%d %s)",
		tCap, len(ncs), common.Pluralise("controller", len(ncs)))
}

// NvmeControllerResults is an alias for protobuf NvmeControllerResult messages
// representing operation results on a number of NVMe controllers.
type NvmeControllerResults []*ctlpb.NvmeControllerResult

func (ncr NvmeControllerResults) HasErrors() bool {
	for _, res := range ncr {
		if res.State.Status != ctlpb.ResponseStatus_CTL_SUCCESS {
			return true
		}
	}
	return false
}

// ScmNamespaces is an alias for protobuf PmemDevice message slice representing
// a number of PMEM device files created on SCM namespaces on a storage node.
type ScmNamespaces []*ctlpb.PmemDevice

func (pds ScmNamespaces) String() string {
	var buf bytes.Buffer

	for _, pd := range pds {
		fmt.Fprintf(&buf, "\t%s\n", pd)
	}

	return buf.String()
}

// ScmMounts are protobuf representations of mounted SCM namespaces identified
// by mount points
type ScmMounts []*ctlpb.ScmMount

// ScmMountResults is an alias for protobuf ScmMountResult message slice
// representing operation results on a number of SCM mounts.
type ScmMountResults []*ctlpb.ScmMountResult

func (smr ScmMountResults) HasErrors() bool {
	for _, res := range smr {
		if res.State.Status != ctlpb.ResponseStatus_CTL_SUCCESS {
			return true
		}
	}
	return false
}

// ScmModules is an alias for protobuf ScmModule message slice representing
// a number of SCM modules installed on a storage node.
type ScmModules []*ctlpb.ScmModule

// ScmModuleResults is an alias for protobuf ScmModuleResult message slice
// representing operation results on a number of SCM modules.
type ScmModuleResults []*ctlpb.ScmModuleResult
