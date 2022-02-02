/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosEventQueue;
import io.daos.DaosIOException;
import io.daos.DaosUtils;
import io.netty.buffer.ByteBuf;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.UnsupportedEncodingException;

/**
 * IO Description for asynchronously update only one entry, dkey/akey.
 */
public class IODescUpdAsync implements DaosEventQueue.Attachment {

  private final short maxKeyLen;

  private final long nativeHdl;

  private ByteBuf descBuffer;

  private long offset;

  private ByteBuf dataBuffer;

  private int requestLen;

  private DaosEventQueue.Event event;

  private boolean resultParsed;

  private boolean discarded;

  private int retCode = Integer.MAX_VALUE;

  public static final int RET_CODE_SUCCEEDED = Constants.RET_CODE_SUCCEEDED;

  private static final Logger log = LoggerFactory.getLogger(IODescUpdAsync.class);

  /**
   * constructor for non-reusable.
   *
   * @param dkey
   * @param akey
   * @param offset
   * @param dataBuffer
   */
  public IODescUpdAsync(String dkey, String akey, long offset, ByteBuf dataBuffer) {
    this.maxKeyLen = 0;
    this.nativeHdl = 0L;
    this.offset = offset;
    this.dataBuffer = dataBuffer;
    byte[] dkeyBytes = DaosUtils.keyToBytes8(dkey);
    byte[] akeyBytes = DaosUtils.keyToBytes(akey);
    // 8 for null native handle, 4 = 2 + 2 for lens,
    requestLen = 8 + dkeyBytes.length + akeyBytes.length + 4;
    // 4 for return code
    descBuffer = BufferAllocator.objBufWithNativeOrder(requestLen + 4);
    descBuffer.writeLong(0L);
    descBuffer.writeShort(dkeyBytes.length);
    descBuffer.writeBytes(dkeyBytes);
    descBuffer.writeShort(akeyBytes.length);
    descBuffer.writeBytes(akeyBytes);
  }

  /**
   * constructor for reusable.
   *
   * @param maxKeyLen
   */
  public IODescUpdAsync(int maxKeyLen) {
    if (maxKeyLen <= 0 || maxKeyLen > Short.MAX_VALUE) {
      throw new IllegalArgumentException("max key length should be bigger than 0 and less than " + Short.MAX_VALUE
          + ", value is " + maxKeyLen);
    }
    this.maxKeyLen = (short)maxKeyLen;
    requestLen = 8 + 2 + 2 * (maxKeyLen + 2); // 8 for address of native desc, 2 for maxKeyLen
    descBuffer = BufferAllocator.objBufWithNativeOrder(requestLen + 4);
    descBuffer.writeLong(0L);
    descBuffer.writeShort(maxKeyLen);
    DaosObjClient.allocateDescUpdAsync(descBuffer.memoryAddress());
    descBuffer.readerIndex(0);
    nativeHdl = descBuffer.readLong();
    if (nativeHdl == 0L) {
      throw new IllegalStateException("no native desc created");
    }
  }

  public void setDkey(String dkey) {
    checkReusable();
    checkState();
    byte[] bytes = DaosUtils.keyToBytes8(dkey, maxKeyLen);
    descBuffer.writerIndex(10); // 8 (native desc hdl) + 2 (max key len)
    descBuffer.writeShort(bytes.length);
    descBuffer.writeBytes(bytes);
  }

  public void setAkey(String akey) {
    checkReusable();
    checkState();
    byte[] bytes = DaosUtils.keyToBytes(akey, maxKeyLen);
    descBuffer.writerIndex(12 + maxKeyLen); // 8 (native desc hdl) + 2 (max key len) + 2 (key len)
    descBuffer.writeShort(bytes.length);
    descBuffer.writeBytes(bytes);
  }

  public void setOffset(long offset) {
    checkReusable();
    checkState();
    this.offset = offset;
  }

  public void setDataBuffer(ByteBuf dataBuffer) {
    checkReusable();
    checkState();
    this.dataBuffer = dataBuffer;
  }

  private void checkState() {
    if (resultParsed) {
      throw new IllegalStateException("call reuse() first");
    }
  }

  private void checkReusable() {
    if (!isReusable()) {
      throw new IllegalStateException("not reusable");
    }
  }

  public boolean isReusable() {
    return maxKeyLen > 0;
  }

  public boolean isSucceeded() {
    return retCode == RET_CODE_SUCCEEDED;
  }

  public ByteBuf getDescBuffer() {
    return descBuffer;
  }

  public ByteBuf getDataBuffer() {
    return dataBuffer;
  }

  public DaosEventQueue.Event getEvent() {
    return event;
  }

  @Override
  public void setEvent(DaosEventQueue.Event e) {
    this.event = e;
    e.setAttachment(this);
  }

  @Override
  public void reuse() {
    checkReusable();
    descBuffer.readerIndex(0);
    resultParsed = false;
    retCode = -1;
    event = null;
    if (dataBuffer != null) {
      dataBuffer.release();
      dataBuffer = null;
    }
  }

  @Override
  public void ready() {
    descBuffer.writerIndex(descBuffer.capacity());
    descBuffer.readerIndex(requestLen);
    retCode = descBuffer.readInt();
  }

  @Override
  public boolean alwaysBoundToEvt() {
    return false;
  }

  @Override
  public boolean isDiscarded() {
    return discarded;
  }

  @Override
  public void discard() {
    discarded = true;
  }

  public int getReturnCode() {
    return retCode;
  }

  @Override
  public void release() {
    if (descBuffer != null) {
      if (isReusable() & nativeHdl > 0) {
        DaosObjClient.releaseDescUpdAsync(nativeHdl);
      }
      descBuffer.release();
      descBuffer = null;
    }
    if (dataBuffer != null) {
      dataBuffer.release();
      dataBuffer = null;
    }
    if ((!resultParsed) && event != null) {
      try {
        event.abort();
      } catch (DaosIOException e) {
        log.error("failed to abort event bound to " + this, e);
      }
      event = null;
    }
  }

  public long getEqHandle() {
    if (event == null) {
      throw new IllegalStateException("event is not set");
    }
    return event.getEqHandle();
  }

  public short getEventId() {
    if (event == null) {
      throw new IllegalStateException("event is not set");
    }
    return event.getId();
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

  private String readKey(ByteBuf buf, int len) {
    if (len < 0 || len >= buf.capacity() - 10) {
      return "not set";
    }
    byte[] bytes = new byte[len];
    buf.readBytes(bytes);
    try {
      return new String(bytes, Constants.KEY_CHARSET);
    } catch (UnsupportedEncodingException e) {
      return "not set";
    }
  }

  @Override
  public String toString() {
    String dkey, akey;
    if (maxKeyLen == 0) {
      descBuffer.writerIndex(descBuffer.capacity());
      descBuffer.readerIndex(8);
      int l = descBuffer.readShort();
      dkey = readKey(descBuffer, l);
      l = descBuffer.readShort();
      akey = readKey(descBuffer, l);
    } else {
      descBuffer.writerIndex(descBuffer.capacity());
      descBuffer.readerIndex(10);
      int l = descBuffer.readShort();
      dkey = l > maxKeyLen ? "not set" : readKey(descBuffer, l);
      descBuffer.readerIndex(12 + maxKeyLen);
      l = descBuffer.readShort();
      akey = l > maxKeyLen ? "not set" : readKey(descBuffer, l);
    }
    StringBuilder sb = new StringBuilder();
    sb.append("dkey: ").append(dkey).append(", akey entries\n");
    sb.append("[update entry|").append(maxKeyLen > 0 ? "" : "not ")
        .append("reusable|")
        .append(akey).append('|')
        .append(offset).append('|')
        .append(dataBuffer == null ? 0 : dataBuffer.readableBytes()).append('|')
        .append(resultParsed).append('|')
        .append(retCode).append(']');
    return sb.toString();
  }
}
