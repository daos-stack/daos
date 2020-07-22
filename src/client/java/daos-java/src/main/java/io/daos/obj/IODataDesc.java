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
import io.netty.buffer.ByteBuf;
import org.apache.commons.lang.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.nio.BufferUnderflowException;
import java.util.List;

/**
 * IO description for fetching and updating object records on given dkey. Each record is described in {@link Entry}.
 * To make JNI call efficient and avoid memory fragmentation, the dkey and entries are serialized to direct buffers
 * which then de-serialized in native code.
 *
 * <p>
 *   There are two types of buffers, Description Buffer and Data Buffers. The Description Buffer holds entries
 *   description, like their akey, type, size. The Data Buffers of entries holds actual data for either update or fetch.
 *   {@link #release()} method should be called after object update or fetch. For update, user is responsible for
 *   releasing data buffers. For fetch, user can determine who release fetch buffers.
 *   See {@link Entry#release(boolean)}.
 * </p>
 * <p>
 *   For update entries, user should call {@link #createEntryForUpdate(String, IodType, int, int, ByteBuf)}.
 *   And {@link #createEntryForFetch(String, IodType, int, int, int)} for fetch entries. Results of fetch should be get
 *   from each entry by calling {@link Entry#getFetchedData()} For each IODataDesc object, there must be only one type
 *   of action, either update or fetch, among all its entries.
 * </p>
 */
public class IODataDesc {

  private final String dkey;

  private final byte[] dkeyBytes;

  private final List<Entry> akeyEntries;

  private final boolean updateOrFetch;

  private int totalDescBufferLen;

  private int totalRequestBufLen;

  private int totalRequestSize;

  private ByteBuf descBuffer;

  private Throwable cause;

  private boolean encoded;

  private boolean resultParsed;

  /**
   * constructor with list of {@link Entry}.
   *
   * @param dkey
   * distribution key
   * @param keyEntries
   * list of description entries
   * @param updateOrFetch
   * true for update; false for fetch
   * @throws IOException
   */
  protected IODataDesc(String dkey, List<Entry> keyEntries, boolean updateOrFetch) throws IOException {
    this.dkey = dkey;
    this.dkeyBytes = dkey.getBytes(Constants.KEY_CHARSET);
    if (dkeyBytes.length > Short.MAX_VALUE) {
      throw new IllegalArgumentException("dkey length in " + Constants.KEY_CHARSET + " should not exceed "
                      + Short.MAX_VALUE);
    }
    this.akeyEntries = keyEntries;
    this.updateOrFetch = updateOrFetch;
    totalRequestBufLen += (Constants.ENCODED_LENGTH_KEY + dkeyBytes.length);
    for (Entry entry : keyEntries) {
      if (updateOrFetch != entry.updateOrFetch) {
        throw new IllegalArgumentException("entry is " + updateOrFetchStr(entry.updateOrFetch) +". should be " +
            updateOrFetchStr(updateOrFetch));
      }
      totalRequestBufLen += entry.getDescLen();
      totalRequestSize += entry.getRequestSize();
    }
    totalDescBufferLen += totalRequestBufLen;
    if (!updateOrFetch) { // for returned actual size and actual record size
      totalDescBufferLen += keyEntries.size() * Constants.ENCODED_LENGTH_EXTENT * 2;
    }
  }

  public String getDkey() {
    return dkey;
  }

  public int getTotalRequestSize() {
    return totalRequestSize;
  }

  private String updateOrFetchStr(boolean v) {
    return v ? "update" : "fetch";
  }

  /**
   * number of records to fetch or update.
   *
   * @return number of records
   */
  public int getNbrOfEntries() {
    return akeyEntries.size();
  }

  /**
   * total length of all encoded entries, including reserved buffer for holding sizes of returned data and actual record
   * size.
   *
   * @return total length
   */
  public int getDescBufferLen() {
    return totalDescBufferLen;
  }

  /**
   * total length of all encoded entries to request data.
   *
   * @return
   */
  public int getRequestBufLen() {
    return totalRequestBufLen;
  }

  /**
   * encode dkey + entries descriptions to the Description Buffer.
   * encode entries data to Data Buffer.
   */
  public void encode() {
    if (resultParsed) {
      throw new IllegalStateException("result is parsed. cannot encode again");
    }
    if (!encoded) {
      this.descBuffer = BufferAllocator.objBufWithNativeOrder(getDescBufferLen());
      descBuffer.writeShort(dkeyBytes.length);
      descBuffer.writeBytes(dkeyBytes);
      for (Entry entry : akeyEntries) {
        entry.encode(descBuffer);
      }
      encoded = true;
    }
  }

  /**
   * if the object update or fetch succeeded.
   *
   * @return true or false
   */
  public boolean succeeded() {
    return resultParsed;
  }

  public Throwable getCause() {
    return cause;
  }

  protected void setCause(Throwable de) {
    cause = de;
  }

  protected void succeed() {
    resultParsed = true;
  }

  public

  /**
   * parse result after JNI call.
   */
  protected void parseResult() {
    if (updateOrFetch) {
      throw new UnsupportedOperationException("only support for fetch");
    }
    if (!resultParsed) {
      // update actual size
      int idx = getRequestBufLen();
      descBuffer.writerIndex(descBuffer.capacity());
      for (IODataDesc.Entry entry : getAkeyEntries()) {
        descBuffer.readerIndex(idx);
        entry.setActualSize(descBuffer.readInt());
        entry.setActualRecSize(descBuffer.readInt());
        idx += 2 * Constants.ENCODED_LENGTH_EXTENT;
      }
      resultParsed = true;
    }
  }

  /**
   * get reference to the Description Buffer after being encoded.
   * The buffer's reader index and write index should be restored if user
   * changed them.
   *
   * @return ByteBuf
   */
  protected ByteBuf getDescBuffer() {
    if (!encoded) {
      throw new IllegalStateException("not encoded yet");
    }
    return descBuffer;
  }

  public List<Entry> getAkeyEntries() {
    return akeyEntries;
  }

  /**
   * create data description entry for fetch.
   *
   * @param key
   * distribution key
   * @param type
   * iod type, {@see io.daos.obj.IODataDesc.IodType}
   * @param recordSize
   * record size. Should be provided with correct value. Or you may get error or wrong fetch result. You can call
   * {@link DaosObject#getRecordSize(String, String)} to get correct value if you don't know yet.
   * @param offset
   * offset inside akey from which to fetch data, should be a multiple of recordSize
   * @param dataSize
   * size of data to fetch, make it a multiple of recordSize as much as possible. zeros are padded to make actual
   * request size a multiple of recordSize.
   * @return data description entry
   * @throws IOException
   */
  public static Entry createEntryForFetch(String key, IODataDesc.IodType type, int recordSize, int offset,
                                              int dataSize) throws IOException {
    IODataDesc.Entry entry = new IODataDesc.Entry(key, type, recordSize, offset, dataSize);
    return entry;
  }

  /**
   * create data description entry for update.
   *
   * @param key
   * distribution key
   * @param type
   * iod type, {@see io.daos.obj.IODataDesc.IodType}
   * @param recordSize
   * record size. Should be same record size as the first update if any. You can call
   * {@link DaosObject#getRecordSize(String, String)} to get correct value if you don't know yet.
   * @param offset
   * offset inside akey from which to update data, should be a multiple of recordSize
   * @param dataBuffer
   * byte buffer (direct buffer preferred) holding data to update. make sure dataBuffer is ready for being read,
   * for example, buffer position and limit are set correctly for reading.
   * make size a multiple of recordSize as much as possible. zeros are padded to make actual request size a multiple
   * of recordSize.
   * @return data description entry
   * @throws IOException
   */
  public static Entry createEntryForUpdate(String key, IODataDesc.IodType type, int recordSize, int offset,
                                               ByteBuf dataBuffer) throws IOException {
    IODataDesc.Entry entry = new IODataDesc.Entry(key, type, recordSize, offset, dataBuffer);
    return entry;
  }

  /**
   * release all buffers created from this object and its entry objects. Be noted, the fetch data buffers are
   * released too if this desc is for fetch. If you don't want release them too early, please call
   * {@link #release(boolean)} with false as parameter.
   */
  public void release() {
    release(true);
  }

  /**
   * same as {@link #release()}, but give user a choice whether release fetch buffers or not.
   *
   * @param releaseFetchBuffer
   * true to release all fetch buffers, false otherwise.
   */
  public void release(boolean releaseFetchBuffer) {
    if (descBuffer != null) {
      this.descBuffer.release();
      descBuffer = null;
    }
    if (releaseFetchBuffer && !updateOrFetch) {
      akeyEntries.forEach(e -> e.releaseFetchDataBuffer());
    }
  }

  @Override
  public String toString() {
    return toString();
  }

  public String toString(int maxSize) {
    StringBuilder sb = new StringBuilder();
    sb.append("dkey: ").append(dkey).append(", akey entries\n");
    int nbr = 0;
    for (Entry e : akeyEntries) {
      sb.append("[").append(e.toString()).append("]");
      nbr++;
      if (sb.length() < maxSize) {
        sb.append(',');
      } else {
        break;
      }
    }
    if (nbr < akeyEntries.size()) {
      sb.append("...");
    }
    return sb.toString();
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
    private ByteBuf dataBuffer;
    private boolean updateOrFetch;

    private boolean encoded;

    private int actualSize; // to get from value buffer
    private int actualRecSize;

    private static final Logger log = LoggerFactory.getLogger(Entry.class);

    /**
     * construction for fetch.
     *
     * @param key
     * akey to fetch data from
     * @param type
     * akey value type
     * @param recordSize
     * akey record size
     * @param offset
     * offset inside akey, should be a multiple of recordSize
     * @param dataSize
     * size of data to fetch, make it a multiple of recordSize as much as possible. zeros are padded to make actual
     * request size a multiple of recordSize.
     * @throws IOException
     */
    protected Entry(String key, IodType type, int recordSize, int offset, int dataSize) throws IOException {
      if (StringUtils.isBlank(key)) {
        throw new IllegalArgumentException("key is blank");
      }
      this.key = key;
      this.type = type;
      this.keyBytes = key.getBytes(Constants.KEY_CHARSET);
      if (keyBytes.length > Short.MAX_VALUE) {
        throw new IllegalArgumentException("akey length in " + Constants.KEY_CHARSET + " should not exceed "
          + Short.MAX_VALUE + ", akey: " + key);
      }
      this.offset = offset;
      this.recordSize = recordSize;
      this.dataSize = dataSize;
      if (offset%recordSize != 0) {
        throw new IllegalArgumentException("offset (" + offset + ") should be a multiple of recordSize (" + recordSize +
                                          ")." + ", akey: " + key);
      }
      if (dataSize <= 0) {
        throw new IllegalArgumentException("need positive data size, " + dataSize);
      }
      switch (type) {
        case SINGLE:
          if (offset != 0) {
            throw new IllegalArgumentException("offset should be zero for " + type + ", akey: " + key);
          }
          if (dataSize > recordSize) {
            throw new IllegalArgumentException("data size should be no more than record size for " + type +
              ", akey: " + key);
          }
          break;
        case NONE: throw new IllegalArgumentException("need valid IodType, either " + IodType.ARRAY + " or " +
                        IodType.SINGLE + ", akey: " + key);
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
     * @param recordSize
     * akey record size
     * @param offset
     * offset inside akey, should be a multiple of recordSize
     * @param dataBuffer
     * byte buffer (direct buffer preferred) holding data to update. make sure dataBuffer is ready for being read,
     * for example, buffer position and limit are set correctly for reading.
     * make size a multiple of recordSize as much as possible. zeros are padded to make actual request size a multiple
     * of recordSize. user should release the buffer by himself.
     * @throws IOException
     */
    protected Entry(String key, IodType type, int recordSize, int offset, ByteBuf dataBuffer) throws IOException {
      this(key, type, recordSize, offset, dataBuffer.readableBytes());
      this.dataBuffer = dataBuffer;
      this.updateOrFetch = true;
    }

    /**
     * get size of actual data returned.
     *
     * @return actual data size returned
     */
    public int getActualSize() {
      if (updateOrFetch) {
        throw new UnsupportedOperationException("only support for fetch, akey: " + key);
      }
      return actualSize;
    }

    /**
     * set size of actual data returned after fetch.
     *
     * @param actualSize
     */
    public void setActualSize(int actualSize) {
      if (updateOrFetch) {
        throw new UnsupportedOperationException("only support for fetch, akey: " + key);
      }
      this.actualSize = actualSize;
    }

    /**
     * get actual record size.
     *
     * @return record size
     */
    public int getActualRecSize() {
      if (updateOrFetch) {
        throw new UnsupportedOperationException("only support for fetch, akey: " + key);
      }
      return actualRecSize;
    }

    /**
     * set actual record size.
     *
     * @param actualRecSize
     */
    public void setActualRecSize(int actualRecSize) {
      if (updateOrFetch) {
        throw new UnsupportedOperationException("only support for fetch, akey: " + key);
      }
      this.actualRecSize = actualRecSize;
    }

    /**
     * get data buffer holding fetched data. User should read data without changing buffer's readerIndex and writerIndex
     * since the indices are managed based on the actual data returned.
     *
     * @return data buffer with writerIndex set to existing readerIndex + actual data size
     */
    public ByteBuf getFetchedData() {
      if (updateOrFetch) {
        throw new UnsupportedOperationException("only support for fetch, akey: " + key);
      }
      dataBuffer.writerIndex(dataBuffer.readerIndex() + actualSize);
      return dataBuffer;
    }

    /**
     * length of this entry when encoded into the Description Buffer.
     *
     * @return length
     */
    public int getDescLen() {
      // 11 or 19 = key len(2) + iod_type(1) + iod_size(4) + [recx idx(4) + recx nr(4)] + data buffer mem address(8)
      if (type == IodType.ARRAY) {
        return 23 + keyBytes.length;
      }
      return 15 + keyBytes.length;
    }

    public String getKey() {
      return key;
    }

    /**
     * encode entry to the description buffer which will be decoded in native code.<br/>
     *
     * @param descBuffer
     * the description buffer
     */
    protected void encode(ByteBuf descBuffer) {
      if (encoded) {
        return;
      }
      long memoryAddress;
      if (!updateOrFetch) {
        dataBuffer = BufferAllocator.objBufWithNativeOrder(paddedDataSize);
        memoryAddress = dataBuffer.memoryAddress();
      } else {
        memoryAddress = dataBuffer.memoryAddress() + dataBuffer.readerIndex();
      }
      descBuffer.writeShort(keyBytes.length)
                .writeBytes(keyBytes)
                .writeByte(type.value)
                .writeInt(recordSize);
      if (type == IodType.ARRAY) {
        descBuffer.writeInt(offset/recordSize);
        descBuffer.writeInt(paddedDataSize/recordSize);
      }
      descBuffer.writeLong(memoryAddress);
      encoded = true;
    }

    protected void releaseFetchDataBuffer() {
      if (!updateOrFetch && dataBuffer != null) {
        dataBuffer.release();
        dataBuffer = null;
      }
    }

    public int getRequestSize() {
      return dataSize;
    }

    @Override
    public String toString() {
      StringBuilder sb = new StringBuilder();
      sb.append(updateOrFetch ? "update " : "fetch ").append("entry: ");
      sb.append(key).append(type).append(recordSize).append(offset).append(dataSize);
      return sb.toString();
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
