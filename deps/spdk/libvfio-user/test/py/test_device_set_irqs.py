#
# Copyright (c) 2021 Nutanix Inc. All rights reserved.
#
# Authors: John Levon <john.levon@nutanix.com>
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions are met:
#      * Redistributions of source code must retain the above copyright
#        notice, this list of conditions and the following disclaimer.
#      * Redistributions in binary form must reproduce the above copyright
#        notice, this list of conditions and the following disclaimer in the
#        documentation and/or other materials provided with the distribution.
#      * Neither the name of Nutanix nor the names of its contributors may be
#        used to endorse or promote products derived from this software without
#        specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
#  ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
#  DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
#  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
#  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
#  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
#  OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
#  DAMAGE.
#

from libvfio_user import *
import errno
import os

ctx = None
sock = None

argsz = len(vfio_irq_set())


def test_device_set_irqs_setup():
    global ctx, sock

    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None

    ret = vfu_pci_init(ctx)
    assert ret == 0

    ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_REQ_IRQ, 1)
    assert ret == 0
    ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_ERR_IRQ, 1)
    assert ret == 0
    ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_MSIX_IRQ, 2048)
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    sock = connect_client(ctx)


def test_device_set_irqs_no_irq_set():
    hdr = vfio_user_header(VFIO_USER_DEVICE_SET_IRQS, size=0)
    sock.send(hdr)
    vfu_run_ctx(ctx)
    get_reply(sock, expect=errno.EINVAL)


def test_device_set_irqs_short_write():
    payload = struct.pack("II", 0, 0)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_argsz():
    payload = vfio_irq_set(argsz=3, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_REQ_IRQ,
                           start=0, count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_index():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_NUM_IRQS,
                           start=0, count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_flags_MASK_and_UNMASK():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_MASK |
                           VFIO_IRQ_SET_ACTION_UNMASK, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_flags_DATA_NONE_and_DATA_BOOL():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_MASK |
                           VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_DATA_BOOL,
                           index=VFU_DEV_MSIX_IRQ, start=0, count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_start_count_range():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_MASK |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_MSIX_IRQ,
                           start=2047, count=2)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_start_count_range2():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_MASK |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_MSIX_IRQ,
                           start=2049, count=1)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_action_for_err_irq():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_MASK |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_ERR_IRQ,
                           start=0, count=1)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_action_for_req_irq():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_MASK |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_REQ_IRQ,
                           start=0, count=1)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_start_for_count_0():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_MASK |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_MSIX_IRQ,
                           start=1, count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_action_for_count_0():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_MASK |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_action_and_data_type_for_count_0():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_BOOL, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_bad_fds_for_DATA_BOOL():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_BOOL, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=1)

    payload = bytes(payload) + struct.pack("?", False)

    fd = eventfd()

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL, fds=[fd])

    os.close(fd)


def test_device_set_irqs_bad_fds_for_DATA_NONE():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=1)

    fd = eventfd()

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL, fds=[fd])

    os.close(fd)


def test_device_set_irqs_bad_fds_for_count_2():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_EVENTFD, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=2)

    fd = eventfd()

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL, fds=[fd])

    os.close(fd)


def test_device_set_irqs_disable():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_REQ_IRQ,
                           start=0, count=0)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload)

    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_EVENTFD, index=VFU_DEV_REQ_IRQ,
                           start=0, count=1)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload)


def test_device_set_irqs_enable():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_EVENTFD, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=1)

    fd = eventfd()

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload, fds=[fd])


def test_device_set_irqs_trigger_bool_too_small():
    payload = vfio_irq_set(argsz=argsz + 1, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_BOOL, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=2)
    payload = bytes(payload) + struct.pack("?", False)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_trigger_bool_too_large():
    payload = vfio_irq_set(argsz=argsz + 3, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_BOOL, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=2)
    payload = bytes(payload) + struct.pack("???", False, False, False)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload,
        expect=errno.EINVAL)


def test_device_set_irqs_enable_update():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_EVENTFD, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=1)

    fd = eventfd()

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload, fds=[fd])


def test_device_set_irqs_enable_trigger_none():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_EVENTFD, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=2)

    fd1 = eventfd(initval=4)
    fd2 = eventfd(initval=8)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload, fds=[fd1, fd2])

    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_NONE, index=VFU_DEV_MSIX_IRQ,
                           start=1, count=1)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload)

    assert struct.unpack("Q", os.read(fd1, 8))[0] == 4
    assert struct.unpack("Q", os.read(fd2, 8))[0] == 9


def test_device_set_irqs_enable_trigger_bool():
    payload = vfio_irq_set(argsz=argsz, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_EVENTFD, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=2)

    fd1 = eventfd(initval=4)
    fd2 = eventfd(initval=8)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload, fds=[fd1, fd2])

    payload = vfio_irq_set(argsz=argsz + 2, flags=VFIO_IRQ_SET_ACTION_TRIGGER |
                           VFIO_IRQ_SET_DATA_BOOL, index=VFU_DEV_MSIX_IRQ,
                           start=0, count=2)
    payload = bytes(payload) + struct.pack("??", False, True)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload)

    assert struct.unpack("Q", os.read(fd1, 8))[0] == 4
    assert struct.unpack("Q", os.read(fd2, 8))[0] == 9


def test_device_set_irqs_cleanup():
    vfu_destroy_ctx(ctx)

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab: #
