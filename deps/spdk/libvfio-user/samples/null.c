/*
 * Copyright (c) 2019, Nutanix Inc. All rights reserved.
 *     Author: Thanos Makatos <thanos@nutanix.com>
 *             Swapnil Ingle <swapnil.ingle@nutanix.com>
 *             Felipe Franciosi <felipe@nutanix.com>
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *      * Redistributions of source code must retain the above copyright
 *        notice, this list of conditions and the following disclaimer.
 *      * Redistributions in binary form must reproduce the above copyright
 *        notice, this list of conditions and the following disclaimer in the
 *        documentation and/or other materials provided with the distribution.
 *      * Neither the name of Nutanix nor the names of its contributors may be
 *        used to endorse or promote products derived from this software without
 *        specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 *  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 *  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 *  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 *  DAMAGE.
 *
 */

/* null PCI device, doesn't do anything */

#include <stdio.h>
#include <err.h>
#include <stdlib.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

#include "common.h"
#include "libvfio-user.h"

static void
null_log(vfu_ctx_t *vfu_ctx UNUSED, UNUSED int level, char const *msg)
{
	fprintf(stderr, "null: %s", msg);
}


static void* null_drive(void *arg)
{
    vfu_ctx_t *vfu_ctx = (vfu_ctx_t*)arg;
    int ret = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    if (ret != 0) {
        fprintf(stderr, "failed to enable cancel state: %s\n", strerror(ret));
        return NULL;
    }
    ret = pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, NULL);
    if (ret != 0) {
        fprintf(stderr, "failed to enable cancel type: %s\n", strerror(ret));
        return NULL;
    }
    ret = vfu_realize_ctx(vfu_ctx);
    if (ret < 0) {
        fprintf(stderr, "failed to realize device\n");
        return NULL;
    }
    ret = vfu_attach_ctx(vfu_ctx);
    if (ret < 0) {
        fprintf(stderr, "failed to attach device\n");
        return NULL;
    }
    printf("starting device emulation\n");
    vfu_run_ctx(vfu_ctx);
    return NULL;
}

int main(int argc, char **argv)
{
    int ret;
    pthread_t thread;

    if (argc != 2) {
        errx(EXIT_FAILURE, "missing vfio-user socket path");
    }

    vfu_ctx_t *vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK, argv[1], 0, NULL,
                                        VFU_DEV_TYPE_PCI);
    if (vfu_ctx == NULL) {
        err(EXIT_FAILURE, "failed to create libvfio-user context");
    }

    ret = vfu_setup_log(vfu_ctx, null_log, LOG_DEBUG);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to setup log");
    }

    if (vfu_pci_init(vfu_ctx, VFU_PCI_TYPE_CONVENTIONAL,
                     PCI_HEADER_TYPE_NORMAL, 0) < 0) {
        err(EXIT_FAILURE, "vfu_pci_init() failed");
    }

    ret = pthread_create(&thread, NULL, null_drive, vfu_ctx);
    if (ret != 0) {
        errno = ret;
        err(EXIT_FAILURE, "failed to create pthread");
    }

    printf("press enter to stop device emulation and clean up\n");
    if (getchar() == EOF) {
        err(EXIT_FAILURE, NULL);
    }

    ret = pthread_cancel(thread);
    if (ret != 0) {
        errno = ret;
        err(EXIT_FAILURE, "failed to create pthread");
    }
    vfu_destroy_ctx(vfu_ctx);

    printf("device emulation stopped and cleaned up, press enter to exit\n");
    if (getchar() == EOF) {
        err(EXIT_FAILURE, NULL);
    }

    return ret;
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
