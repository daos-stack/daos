/**
 * Copyright (c) 2013-2022 UChicago Argonne, LLC and The HDF Group.
 * Copyright (c) 2022-2023 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "example_rpc.h"
#include "example_rpc_engine.h"

#include <aio.h>
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* example_rpc:
 * This is an example RPC operation.  It includes a small bulk transfer,
 * driven by the server, that moves data from the client to the server.  The
 * server writes the data to a local file.
 */

/* There are 3 key callbacks here:
 * - my_rpc_handler(): handles an incoming RPC operation
 * - my_rpc_handler_bulk_cb(): handles completion of bulk transfer
 * - my_rpc_handler_write_cb(): handles completion of async write and sends
 *   response
 */

/* NOTES: this is all event-driven.  Data is written using an aio operation
 * with SIGEV_THREAD notification.
 *
 * Note that the open and close are blocking for now because there is no
 * standard aio variant of those functions.
 *
 * All I/O calls *could* be blocking here.  The problem with that approach is
 * that you would need to use a thread pool with mercury to prevent it from
 * stalling while running callbacks, and the threadpool size would then
 * dictate both request concurrency and I/O concurrency simultaneously.  You
 * would still also need to handle callbacks for the HG transfer.
 */
static hg_return_t
my_rpc_handler(hg_handle_t handle);
static hg_return_t
my_rpc_handler_bulk_cb(const struct hg_cb_info *info);
static void
my_rpc_handler_write_cb(union sigval sig);

/* struct used to carry state of overall operation across callbacks */
struct my_rpc_state {
    hg_size_t size;
    void *buffer;
    hg_bulk_t bulk_handle;
    hg_handle_t handle;
    struct aiocb acb;
    my_rpc_in_t in;
};

/* register this particular rpc type with Mercury */
hg_id_t
my_rpc_register(void)
{
    hg_class_t *hg_class;
    hg_id_t tmp;

    hg_class = hg_engine_get_class();

    tmp = MERCURY_REGISTER(
        hg_class, "my_rpc", my_rpc_in_t, my_rpc_out_t, my_rpc_handler);

    return (tmp);
}

/* callback/handler triggered upon receipt of rpc request */
static hg_return_t
my_rpc_handler(hg_handle_t handle)
{
    hg_return_t ret;
    struct my_rpc_state *my_rpc_state_p;
    const struct hg_info *hgi;

    /* set up state structure */
    my_rpc_state_p = malloc(sizeof(*my_rpc_state_p));
    assert(my_rpc_state_p);
    my_rpc_state_p->size = 512;
    my_rpc_state_p->handle = handle;
    /* This includes allocating a target buffer for bulk transfer */
    my_rpc_state_p->buffer = calloc(1, 512);
    assert(my_rpc_state_p->buffer);

    /* decode input */
    ret = HG_Get_input(handle, &my_rpc_state_p->in);
    assert(ret == HG_SUCCESS);

    printf(
        "Got RPC request with input_val: %d\n", my_rpc_state_p->in.input_val);

    /* register local target buffer for bulk access */
    hgi = HG_Get_info(handle);
    assert(hgi);
    ret = HG_Bulk_create(hgi->hg_class, 1, &my_rpc_state_p->buffer,
        &my_rpc_state_p->size, HG_BULK_WRITE_ONLY,
        &my_rpc_state_p->bulk_handle);
    assert(ret == 0);

    /* initiate bulk transfer from client to server */
    ret = HG_Bulk_transfer(hgi->context, my_rpc_handler_bulk_cb, my_rpc_state_p,
        HG_BULK_PULL, hgi->addr, my_rpc_state_p->in.bulk_handle, 0,
        my_rpc_state_p->bulk_handle, 0, my_rpc_state_p->size, HG_OP_ID_IGNORE);
    assert(ret == 0);
    (void) ret;

    return (0);
}

/* callback triggered upon completion of bulk transfer */
static hg_return_t
my_rpc_handler_bulk_cb(const struct hg_cb_info *info)
{
    struct my_rpc_state *my_rpc_state_p = info->arg;
    int ret;
    char filename[256];

    assert(info->ret == 0);

    /* open file (NOTE: this is blocking for now, for simplicity ) */
    sprintf(filename, "/tmp/hg-stock-%d.txt", my_rpc_state_p->in.input_val);
    memset(&my_rpc_state_p->acb, 0, sizeof(my_rpc_state_p->acb));
    my_rpc_state_p->acb.aio_fildes =
        open(filename, O_WRONLY | O_CREAT, S_IWUSR | S_IRUSR);
    assert(my_rpc_state_p->acb.aio_fildes > -1);

    /* set up async I/O operation (write the bulk data that we just pulled
     * from the client)
     */
    my_rpc_state_p->acb.aio_offset = 0;
    my_rpc_state_p->acb.aio_buf = my_rpc_state_p->buffer;
    my_rpc_state_p->acb.aio_nbytes = 512;
    my_rpc_state_p->acb.aio_sigevent.sigev_notify = SIGEV_THREAD;
    my_rpc_state_p->acb.aio_sigevent.sigev_notify_attributes = NULL;
    my_rpc_state_p->acb.aio_sigevent.sigev_notify_function =
        my_rpc_handler_write_cb;
    my_rpc_state_p->acb.aio_sigevent.sigev_value.sival_ptr = my_rpc_state_p;

    /* post async write (just dump data to stdout) */
    ret = aio_write(&my_rpc_state_p->acb);
    assert(ret == 0);
    (void) ret;

    return (0);
}

/* callback triggered upon completion of async write */
static void
my_rpc_handler_write_cb(union sigval sig)
{
    struct my_rpc_state *my_rpc_state_p = sig.sival_ptr;
    hg_return_t ret;
    int rc;
    my_rpc_out_t out;

    rc = aio_error(&my_rpc_state_p->acb);
    assert(rc == 0);
    (void) rc;
    out.ret = 0;

    /* NOTE: really this should be nonblocking */
    close(my_rpc_state_p->acb.aio_fildes);

    /* send ack to client */
    /* NOTE: don't bother specifying a callback here for completion of sending
     * response.  This is just a best effort response.
     */
    ret = HG_Respond(my_rpc_state_p->handle, NULL, NULL, &out);
    assert(ret == HG_SUCCESS);
    (void) ret;

    HG_Bulk_free(my_rpc_state_p->bulk_handle);
    HG_Destroy(my_rpc_state_p->handle);
    free(my_rpc_state_p->buffer);
    free(my_rpc_state_p);

    return;
}
