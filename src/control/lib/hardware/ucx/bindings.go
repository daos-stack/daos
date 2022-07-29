//
// (C) Copyright 2022 Intel Corporation.
//
// SPDX-License-Identifier: BSD-2-Clause-Patent
//
//go:build ucx
// +build ucx

package ucx

/*
#include <assert.h>
#include <stdlib.h>
#include <uct/api/uct.h>

void
call_ucs_debug_disable_signal(void *fn, int signum)
{
	void (*disable)(int);

	assert(fn != NULL);
	disable = fn;

	disable(signum);
}

uct_component_h
get_component_from_list(uct_component_h *components, uint idx)
{
	assert(components != NULL);

	return components[idx];
}

char *
get_component_attr_name(uct_component_attr_t *attr)
{
	assert(attr != NULL);

	return attr->name;
}

char *
get_md_resource_name_from_list(uct_md_resource_desc_t *mdr, uint idx)
{
	assert(mdr != NULL);

	return mdr[idx].md_name;
}

char *
get_tl_resource_name_from_list(uct_tl_resource_desc_t *tlr, uint idx)
{
	assert(tlr != NULL);

	return tlr[idx].tl_name;
}

char *
get_tl_resource_device_from_list(uct_tl_resource_desc_t *tlr, uint idx)
{
	assert(tlr != NULL);

	return tlr[idx].dev_name;
}

uct_device_type_t
get_tl_resource_type_from_list(uct_tl_resource_desc_t *tlr, uint idx)
{
	assert(tlr != NULL);

	return tlr[idx].dev_type;
}

const char *
get_tl_resource_dev_type_str(void *map, uct_device_type_t dev_type)
{
	const char **names = (const char **)map;

	return names[dev_type];
}

void
alloc_uct_component_attr_md_resources(uct_component_attr_t *attr, unsigned count)
{
	attr->md_resources = calloc(sizeof(*attr->md_resources), count);
}

ucs_status_t
call_uct_query_components(void *fn, uct_component_h **components, unsigned *num_components)
{
	ucs_status_t (*query)(uct_component_h **, unsigned *);

	assert(fn != NULL);
	query = fn;

	return query(components, num_components);
}

void
call_uct_release_component_list(void *fn, uct_component_h *components)
{
	void (*release)(uct_component_h *);

	assert(fn != NULL);
	release = fn;

	release(components);
}

ucs_status_t
call_uct_component_query(void *fn, uct_component_h component, uct_component_attr_t *attr)
{
	ucs_status_t (*query)(uct_component_h, uct_component_attr_t *);

	assert(fn != NULL);
	query = fn;

	return query(component, attr);
}

ucs_status_t
call_uct_md_config_read(void *fn, uct_component_h component, uct_md_config_t **md_config)
{
	ucs_status_t (*cfg_read)(uct_component_h, const char *, const char *, uct_md_config_t **);

	assert(fn != NULL);
	cfg_read = fn;

	return cfg_read(component, NULL, NULL, md_config);
}

void
call_uct_config_release(void *fn, void *config)
{
	void (*release)(void *);

	assert(fn != NULL);
	release = fn;

	release(config);
}

ucs_status_t
call_uct_md_open(void *fn, uct_component_h component, const char *md_name,
		 const uct_md_config_t *config, uct_md_h *md)
{
	ucs_status_t (*md_open)(uct_component_h, const char *, const uct_md_config_t *, uct_md_h *);

	assert(fn != NULL);
	md_open = fn;

	return md_open(component, md_name, config, md);
}

void
call_uct_md_close(void *fn, uct_md_h md)
{
	void (*md_close)(uct_md_h);

	assert(fn != NULL);
	md_close = fn;

	md_close(md);
}

ucs_status_t
call_uct_md_query_tl_resources(void *fn, uct_md_h md, uct_tl_resource_desc_t **resources,
			       unsigned *num_resources)
{
	ucs_status_t (*query_tl)(uct_md_h, uct_tl_resource_desc_t **, unsigned *);

	assert(fn != NULL);
	query_tl = fn;

	return query_tl(md, resources, num_resources);
}

void
call_uct_release_tl_resource_list(void *fn, uct_tl_resource_desc_t *resources)
{
	void (*release)(uct_tl_resource_desc_t *);

	assert(fn != NULL);
	release = fn;

	release(resources);
}
*/
import "C"

import (
	"fmt"
	"syscall"
	"unsafe"

	"github.com/pkg/errors"

	"github.com/daos-stack/daos/src/control/lib/dlopen"
)

// Load dynamically loads the UCX libraries and provides a method to unload them.
func Load() (func(), error) {
	ucsHdl, err := openUCS()
	if err != nil {
		return nil, err
	}
	defer ucsHdl.Close()

	if err := ucsDisableSignal(ucsHdl, syscall.SIGSEGV); err != nil {
		return nil, errors.Wrap(err, "disabling UCX signal handling")
	}

	hdl, err := openUCT()
	if err != nil {
		return nil, err
	}
	return func() {
		hdl.Close()
	}, nil
}

func openUCT() (*dlopen.LibHandle, error) {
	return dlopen.GetHandle([]string{"libuct.so"})
}

func openUCS() (*dlopen.LibHandle, error) {
	return dlopen.GetHandle([]string{"libucs.so"})
}

func ucsDisableSignal(hdl *dlopen.LibHandle, sig syscall.Signal) error {
	fn, err := getLibFuncPtr(hdl, "ucs_debug_disable_signal")
	if err != nil {
		return err
	}
	C.call_ucs_debug_disable_signal(fn, C.int(sig))
	return nil
}

func getLibFuncPtr(hdl *dlopen.LibHandle, fnName string) (unsafe.Pointer, error) {
	fnPtr, err := hdl.GetSymbolPointer(fnName)
	if err != nil {
		return nil, err
	}

	if fnPtr == nil {
		return nil, errors.Errorf("%q is nil", fnName)
	}

	return fnPtr, nil
}

type uctComponent struct {
	cComponent      C.uct_component_h
	name            string
	flags           uint64
	mdResourceCount uint
}

func getUCTComponents(uctHdl *dlopen.LibHandle) ([]*uctComponent, func() error, error) {
	cComponents, numComponents, err := uctQueryComponents(uctHdl)
	if err != nil {
		return nil, nil, errors.Wrap(err, "getting component list")
	}

	components := make([]*uctComponent, 0, numComponents)
	for i := C.uint(0); i < numComponents; i++ {
		comp := C.get_component_from_list(cComponents, i)

		var attr C.uct_component_attr_t

		attr.field_mask = C.UCT_COMPONENT_ATTR_FIELD_NAME |
			C.UCT_COMPONENT_ATTR_FIELD_MD_RESOURCE_COUNT |
			C.UCT_COMPONENT_ATTR_FIELD_FLAGS
		if err := uctComponentQuery(uctHdl, comp, &attr); err != nil {
			uctReleaseComponentList(uctHdl, cComponents)
			return nil, nil, errors.Wrap(err, "querying component details")
		}

		components = append(components, &uctComponent{
			cComponent:      comp,
			name:            C.GoString(C.get_component_attr_name(&attr)),
			flags:           uint64(attr.flags),
			mdResourceCount: uint(attr.md_resource_count),
		})
	}

	return components, func() error {
		return uctReleaseComponentList(uctHdl, cComponents)
	}, nil
}

func uctQueryComponents(uctHdl *dlopen.LibHandle) (*C.uct_component_h, C.uint, error) {
	fn, err := getLibFuncPtr(uctHdl, "uct_query_components")
	if err != nil {
		return nil, 0, err
	}

	var cComponents *C.uct_component_h
	var cNumComponents C.uint

	status := C.call_uct_query_components(fn, &cComponents, &cNumComponents)
	if status != C.UCS_OK {
		return nil, 0, errors.Errorf("uct_query_components() failed: %d", status)
	}
	return cComponents, cNumComponents, nil
}

func uctReleaseComponentList(uctHdl *dlopen.LibHandle, components *C.uct_component_h) error {
	fn, err := getLibFuncPtr(uctHdl, "uct_release_component_list")
	if err != nil {
		return err
	}

	C.call_uct_release_component_list(fn, components)
	return nil
}

func getMDResourceNames(uctHdl *dlopen.LibHandle, component *uctComponent) ([]string, error) {
	var attr C.uct_component_attr_t

	attr.field_mask = C.UCT_COMPONENT_ATTR_FIELD_MD_RESOURCES
	C.alloc_uct_component_attr_md_resources(&attr, C.uint(component.mdResourceCount))
	if attr.md_resources == nil {
		return nil, errors.New("failed to allocate memory for MD resources")
	}
	defer C.free(unsafe.Pointer(attr.md_resources))

	if err := uctComponentQuery(uctHdl, component.cComponent, &attr); err != nil {
		return nil, errors.Wrapf(err, "getting MD resources for %q", component.name)
	}

	names := make([]string, 0, component.mdResourceCount)
	for i := uint(0); i < component.mdResourceCount; i++ {
		name := C.get_md_resource_name_from_list(attr.md_resources, C.uint(i))
		names = append(names, C.GoString(name))
	}

	return names, nil
}

func uctComponentQuery(uctHdl *dlopen.LibHandle, component C.uct_component_h, attr *C.uct_component_attr_t) error {
	fn, err := getLibFuncPtr(uctHdl, "uct_component_query")
	if err != nil {
		return err
	}

	status := C.call_uct_component_query(fn, component, attr)
	if status != C.UCS_OK {
		return errors.Errorf("uct_component_query() failed: %d", status)
	}

	return nil
}

type uctMDConfig struct {
	component string
	cCfg      *C.uct_md_config_t
}

func getComponentMDConfig(uctHdl *dlopen.LibHandle, comp *uctComponent) (*uctMDConfig, func() error, error) {
	mdCfg := &uctMDConfig{
		component: comp.name,
	}

	cCfg, err := uctMDConfigRead(uctHdl, comp.cComponent)
	if err != nil {
		return nil, nil, errors.Wrapf(err, "fetching MD config for %q", comp.name)
	}

	mdCfg.cCfg = cCfg

	return mdCfg, func() error {
		return errors.Wrap(uctMDConfigFree(uctHdl, unsafe.Pointer(cCfg)), "freeing MD config")
	}, nil
}

func uctMDConfigRead(uctHdl *dlopen.LibHandle, component C.uct_component_h) (*C.uct_md_config_t, error) {
	fn, err := getLibFuncPtr(uctHdl, "uct_md_config_read")
	if err != nil {
		return nil, err
	}

	var cfg *C.uct_md_config_t
	status := C.call_uct_md_config_read(fn, component, &cfg)
	if status != C.UCS_OK {
		return nil, errors.Errorf("uct_md_config_read() failed: %d", status)
	}

	return cfg, nil
}

func uctMDConfigFree(uctHdl *dlopen.LibHandle, config unsafe.Pointer) error {
	fn, err := getLibFuncPtr(uctHdl, "uct_config_release")
	if err != nil {
		return err
	}

	C.call_uct_config_release(fn, config)

	return nil
}

type uctMD struct {
	name string
	cMD  C.uct_md_h
}

func openMDResource(uctHdl *dlopen.LibHandle, comp *uctComponent, mdName string, cfg *uctMDConfig) (*uctMD, func() error, error) {
	cMD, err := uctMDOpen(uctHdl, comp.cComponent, mdName, cfg.cCfg)
	if err != nil {
		return nil, nil, errors.Wrapf(err, "opening MD %q", mdName)
	}

	md := &uctMD{
		name: mdName,
		cMD:  cMD,
	}
	return md, func() error {
		return errors.Wrapf(uctMDClose(uctHdl, cMD), "closing MD %q", mdName)
	}, nil
}

func uctMDOpen(uctHdl *dlopen.LibHandle, component C.uct_component_h, name string, config *C.uct_md_config_t) (C.uct_md_h, error) {
	fn, err := getLibFuncPtr(uctHdl, "uct_md_open")
	if err != nil {
		return nil, err
	}

	var md C.uct_md_h
	cName := C.CString(name)
	defer C.free(unsafe.Pointer(cName))
	status := C.call_uct_md_open(fn, component, cName, config, &md)
	if status != C.UCS_OK {
		return nil, errors.Errorf("uct_md_open() failed: %d", status)
	}

	return md, nil
}

func uctMDClose(uctHdl *dlopen.LibHandle, md C.uct_md_h) error {
	fn, err := getLibFuncPtr(uctHdl, "uct_md_close")
	if err != nil {
		return err
	}

	C.call_uct_md_close(fn, md)
	return nil
}

type transportDev struct {
	transport string
	device    string
	devType   C.uct_device_type_t
}

func (d *transportDev) String() string {
	return fmt.Sprintf("transport=%q, device=%q", d.transport, d.device)
}

func (d *transportDev) isNetwork() bool {
	return d.devType == C.UCT_DEVICE_TYPE_NET
}

func getMDTransportDevices(uctHdl *dlopen.LibHandle, md *uctMD) ([]*transportDev, error) {
	tlResources, tlResourceCount, err := uctMDQueryTLResources(uctHdl, md.cMD)
	if err != nil {
		return nil, errors.Wrapf(err, "querying TL resources for %q", md.name)
	}
	defer uctReleaseTLResourceList(uctHdl, tlResources)

	tlDevs := make([]*transportDev, 0, int(tlResourceCount))
	for i := C.uint(0); i < tlResourceCount; i++ {
		tlDevs = append(tlDevs, &transportDev{
			transport: C.GoString(C.get_tl_resource_name_from_list(tlResources, i)),
			device:    C.GoString(C.get_tl_resource_device_from_list(tlResources, i)),
			devType:   C.get_tl_resource_type_from_list(tlResources, i),
		})
	}

	return tlDevs, nil
}

func uctMDQueryTLResources(uctHdl *dlopen.LibHandle, md C.uct_md_h) (*C.uct_tl_resource_desc_t, C.uint, error) {
	fn, err := getLibFuncPtr(uctHdl, "uct_md_query_tl_resources")
	if err != nil {
		return nil, 0, err
	}

	var resources *C.uct_tl_resource_desc_t
	var num_resources C.uint
	status := C.call_uct_md_query_tl_resources(fn, md, &resources, &num_resources)
	if status != C.UCS_OK {
		return nil, 0, errors.Errorf("uct_md_query_tl_resources() failed: %d", status)
	}

	return resources, num_resources, nil
}

func uctReleaseTLResourceList(uctHdl *dlopen.LibHandle, resources *C.uct_tl_resource_desc_t) error {
	fn, err := getLibFuncPtr(uctHdl, "uct_release_tl_resource_list")
	if err != nil {
		return err
	}

	C.call_uct_release_tl_resource_list(fn, resources)
	return nil
}
