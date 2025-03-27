/*
 * Copyright (c) 2020 Nutanix Inc. All rights reserved.
 *
 * Authors: Thanos Makatos <thanos@nutanix.com>
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

#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdlib.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/eventfd.h>
#include <time.h>
#include <err.h>
#include <assert.h>
#include <sys/stat.h>
#include <libgen.h>
#include <pthread.h>
#include <linux/limits.h>

#include "common.h"
#include "libvfio-user.h"
#include "rte_hash_crc.h"
#include "tran_sock.h"

#define CLIENT_MAX_FDS (32)

/* This is low, so we get testing of vfu_dma_read/write() chunking. */
#define CLIENT_MAX_DATA_XFER_SIZE (1024)

static char const *irq_to_str[] = {
    [VFU_DEV_INTX_IRQ] = "INTx",
    [VFU_DEV_MSI_IRQ] = "MSI",
    [VFU_DEV_MSIX_IRQ] = "MSI-X",
    [VFU_DEV_ERR_IRQ] = "ERR",
    [VFU_DEV_REQ_IRQ] = "REQ"
};

void
vfu_log(UNUSED vfu_ctx_t *vfu_ctx, UNUSED int level,
        const char *fmt, ...)
{
    va_list ap;

    printf("client: ");

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

static int
init_sock(const char *path)
{
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    int sock;

    /* TODO path should be defined elsewhere */
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path);

    if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        err(EXIT_FAILURE, "failed to open socket %s", path);
    }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        err(EXIT_FAILURE, "failed to connect server");
    }
    return sock;
}

static void
send_version(int sock)
{
    struct vfio_user_version cversion;
    struct iovec iovecs[3] = { { 0 } };
    char client_caps[1024];
    int msg_id = 0xbada55;
    int slen;
    int ret;

    slen = snprintf(client_caps, sizeof(client_caps),
        "{"
            "\"capabilities\":{"
                "\"max_msg_fds\":%u,"
                "\"max_data_xfer_size\":%u,"
                "\"migration\":{"
                    "\"pgsize\":%zu"
                "}"
            "}"
         "}", CLIENT_MAX_FDS, CLIENT_MAX_DATA_XFER_SIZE, sysconf(_SC_PAGESIZE));

    cversion.major = LIB_VFIO_USER_MAJOR;
    cversion.minor = LIB_VFIO_USER_MINOR;

    /* [0] is for the header. */
    iovecs[1].iov_base = &cversion;
    iovecs[1].iov_len = sizeof(cversion);
    iovecs[2].iov_base = client_caps;
    /* Include the NUL. */
    iovecs[2].iov_len = slen + 1;

    ret = tran_sock_send_iovec(sock, msg_id, false, VFIO_USER_VERSION,
                               iovecs, ARRAY_SIZE(iovecs), NULL, 0, 0);

    if (ret < 0) {
        err(EXIT_FAILURE, "failed to send client version message");
    }
}

static void
recv_version(int sock, int *server_max_fds, size_t *server_max_data_xfer_size,
             size_t *pgsize)
{
    struct vfio_user_version *sversion = NULL;
    struct vfio_user_header hdr;
    size_t vlen;
    int ret;

    ret = tran_sock_recv_alloc(sock, &hdr, true, NULL,
                               (void **)&sversion, &vlen);

    if (ret < 0) {
        err(EXIT_FAILURE, "failed to receive version");
    }

    if (hdr.cmd != VFIO_USER_VERSION) {
        errx(EXIT_FAILURE, "msg%hx: invalid cmd %hu (expected %u)",
               hdr.msg_id, hdr.cmd, VFIO_USER_VERSION);
    }

    if (vlen < sizeof(*sversion)) {
        errx(EXIT_FAILURE, "VFIO_USER_VERSION: invalid size %lu", vlen);
    }

    if (sversion->major != LIB_VFIO_USER_MAJOR) {
        errx(EXIT_FAILURE, "unsupported server major %hu (must be %u)",
               sversion->major, LIB_VFIO_USER_MAJOR);
    }

    /*
     * The server is supposed to tell us the minimum agreed version.
     */
    if (sversion->minor > LIB_VFIO_USER_MINOR) {
        errx(EXIT_FAILURE, "unsupported server minor %hu (must be <= %u)",
               sversion->minor, LIB_VFIO_USER_MINOR);
    }

    *server_max_fds = 1;
    *server_max_data_xfer_size = VFIO_USER_DEFAULT_MAX_DATA_XFER_SIZE;
    *pgsize = sysconf(_SC_PAGESIZE);

    if (vlen > sizeof(*sversion)) {
        const char *json_str = (const char *)sversion->data;
        size_t len = vlen - sizeof(*sversion);

        if (json_str[len - 1] != '\0') {
            errx(EXIT_FAILURE, "ignoring invalid JSON from server");
        }

        ret = tran_parse_version_json(json_str, server_max_fds,
                                      server_max_data_xfer_size, pgsize);

        if (ret < 0) {
            err(EXIT_FAILURE, "failed to parse server JSON \"%s\"", json_str);
        }
    }

    free(sversion);
}

static void
negotiate(int sock, int *server_max_fds, size_t *server_max_data_xfer_size,
          size_t *pgsize)
{
    send_version(sock);
    recv_version(sock, server_max_fds, server_max_data_xfer_size, pgsize);
}

static void
send_device_reset(int sock)
{
    int ret = tran_sock_msg(sock, 1, VFIO_USER_DEVICE_RESET,
                            NULL, 0, NULL, NULL, 0);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to reset device");
    }
}

/* returns whether a VFIO migration capability is found */
static bool
get_region_vfio_caps(struct vfio_info_cap_header *header,
                     struct vfio_region_info_cap_sparse_mmap **sparse)
{
    struct vfio_region_info_cap_type *type;
    unsigned int i;
    bool migr = false;

    while (true) {
        switch (header->id) {
            case VFIO_REGION_INFO_CAP_SPARSE_MMAP:
                *sparse = (struct vfio_region_info_cap_sparse_mmap *)header;
                printf("client: %s: Sparse cap nr_mmap_areas %d\n", __func__,
                       (*sparse)->nr_areas);
                for (i = 0; i < (*sparse)->nr_areas; i++) {
                    printf("client: %s: area %d offset %#llx size %llu\n",
                           __func__, i, (*sparse)->areas[i].offset,
                           (*sparse)->areas[i].size);
                }
                break;
            case VFIO_REGION_INFO_CAP_TYPE:
                type = (struct vfio_region_info_cap_type*)header;
                if (type->type != VFIO_REGION_TYPE_MIGRATION ||
                    type->subtype != VFIO_REGION_SUBTYPE_MIGRATION) {
                    errx(EXIT_FAILURE, "bad region type %d/%d", type->type,
                         type->subtype);
                }
                migr = true;
                printf("client: migration region\n");
                break;
            default:
                errx(EXIT_FAILURE, "bad VFIO cap ID %#x", header->id);
        }
        if (header->next == 0) {
            break;
        }
        header = (struct vfio_info_cap_header*)((char*)header + header->next - sizeof(struct vfio_region_info));
    }
    return migr;
}

static void
do_get_device_region_info(int sock, struct vfio_region_info *region_info,
                          int *fds, size_t *nr_fds)
{
    int ret = tran_sock_msg_fds(sock, 0xabcd, VFIO_USER_DEVICE_GET_REGION_INFO,
                                region_info, region_info->argsz, NULL,
                                region_info, region_info->argsz, fds, nr_fds);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to get device region info");
    }
}

static void
mmap_sparse_areas(int *fds, struct vfio_region_info *region_info,
                  struct vfio_region_info_cap_sparse_mmap *sparse)
{
    size_t i;

    for (i = 0; i < sparse->nr_areas; i++) {

        ssize_t ret;
        void *addr;
        char pathname[PATH_MAX];
        char buf[PATH_MAX] = "";

        ret = snprintf(pathname, sizeof(pathname), "/proc/self/fd/%d", fds[i]);
        assert(ret != -1 && (size_t)ret < sizeof(pathname));
        ret = readlink(pathname, buf, sizeof(buf) - 1);
        if (ret == -1) {
            err(EXIT_FAILURE, "failed to resolve file descriptor %d", fds[i]);
        }
        addr = mmap(NULL, sparse->areas[i].size, PROT_READ | PROT_WRITE,
                    MAP_SHARED, fds[i], region_info->offset +
                    sparse->areas[i].offset);
        if (addr == MAP_FAILED) {
            err(EXIT_FAILURE,
                "failed to mmap sparse region #%lu in %s (%#llx-%#llx)",
                i, buf, sparse->areas[i].offset,
                sparse->areas[i].offset + sparse->areas[i].size - 1);
        }

        ret = munmap(addr, sparse->areas[i].size);
        assert(ret == 0);
    }
}

static void
get_device_region_info(int sock, uint32_t index)
{
    struct vfio_region_info *region_info;
    size_t cap_sz;
    size_t size = sizeof(struct vfio_region_info);
    int fds[CLIENT_MAX_FDS] = { 0 };
    size_t nr_fds = ARRAY_SIZE(fds);


    region_info = alloca(size);
    memset(region_info, 0, size);
    region_info->argsz = size;
    region_info->index = index;

    do_get_device_region_info(sock, region_info, NULL, 0);
    if (region_info->argsz > size) {
        size = region_info->argsz;
        region_info = alloca(size);
        memset(region_info, 0, size);
        region_info->argsz = size;
        region_info->index = index;
        do_get_device_region_info(sock, region_info, fds, &nr_fds);
        assert(region_info->argsz == size);
    } else {
        nr_fds = 0;
    }

    cap_sz = region_info->argsz - sizeof(struct vfio_region_info);
    printf("client: %s: region_info[%d] offset %#llx flags %#x size %llu "
           "cap_sz %lu #FDs %lu\n", __func__, index, region_info->offset,
           region_info->flags, region_info->size, cap_sz, nr_fds);
    if (cap_sz) {
        struct vfio_region_info_cap_sparse_mmap *sparse = NULL;
        if (get_region_vfio_caps((struct vfio_info_cap_header*)(region_info + 1),
                                 &sparse)) {
            if (sparse != NULL) {
                assert((index == VFU_PCI_DEV_BAR1_REGION_IDX && nr_fds == 2) ||
                       (index == VFU_PCI_DEV_MIGR_REGION_IDX && nr_fds == 1));
                assert(nr_fds == sparse->nr_areas);
                mmap_sparse_areas(fds, region_info, sparse);
            }
        }
    }
}

static void
get_device_regions_info(int sock, struct vfio_user_device_info *client_dev_info)
{
    unsigned int i;

    for (i = 0; i < client_dev_info->num_regions; i++) {
        get_device_region_info(sock, i);
    }
}

static void
get_device_info(int sock, struct vfio_user_device_info *dev_info)
{
    uint16_t msg_id = 0xb10c;
    int ret;

    dev_info->argsz = sizeof(*dev_info);

    ret = tran_sock_msg(sock, msg_id,
                        VFIO_USER_DEVICE_GET_INFO,
                        dev_info, sizeof(*dev_info),
                        NULL,
                        dev_info, sizeof(*dev_info));

    if (ret < 0) {
        err(EXIT_FAILURE, "failed to get device info");
    }

    if (dev_info->num_regions != 10) {
        errx(EXIT_FAILURE, "bad number of device regions %d",
             dev_info->num_regions);
    }

    printf("client: devinfo: flags %#x, num_regions %d, num_irqs %d\n",
           dev_info->flags, dev_info->num_regions, dev_info->num_irqs);
}

static int
configure_irqs(int sock)
{
    struct iovec iovecs[2] = { { 0, } };
    struct vfio_irq_set irq_set;
    uint16_t msg_id = 0x1bad;
    int irq_fd;
    int i, ret;

    for (i = 0; i < VFU_DEV_NUM_IRQS; i++) { /* TODO move body of loop into function */
        struct vfio_irq_info vfio_irq_info = {
            .argsz = sizeof(vfio_irq_info),
            .index = i
        };
        ret = tran_sock_msg(sock, msg_id,
                            VFIO_USER_DEVICE_GET_IRQ_INFO,
                            &vfio_irq_info, sizeof(vfio_irq_info),
                            NULL,
                            &vfio_irq_info, sizeof(vfio_irq_info));
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to get %s info", irq_to_str[i]);
        }
        if (vfio_irq_info.count > 0) {
            printf("client: IRQ %s: count=%d flags=%#x\n",
                   irq_to_str[i], vfio_irq_info.count, vfio_irq_info.flags);
        }
    }

    msg_id++;

    irq_set.argsz = sizeof(irq_set);
    irq_set.flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set.index = 0;
    irq_set.start = 0;
    irq_set.count = 1;
    irq_fd = eventfd(0, 0);
    if (irq_fd == -1) {
        err(EXIT_FAILURE, "failed to create eventfd");
    }

    /* [0] is for the header. */
    iovecs[1].iov_base = &irq_set;
    iovecs[1].iov_len = sizeof(irq_set);

    ret = tran_sock_msg_iovec(sock, msg_id, VFIO_USER_DEVICE_SET_IRQS,
                              iovecs, ARRAY_SIZE(iovecs),
                              &irq_fd, 1,
                              NULL, NULL, 0, NULL, 0);

    if (ret < 0) {
        err(EXIT_FAILURE, "failed to send configure IRQs message");
    }

    return irq_fd;
}

static int
access_region(int sock, int region, bool is_write, uint64_t offset,
            void *data, size_t data_len)
{
    static int msg_id = 0xf00f;
    struct vfio_user_region_access send_region_access = {
        .offset = offset,
        .region = region,
        .count = data_len
    };
    struct iovec send_iovecs[3] = {
        [1] = {
            .iov_base = &send_region_access,
            .iov_len = sizeof(send_region_access)
        },
        [2] = {
            .iov_base = data,
            .iov_len = data_len
        }
    };
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    struct vfio_user_region_access *recv_data;
    size_t nr_send_iovecs, recv_data_len;
    int op, ret;

    if (is_write) {
        op = VFIO_USER_REGION_WRITE;
        nr_send_iovecs = 3;
        recv_data_len = sizeof(*recv_data);
    } else {
        op = VFIO_USER_REGION_READ;
        nr_send_iovecs = 2;
        recv_data_len = sizeof(*recv_data) + data_len;
    }

    recv_data = calloc(1, recv_data_len);

    if (recv_data == NULL) {
        err(EXIT_FAILURE, "failed to alloc recv_data");
    }

    pthread_mutex_lock(&mutex);
    ret = tran_sock_msg_iovec(sock, msg_id--, op,
                              send_iovecs, nr_send_iovecs,
                              NULL, 0, NULL,
                              recv_data, recv_data_len, NULL, 0);
    pthread_mutex_unlock(&mutex);
    if (ret != 0) {
        warn("failed to %s region %d %#lx-%#lx",
             is_write ? "write to" : "read from", region, offset,
             offset + data_len - 1);
        free(recv_data);
        return ret;
    }
    if (recv_data->count != data_len) {
        warnx("bad %s data count, expected=%lu, actual=%d",
             is_write ? "write" : "read", data_len,
             recv_data->count);
        free(recv_data);
        errno = EINVAL;
        return -1;
    }

    /*
     * TODO we could avoid the memcpy if tran_sock_msg_iovec() received the
     * response into an iovec, but it's some work to implement it.
     */
    if (!is_write) {
        memcpy(data, ((char *)recv_data) + sizeof(*recv_data), data_len);
    }
    free(recv_data);
    return 0;
}

static void
access_bar0(int sock, time_t *t)
{
    int ret;

    assert(t != NULL);

    ret = access_region(sock, VFU_PCI_DEV_BAR0_REGION_IDX, true, 0, t, sizeof(*t));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to write to BAR0");
    }

    printf("client: wrote to BAR0: %ld\n", *t);

    ret = access_region(sock, VFU_PCI_DEV_BAR0_REGION_IDX, false, 0, t, sizeof(*t));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to read from BAR0");
    }

    printf("client: read from BAR0: %ld\n", *t);
}

static void
wait_for_irq(int irq_fd)
{
    uint64_t val;

    if (read(irq_fd, &val, sizeof(val)) == -1) {
        err(EXIT_FAILURE, "failed to read from irqfd");
    }
    printf("client: INTx triggered!\n");
}

static void
handle_dma_write(int sock, struct vfio_user_dma_map *dma_regions,
                 int nr_dma_regions, int *dma_region_fds)
{
    struct vfio_user_dma_region_access dma_access;
    struct vfio_user_header hdr;
    int ret, i;
    size_t size = sizeof(dma_access);
    uint16_t msg_id = 0xcafe;
    void *data;

    ret = tran_sock_recv(sock, &hdr, false, &msg_id, &dma_access, &size);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to receive DMA read");
    }

    data = calloc(dma_access.count, 1);
    if (data == NULL) {
        err(EXIT_FAILURE, NULL);
    }

    if (recv(sock, data, dma_access.count, 0) == -1) {
        err(EXIT_FAILURE, "failed to receive DMA read data");
    }

    for (i = 0; i < nr_dma_regions; i++) {
        off_t offset;
        ssize_t c;

        if (dma_access.addr < dma_regions[i].addr ||
            dma_access.addr >= dma_regions[i].addr + dma_regions[i].size) {
            continue;
        }

        offset = dma_regions[i].offset + dma_access.addr;

        c = pwrite(dma_region_fds[i], data, dma_access.count, offset);

        if (c != (ssize_t)dma_access.count) {
            err(EXIT_FAILURE, "failed to write to fd=%d at [%#lx-%#lx)",
                    dma_region_fds[i], offset, offset + dma_access.count);
        }
        break;
    }

    assert(i != nr_dma_regions);

    ret = tran_sock_send(sock, msg_id, true, VFIO_USER_DMA_WRITE,
                         &dma_access, sizeof(dma_access));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to send reply of DMA write");
    }
    free(data);
}

static void
handle_dma_read(int sock, struct vfio_user_dma_map *dma_regions,
                int nr_dma_regions, int *dma_region_fds)
{
    struct vfio_user_dma_region_access dma_access, *response;
    struct vfio_user_header hdr;
    int ret, i, response_sz;
    size_t size = sizeof(dma_access);
    uint16_t msg_id = 0xcafe;
    void *data;

    ret = tran_sock_recv(sock, &hdr, false, &msg_id, &dma_access, &size);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to receive DMA read");
    }

    response_sz = sizeof(dma_access) + dma_access.count;
    response = calloc(response_sz, 1);
    if (response == NULL) {
        err(EXIT_FAILURE, NULL);
    }
    response->addr = dma_access.addr;
    response->count = dma_access.count;
    data = (char *)response->data;

    for (i = 0; i < nr_dma_regions; i++) {
        off_t offset;
        ssize_t c;

        if (dma_access.addr < dma_regions[i].addr ||
            dma_access.addr >= dma_regions[i].addr + dma_regions[i].size) {
            continue;
        }

        offset = dma_regions[i].offset + dma_access.addr;

        c = pread(dma_region_fds[i], data, dma_access.count, offset);

        if (c != (ssize_t)dma_access.count) {
            err(EXIT_FAILURE, "failed to read from fd=%d at [%#lx-%#lx)",
                    dma_region_fds[i], offset, offset + dma_access.count);
        }
        break;
    }

    assert(i != nr_dma_regions);

    ret = tran_sock_send(sock, msg_id, true, VFIO_USER_DMA_READ,
                         response, response_sz);
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to send reply of DMA read");
    }
    free(response);
}

static void
handle_dma_io(int sock, struct vfio_user_dma_map *dma_regions,
              int nr_dma_regions, int *dma_region_fds)
{
    size_t i;

    for (i = 0; i < 4096 / CLIENT_MAX_DATA_XFER_SIZE; i++) {
        handle_dma_write(sock, dma_regions, nr_dma_regions, dma_region_fds);
    }
    for (i = 0; i < 4096 / CLIENT_MAX_DATA_XFER_SIZE; i++) {
        handle_dma_read(sock, dma_regions, nr_dma_regions, dma_region_fds);
    }
}

static void
get_dirty_bitmap(int sock, struct vfio_user_dma_map *dma_region)
{
    uint64_t bitmap_size = _get_bitmap_size(dma_region->size,
                                            sysconf(_SC_PAGESIZE));
    struct vfio_user_dirty_pages *dirty_pages;
    struct vfio_user_bitmap_range *range;
    char *bitmap;
    size_t size;
    void *data;
    int ret;

    size = sizeof(*dirty_pages) + sizeof(*range) + bitmap_size;

    data = calloc(1, size);
    assert(data != NULL);

    dirty_pages = data;
    dirty_pages->flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_GET_BITMAP;
    dirty_pages->argsz = sizeof(*dirty_pages) + sizeof(*range) + bitmap_size;

    range = data + sizeof(*dirty_pages);
    range->iova = dma_region->addr;
    range->size = dma_region->size;
    range->bitmap.size = bitmap_size;
    range->bitmap.pgsize = sysconf(_SC_PAGESIZE);

    bitmap = data + sizeof(*dirty_pages) + sizeof(*range);

    ret = tran_sock_msg(sock, 0x99, VFIO_USER_DIRTY_PAGES,
                        data, sizeof(*dirty_pages) + sizeof(*range),
                        NULL, data, size);
    if (ret != 0) {
        err(EXIT_FAILURE, "failed to get dirty page bitmap");
    }

    printf("client: %s: %#lx-%#lx\t%#x\n", __func__, range->iova,
           range->iova + range->size - 1, bitmap[0]);

    free(data);
}

static void
usage(char *argv0)
{
    fprintf(stderr, "Usage: %s [-h] [-m src|dst] /path/to/socket\n",
            basename(argv0));
}

/*
 * Normally each time the source client (QEMU) would read migration data from
 * the device it would send them to the destination client. However, since in
 * our sample both the source and the destination client are the same process,
 * we simply accumulate the migration data of each iteration and apply it to
 * the destination server at the end.
 *
 * Performs as many migration loops as @nr_iters or until the device has no
 * more migration data (pending_bytes is zero), which ever comes first. The
 * result of each migration iteration is stored in @migr_iter.  @migr_iter must
 * be at least @nr_iters.
 *
 * @returns the number of iterations performed
 */
static size_t
do_migrate(int sock, size_t nr_iters, struct iovec *migr_iter)
{
    int ret;
    uint64_t pending_bytes, data_offset, data_size;
    size_t i = 0;

    assert(nr_iters > 0);

    /* XXX read pending_bytes */
    ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, false,
                        offsetof(struct vfio_user_migration_info, pending_bytes),
                        &pending_bytes, sizeof(pending_bytes));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to read pending_bytes");
    }

    for (i = 0; i < nr_iters && pending_bytes > 0; i++) {

        /* XXX read data_offset and data_size */
        ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, false,
                            offsetof(struct vfio_user_migration_info, data_offset),
                            &data_offset, sizeof(data_offset));
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to read data_offset");
        }

        ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, false,
                            offsetof(struct vfio_user_migration_info, data_size),
                            &data_size, sizeof(data_size));
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to read data_size");
        }

        migr_iter[i].iov_len = data_size;
        migr_iter[i].iov_base = malloc(data_size);
        if (migr_iter[i].iov_base == NULL) {
            err(EXIT_FAILURE, "failed to allocate migration buffer");
        }

        /* XXX read migration data */
        ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, false,
                            data_offset,
                            (char *)migr_iter[i].iov_base, data_size);
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to read migration data");
        }

        /* FIXME send migration data to the destination client process */

        /*
         * XXX read pending_bytes again to indicate to the server that the
         * migration data have been consumed.
         */
        ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, false,
                            offsetof(struct vfio_user_migration_info, pending_bytes),
                            &pending_bytes, sizeof(pending_bytes));
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to read pending_bytes");
        }
    }
    return i;
}

struct fake_guest_data {
    int sock;
    size_t bar1_size;
    bool done;
    uint32_t *crcp;
};

static void *
fake_guest(void *arg)
{
    struct fake_guest_data *fake_guest_data = arg;
    int ret;
    char buf[fake_guest_data->bar1_size];
    FILE *fp = fopen("/dev/urandom", "r");
    uint32_t crc = 0;

    if (fp == NULL) {
        err(EXIT_FAILURE, "failed to open /dev/urandom");
    }


    do {
        ret = fread(buf, fake_guest_data->bar1_size, 1, fp);
        if (ret != 1) {
            errx(EXIT_FAILURE, "short read %d", ret);
        }
        ret = access_region(fake_guest_data->sock, 1, true, 0, buf,
                            fake_guest_data->bar1_size);
        if (ret != 0) {
            err(EXIT_FAILURE, "fake guest failed to write garbage to BAR1");
        }
        crc = rte_hash_crc(buf, fake_guest_data->bar1_size, crc);
        __sync_synchronize();
    } while (!fake_guest_data->done);

    *fake_guest_data->crcp = crc;

    return NULL;
}

static size_t
migrate_from(int sock, size_t *nr_iters, struct iovec **migr_iters,
             uint32_t *crcp, size_t bar1_size)
{
    uint32_t device_state;
    int ret;
    size_t _nr_iters;
    pthread_t thread;
    struct fake_guest_data fake_guest_data = {
        .sock = sock,
        .bar1_size = bar1_size,
        .done = false,
        .crcp = crcp
    };

    ret = pthread_create(&thread, NULL, fake_guest, &fake_guest_data);
    if (ret != 0) {
        errno = ret;
        err(EXIT_FAILURE, "failed to create pthread");
    }

    *nr_iters = 2;
    *migr_iters = malloc(sizeof(struct iovec) * *nr_iters);
    if (*migr_iters == NULL) {
        err(EXIT_FAILURE, NULL);
    }

    /*
     * XXX set device state to pre-copy. This is technically optional but any
     * VMM that cares about performance needs this.
     */
    device_state = VFIO_DEVICE_STATE_V1_SAVING | VFIO_DEVICE_STATE_V1_RUNNING;
    ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, true,
                        offsetof(struct vfio_user_migration_info, device_state),
                        &device_state, sizeof(device_state));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to write to device state");
    }

    _nr_iters = do_migrate(sock, 1, *migr_iters);
    assert(_nr_iters == 1);
    printf("client: stopping fake guest thread\n");
    fake_guest_data.done = true;
    __sync_synchronize();
    ret = pthread_join(thread, NULL);
    if (ret != 0) {
        errno = ret;
        err(EXIT_FAILURE, "failed to join fake guest pthread");
    }

    printf("client: setting device state to stop-and-copy\n");

    device_state = VFIO_DEVICE_STATE_V1_SAVING;
    ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, true,
                        offsetof(struct vfio_user_migration_info, device_state),
                        &device_state, sizeof(device_state));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to write to device state");
    }

    _nr_iters += do_migrate(sock, 1, (*migr_iters) + _nr_iters);
    if (_nr_iters != 2) {
        errx(EXIT_FAILURE,
             "expected 2 iterations instead of %ld while in stop-and-copy state",
             _nr_iters);
    }

    /* XXX read device state, migration must have finished now */
    device_state = VFIO_DEVICE_STATE_V1_STOP;
    ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, true,
                              offsetof(struct vfio_user_migration_info, device_state),
                              &device_state, sizeof(device_state));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to write to device state");
    }

    return _nr_iters;
}

static int
migrate_to(char *old_sock_path, int *server_max_fds,
           size_t *server_max_data_xfer_size, size_t *pgsize, size_t nr_iters,
           struct iovec *migr_iters, char *path_to_server,
           uint32_t src_crc, size_t bar1_size)
{
    int ret, sock;
    char *sock_path;
    struct stat sb;
    uint32_t device_state = VFIO_DEVICE_STATE_V1_RESUMING;
    uint64_t data_offset, data_len;
    size_t i;
    uint32_t dst_crc;
    char buf[bar1_size];

    assert(old_sock_path != NULL);

    printf("client: starting destination server\n");

    ret = asprintf(&sock_path, "%s_migrated", old_sock_path);
    if (ret == -1) {
        err(EXIT_FAILURE, "failed to asprintf");
    }

    ret = fork();
    if (ret == -1) {
        err(EXIT_FAILURE, "failed to fork");
    }
    if (ret > 0) { /* child (destination server) */
        char *_argv[] = {
            path_to_server,
            (char *)"-v",
            sock_path,
            NULL
        };
        ret = execvp(_argv[0] , _argv);
        if (ret != 0) {
            err(EXIT_FAILURE, "failed to start destination server (%s)",
                              path_to_server);
        }
    }

    /* parent (client) */

    /* wait for the server to come up */
    while (stat(sock_path, &sb) == -1) {
        if (errno != ENOENT) {
            err(EXIT_FAILURE, "failed to stat %s", sock_path);
        }
    }
   if ((sb.st_mode & S_IFMT) != S_IFSOCK) {
       errx(EXIT_FAILURE, "%s: not a socket", sock_path);
   }

    /* connect to the destination server */
    sock = init_sock(sock_path);
    free(sock_path);

    negotiate(sock, server_max_fds, server_max_data_xfer_size, pgsize);

    /* XXX set device state to resuming */
    ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, true,
                        offsetof(struct vfio_user_migration_info, device_state),
                        &device_state, sizeof(device_state));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to set device state to resuming");
    }

    for (i = 0; i < nr_iters; i++) {

        /* XXX read data offset */
        ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, false,
                            offsetof(struct vfio_user_migration_info, data_offset),
                            &data_offset, sizeof(data_offset));
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to read migration data offset");
        }

        /* XXX write migration data */

        /*
         * TODO write half of migration data via regular write and other half via
         * memopy map.
         */
        printf("client: writing migration device data %#lx-%#lx\n",
               data_offset, data_offset + migr_iters[i].iov_len - 1);
        ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, true,
                            data_offset, migr_iters[i].iov_base,
                            migr_iters[i].iov_len);
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to write device migration data");
        }

        /* XXX write data_size */
        data_len = migr_iters[i].iov_len;
        ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, true,
                            offsetof(struct vfio_user_migration_info, data_size),
                            &data_len, sizeof(data_len));
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to write migration data size");
        }
    }

    /* XXX set device state to running */
    device_state = VFIO_DEVICE_STATE_V1_RUNNING;
    ret = access_region(sock, VFU_PCI_DEV_MIGR_REGION_IDX, true,
                            offsetof(struct vfio_user_migration_info, device_state),
                            &device_state, sizeof(device_state));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to set device state to running");
    }

    /* validate contents of BAR1 */

    if (access_region(sock, 1, false, 0, buf, bar1_size) != 0) {
        err(EXIT_FAILURE, "failed to read BAR1");
    }

    dst_crc = rte_hash_crc(buf, bar1_size, 0);

    if (dst_crc != src_crc) {
        fprintf(stderr, "client: CRC mismatch: %u != %u\n", src_crc, dst_crc);
        abort();
    }

    return sock;
}

static void
map_dma_regions(int sock, struct vfio_user_dma_map *dma_regions,
                int *dma_region_fds, int nr_dma_regions)
{
    int i, ret;

    for (i = 0; i < nr_dma_regions; i++) {
        struct iovec iovecs[2] = {
            /* [0] is for the header. */
            [1] = {
                .iov_base = &dma_regions[i],
                .iov_len = sizeof(*dma_regions)
            }
        };
        ret = tran_sock_msg_iovec(sock, 0x1234 + i, VFIO_USER_DMA_MAP,
                                  iovecs, ARRAY_SIZE(iovecs),
                                  &dma_region_fds[i], 1,
                                  NULL, NULL, 0, NULL, 0);
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to map DMA regions");
        }
    }
}

int main(int argc, char *argv[])
{
    char template[] = "/tmp/libvfio-user.XXXXXX";
    int ret, sock, irq_fd;
    struct vfio_user_dma_map *dma_regions;
    struct vfio_user_device_info client_dev_info = {0};
    int *dma_region_fds;
    int i;
    int tmpfd;
    int server_max_fds;
    size_t server_max_data_xfer_size;
    size_t pgsize;
    int nr_dma_regions;
    struct vfio_user_dirty_pages dirty_pages = {0};
    int opt;
    time_t t;
    char *path_to_server = NULL;
    vfu_pci_hdr_t config_space;
    struct iovec *migr_iters;
    size_t nr_iters;
    uint32_t crc;
    size_t bar1_size = 0x3000; /* FIXME get this value from region info */

    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (argc != optind + 1) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    sock = init_sock(argv[optind]);

    /*
     * VFIO_USER_VERSION
     *
     * Do intial negotiation with the server, and discover parameters.
     */
    negotiate(sock, &server_max_fds, &server_max_data_xfer_size, &pgsize);

    /* try to access a bogus region, we should get an error */
    ret = access_region(sock, 0xdeadbeef, false, 0, &ret, sizeof(ret));
    if (ret != -1 || errno != EINVAL) {
        errx(EXIT_FAILURE,
             "expected EINVAL accessing bogus region, got %d instead", errno);
    }

    /* XXX VFIO_USER_DEVICE_GET_INFO */
    get_device_info(sock, &client_dev_info);

    /* VFIO_USER_DEVICE_GET_REGION_INFO */
    get_device_regions_info(sock, &client_dev_info);

    ret = access_region(sock, VFU_PCI_DEV_CFG_REGION_IDX, false, 0, &config_space,
                        sizeof(config_space));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to read PCI configuration space");
    }

    assert(config_space.id.vid == 0xdead);
    assert(config_space.id.did == 0xbeef);
    assert(config_space.ss.vid == 0xcafe);
    assert(config_space.ss.sid == 0xbabe);

    /* XXX VFIO_USER_DEVICE_RESET */
    send_device_reset(sock);

    /*
     * XXX VFIO_USER_DMA_MAP
     *
     * Tell the server we have some DMA regions it can access.
     */
    nr_dma_regions = server_max_fds << 1;

    umask(0022);

    if ((tmpfd = mkstemp(template)) == -1) {
        err(EXIT_FAILURE, "failed to create backing file");
    }

    if ((ret = ftruncate(tmpfd, nr_dma_regions * sysconf(_SC_PAGESIZE))) == -1) {
        err(EXIT_FAILURE, "failed to truncate file");
    }

    unlink(template);

    dma_regions = alloca(sizeof(*dma_regions) * nr_dma_regions);
    dma_region_fds = alloca(sizeof(*dma_region_fds) * nr_dma_regions);

    for (i = 0; i < nr_dma_regions; i++) {
        dma_regions[i].argsz = sizeof(struct vfio_user_dma_map);
        dma_regions[i].addr = i * sysconf(_SC_PAGESIZE);
        dma_regions[i].size = sysconf(_SC_PAGESIZE);
        dma_regions[i].offset = dma_regions[i].addr;
        dma_regions[i].flags = VFIO_USER_F_DMA_REGION_READ | VFIO_USER_F_DMA_REGION_WRITE;
        dma_region_fds[i] = tmpfd;
    }

    map_dma_regions(sock, dma_regions, dma_region_fds, nr_dma_regions);

    /*
     * XXX VFIO_USER_DEVICE_GET_IRQ_INFO and VFIO_IRQ_SET_ACTION_TRIGGER
     * Query interrupts and configure an eventfd to be associated with INTx.
     */
    irq_fd = configure_irqs(sock);

    dirty_pages.argsz = sizeof(dirty_pages);
    dirty_pages.flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_START;
    ret = tran_sock_msg(sock, 0, VFIO_USER_DIRTY_PAGES,
                        &dirty_pages, sizeof(dirty_pages),
                        NULL, NULL, 0);
    if (ret != 0) {
        err(EXIT_FAILURE, "failed to start dirty page logging");
    }

    /*
     * XXX VFIO_USER_REGION_READ and VFIO_USER_REGION_WRITE
     *
     * BAR0 in the server does not support memory mapping so it must be accessed
     * via explicit messages.
     */
    t = time(NULL) + 1;
    access_bar0(sock, &t);

    wait_for_irq(irq_fd);

    /* FIXME check that above took at least 1s */

    handle_dma_io(sock, dma_regions, nr_dma_regions, dma_region_fds);

    for (i = 0; i < nr_dma_regions; i++) {
        get_dirty_bitmap(sock, &dma_regions[i]);
    }

    dirty_pages.argsz = sizeof(dirty_pages);
    dirty_pages.flags = VFIO_IOMMU_DIRTY_PAGES_FLAG_STOP;
    ret = tran_sock_msg(sock, 0, VFIO_USER_DIRTY_PAGES,
                        &dirty_pages, sizeof(dirty_pages),
                        NULL, NULL, 0);
    if (ret != 0) {
        err(EXIT_FAILURE, "failed to stop dirty page logging");
    }

    /* BAR1 can be memory mapped and read directly */

    /*
     * XXX VFIO_USER_DMA_UNMAP
     *
     * unmap the first group of the DMA regions
     */
    for (i = 0; i < server_max_fds; i++) {
        struct vfio_user_dma_unmap r = {
            .argsz = sizeof(r),
            .addr = dma_regions[i].addr,
            .size = dma_regions[i].size
        };
        ret = tran_sock_msg(sock, 7, VFIO_USER_DMA_UNMAP, &r, sizeof(r),
                            NULL, &r, sizeof(r));
        if (ret < 0) {
            err(EXIT_FAILURE, "failed to unmap DMA region");
        }
    }

    /*
     * Schedule an interrupt in 10 seconds from now in the old server and then
     * immediatelly migrate the device. The new server should deliver the
     * interrupt. Hopefully 10 seconds should be enough for migration to finish.
     * TODO make this value a command line option.
     */
    t = time(NULL) + 10;
    ret = access_region(sock, VFU_PCI_DEV_BAR0_REGION_IDX, true, 0, &t, sizeof(t));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to write to BAR0");
    }

    nr_iters = migrate_from(sock, &nr_iters, &migr_iters, &crc, bar1_size);

    /*
     * Normally the client would now send the device state to the destination
     * client and then exit. We don't demonstrate how this works as this is a
     * client implementation detail. Instead, the client starts the destination
     * server and then applies the migration data.
     */
    if (asprintf(&path_to_server, "%s/server", dirname(argv[0])) == -1) {
        err(EXIT_FAILURE, "failed to asprintf");
    }

    sock = migrate_to(argv[optind], &server_max_fds, &server_max_data_xfer_size,
                      &pgsize, nr_iters, migr_iters, path_to_server,
                      crc, bar1_size);
    free(path_to_server);
    for (i = 0; i < (int)nr_iters; i++) {
        free(migr_iters[i].iov_base);
    }
    free(migr_iters);

    /*
     * Now we must reconfigure the destination server.
     */

    /*
     * XXX reconfigure DMA regions, note that the first half of the has been
     * unmapped.
     */
    map_dma_regions(sock, dma_regions + server_max_fds,
                    dma_region_fds + server_max_fds,
                    nr_dma_regions - server_max_fds);

    /*
     * XXX reconfigure IRQs.
     * FIXME is this something the client needs to do? I would expect so since
     * it's the client that creates and provides the FD. Do we need to save some
     * state in the migration data?
     */
    irq_fd = configure_irqs(sock);

    wait_for_irq(irq_fd);

    handle_dma_io(sock, dma_regions + server_max_fds,
                  nr_dma_regions - server_max_fds,
                  dma_region_fds + server_max_fds);

    struct vfio_user_dma_unmap r = {
        .argsz = sizeof(r),
        .addr = 0,
        .size = 0,
        .flags = VFIO_DMA_UNMAP_FLAG_ALL
    };
    ret = tran_sock_msg(sock, 8, VFIO_USER_DMA_UNMAP, &r, sizeof(r),
                        NULL, &r, sizeof(r));
    if (ret < 0) {
        err(EXIT_FAILURE, "failed to unmap all DMA regions");
    }

    return 0;
}

/* ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: */
