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
#include <daos_errno.h>
#include <gurt/common.h>
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

static inline int
set_dpe_str(struct daos_prop_entry *dpe, d_string_t str)
{
	if (dpe == NULL || str == NULL)
		return -DER_INVAL;

	// Duplicate the string with the DAOS allocator to avoid
	// false positives in NLT testing.
	// NB: Caller must free the source string.
	D_ASPRINTF(dpe->dpe_str, "%s", str);
	if (dpe->dpe_str == NULL)
		return -DER_NOMEM;

	return 0;
}

static inline int
set_dpe_val(struct daos_prop_entry *dpe, uint64_t val)
{
	if (dpe == NULL)
		return -DER_INVAL;

	dpe->dpe_val = val;
	return 0;
}

static inline int
set_dpe_val_ptr(struct daos_prop_entry *dpe, void *val_ptr)
{
	if (dpe == NULL)
		return -DER_INVAL;

	dpe->dpe_val_ptr = val_ptr;
	return 0;
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

// IsUnset returns true if the property value is unset.
func (p *property) IsUnset() bool {
	return bool(C.dpe_is_unset(p.entry))
}

// GetString returns the property value as a string.
func (p *property) GetString() string {
	dpeStr := C.get_dpe_str(p.entry)
	if dpeStr == nil {
		return ""
	}

	return C.GoString(dpeStr)
}

// GetValue returns the property value as a uint64.
func (p *property) GetValue() uint64 {
	return uint64(C.get_dpe_val(p.entry))
}

// GetValuePtr returns an unsafe.Pointer wrapping the
// C pointer stored in the property.
func (p *property) GetValuePtr() unsafe.Pointer {
	return unsafe.Pointer(C.get_dpe_val_ptr(p.entry))
}

// SetString sets the property value to the supplied string.
func (p *property) SetString(str string) error {
	// The string will be copied in set_dpe_str() and then
	// freed via daos_prop_free().
	cStr, free := toCString(str)
	defer free()
	return ErrorFromRC(int(C.set_dpe_str(p.entry, cStr)))
}

// SetValue sets the property value to the supplied value.
func (p *property) SetValue(val uint64) error {
	return ErrorFromRC(int(C.set_dpe_val(p.entry, C.uint64_t(val))))
}

// SetValuePtr sets the property value to the supplied pointer.
// NB: The pointer must be in C-allocated memory!
func (p *property) SetValuePtr(val unsafe.Pointer) error {
	return ErrorFromRC(int(C.set_dpe_val_ptr(p.entry, val)))
}

func (p *property) String() string {
	return fmt.Sprintf("%+v", p.entry)
}

func newPropertyList(count int) (*propertyList, error) {
	if count < 0 {
		return nil, errors.Wrap(InvalidInput, "negative count")
	}

	cProps := C.daos_prop_alloc(C.uint(count))
	if cProps == nil {
		return nil, errors.Wrap(NoMemory, "failed to allocate DAOS property list")
	}

	// Set to zero initially, will be incremented as properties are added.
	cProps.dpp_nr = 0

	return &propertyList{
		cProps:  cProps,
		entries: unsafe.Slice(cProps.dpp_entries, count),
	}, nil
}

// Free releases the underlying C property list.
func (pl *propertyList) Free() {
	if pl == nil {
		return
	}
	C.daos_prop_free(pl.cProps)
}

// Count returns the number of properties in the property list.
func (pl *propertyList) Count() int {
	return int(pl.cProps.dpp_nr)
}

// ToPtr returns an unsafe.Pointer to the underlying C property list.
// Callers must cast it to a *C.daos_prop_t when passing it to libdaos
// API functions. The property list may not be modified after this
// call, in order to avoid undefined behavior.
func (pl *propertyList) ToPtr() unsafe.Pointer {
	pl.immutable = true
	return unsafe.Pointer(pl.cProps)
}
