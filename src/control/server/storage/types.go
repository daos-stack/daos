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
package storage

import "github.com/pkg/errors"

const (
	ScmClassDCPM ScmClass = "dcpm"
	ScmClassRAM  ScmClass = "ram"
)

// ScmClass specifies device type for Storage Class Memory
type ScmClass string

// UnmarshalYAML implements yaml.Unmarshaler on ScmClass type
func (s *ScmClass) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var class string
	if err := unmarshal(&class); err != nil {
		return err
	}

	scmClass := ScmClass(class)
	switch scmClass {
	case ScmClassDCPM, ScmClassRAM:
		*s = scmClass
	default:
		return errors.Errorf("scm_class value %v not supported in config (dcpm/ram)", scmClass)
	}
	return nil
}

const (
	BdevClassNone   BdevClass = ""
	BdevClassNvme   BdevClass = "nvme"
	BdevClassMalloc BdevClass = "malloc"
	BdevClassKdev   BdevClass = "kdev"
	BdevClassFile   BdevClass = "file"
)

// BdevClass specifies block device type for block device storage
type BdevClass string

// UnmarshalYAML implements yaml.Unmarshaler on BdevClass type
func (b *BdevClass) UnmarshalYAML(unmarshal func(interface{}) error) error {
	var class string
	if err := unmarshal(&class); err != nil {
		return err
	}
	bdevClass := BdevClass(class)
	switch bdevClass {
	case BdevClassNvme, BdevClassMalloc, BdevClassKdev, BdevClassFile:
		*b = bdevClass
	default:
		return errors.Errorf("bdev_class value %v not supported in config (nvme/malloc/kdev/file)", bdevClass)
	}
	return nil
}
