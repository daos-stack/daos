/*
 * (C) Copyright 2018-2019 Intel Corporation.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * GOVERNMENT LICENSE RIGHTS-OPEN SOURCE SOFTWARE
 * The Government's rights to use, modify, reproduce, release, perform, display,
 * or disclose this software are subject to the terms of the Apache License as
 * provided in Contract No. B609815.
 * Any reproduction of computer software, computer software documentation, or
 * portions thereof marked with this legend must also reproduce the markings.
 */

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;

import java.io.IOException;
import java.nio.ByteBuffer;

public class IOKeyDesc {

  private String dkey;

  private byte[] dkeyBytes;

  private final int maxNbrOfKeys;

  private final int nbrOfKeysPerCall;

  private final int keyLen;

  private final ByteBuffer anchorBuffer;

  private final ByteBuffer descBuffer;

  private final ByteBuffer keyBuffer;

  public IOKeyDesc(String dkey, int maxNbrOfKeys, int keyLen, int bufferSize) throws IOException {
    this.dkey = dkey;
    if (dkey != null) {
      dkeyBytes = dkey.getBytes(Constants.KEY_CHARSET);
    }
    this.maxNbrOfKeys = maxNbrOfKeys;
    this.keyLen = keyLen;
    this.nbrOfKeysPerCall = bufferSize/keyLen;
    // 1 for end of list
    anchorBuffer = BufferAllocator.directBuffer(1 + IOKeyDesc.getAnchorTypeLen());
    anchorBuffer.order(Constants.DEFAULT_ORDER);
    // 4 for actual number of keys returned, (4 + dkeyBytes.length) for dkeys
    int descLen = 4 + ((dkeyBytes == null) ? 0 : (4 + dkeyBytes.length))
                  + IOKeyDesc.getKeyDescLen() * nbrOfKeysPerCall;
    descBuffer = BufferAllocator.directBuffer(descLen);
    keyBuffer = BufferAllocator.directBuffer(keyLen * nbrOfKeysPerCall);
    descBuffer.order(Constants.DEFAULT_ORDER);
    keyBuffer.order(Constants.DEFAULT_ORDER);
  }

  public int getNbrOfKeysPerCall() {
    return nbrOfKeysPerCall;
  }

  public ByteBuffer getDescBuffer() {
    return descBuffer;
  }

  public ByteBuffer getAnchorBuffer() {
    return anchorBuffer;
  }

  public ByteBuffer getKeyBuffer() {
    return keyBuffer;
  }

  public void encode() {

  }

  public static int getKeyDescLen() {
    return 8    // daos_size_t kd_key_len
      + 4   // uint32_t kd_val_type
      + 2   // uint16_t kd_csum_type
      + 2;  // uint16_t kd_csum_len
  }

  public static int getAnchorTypeLen() {
    return 2      // uint16_t da_type
      + 2     // uint16_t	da_shard
      + 4     // uint32_t	da_flags
      + 120;  // uint8_t		da_buf[DAOS_ANCHOR_BUF_MAX]
  }
}
