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
		return ObjectClass(0), errors.Wrap(NotImpl, "no object class resolver set; can't resolve class name")
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

// ObjectClassFromString parses a string to an ObjectClass.
func ObjectClassFromString(s string) (ObjectClass, error) {
	var oc ObjectClass
	if err := oc.FromString(s); err != nil {
		return oc, err
	}
	return oc, nil
}

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

type (
	// ObjectID represents a DAOS object ID.
	ObjectID C.daos_obj_id_t
)

// Class returns the derived object class.
func (oid ObjectID) Class() ObjectClass {
	return ObjectClass(C.daos_obj_id2class(C.daos_obj_id_t(oid)))
}

// IsZero returns true if the ObjectID is the zero value.
func (oid ObjectID) IsZero() bool {
	return oid.hi == 0 && oid.lo == 0
}

// FromString parses a string to an ObjectID.
func (oid *ObjectID) FromString(s string) error {
	var hi, lo uint64

	if _, err := fmt.Sscanf(s, "%d.%d", &hi, &lo); err != nil {
		return errors.Wrapf(InvalidInput, "invalid object ID %q: %v", s, err)
	}

	oid.hi = C.uint64_t(hi)
	oid.lo = C.uint64_t(lo)
	return nil
}

func (oid ObjectID) String() string {
	return fmt.Sprintf("%d.%d", oid.hi, oid.lo)
}

// ObjectIDFromString parses a string to an ObjectID.
func ObjectIDFromString(s string) (ObjectID, error) {
	var oid ObjectID
	if err := oid.FromString(s); err != nil {
		return ObjectID{}, err
	}
	return oid, nil
}
