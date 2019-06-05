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

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <hwloc.h>

const int SUCCESS = 0;
const int FAILURE = -1;
const int ERROR_DLOPEN = -2;
const int ERROR_DLSYM = -3;

void * handle = NULL;
hwloc_topology_t topology;

const char * topology_init = "hwloc_topology_init";
const char * topology_set_flags = "hwloc_topology_set_flags";
const char * topology_load = "hwloc_topology_load";
const char * get_type_depth = "hwloc_get_type_depth";
const char * get_nbobjs_by_depth = "hwloc_get_nbobjs_by_depth";
const char * get_obj_by_depth = "hwloc_get_obj_by_depth";
const char * topology_destroy = "hwloc_topology_destroy";
const char * bitmap_asprintf = "hwloc_bitmap_asprintf";

int (* netdetect_topology_init)(hwloc_topology_t *) = NULL;
int (* netdetect_topology_set_flags)(hwloc_topology_t, unsigned long) = NULL;
int (* netdetect_topology_load)(hwloc_topology_t) = NULL;
int (* netdetect_get_type_depth)(hwloc_topology_t, hwloc_obj_type_t) = NULL;
unsigned (* netdetect_get_nbobjs_by_depth)(hwloc_topology_t, unsigned) = NULL;
hwloc_obj_t (* netdetect_get_obj_by_depth)(hwloc_topology_t, unsigned, unsigned) = NULL;
void (* netdetect_topology_destroy)(hwloc_topology_t) = NULL;
int (* netdetect_bitmap_asprintf)(char **, hwloc_const_bitmap_t) = NULL;


int loadLib(char *lib) {
    char *error = NULL;

    handle = dlopen(lib, RTLD_NOW);
    if (!handle) {
        error = dlerror();
        fprintf(stderr, "%s\n", error);
        return ERROR_DLOPEN;
    }
    return SUCCESS;
}

int InitializeTopologyLib(char * lib) {
    int status = FAILURE;
    char *error = NULL;

    status = loadLib(lib);
    if (status != SUCCESS) {
        fprintf(stdout, "Error on load lib ...\n");
        return status;
    }

    netdetect_topology_init = (int (*)(hwloc_topology_t *))
        dlsym(handle, topology_init);
    netdetect_topology_set_flags = (int (*)(hwloc_topology_t, unsigned long))
        dlsym(handle, topology_set_flags);
    netdetect_topology_load = (int (*)(hwloc_topology_t))
        dlsym(handle, topology_load);
    netdetect_get_type_depth = (int (*)(hwloc_topology_t, hwloc_obj_type_t))
        dlsym(handle, get_type_depth);
    netdetect_get_nbobjs_by_depth = (unsigned (*)(hwloc_topology_t, unsigned))
        dlsym(handle, get_nbobjs_by_depth);
    netdetect_get_obj_by_depth = (hwloc_obj_t (*)(hwloc_topology_t, unsigned, unsigned))
        dlsym(handle, get_obj_by_depth);
    netdetect_topology_destroy = (void (*)(hwloc_topology_t))
        dlsym(handle, topology_destroy);
    netdetect_bitmap_asprintf = (int (*)(char **, hwloc_const_bitmap_t))
        dlsym(handle, bitmap_asprintf);

    error = dlerror();
    if (error) {
        fprintf(stderr, "%s\n", error);
        return ERROR_DLSYM;
    }

    if (netdetect_topology_init == NULL ||
        netdetect_topology_set_flags == NULL ||
        netdetect_topology_load == NULL ||
        netdetect_get_type_depth == NULL ||
        netdetect_get_nbobjs_by_depth == NULL ||
        netdetect_get_obj_by_depth == NULL ||
        netdetect_topology_destroy == NULL ||
        netdetect_bitmap_asprintf == NULL) {
            fprintf(stderr, "One of the functions was null...\n");
            fflush(stderr);
            return ERROR_DLSYM;
    }

    fprintf(stdout, "hwloc mapped successfully.\n");
    fflush(stdout);
    return status;
}

int cleanup() {
    if (handle) {

        dlclose(handle);
        handle = NULL;
    }
}

char * GetAffinityForIONodes(void) {
    char * affinity;
    char * cpuset;
    char * nodeset;
    char * tmp;
    int depth;
    int numObj;
    int i;
    hwloc_obj_t nodeIO;
    hwloc_obj_t nodeAncestor;

    netdetect_topology_init(&topology);
    netdetect_topology_set_flags(topology, HWLOC_TOPOLOGY_FLAG_IO_DEVICES);
    netdetect_topology_load(topology);

    depth = netdetect_get_type_depth(topology, HWLOC_OBJ_OS_DEVICE);
    printf("Depth of OS device is %d\n", depth);

    numObj = netdetect_get_nbobjs_by_depth(topology, depth);
    printf("There are %d objects at depth %d\n", numObj, depth);

    affinity = (char *)malloc(sizeof(char));
    affinity[0] = '\0';
    for (i = 0; i < numObj; i++) {
        nodeIO = netdetect_get_obj_by_depth(topology, depth, i);
        nodeAncestor = hwloc_get_non_io_ancestor_obj(topology, nodeIO);
        netdetect_bitmap_asprintf(&cpuset, nodeAncestor->cpuset);
        netdetect_bitmap_asprintf(&nodeset, nodeAncestor->nodeset);
        fprintf(stdout, "Name: %s:%s:%s\n", nodeIO->name, cpuset, nodeset);
        asprintf(&tmp, "%s%s:%s:%s;", affinity, nodeIO->name, cpuset, nodeset);
        affinity = (char *)realloc(affinity, strlen(tmp) + 1);
        affinity[0] = '\0';
        strcat(affinity, tmp);
        free(tmp);
        free(cpuset);
        free(nodeset);
    }

    netdetect_topology_destroy(topology);
    cleanup();
    return affinity;
}