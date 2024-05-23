/*
 * (C) Copyright 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.dfs;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosEventQueue;
import io.daos.DaosIOException;
import io.netty.buffer.ByteBuf;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * description of dfs IO
 */
public class IODfsDesc implements DaosEventQueue.Attachment {

  private final ByteBuf descBuffer;
  private final ByteBuf dataBuffer;
  private final long natveHandle;
  private final int retOffset;
  private final int descBufLen;
  private DaosEventQueue.Event event;

  private int retCode = Integer.MAX_VALUE;
  private boolean resultParsed;
  private boolean readOrWrite; // default to write

  private int actualLength;

  private boolean released;
  private boolean discarded;

  private static final Logger log = LoggerFactory.getLogger(IODfsDesc.class);

  protected IODfsDesc(ByteBuf dataBuffer, DaosEventQueue eq) {
    this.dataBuffer = dataBuffer;
    // nativeHandle + dataMemoryAddr + eventGrpHandle + offset + length + event ID +
    // rc + actual len (for read)
    retOffset = 8 + 8 + 8 + 8 + 8 + 2;
    descBufLen =  retOffset + 4 + 4;
    descBuffer = BufferAllocator.objBufWithNativeOrder(descBufLen);
    descBuffer.writerIndex(8); // skip nativeHandle
    descBuffer.writeLong(dataBuffer.memoryAddress());
    descBuffer.writeLong(eq.getEqWrapperHdl());
    natveHandle = DaosFsClient.allocateDfsDesc(descBuffer.memoryAddress());
  }

  public void setReadOrWrite(boolean readOrWrite) {
    this.readOrWrite = readOrWrite;
  }

  @Override
  public void setEvent(DaosEventQueue.Event e) {
    this.event = e;
    this.event.setAttachment(this);
  }

  @Override
  public void reuse() {
    this.resultParsed = false;
    retCode = Integer.MAX_VALUE;
    actualLength = -1;
  }

  @Override
  public void ready() {
    descBuffer.readerIndex(retOffset);
    descBuffer.writerIndex(descBufLen);
    retCode = descBuffer.readInt();
    if (readOrWrite) {
      dataBuffer.readerIndex(0);
      actualLength = descBuffer.readInt();
      dataBuffer.writerIndex(actualLength);
    }
    resultParsed = true;
  }

  @Override
  public boolean alwaysBoundToEvt() {
    return false;
  }

  @Override
  public void discard() {
    discarded = true;
  }

  @Override
  public boolean isDiscarded() {
    return discarded;
  }

  /**
   * release desc buffer. data buffer should be released outside.
   */
  @Override
  public void release() {
    if (released) {
      return;
    }
    if ((!resultParsed) && event != null) {
      try {
        event.abort();
      } catch (DaosIOException e) {
        log.error("failed to abort event bound to " + this, e);
      }
      event = null;
    }
    if (descBuffer != null) {
      descBuffer.release();
    }
    if (natveHandle != 0) {
      DaosFsClient.releaseDfsDesc(natveHandle);
    }
    released = true;
  }

  public void encode(long offset, long len) {
    if (resultParsed) {
      throw new IllegalStateException("reuse() should be called first");
    }
    descBuffer.readerIndex(0);
    descBuffer.writerIndex(24); // skip native handle, memory address and eq handle
    descBuffer.writeLong(offset).writeLong(len);
    if (event == null) {
      throw new IllegalStateException("event is not set");
    }
    descBuffer.writeShort(event.getId());
  }

  public boolean isSucceeded() {
    return retCode == Constants.RET_CODE_SUCCEEDED;
  }

  public ByteBuf getDescBuffer() {
    return descBuffer;
  }

  public int getActualLength() {
    if (readOrWrite) {
      return actualLength;
    }
    throw new UnsupportedOperationException("for read only");
  }
}
