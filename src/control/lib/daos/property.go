//
// (C) Copyright 2025 Google LLC
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

package daos

import (
	"fmt"
	"unsafe"

	"github.com/pkg/errors"
)

/*
#include <daos_prop.h>

static inline char *
get_dpe_str(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return NULL;

	return dpe->dpe_str;
}

static inline uint64_t
get_dpe_val(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return -1;

	return dpe->dpe_val;
}

static inline void *
get_dpe_val_ptr(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return NULL;

	return dpe->dpe_val_ptr;
}

static inline bool
dpe_is_unset(struct daos_prop_entry *dpe)
{
	if (dpe == NULL)
		return 0;

	return dpe->dpe_flags & DAOS_PROP_ENTRY_NOT_SET;
}

static inline void
set_dpe_str(struct daos_prop_entry *dpe, d_string_t str)
{
	if (dpe == NULL)
		return;

	dpe->dpe_str = str;
}

static inline void
set_dpe_val(struct daos_prop_entry *dpe, uint64_t val)
{
	if (dpe == NULL)
		return;

	dpe->dpe_val = val;
}

static inline void
set_dpe_val_ptr(struct daos_prop_entry *dpe, void *val_ptr)
{
	if (dpe == NULL)
		return;

	dpe->dpe_val_ptr = val_ptr;
}
*/
import "C"

var (
	// ErrPropertyListImmutable indicates that the property list should not be modified or reused.
	ErrPropertyListImmutable = errors.New("property list is immutable")
)

type (
	property struct {
		idx   C.int
		entry *C.struct_daos_prop_entry
	}

	propertyList struct {
		cProps    *C.daos_prop_t
		entries   []C.struct_daos_prop_entry
		immutable bool
	}
)

func (p *property) IsUnset() bool {
	return bool(C.dpe_is_unset(p.entry))
}

func (p *property) GetString() string {
	dpeStr := C.get_dpe_str(p.entry)
	if dpeStr == nil {
		return ""
	}

	return C.GoString(dpeStr)
}

func (p *property) GetValue() uint64 {
	return uint64(C.get_dpe_val(p.entry))
}

func (p *property) GetValuePtr() unsafe.Pointer {
	return unsafe.Pointer(C.get_dpe_val_ptr(p.entry))
}

func (p *property) SetString(str string) {
	C.set_dpe_str(p.entry, C.d_string_t(C.CString(str)))
}

func (p *property) SetValue(val uint64) {
	C.set_dpe_val(p.entry, C.uint64_t(val))
}

func (p *property) SetValuePtr(val unsafe.Pointer) {
	C.set_dpe_val_ptr(p.entry, val)
}

func (p *property) String() string {
	return fmt.Sprintf("%+v", p.entry)
}

func newPropertyList(count int) (*propertyList, error) {
	if count < 0 {
		return nil, errors.Wrap(InvalidInput, "negative count")
	}

	props := C.daos_prop_alloc(C.uint(count))
	if props == nil {
		return nil, errors.Wrap(NoMemory, "failed to allocate property list")
	}

	// Set to zero initially, will be incremented as properties are added.
	props.dpp_nr = 0

	return &propertyList{
		cProps:  props,
		entries: unsafe.Slice(props.dpp_entries, count),
	}, nil
}

func (pl *propertyList) Free() {
	C.daos_prop_free(pl.cProps)
}

func (pl *propertyList) Count() int {
	return int(pl.cProps.dpp_nr)
}

func (pl *propertyList) Properties() (props []*property) {
	for i := range pl.entries {
		props = append(props, &property{
			idx:   C.int(i),
			entry: &pl.entries[i],
		})
	}
	return
}

func (pl *propertyList) ToPtr() unsafe.Pointer {
	// NB: Once the property list has been sent to the C API,
	// it should not be modified or reused.
	pl.immutable = true
	return unsafe.Pointer(pl.cProps)
}
