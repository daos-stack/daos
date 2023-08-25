package daos

import (
	"strings"

	"github.com/pkg/errors"
)

/*
#cgo LDFLAGS: -ldaos

#include <daos.h>
#include <daos_obj_class.h>
*/
import "C"

type (
	ObjectClass C.daos_oclass_id_t
)

func (oc *ObjectClass) FromString(cls string) error {
	cStr := C.CString(strings.ToUpper(cls))
	defer freeString(cStr)

	*oc = ObjectClass(C.daos_oclass_name2id(cStr))
	if *oc == C.OC_UNKNOWN {
		return errors.Wrapf(InvalidInput, "invalid object class %q", cls)
	}

	return nil
}

func (oc ObjectClass) String() string {
	var oclass [10]C.char

	C.daos_oclass_id2name(C.daos_oclass_id_t(oc), &oclass[0])
	return C.GoString(&oclass[0])
}
