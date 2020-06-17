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
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.List;

/**
 * IO description for fetching and updating object records on given dkey. Each record is described in {@link Entry}.
 * To make JNI call efficient and avoid memory fragmentation, the dkey and entries are serialized to direct buffers
 * which then de-serialized in native code.
 *
 * <p>
 *   There are two buffers, Description Buffer and Data Buffer. The Description Buffer holds record description, like
 *   its akey, type, size and index in the Data Buffer. the Data Buffer holds actual data for either update or fetch.
 * </p>
 */
public class IODataDesc {

  private final String dkey;

  private final byte[] dkeyBytes;

  private final List<Entry> akeyEntries;

//  private DaosObjClient objClient;

  private int totalDescBufferLen;

  private int totalDataBufferLen;

  private ByteBuffer descBuffer;

  private ByteBuffer dataBuffer;

  private boolean encoded;

  /**
   * constructor with list of {@link Entry}.
   *
   * @param dkey
   * distribution key
   * @param keyEntries
   * list of description entries
   * @throws IOException
   */
  protected IODataDesc(String dkey, List<Entry> keyEntries) throws IOException {
    this.dkey = dkey;
    this.dkeyBytes = dkey.getBytes(Constants.KEY_CHARSET);
    if (dkeyBytes.length > Short.MAX_VALUE) {
      throw new IllegalArgumentException("dkey length in " + Constants.KEY_CHARSET + " should not exceed "
                      + Short.MAX_VALUE);
    }
    this.akeyEntries = keyEntries;
    totalDescBufferLen += (2 + dkeyBytes.length);
    for (Entry entry : keyEntries) {
      totalDescBufferLen += entry.getDescLen();
      totalDataBufferLen += entry.getBufferLen();
    }
  }

//  protected void setObjClient(DaosObjClient objClient) {
//    this.objClient = objClient;
//  }

  /**
   * number of records to fetch or update.
   *
   * @return number of records
   */
  public int getNbrOfEntries() {
    return akeyEntries.size();
  }

  /**
   * total length of all encoded entries.
   *
   * @return total length
   */
  public int getDescBufferLen() {
    return totalDescBufferLen;
  }

  /**
   * total length of all encoded data.
   *
   * @return
   */
  public int getDataBufferLen() {
    return totalDataBufferLen;
  }

  /**
   * encode dkey + entries descriptions to the Description Buffer.
   * encode entries data to Data Buffer.
   */
  public void encode() {
    if (!encoded) {
      this.descBuffer = BufferAllocator.directBuffer(getDescBufferLen());
      this.dataBuffer = BufferAllocator.directBuffer(getDataBufferLen());
      descBuffer.order(Constants.DEFAULT_ORDER);
      dataBuffer.order(Constants.DEFAULT_ORDER);
      descBuffer.putShort((short)dkeyBytes.length);
      descBuffer.put(dkeyBytes);
      for (Entry entry : akeyEntries) {
        entry.setGlobalDataBuffer(dataBuffer);
        entry.encode(descBuffer);
      }
      encoded = true;
    }
  }

  /**
   * get reference to the Description Buffer after being encoded.
   *
   * @return ByteBuffer
   */
  public ByteBuffer getDescBuffer() {
    if (!encoded) {
      throw new IllegalStateException("not encoded yet");
    }
    return descBuffer;
  }

  /**
   * get reference to the Data Buffer after being encoded.
   *
   * @return ByteBuffer
   */
  public ByteBuffer getDataBuffer() {
    if (!encoded) {
      throw new IllegalStateException("not encoded yet");
    }
    return dataBuffer;
  }

  /**
   * A entry to describe record update or fetch on given akey. For array, each entry object represents consecutive
   * records of given key. Multiple entries should be created for non-consecutive records of given key.
   */
  public static class Entry {
    private final String key;
    private final IodType type;
    private final byte[] keyBytes;
    private final int offset;
    private final int recordSize;
    private final int dataSize;
    private int paddedDataSize;
//    private int iodSize;
    private ByteBuffer dataBuffer;
    private boolean updateOrFetch;

//    private DaosObjClient objClient;

    private ByteBuffer globalBuffer;
    private int globalBufIdx = -1;
    private int actualSize; // to get from value buffer

    private static final Logger log = LoggerFactory.getLogger(Entry.class);

    /**
     * construction for fetch.
     *
     * @param key
     * akey to fetch data from
     * @param type
     * akey value type
     * @param
     * offset inside akey, should be a multiple of recordSize
     * @param recordSize
     * akey record size
     * @param dataSize
     * size of data to fetch, make it a multiple of recordSize as much as possible. zeros are padded to make actual
     * request size a multiple of recordSize.
     * @throws IOException
     */
    protected Entry(String key, IodType type, int offset, int recordSize, int dataSize) throws IOException {
      this.key = key;
      this.type = type;
      this.keyBytes = key.getBytes(Constants.KEY_CHARSET);
      if (keyBytes.length > Short.MAX_VALUE) {
        throw new IllegalArgumentException("akey length in " + Constants.KEY_CHARSET + " should not exceed "
          + Short.MAX_VALUE);
      }
      this.offset = offset;
      this.recordSize = recordSize;
      this.dataSize = dataSize;
      if (offset%recordSize != 0) {
        throw new IllegalArgumentException("offset (" + offset + ") should be a multiple of recordSize (" + recordSize +
                                          ").");
      }
      switch (type) {
        case SINGLE:
          if (offset != 0) {
            throw new IllegalArgumentException("offset should be zero for " + type);
          }
          if (dataSize > recordSize) {
            throw new IllegalArgumentException("data size should be no more than record size for " + type);
          }
          break;
        case NONE: throw new IllegalArgumentException("need valid IodType, either " + IodType.ARRAY + " or " +
                        IodType.SINGLE);
      }
      // pad data size and make it a multiple of record size
      int r = dataSize % recordSize;
      if (r != 0) {
        paddedDataSize = dataSize + (recordSize - r);
      } else {
        paddedDataSize = dataSize;
      }
    }

    /**
     * construction for update.
     *
     * @param key
     * akey to update on
     * @param type
     * akey value type
     * @param offset
     * offset inside akey, should be a multiple of recordSize
     * @param recordSize
     * akey record size
     * @param dataBuffer
     * byte buffer (direct buffer preferred) holding data to update. make sure dataBuffer is ready for being read,
     * for example, buffer position and limit are set correctly for reading.
     * make size a multiple of recordSize as much as possible. zeros are padded to make actual request size a multiple
     * of recordSize.
     * @throws IOException
     */
    protected Entry(String key, IodType type, int offset, int recordSize, ByteBuffer dataBuffer) throws IOException {
      this(key, type, offset, recordSize, dataBuffer.limit() - dataBuffer.position());
      this.dataBuffer = dataBuffer;
      this.updateOrFetch = true;
    }

//    protected void setObjClient(DaosObjClient objClient) {
//      this.objClient = objClient;
//      if (type == IodType.ARRAY) {
//        iodSize = objClient.getDaosIodArraySize();
//      } else {
//        iodSize = objClient.getDaosIodSingleSize();
//      }
//    }

    /**
     * length of this entry when encoded into the Description Buffer.
     *
     * @return length
     */
    public int getDescLen() {
      // 11 or 19 = key len(2) + iod_type(1) + iod_size(4) + [recx idx(4) + recx nr(4)] + data buffer idx(4)
      if (type == IodType.ARRAY) {
        return 19 + keyBytes.length;
      }
      return 11 + keyBytes.length;
    }

    /**
     * length of this entry when encoded into the Data Buffer.
     *
     * @return length
     */
    public int getBufferLen() {
      return 4 + paddedDataSize;
    }

    /**
     * keep reference of <code>globalBuffer</code>.<br/>
     * set entry index in <code>globalBuffer</code>.<br/>
     * when it's update, write entry data to <code>globalBuffer</code>.
     *
     * @param globalBuffer
     * global data buffer
     */
    public void setGlobalDataBuffer(ByteBuffer globalBuffer) {
      this.globalBuffer = globalBuffer;
      this.globalBufIdx = globalBuffer.position();
      if (log.isDebugEnabled()) {
        log.debug(key + " buffer index: " + globalBufIdx);
      }
      globalBuffer.putInt(paddedDataSize);
      if (updateOrFetch) { // update
        globalBuffer.put(dataBuffer); // TODO: tune heap buffer
        int padSize = paddedDataSize - dataSize;
        if (padSize > 0) {
          globalBuffer.position(globalBuffer.position() + padSize);
        }
        if ((globalBuffer.position() - globalBufIdx) != getBufferLen()) {
          throw new IllegalStateException("global buffer should be filled with data of size " + getBufferLen());
        }
      } else { // fetch
        globalBuffer.position(globalBufIdx + getBufferLen());
      }
    }

    /**
     * encode entry to the description buffer which will be decoded in native code.<br/>
     *
     * @param descBuffer
     * the description buffer
     */
    public void encode(ByteBuffer descBuffer) {
      if (globalBufIdx == -1) {
        throw new IllegalStateException("value buffer index is not set");
      }
      descBuffer.putShort((short)keyBytes.length)
                .put(keyBytes)
                .put(type.value)
                .putInt(recordSize);
      if (type == IodType.ARRAY) {
        descBuffer.putInt(offset/recordSize);
        descBuffer.putInt(paddedDataSize/recordSize);
      }
      descBuffer.putInt(globalBufIdx);
    }
  }

  public enum IodType {
    NONE((byte)0), SINGLE((byte)1), ARRAY((byte)2);

    private byte value;

    IodType(byte value) {
      this.value = value;
    }

    public byte getValue() {
      return value;
    }
  }
}
