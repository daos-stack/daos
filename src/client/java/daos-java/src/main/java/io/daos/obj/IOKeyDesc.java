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

/**
 * A class to describe key listing, including approximate key length, number of keys to retrieve and batch size
 */
public class IOKeyDesc {

  private String dkey;

  private byte[] dkeyBytes;

  private final int nbrOfKeys;

  private final int batchSize;

  private final int keyLen;

  private final ByteBuffer anchorBuffer;

  private final ByteBuffer descBuffer;

  private final ByteBuffer keyBuffer;

  private boolean encoded;

  /**
   * constructor to set all parameters.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param nbrOfKeys
   * number of keys to list. The listing could stop if <code>nbrOfKeys</code> exceeds actual number of keys
   * @param keyLen
   * approximate key length, so that buffer size can be well-calculated
   * @param batchSize
   * how many keys to list per native method call
   * @throws IOException
   */
  protected IOKeyDesc(String dkey, int nbrOfKeys, int keyLen, int batchSize) throws IOException {
    this.dkey = dkey;
    if (dkey != null) {
      dkeyBytes = dkey.getBytes(Constants.KEY_CHARSET);
      if (dkeyBytes.length > Short.MAX_VALUE) {
        throw new IllegalArgumentException("dkey length in " + Constants.KEY_CHARSET + " should not exceed "
          + Short.MAX_VALUE);
      }
    }
    if (nbrOfKeys < 1) {
      throw new IllegalArgumentException("nbrOfKeys should be at least 1, " + nbrOfKeys);
    }
    this.nbrOfKeys = nbrOfKeys;
    this.keyLen = keyLen;
    if (nbrOfKeys < batchSize) {
      this.batchSize = nbrOfKeys;
    } else {
      this.batchSize = batchSize;
    }
    // 1 byte for anchor status
    anchorBuffer = BufferAllocator.directBuffer(1 + IOKeyDesc.getAnchorTypeLen());
    anchorBuffer.order(Constants.DEFAULT_ORDER);
    // 4 for actual number of keys returned, (2 + dkeyBytes.length) for dkeys
    int descLen = 4 + ((dkeyBytes == null) ? 0 : (2 + dkeyBytes.length))
                  + IOKeyDesc.getKeyDescLen() * this.batchSize;
    if (descLen < 0) {
      throw new IllegalArgumentException("too big batchSize. " + this.batchSize);
    }
    descBuffer = BufferAllocator.directBuffer(descLen);
    keyBuffer = BufferAllocator.directBuffer(keyLen * this.batchSize);
    descBuffer.order(Constants.DEFAULT_ORDER);
    keyBuffer.order(Constants.DEFAULT_ORDER);
  }

  /**
   * constructor with default batch size, {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * distribution key for listing akeys. null for listing dkeys
   * @param nbrOfKeys
   * number of keys to list. The listing could stop if <code>nbrOfKeys</code> exceeds actual number of keys
   * @param keyLen
   * approximate key length, so that buffer size can be well-calculated
   * @throws IOException
   */
  protected IOKeyDesc(String dkey, int nbrOfKeys, int keyLen) throws IOException {
    this(dkey, nbrOfKeys, keyLen, Constants.KEY_LIST_BATCH_SIZE_DEFAULT);
  }

  /**
   * constructor with default key length, {@linkplain Constants#KEY_LIST_LEN_DEFAULT} and batch size,
   * {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * @param nbrOfKeys
   * @throws IOException
   */
  protected IOKeyDesc(String dkey, int nbrOfKeys) throws IOException {
    this(dkey, nbrOfKeys, Constants.KEY_LIST_LEN_DEFAULT, Constants.KEY_LIST_BATCH_SIZE_DEFAULT);
  }

  /**
   * constructor to list all keys (Integer.MAX_VALUE) with default key length,
   * {@linkplain Constants#KEY_LIST_LEN_DEFAULT} and batch size, {@linkplain Constants#KEY_LIST_BATCH_SIZE_DEFAULT}.
   *
   * @param dkey
   * @throws IOException
   */
  protected IOKeyDesc(String dkey) throws IOException {
    this(dkey, Integer.MAX_VALUE, Constants.KEY_LIST_LEN_DEFAULT, Constants.KEY_LIST_BATCH_SIZE_DEFAULT);
  }

  public int getKeyLen() {
    return keyLen;
  }

  public int getBatchSize() {
    return batchSize;
  }

  public ByteBuffer getDescBuffer() {
    if (!encoded) {
      throw new IllegalStateException("not encoded yet");
    }
    return descBuffer;
  }

  public ByteBuffer getAnchorBuffer() {
    return anchorBuffer;
  }

  public ByteBuffer getKeyBuffer() {
    return keyBuffer;
  }

  public String getDkey() {
    return dkey;
  }

  public void encode() {
    if (!encoded) {
      descBuffer.position(4); // reserve for actual number of keys returned
      if (dkeyBytes != null) {
        descBuffer.putShort((short) dkeyBytes.length);
        descBuffer.put(dkeyBytes);
      }
      anchorBuffer.put((byte) 0);
      encoded = true;
    }
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
