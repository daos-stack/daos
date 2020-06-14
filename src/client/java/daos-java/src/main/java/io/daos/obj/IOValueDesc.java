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
import java.util.List;

public class IOValueDesc {

  private final String dkey;

  private final byte[] dkeyBytes;

  private final List<Entry> akeyEntries;

  private int totalEncodeLen;

  private int totalValueLen;

  private ByteBuffer descBuffer;

  private ByteBuffer valueBuffer;

  private boolean encoded;

  /**
   * constructor for fetch.
   *
   * @param dkey
   * distribution key
   * @param keyEntries
   * list of description entries
   * @throws IOException
   */
  public IOValueDesc(String dkey, List<Entry> keyEntries) throws IOException {
    this.dkey = dkey;
    this.dkeyBytes = dkey.getBytes(Constants.KEY_CHARSET);
    this.akeyEntries = keyEntries;
    totalEncodeLen += (4 + dkeyBytes.length);
    for (Entry entry : keyEntries) {
      totalEncodeLen += entry.getEncodeLen();
      totalValueLen += entry.getLenInValueBuffer();
    }
  }

  public int getNbrOfEntries() {
    return akeyEntries.size();
  }

  public int getEncodeLen() {
    return totalEncodeLen;
  }

  public int getValueLen() {
    return totalValueLen;
  }

  public void encode() {
    if (!encoded) {
      this.descBuffer = BufferAllocator.directBuffer(getEncodeLen());
      this.valueBuffer = BufferAllocator.directBuffer(getValueLen());
      descBuffer.order(Constants.DEFAULT_ORDER);
      valueBuffer.order(Constants.DEFAULT_ORDER);
      descBuffer.putInt(dkeyBytes.length);
      descBuffer.put(dkeyBytes);
      for (Entry entry : akeyEntries) {
        entry.setGlobalValueBuffer(valueBuffer);
        entry.encode(descBuffer);
      }
      encoded = true;
    }
  }

  public ByteBuffer getDescBuffer() {
    if (!encoded) {
      throw new IllegalStateException("not encoded yet");
    }
    return descBuffer;
  }

  public ByteBuffer getValueBuffer() {
    if (!encoded) {
      throw new IllegalStateException("not encoded yet");
    }
    return valueBuffer;
  }

  public static class Entry {
    private String key;
    private IodType type;
    private byte[] keyBytes;
    private int recordSize;
    private int dataSize;
    private ByteBuffer valueBuffer;
    private boolean updateOrFetch;

    private ByteBuffer globalBuffer;
    private int globalBufIdx = -1;
    private int actualSize; // to get from value buffer

    /**
     * construction for fetch.
     *
     * @param key
     * @param type
     * @param recordSize
     * @param dataSize
     * fetch request size
     * @throws IOException
     */
    public Entry(String key, IodType type, int recordSize, int dataSize) throws IOException {
      this.key = key;
      this.type = type;
      this.keyBytes = key.getBytes(Constants.KEY_CHARSET);
      this.recordSize = recordSize;
      this.dataSize = dataSize;
    }

    /**
     * construction for update.
     *
     * @param key
     * @param type
     * @param recordSize
     * @param valueBuffer
     * @throws IOException
     */
    public Entry(String key, IodType type, int recordSize, ByteBuffer valueBuffer) throws IOException {
      this(key, type, recordSize, valueBuffer.limit());
      this.valueBuffer = valueBuffer;
      this.updateOrFetch = true;
    }

    public int getEncodeLen() {
      return 14                  // key length4 + type2 + record size4 + value buffer index4 + request size4
              + keyBytes.length; // key bytes length
    }

    public int getLenInValueBuffer() {
      return 4 + dataSize;
    }

    public void setGlobalValueBuffer(ByteBuffer globalBuffer) {
      this.globalBuffer = globalBuffer;
      this.globalBufIdx = globalBuffer.position();
      if (updateOrFetch) {
        globalBuffer.putInt(valueBuffer.limit());
        globalBuffer.put(valueBuffer);
      } else {
        globalBuffer.position(globalBufIdx + getLenInValueBuffer());
      }
    }

    public void encode(ByteBuffer descBuffer) {
      if (globalBufIdx == -1) {
        throw new IllegalStateException("value buffer index is not set");
      }
      descBuffer.putInt(keyBytes.length)
                .putShort(type.value)
                .put(keyBytes)
                .putInt(recordSize)
                .putInt(globalBufIdx)
                .putInt(recordSize);
    }
  }

  public enum IodType {
    NONE((short)0), SINGLE((short)1), ARRAY((short)2);

    private short value;

    IodType(short value) {
      this.value = value;
    }

    public short getValue() {
      return value;
    }
  }
}
