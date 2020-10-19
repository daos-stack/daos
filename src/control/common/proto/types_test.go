//
// (C) Copyright 2020 Intel Corporation.
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
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"

	ctlpb "github.com/daos-stack/daos/src/control/common/proto/ctl"
	"github.com/daos-stack/daos/src/control/server/storage"
)

func TestProto_ConvertNvmeHealth(t *testing.T) {
	pb := MockNvmeHealth(1)
	native, err := (*NvmeHealth)(pb).ToNative()
	if err != nil {
		t.Fatal(err)
	}
	expNative := storage.MockNvmeHealth(1)

	if diff := cmp.Diff(expNative, native); diff != "" {
		t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
	}
}

func TestProto_ConvertNvmeNamespace(t *testing.T) {
	pb := MockNvmeNamespace()
	native, err := (*NvmeNamespace)(pb).ToNative()
	if err != nil {
		t.Fatal(err)
	}
	expNative := storage.MockNvmeNamespace()

	if diff := cmp.Diff(expNative, native); diff != "" {
		t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
	}
}

func TestProto_ConvertNvmeController(t *testing.T) {
	pb := MockNvmeController()
	native, err := (*NvmeController)(pb).ToNative()
	if err != nil {
		t.Fatal(err)
	}
	expNative := storage.MockNvmeController()

	cmpOpts := []cmp.Option{
		cmpopts.IgnoreFields(storage.NvmeController{}, "HealthStats", "Serial"),
	}
	if diff := cmp.Diff(expNative, native, cmpOpts...); diff != "" {
		t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
	}
}

func TestProto_ConvertNvmeControllers(t *testing.T) {
	pbs := []*ctlpb.NvmeController{
		MockNvmeController(1),
		MockNvmeController(2),
		MockNvmeController(3),
	}
	natives, err := (*NvmeControllers)(&pbs).ToNative()
	if err != nil {
		t.Fatal(err)
	}

	var convertedNatives NvmeControllers
	if err := convertedNatives.FromNative(natives); err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(pbs,
		([]*ctlpb.NvmeController)(convertedNatives)); diff != "" {

		t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
	}
}

func TestProto_ConvertScmModule(t *testing.T) {
	pb := MockScmModule()
	native, err := (*ScmModule)(pb).ToNative()
	if err != nil {
		t.Fatal(err)
	}
	expNative := storage.MockScmModule()

	if diff := cmp.Diff(expNative, native); diff != "" {
		t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
	}
}

func TestProto_ConvertScmModules(t *testing.T) {
	pbs := []*ctlpb.ScmModule{
		MockScmModule(1),
		MockScmModule(2),
		MockScmModule(3),
	}
	natives, err := (*ScmModules)(&pbs).ToNative()
	if err != nil {
		t.Fatal(err)
	}

	var convertedNatives ScmModules
	if err := convertedNatives.FromNative(natives); err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(pbs,
		([]*ctlpb.ScmModule)(convertedNatives)); diff != "" {

		t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
	}
}

func TestProto_ConvertScmNamespace(t *testing.T) {
	pb := MockScmNamespace()
	pb.Mount = MockScmMountPoint()
	native, err := (*ScmNamespace)(pb).ToNative()
	if err != nil {
		t.Fatal(err)
	}
	expNative := storage.MockScmNamespace()
	expNative.Mount = storage.MockScmMountPoint()

	if diff := cmp.Diff(expNative, native); diff != "" {
		t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
	}
}

func TestProto_ConvertScmNamespaces(t *testing.T) {
	pbs := []*ctlpb.ScmNamespace{
		MockScmNamespace(1),
		MockScmNamespace(2),
		MockScmNamespace(3),
	}
	natives, err := (*ScmNamespaces)(&pbs).ToNative()
	if err != nil {
		t.Fatal(err)
	}

	var convertedNatives ScmNamespaces
	if err := convertedNatives.FromNative(natives); err != nil {
		t.Fatal(err)
	}
	if diff := cmp.Diff(pbs,
		([]*ctlpb.ScmNamespace)(convertedNatives)); diff != "" {

		t.Fatalf("unexpected result (-want, +got):\n%s\n", diff)
	}
}
