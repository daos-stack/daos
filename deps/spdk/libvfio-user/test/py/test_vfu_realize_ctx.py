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

import errno
from libvfio_user import *


def test_vfu_realize_ctx_twice():
    ctx = vfu_create_ctx()
    assert ctx is not None

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    vfu_destroy_ctx(ctx)


def test_vfu_unrealized_ctx():
    ctx = vfu_create_ctx()
    assert ctx is not None

    vfu_run_ctx(ctx, errno.EINVAL)

    vfu_destroy_ctx(ctx)


def test_vfu_realize_ctx_default():
    ctx = vfu_create_ctx()
    assert ctx is not None

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    vfu_destroy_ctx(ctx)


def test_vfu_realize_ctx_pci_bars():
    ctx = vfu_create_ctx()
    assert ctx is not None

    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR0_REGION_IDX, size=4096,
                           flags=VFU_REGION_FLAG_RW)
    assert ret == 0
    ret = vfu_setup_region(ctx, index=VFU_PCI_DEV_BAR1_REGION_IDX, size=4096,
                           flags=(VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM))
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    # region_type should be set non-MEM BAR, unset otherwise
    hdr = get_pci_header(ctx)
    assert hdr.bars[0].io == 0x1
    assert hdr.bars[1].io == 0

    vfu_destroy_ctx(ctx)


def test_vfu_realize_ctx_irqs():
    ctx = vfu_create_ctx()
    assert ctx is not None

    ret = vfu_setup_device_nr_irqs(ctx, VFU_DEV_INTX_IRQ, 1)
    assert ret == 0

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    # verify INTA# is available
    hdr = get_pci_header(ctx)
    assert hdr.intr.ipin == 0x1

    vfu_destroy_ctx(ctx)


def test_vfu_realize_ctx_caps():
    ctx = vfu_create_ctx()
    assert ctx is not None

    ret = vfu_pci_init(ctx)
    assert ret == 0

    pos = vfu_pci_add_capability(ctx, pos=0, flags=0, data=struct.pack(
            "ccHH", to_byte(PCI_CAP_ID_PM), b'\0', 0, 0))
    assert pos == PCI_STD_HEADER_SIZEOF

    ret = vfu_realize_ctx(ctx)
    assert ret == 0

    # hdr.sts.cl should be 0x1
    hdr = get_pci_header(ctx)
    assert hdr.sts == (1 << 4)

    vfu_destroy_ctx(ctx)
