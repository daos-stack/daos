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
import struct

ctx = None


def client_version_json(expect=0, json=''):
    sock = connect_sock()

    payload = struct.pack("HH%dsc" % len(json),
                          LIBVFIO_USER_MAJOR, LIBVFIO_USER_MINOR, json, b'\0')

    hdr = vfio_user_header(VFIO_USER_VERSION, size=len(payload))
    sock.send(hdr + payload)
    vfu_attach_ctx(ctx, expect=expect)
    payload = get_reply(sock, expect=expect)
    sock.close()

    return payload


def test_server_setup():
    global ctx

    ctx = vfu_create_ctx()
    assert ctx is not None

    ret = vfu_realize_ctx(ctx)
    assert ret == 0


def test_short_write():
    sock = connect_sock()
    hdr = vfio_user_header(VFIO_USER_VERSION, size=0)
    sock.send(hdr)

    vfu_attach_ctx(ctx, expect=errno.EINVAL)
    get_reply(sock, expect=errno.EINVAL)


def test_long_write():
    sock = connect_sock()
    hdr = vfio_user_header(VFIO_USER_VERSION, size=SERVER_MAX_MSG_SIZE + 1)
    sock.send(hdr)

    ret = vfu_attach_ctx(ctx, expect=errno.EINVAL)
    assert ret == -1
    assert c.get_errno() == errno.EINVAL


def test_bad_command():
    sock = connect_sock()

    payload = struct.pack("HHs", LIBVFIO_USER_MAJOR, LIBVFIO_USER_MINOR, b"")
    hdr = vfio_user_header(999, size=len(payload))
    sock.send(hdr + payload)

    vfu_attach_ctx(ctx, expect=errno.EINVAL)
    get_reply(sock, expect=errno.EINVAL)


def test_invalid_major():
    sock = connect_sock()

    payload = struct.pack("HHs", 999, LIBVFIO_USER_MINOR, b"")
    hdr = vfio_user_header(VFIO_USER_VERSION, size=len(payload))
    sock.send(hdr + payload)

    vfu_attach_ctx(ctx, expect=errno.EINVAL)
    get_reply(sock, expect=errno.EINVAL)


def test_invalid_json_missing_NUL():
    sock = connect_sock()

    payload = struct.pack("HHcc", LIBVFIO_USER_MAJOR, LIBVFIO_USER_MINOR,
                          b"{", b"}")
    hdr = vfio_user_header(VFIO_USER_VERSION, size=len(payload))
    sock.send(hdr + payload)

    vfu_attach_ctx(ctx, expect=errno.EINVAL)
    get_reply(sock, expect=errno.EINVAL)


def test_invalid_json_missing_closing_brace():
    client_version_json(errno.EINVAL,  b"{")


def test_invalid_json_missing_closing_quote():
    client_version_json(errno.EINVAL, b'"')


def test_invalid_json_bad_capabilities_object():
    client_version_json(errno.EINVAL, b'{ "capabilities": "23" }')


def test_invalid_json_bad_max_fds():
    client_version_json(errno.EINVAL,
                        b'{ "capabilities": { "max_msg_fds": "foo" } }')


def test_invalid_json_bad_max_fds2():
    client_version_json(errno.EINVAL,
                        b'{ "capabilities": { "max_msg_fds": -1 } }')


def test_invalid_json_bad_max_fds3():
    client_version_json(errno.EINVAL,
                        b'{ "capabilities": { "max_msg_fds": %d } }' %
                        (VFIO_USER_CLIENT_MAX_FDS_LIMIT + 1))


def test_invalid_json_bad_migration_object():
    client_version_json(errno.EINVAL,
                        b'{ "capabilities": { "migration": "23" } }')


def test_invalid_json_bad_pgsize():
    client_version_json(errno.EINVAL, b'{ "capabilities": ' +
                        b'{ "migration": { "pgsize": "foo" } } }')


#
# FIXME: need vfu_setup_device_migration_callbacks() to be able to test this
# failure mode.
#
def test_invalid_json_bad_pgsize2():
    if False:
        client_version_json(errno.EINVAL,
            b'{ "capabilities": { "migration": { "pgsize": 4095 } } }')


def test_valid_negotiate_no_json():
    sock = connect_sock()

    payload = struct.pack("HH", LIBVFIO_USER_MAJOR, LIBVFIO_USER_MINOR)
    hdr = vfio_user_header(VFIO_USER_VERSION, size=len(payload))
    sock.send(hdr + payload)

    vfu_attach_ctx(ctx)

    payload = get_reply(sock)
    (major, minor, json_str, _) = struct.unpack("HH%dsc" % (len(payload) - 5),
                                                payload)
    assert major == LIBVFIO_USER_MAJOR
    assert minor == LIBVFIO_USER_MINOR
    json = parse_json(json_str)
    assert json.capabilities.max_msg_fds == SERVER_MAX_FDS
    assert json.capabilities.max_data_xfer_size == SERVER_MAX_DATA_XFER_SIZE
    # FIXME: migration object checks

    disconnect_client(ctx, sock)


def test_valid_negotiate_empty_json():
    client_version_json(json=b'{}')

    # notice client closed connection
    vfu_run_ctx(ctx, expect=errno.ENOTCONN)


def test_valid_negotiate_json():
    client_version_json(json=bytes(
        '{ "capabilities": { "max_msg_fds": %s, "max_data_xfer_size": %u } }' %
        (VFIO_USER_CLIENT_MAX_FDS_LIMIT, VFIO_USER_DEFAULT_MAX_DATA_XFER_SIZE),
         "utf-8"))

    # notice client closed connection
    vfu_run_ctx(ctx, expect=errno.ENOTCONN)


def test_destroying():
    vfu_destroy_ctx(ctx)

# ex: set tabstop=4 shiftwidth=4 softtabstop=4 expandtab:
