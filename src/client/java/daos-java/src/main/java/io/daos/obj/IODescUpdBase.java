/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosUtils;
import io.netty.buffer.ByteBuf;

/**
 * IO Description for update only one entry, dkey/akey.
 */
public class IODescUpdBase {

    protected ByteBuf descBuffer;

    protected long offset;

    protected ByteBuf dataBuffer;

    protected int requestLen;

    public static final int RET_CODE_SUCCEEDED = Constants.RET_CODE_SUCCEEDED;

    protected IODescUpdBase(String dkey, String akey, long offset, ByteBuf dataBuffer, boolean async) {
        this.offset = offset;
        this.dataBuffer = dataBuffer;
        byte[] dkeyBytes = DaosUtils.keyToBytes8(dkey);
        byte[] akeyBytes = DaosUtils.keyToBytes(akey);
        int handleLen = 0;
        int retLen = 0;
        if (async) {
            // 8 for null native handle
            // 4 for return code
            handleLen = 8;
            retLen = 4;
        }
        // 4 = 2 + 2 for lens,
        requestLen = handleLen + dkeyBytes.length + akeyBytes.length + 4;
        descBuffer = BufferAllocator.objBufWithNativeOrder(requestLen + retLen);
        if (async) {
            descBuffer.writeLong(0L);
        }
        descBuffer.writeShort(dkeyBytes.length);
        descBuffer.writeBytes(dkeyBytes);
        descBuffer.writeShort(akeyBytes.length);
        descBuffer.writeBytes(akeyBytes);
    }

    /**
     * for reusable async update
     */
    protected IODescUpdBase() {}

    public ByteBuf getDataBuffer() {
        return dataBuffer;
    }

    public ByteBuf getDescBuffer() {
        return descBuffer;
    }

    public long getDestOffset() {
        return offset;
    }

    public int readableBytes() {
        return dataBuffer.readableBytes();
    }

    public long dataMemoryAddress() {
        return dataBuffer.memoryAddress();
    }

    public long descMemoryAddress() {
        return descBuffer.memoryAddress();
    }
}
