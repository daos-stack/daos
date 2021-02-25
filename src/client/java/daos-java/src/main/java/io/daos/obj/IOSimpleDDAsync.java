/*
 * (C) Copyright 2018-2020 Intel Corporation.
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
import io.daos.DaosEventQueue;
import io.netty.buffer.ByteBuf;
import org.apache.commons.lang.StringUtils;

import java.io.IOException;
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
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
 *   See {@link Entry#releaseDataBuffer()}.
 * </p>
 * <p>
 *   For update entries, user should call {@link #addEntryForUpdate(String, int, ByteBuf)}.
 *   And {@link #addEntryForFetch(String, int, int)} for fetch entries. Results of fetch should be get
 *   from each entry by calling {@link Entry#getFetchedData()} For each IODataDesc object, there must be only one
 *   type of action, either update or fetch, among all its entries.
 * </p>
 */
public class IOSimpleDDAsync extends IODataDescBase implements DaosEventQueue.Attachment {

  private final long eqWrapperHandle;

  private DaosEventQueue.Event event;

  private int retCode = Integer.MAX_VALUE;

  public static final int RET_CODE_SUCCEEDED = Constants.RET_CODE_SUCCEEDED;

  /**
   * constructor for non-reusable description.
   * User should call {@link #addEntryForFetch(String, int, int)} or
   * {@link #addEntryForUpdate(String, int, ByteBuf)} to add entries.
   * {@link #release()} should be called after it's done.
   *
   * @param dkey
   * distribution key
   * @param updateOrFetch
   * true for update; false for fetch
   * @param eqWrapperHandle
   * handle of EQ wrapper
   * @throws IOException
   */
  protected IOSimpleDDAsync(String dkey, boolean updateOrFetch, long eqWrapperHandle) throws IOException {
    super(dkey, updateOrFetch);
    this.eqWrapperHandle = eqWrapperHandle;
  }

  @Override
  public void setDkey(String dkey) {
    throw new UnsupportedOperationException("setDkey is not supported");
  }

  /**
   * encode dkey + entries descriptions to the Description Buffer.
   * encode entries data to Data Buffer.
   */
  @Override
  public void encode() {
    if (!resultParsed) {
      if (encoded) {
        return;
      }
      if (event == null) {
        throw new IllegalStateException("event is not set");
      }
      calcBufferLen(); // total length before allocating buffer
      this.descBuffer = BufferAllocator.objBufWithNativeOrder(totalDescBufferLen);
      descBuffer.writeLong(eqWrapperHandle);
      descBuffer.writeShort(event.getId());
      descBuffer.writeShort(dkeyBytes.length);
      descBuffer.writeBytes(dkeyBytes);
      descBuffer.writeShort(akeyEntries.size());
      for (Entry entry : akeyEntries) {
        entry.encode();
      }
      encoded = true;
      return;
    }
    throw new IllegalStateException("result is parsed. cannot encode again");
  }

  private void calcBufferLen() {
    if (akeyEntries.isEmpty()) {
      throw new IllegalStateException("no entry added");
    }
    // 2 (number of akey entries) + 8 (eqhandle) + 2 (event id)
    totalRequestBufLen += (Constants.ENCODED_LENGTH_KEY + dkeyBytes.length + 12);
    for (Entry entry : akeyEntries) {
      totalRequestBufLen += entry.getDescLen();
      totalRequestSize += entry.getRequestSize();
    }

    totalDescBufferLen += totalRequestBufLen;
    if (!updateOrFetch) { // for return code and returned actual size
      totalDescBufferLen += 4 + akeyEntries.size() * Constants.ENCODED_LENGTH_EXTENT;
    }
  }

  /**
   * if the object update or fetch succeeded.
   *
   * @return true or false
   */
  @Override
  public boolean isSucceeded() {
    return retCode == RET_CODE_SUCCEEDED;
  }

  @Override
  public IODataDesc duplicate() {
    throw new UnsupportedOperationException("duplicate is not supported");
  }

  protected void setCause(Throwable de) {
    cause = de;
  }

  protected void parseUpdateResult() {
    if (updateOrFetch) {
      descBuffer.writerIndex(descBuffer.capacity());
      descBuffer.readerIndex(totalRequestBufLen);
      retCode = descBuffer.readInt();
      resultParsed = true;
      return;
    }
    throw new UnsupportedOperationException("only support for update");
  }

  /**
   * parse fetch result after JNI call.
   */
  protected void parseFetchResult() {
    if (!updateOrFetch) {
      if (resultParsed) {
        return;
      }
      // update actual size
      int idx = getRequestBufLen();
      descBuffer.writerIndex(descBuffer.capacity());
      for (Entry entry : akeyEntries) {
        descBuffer.readerIndex(idx);
        entry.setActualSize(descBuffer.readInt());
        ByteBuf dataBuffer = entry.dataBuffer;
        dataBuffer.writerIndex(dataBuffer.readerIndex() + entry.actualSize);
        idx += Constants.ENCODED_LENGTH_EXTENT;
      }
      resultParsed = true;
      return;
    }
    throw new UnsupportedOperationException("only support for fetch");
  }

  /**
   * create data description entry for fetch.
   *
   * @param key
   * distribution key
   * @param offset
   * offset inside akey from which to fetch data, should be a multiple of recordSize
   * @param dataSize
   * size of data to fetch, make it a multiple of recordSize as much as possible. zeros are padded to make actual
   * request size a multiple of recordSize.
   * @return data description entry
   * @throws IOException
   */
  public Entry addEntryForFetch(String key, int offset,
                                              int dataSize) throws IOException {
    if (updateOrFetch) {
      throw new IllegalArgumentException("It's desc for update");
    }
    AsyncEntry e = new AsyncEntry(key, offset, dataSize);
    akeyEntries.add(e);
    return e;
  }

  /**
   * create data description entry for update.
   *
   * @param key
   * distribution key
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
  public Entry addEntryForUpdate(String key, int offset, ByteBuf dataBuffer) throws IOException {
    if (!updateOrFetch) {
      throw new IllegalArgumentException("It's desc for fetch");
    }
    AsyncEntry e = new AsyncEntry(key, offset, dataBuffer);
    akeyEntries.add(e);
    return e;
  }

  @Override
  public void setEvent(DaosEventQueue.Event e) {
    this.event = e;
    event.setAttachment(this);
  }

  @Override
  public void reuse() {
    throw new UnsupportedOperationException("not reusable");
  }

  @Override
  public boolean isReusable() {
    return false;
  }

  @Override
  public void ready() {
    if (updateOrFetch) {
      parseUpdateResult();
    } else {
      parseFetchResult();
    }
  }

  @Override
  public boolean alwaysBoundToEvt() {
    return false;
  }

  /**
   * release all buffers created from this object and its entry objects. Be noted, the fetch data buffers are
   * released too if this desc is for fetch. If you don't want release them too early, please call
   * {@link #release(boolean)} with false as parameter.
   */
  @Override
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
      akeyEntries.forEach(e -> e.releaseDataBuffer());
      akeyEntries.clear();
    }
  }

  @Override
  public String toString() {
    return toString(2048);
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
  public class AsyncEntry extends BaseEntry {
    /**
     * constructor for fetch.
     *
     * @param key
     * akey to update on
     * akey record size
     * @param offset
     * offset inside akey, should be a multiple of recordSize
     * @param dataSize
     * size of data to fetch
     * @throws IOException
     */
    private AsyncEntry(String key, int offset, int dataSize)
        throws IOException {
      if (StringUtils.isBlank(key)) {
        throw new IllegalArgumentException("key is blank");
      }
      this.key = key;
      this.keyBytes = key.getBytes(Constants.KEY_CHARSET);
      if (keyBytes.length > Short.MAX_VALUE) {
        throw new IllegalArgumentException("akey length in " + Constants.KEY_CHARSET + " should not exceed "
            + Short.MAX_VALUE + ", akey: " + key);
      }
      this.offset = offset;
      this.dataSize = dataSize;
      if (dataSize <= 0) {
        throw new IllegalArgumentException("data size should be positive, " + dataSize);
      }
    }

    /**
     * construction for update.
     *
     * @param key
     * akey to update on
     * @param offset
     * offset inside akey, should be a multiple of recordSize
     * @param dataBuffer
     * byte buffer (direct buffer preferred) holding data to update. make sure dataBuffer is ready for being read,
     * for example, buffer position and limit are set correctly for reading.
     * make size a multiple of recordSize as much as possible. zeros are padded to make actual request size a multiple
     * of recordSize. user should release the buffer by himself.
     * @throws IOException
     */
    protected AsyncEntry(String key, int offset, ByteBuf dataBuffer) throws IOException {
      this(key, offset, dataBuffer.readableBytes());
      this.dataBuffer = dataBuffer;
    }

    /**
     * length of this entry when encoded into the Description Buffer.
     *
     * @return length
     */
    @Override
    public int getDescLen() {
      // 10 or 18 = key len(2) + [recx idx(4) + recx nr(4)] + data buffer mem address(8)
      return 18 + keyBytes.length;
    }

    /**
     * encode entry to the description buffer which will be decoded in native code.
     */
    @Override
    protected void encode() {
      if (!encoded) {
        long memoryAddress;
        if (!updateOrFetch) {
          dataBuffer = BufferAllocator.objBufWithNativeOrder(dataSize);
          memoryAddress = dataBuffer.memoryAddress();
        } else {
          memoryAddress = dataBuffer.memoryAddress() + dataBuffer.readerIndex();
        }
        descBuffer.writeShort(keyBytes.length);
        descBuffer.writeBytes(keyBytes);
        descBuffer.writeInt(offset);
        descBuffer.writeInt(dataSize);
        descBuffer.writeLong(memoryAddress);
        encoded = true;
      }
    }

    @Override
    public String toString() {
      StringBuilder sb = new StringBuilder();
      sb.append(updateOrFetch ? "update " : "fetch ").append("entry: ");
      sb.append(key).append('|')
        .append(offset).append('|')
        .append(dataSize);
      return sb.toString();
    }
  }
}
