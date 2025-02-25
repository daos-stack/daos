//
// (C) Copyright 2024-2025 Intel Corporation.
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package api

import (
	"strings"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

/*
#include <daos.h>
#include <daos_obj_class.h>
*/
import "C"

func init() {
	// Any user of this package will automatically benefit from these
	// helpers in the daos package.
	daos.SetObjectClassHelpers(ObjectClassName, ObjectClassFromName)
}

// ObjectClassFromName returns a new ObjectClass for the given name.
func ObjectClassFromName(name string) (daos.ObjectClass, error) {
	upperName := strings.ToUpper(name)
	cStr := C.CString(upperName)
	defer freeString(cStr)

	id := daos_oclass_name2id(cStr)
	if id == C.OC_UNKNOWN {
		return daos.ObjectClass(C.OC_UNKNOWN), errors.Wrapf(daos.InvalidInput, "invalid object class %q", name)
	}

	return daos.ObjectClass(id), nil
}

// ObjectClassNameFromID returns a new ObjectClass for the given ID.
func ObjectClassName(class daos.ObjectClass) string {
	var oclass [10]C.char

	if rc := daos_oclass_id2name(C.daos_oclass_id_t(class), &oclass[0]); rc != 0 {
		return errors.Wrapf(daos.InvalidInput, "invalid object class %d", class).Error()
	}

	return C.GoString(&oclass[0])
}
