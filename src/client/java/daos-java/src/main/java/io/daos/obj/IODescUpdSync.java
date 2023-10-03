/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.daos.DaosIOException;
import io.netty.buffer.ByteBuf;

/**
 * IO Description for synchronously update only one entry, dkey/akey.
 */
public class IODescUpdSync extends IODescUpdBase {

    public IODescUpdSync(String dkey, String akey, long offset, ByteBuf dataBuffer) {
        super(dkey, akey, offset, dataBuffer, false);
    }

    /**
     * native desc released in native code
     */
    public void release() {
        if (descBuffer != null) {
            descBuffer.release();
            descBuffer = null;
        }
        if (dataBuffer != null) {
            dataBuffer.release();
            dataBuffer = null;
        }
    }
}
