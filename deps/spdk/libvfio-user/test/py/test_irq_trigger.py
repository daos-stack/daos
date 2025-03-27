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
import ctypes as c
import errno

ctx = None
sock = None


def test_irq_trigger_setup():
    global ctx, sock

    ctx = vfu_create_ctx(flags=LIBVFIO_USER_FLAG_ATTACH_NB)
    assert ctx is not None

    ret = vfu_pci_init(ctx)
    assert ret == 0

    ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_MSIX_IRQ, 2048)
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    sock = connect_client(ctx)


def test_irq_trigger_bad_subindex():
    ret = vfu_irq_trigger(ctx, 2048)
    assert ret == -1
    assert c.get_errno() == errno.EINVAL

    ret = vfu_irq_trigger(ctx, 2049)
    assert ret == -1
    assert c.get_errno() == errno.EINVAL


def test_irq_trigger_no_interrupt():
    ret = vfu_irq_trigger(ctx, 0)
    assert ret == -1
    assert c.get_errno() == errno.ENOENT


def test_irq_trigger():
    # struct vfio_irq_set
    payload = struct.pack("IIIII", 20, VFIO_IRQ_SET_ACTION_TRIGGER |
                          VFIO_IRQ_SET_DATA_EVENTFD, VFU_DEV_MSIX_IRQ, 8, 1)

    fd = eventfd(initval=4)

    msg(ctx, sock, VFIO_USER_DEVICE_SET_IRQS, payload, fds=[fd])

    vfu_irq_trigger(ctx, 8)

    assert struct.unpack("Q", os.read(fd, 8))[0] == 5


def test_irq_trigger_cleanup():
    vfu_destroy_ctx(ctx)

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab
