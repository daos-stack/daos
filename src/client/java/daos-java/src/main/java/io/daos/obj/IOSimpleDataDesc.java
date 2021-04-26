/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosEventQueue;
import io.daos.DaosIOException;
import io.netty.buffer.ByteBuf;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.io.UnsupportedEncodingException;

/**
 * IO simple description for fetching and updating object records on given dkey. The differences between this class and
 * {@link IODataDescSync} are,
 * - this class defaults record size as 1, iod type as ARRAY.
 * - this class is always reusable.
 * - this class support asynchronous update/fetch.
 *
 * Each record is described in {@link SimpleEntry}.
 * To make JNI call efficient and avoid memory fragmentation, the dkey and entries are serialized to direct buffers
 * which then de-serialized in native code.
 *
 * <p>
 *   There are two types of buffers, Description Buffer and Data Buffers. The Description Buffer holds entries
 *   description, like their akey, type, size. The Data Buffers of entries holds actual data for either update or fetch.
 *   {@link #release()} method should be called after object update or fetch. For update, user is responsible for
 *   releasing data buffers. For fetch, user can determine who release fetch buffers.
 *   See {@link SimpleEntry#release(boolean)}.
 * </p>
 */
public class IOSimpleDataDesc extends IODataDescBase implements DaosEventQueue.Attachment {

  private boolean dkeyChanged;

  private final int maxKenLen;

  private int dkeyLen;

  private int nbrOfAkeysToRequest;

  private final boolean async;

  private boolean released;

  private DaosEventQueue.Event event;

  private final long eqHandle;

  private int retCode = Integer.MAX_VALUE;

  public static final int RET_CODE_SUCCEEDED = Constants.RET_CODE_SUCCEEDED;

  private static final Logger log = LoggerFactory.getLogger(IOSimpleDataDesc.class);

  /**
   * Create simple description for synchronous or asynchronous update/fetch depending on
   * if <code>eqWrapperHandle</code> has zero value.
   *
   * @param maxKeyStrLen
   * max key len in str
   * @param nbrOfEntries
   * number of akey entries
   * @param entryBufLen
   * buffer length of each entry
   * @param eqWrapperHandle
   * 0L for synchronous. asynchronous otherwise.
   */
  protected IOSimpleDataDesc(int maxKeyStrLen, int nbrOfEntries, int entryBufLen, long eqWrapperHandle) {
    super(null, true);
    if (maxKeyStrLen > Short.MAX_VALUE/2 || maxKeyStrLen <= 0) {
      throw new IllegalArgumentException("number of entries should be positive and no larger than " +
          Short.MAX_VALUE/2 + ". " + maxKeyStrLen);
    }
    this.maxKenLen = maxKeyStrLen * 2; // 2 bytes per string character
    if (nbrOfEntries > Short.MAX_VALUE || nbrOfEntries < 0) {
      throw new IllegalArgumentException("number of entries should be positive and no larger than " + Short.MAX_VALUE +
          ". " + nbrOfEntries);
    }
    this.eqHandle = eqWrapperHandle;
    this.async = eqWrapperHandle != 0;
    // 8 for storing native desc pointer
    // 2 for storing maxKenLen
    // 2 for number of entries
    // 2 for dkey length
    // 2 for actual number of entries starting from first entry having data
    int tmpLen = (16 + maxKenLen);
    // 8 for native EQ pointer
    // 2 for event ID
    tmpLen += async ? 10 : 0;
    for (int i = 0; i < nbrOfEntries; i++) {
      SimpleEntry entry = new SimpleEntry(entryBufLen);
      akeyEntries.add(entry);
      tmpLen += entry.getDescLen();
    }
    totalRequestBufLen = tmpLen;
    // for rc and returned actual size
    tmpLen += 4 + nbrOfEntries * Constants.ENCODED_LENGTH_EXTENT;
    totalDescBufferLen = tmpLen;
    this.descBuffer = BufferAllocator.objBufWithNativeOrder(totalDescBufferLen);
    prepareNativeDesc();
    if (!async) {
      // native desc handle written to the start of descBuffer in native code.
      DaosObjClient.allocateSimpleDesc(descBuffer.memoryAddress(), false);
      checkNativeDesc();
    } // group managed native desc for asynchronous desc
  }

  private void checkLen(int len, String keyType) {
    if (len > maxKenLen) {
      throw new IllegalArgumentException(keyType + " length should not exceed "
          + maxKenLen/2 + ". " + len/2);
    }
  }

  protected void checkNativeDesc() {
    descBuffer.readerIndex(0);
    if (descBuffer.readLong() == 0L) {
      throw new IllegalStateException("no native desc created");
    }
  }

  @Override
  public void setEvent(DaosEventQueue.Event event) {
    this.event = event;
    event.setAttachment(this);
  }

  public void setUpdateOrFetch(boolean updateOrFetch) {
    this.updateOrFetch = updateOrFetch;
  }

  public boolean isUpdateOrFetch() {
    return updateOrFetch;
  }

  public int getNbrOfAkeysToRequest() {
    return nbrOfAkeysToRequest;
  }

  public boolean isAsync() {
    return async;
  }

  @Override
  public void setDkey(String dkey) {
    this.dkey = dkey;
    this.dkeyLen = dkey.length() * 2;
    checkLen(dkeyLen, "dkey");
    dkeyChanged = true;
  }

  /**
   * encode dkey + entries descriptions to the Description Buffer.
   * encode entries data to Data Buffer.
   */
  @Override
  public void encode() {
    if (encoded) {
      return;
    }
    if (nbrOfAkeysToRequest == 0) {
      throw new IllegalArgumentException("at least one of entries should have data");
    }
    if (resultParsed) {
      throw new IllegalStateException("reuse() method is not called");
    }
    descBuffer.readerIndex(0);
    descBuffer.writerIndex(12);
    if (async) { // assuming same event queue
      descBuffer.writerIndex(descBuffer.writerIndex() + 8);
      descBuffer.writeShort(event.getId());
    }
    if (dkeyChanged) {
      descBuffer.writeShort(dkeyLen);
      writeKey(dkey);
    } else {
      descBuffer.writerIndex(descBuffer.writerIndex() + 2 + maxKenLen);
    }
    descBuffer.writeShort(nbrOfAkeysToRequest);
    int count = 0;
    for (Entry entry : akeyEntries) {
      if (!((SimpleEntry)entry).isReused()) {
        break;
      }
      entry.encode();
      count++;
    }
    if (nbrOfAkeysToRequest > count) {
      throw new IllegalStateException("number of akeys to request " + nbrOfAkeysToRequest + ", should not exceed " +
          "total reused entries, " + count);
    }
    encoded = true;
  }

  private void prepareNativeDesc() {
    // skip native handle
    descBuffer.writeLong(0L);
    descBuffer.writeShort(maxKenLen);
    descBuffer.writeShort(akeyEntries.size());
    if (async) { // for asynchronous
      descBuffer.writeLong(eqHandle);
      // skip event id
      descBuffer.writerIndex(descBuffer.writerIndex() + 2);
    }
    // skip dkeylen, dkey and nbr of requests
    descBuffer.writerIndex(descBuffer.writerIndex() + 2 + maxKenLen + 2);
    for (Entry entry : akeyEntries) {
      ((SimpleEntry)entry).putAddress(descBuffer);
    }
  }

  private void writeKey(String dkey) {
    int pos = descBuffer.writerIndex();
    for (int i = 0; i < dkey.length(); i++) {
      descBuffer.writeShort(dkey.charAt(i));
    }
    descBuffer.writerIndex(pos + maxKenLen);
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
  public IODataDesc duplicate() throws IOException {
    throw new UnsupportedOperationException("duplicate is not supported");
  }

  @Override
  public ByteBuf getDescBuffer() {
    return descBuffer;
  }

  @Override
  public SimpleEntry getEntry(int index) {
    return (SimpleEntry) akeyEntries.get(index);
  }

  public int getRetCode() {
    return retCode;
  }

  protected void setCause(Throwable de) {
    cause = de;
  }

  protected void parseUpdateResult() {
    if (async) {
      descBuffer.writerIndex(descBuffer.capacity());
      descBuffer.readerIndex(totalRequestBufLen);
      retCode = descBuffer.readInt();
    } else {
      retCode = RET_CODE_SUCCEEDED;
    }
    resultParsed = true;
  }

  /**
   * parse result after JNI call.
   */
  protected void parseFetchResult() {
    if (!updateOrFetch) {
      if (resultParsed) {
        return;
      }
      int nbrOfReq = nbrOfAkeysToRequest;
      int count = 0;
      // update actual size
      int idx = totalRequestBufLen;
      descBuffer.writerIndex(descBuffer.capacity());
      descBuffer.readerIndex(idx);
      retCode = descBuffer.readInt();
      if (retCode != RET_CODE_SUCCEEDED) {
        resultParsed = true;
        return;
      }
      idx += 4;
      for (Entry entry : akeyEntries) {
        if (count < nbrOfReq) {
          descBuffer.readerIndex(idx);
          entry.setActualSize(descBuffer.readInt());
          ByteBuf dataBuffer = entry.dataBuffer;
          dataBuffer.writerIndex(dataBuffer.readerIndex() + entry.actualSize);
          idx += Constants.ENCODED_LENGTH_EXTENT;
          continue;
        }
        break;
      }
      resultParsed = true;
      return;
    }
    throw new UnsupportedOperationException("only support for fetch");
  }

  /**
   * should be called before setting keys and entries except it's first time use.
   */
  @Override
  public void reuse() {
    this.resultParsed = false;
    this.nbrOfAkeysToRequest = 0;
    this.totalRequestSize = 0;
    this.dkeyChanged = false;
    this.encoded = false;
    this.retCode = Integer.MAX_VALUE;
    for (Entry e : akeyEntries) {
      e.actualSize = 0;
      ((SimpleEntry)e).reused = false;
      ((SimpleEntry)e).akeyChanged = false;
    }
  }

  @Override
  public boolean isReusable() {
    return true;
  }

  @Override
  public void ready() {
    if (isUpdateOrFetch()) {
      parseUpdateResult();
    } else {
      parseFetchResult();
    }
  }

  @Override
  public boolean alwaysBoundToEvt() {
    return async;
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
    if (!released) {
      if (!async) {
        descBuffer.readerIndex(0);
        descBuffer.writerIndex(descBuffer.capacity());
        long nativeDescPtr = descBuffer.readLong();
        if (hasNativeDec(nativeDescPtr)) {
          DaosObjClient.releaseDescSimple(nativeDescPtr);
        }
      } // otherwise, native desc will be released in SimpleDataDescGrp
      this.descBuffer.release();
      if ((!resultParsed) && event != null) {
        try {
          event.abort();
        } catch (DaosIOException e) {
          log.error("failed to abort event bound to " + this, e);
        }
        event = null;
      }
      this.released = true;
    }
    if (updateOrFetch || releaseFetchBuffer) {
      for (Entry entry : akeyEntries) {
        entry.releaseDataBuffer();
      }
    }
  }

  private boolean hasNativeDec(long nativeDescPtr) {
    return nativeDescPtr != 0L;
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
  public class SimpleEntry extends BaseEntry {
    private boolean akeyChanged;
    private int akeyLen;
    private boolean reused;

    /**
     * construction for reusable entry.
     *
     * @param bufferLen
     * @throws IOException
     */
    protected SimpleEntry(int bufferLen) {
      this.dataBuffer = BufferAllocator.objBufWithNativeOrder(bufferLen);
    }

    /**
     * length of this entry when encoded into the Description Buffer.
     *
     * @return length
     */
    @Override
    public int getDescLen() {
      // 22 = dkey len(2) + recx idx(8) + recx nr(4) + data buffer mem address(8)
      return 22 + maxKenLen;
    }

    public boolean isReused() {
      return reused;
    }

    /**
     * this method should be called before reusing the data buffer.
     * The data buffer will be cleared before returning to user.
     *
     * @return reused original buffer
     */
    public ByteBuf reuseBuffer() {
      this.dataBuffer.clear();
      return this.dataBuffer;
    }

    /**
     * set Akey and its info for update.
     * User should call {@link #reuseBuffer()} before calling this method.
     *
     * @param akey
     *  null for reusing existing akey.
     * @param offset
     * @param buf
     * reused data buffer
     * @throws UnsupportedEncodingException
     */
    public void setEntryForUpdate(String akey, long offset, ByteBuf buf) {
      if (buf.readerIndex() != 0) {
        throw new IllegalArgumentException("buffer's reader index should be 0. " + buf.readerIndex());
      }
      setEntry(akey, offset, buf, 0);
    }

    /**
     * set Akey and its info for fetch.
     * {@link #reuseBuffer()} is not necessary to be called since it'll be called automatically inside
     * this method.
     *
     * @param akey
     * null for reusing existing akey.
     * @param offset
     * @param fetchDataSize
     * @throws UnsupportedEncodingException
     */
    public void setEntryForFetch(String akey, long offset, int fetchDataSize) {
      this.dataBuffer.clear();
      setEntry(akey, offset, this.dataBuffer, fetchDataSize);
    }

    private void setEntry(String akey, long offset, ByteBuf buf, int fetchDataSize) {
      if (akey != null) {
        setAkey(akey);
      }
      this.offset = offset;
      if (updateOrFetch) {
        this.dataSize = buf.readableBytes();
      } else {
        this.dataSize = fetchDataSize;
        if (dataSize > buf.capacity()) {
          throw new IllegalArgumentException("data size, " + dataSize + "should not exceed buffer capacity, " +
              buf.capacity());
        }
      }
      if (dataSize <= 0) {
        throw new IllegalArgumentException("data size should be positive, " + dataSize);
      }
      if (buf != dataBuffer) {
        throw new IllegalArgumentException("buffer mismatch");
      }
      nbrOfAkeysToRequest++;
      totalRequestSize += dataSize;
      this.reused = true;
    }

    private void setAkey(String akey) {
      this.key = akey;
      this.akeyLen = akey.length() * 2;
      checkLen(akeyLen, "akey");
      this.akeyChanged = true;
    }

    /**
     * encode entry to the description buffer which will be decoded in native code.<br/>
     *
     */
    @Override
    protected void encode() {
      if (akeyChanged) {
        descBuffer.writeShort(akeyLen);
        writeKey(key);
      } else {
        descBuffer.writerIndex(descBuffer.writerIndex() + 2 + maxKenLen);
      }
      descBuffer.writeLong(offset);
      descBuffer.writeInt(dataSize);
      // skip memory address
      descBuffer.writerIndex(descBuffer.writerIndex() + 8);
    }

    private void putAddress(ByteBuf descBuffer) {
      // skip akeylen, akey, offset and length
      descBuffer.writerIndex(descBuffer.writerIndex() + maxKenLen + 14);
      descBuffer.writeLong(dataBuffer.memoryAddress());
    }

    public boolean isFetchBufReleased() {
      if (!updateOrFetch) {
        return dataBuffer == null;
      }
      throw new UnsupportedOperationException("only support for fetch, akey: " + key);
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
