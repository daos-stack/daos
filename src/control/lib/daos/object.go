//
// (C) Copyright 2024-2025 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"fmt"

	"github.com/pkg/errors"
)

/*
#include <daos.h>
*/
import "C"

// NB: We can't use the oclass_name2id() and oclass_id2name() helpers
// in this package because they are client-only symbols and we don't
// want this package to depend directly on libdaos. :/
//
// TODO(?): Move the oclass helpers into the common library?

var (
	objClass2String objClassIDStringer = func(oc ObjectClass) string {
		return fmt.Sprintf("0x%x", oc)
	}
	objClassName2Class objClassNameResolver = func(name string) (ObjectClass, error) {
		return ObjectClass(0), errors.New("no object class resolver set; can't resolve class name")
	}
)

// SetObjectClassHelpers provides a mechanism for injecting the object API
// methods into this package without introducing a hard dependency on libdaos.
// Generally only called from object.go in the api package.
func SetObjectClassHelpers(stringer objClassIDStringer, resolver objClassNameResolver) {
	objClass2String = stringer
	objClassName2Class = resolver
}

type (
	objClassNameResolver func(string) (ObjectClass, error)
	objClassIDStringer   func(ObjectClass) string

	// ObjectClass represents an object class.
	ObjectClass C.daos_oclass_id_t
)

// FromString resolves a string to an ObjectClass.
func (oc *ObjectClass) FromString(name string) error {
	class, err := objClassName2Class(name)
	if err != nil {
		return err
	}
	*oc = class
	return nil
}

func (oc ObjectClass) String() string {
	return objClass2String(oc)
}

func (oc ObjectClass) MarshalJSON() ([]byte, error) {
	return []byte(`"` + oc.String() + `"`), nil
}
