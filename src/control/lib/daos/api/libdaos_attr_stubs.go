//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build test_stubs
// +build test_stubs

package api

import (
	"unsafe"

	"github.com/daos-stack/daos/src/control/lib/daos"
)

import "C"

var (
	daos_default_AttrList daos.AttributeList = daos.AttributeList{
		{
			Name:  "one",
			Value: []byte("1"),
		},
		{
			Name:  "two",
			Value: []byte("2"),
		},
		{
			Name:  "three",
			Value: []byte("3"),
		},
	}
)

func daos_test_get_mappedNames(nameMap map[string]struct{}) []string {
	names := make([]string, 0, len(nameMap))
	for name := range nameMap {
		names = append(names, name)
	}
	return names
}

func list_attrs(buf *C.char, size *C.size_t, RCList []C.int, CallCount *int, RC C.int, AttrList daos.AttributeList) C.int {
	if len(RCList) > 0 {
		rc := RCList[*CallCount]
		*CallCount++
		if rc != 0 {
			return rc
		}
	}
	if RC != 0 {
		return RC
	}

	bufSize := 0
	for _, attr := range AttrList {
		bufSize += len(attr.Name) + 1
	}
	*size = C.size_t(bufSize)

	if buf == nil {
		return RC
	}

	bufSlice := unsafe.Slice((*C.char)(buf), bufSize)
	bufPtr := 0
	for _, attr := range AttrList {
		for i := 0; i < len(attr.Name); i++ {
			bufSlice[bufPtr] = C.char(attr.Name[i])
			bufPtr++
		}
		bufSlice[bufPtr] = C.char(0)
		bufPtr++
	}

	return RC
}

func get_attr(n C.int, names **C.char, values *unsafe.Pointer, sizes *C.size_t,
	RCList []C.int, CallCount *int, RC C.int, AttrList daos.AttributeList, SetN *int, ReqNames *map[string]struct{}) C.int {
	if len(RCList) > 0 {
		rc := RCList[*CallCount]
		*CallCount++
		if rc != 0 {
			return rc
		}
	}
	if RC != 0 {
		return RC
	}

	*SetN = int(n)
	*ReqNames = make(map[string]struct{})
	cReqNames := unsafe.Slice(names, n)
	for i := 0; i < int(n); i++ {
		reqNames := *ReqNames
		reqNames[C.GoString(cReqNames[i])] = struct{}{}
	}

	if len(*ReqNames) > 0 && len(AttrList) == 0 {
		return -C.int(daos.Nonexistent)
	}

	attrListMap := AttrList.AsMap()
	reqAttrCt := 0
	for attrName := range *ReqNames {
		if _, ok := attrListMap[attrName]; !ok {
			return -C.int(daos.Nonexistent)
		}
		reqAttrCt++
	}

	if reqAttrCt == 0 {
		return RC
	}

	var valuesSlice []unsafe.Pointer
	if values != nil {
		valuesSlice = unsafe.Slice(values, reqAttrCt)
	}
	sizesSlice := unsafe.Slice(sizes, reqAttrCt)
	idx := 0
	for _, attr := range AttrList {
		reqNames := *ReqNames
		if _, ok := reqNames[attr.Name]; !ok {
			continue
		}
		sizesSlice[idx] = C.size_t(len(attr.Value))
		if values != nil {
			valSlice := unsafe.Slice((*byte)(valuesSlice[idx]), sizesSlice[idx])
			copy(valSlice[:], attr.Value)
		}
		idx++
	}

	return RC
}

func set_attr(n C.int, names **C.char, values *unsafe.Pointer, sizes *C.size_t, RC C.int, AttrList *daos.AttributeList) C.int {
	if RC != 0 {
		return RC
	}

	namesSlice := unsafe.Slice(names, n)
	valuesSlice := unsafe.Slice(values, n)
	sizesSlice := unsafe.Slice(sizes, n)
	attrList := *AttrList
	for i := 0; i < int(n); i++ {
		valueSlice := unsafe.Slice((*byte)(valuesSlice[i]), sizesSlice[i])
		attrList = append(attrList, &daos.Attribute{
			Name:  C.GoString(namesSlice[i]),
			Value: make([]byte, sizesSlice[i]),
		})
		copy(attrList[len(attrList)-1].Value, valueSlice)
	}
	*AttrList = attrList

	return RC
}

func del_attr(n C.int, name **C.char, RC C.int, AttrNames *[]string) C.int {
	if RC != 0 {
		return RC
	}

	attrNames := *AttrNames
	nameSlice := unsafe.Slice(name, n)
	for i := 0; i < int(n); i++ {
		attrNames = append(attrNames, C.GoString(nameSlice[i]))
	}
	*AttrNames = attrNames

	return RC
}
