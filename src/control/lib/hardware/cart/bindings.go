//
// (C) Copyright 2024 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package cart

/*
#cgo LDFLAGS: -lcart -lgurt

#include <cart/types.h>
#include <cart/api.h>
*/
import "C"

import (
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/daos"
	"github.com/daos-stack/daos/src/control/logging"
)

func getProtocolInfo(log logging.Logger, provider string) ([]*crtFabricDevice, error) {
	var cInfo *C.struct_crt_protocol_info
	var cProtoStr *C.char
	if provider != "" {
		log.Debugf("getting fabric protocol info from CART for %q", provider)
		cProtoStr = C.CString(provider)
		defer C.free(unsafe.Pointer(cProtoStr))
	} else {
		log.Debug("getting all fabric protocol info from CART")
	}

	if err := daos.Status(C.crt_protocol_info_get(cProtoStr, &cInfo)); err != daos.Success {
		return nil, errors.Wrap(err, "crt_hg_get_protocol_info")
	}
	defer C.crt_protocol_info_free(cInfo)

	infoList := make([]*crtFabricDevice, 0)

	for cur := cInfo; cur != nil; cur = cur.next {
		infoList = append(infoList, cToCrtProtocolInfo(cur))
	}

	log.Debugf("CART protocol info discovered:\n%+v", infoList)
	return infoList, nil
}

func cToCrtProtocolInfo(cInfo *C.struct_crt_protocol_info) *crtFabricDevice {
	return &crtFabricDevice{
		Class:    C.GoString(cInfo.class_name),
		Protocol: C.GoString(cInfo.protocol_name),
		Device:   C.GoString(cInfo.device_name),
	}
}
