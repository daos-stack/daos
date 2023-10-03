/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import io.daos.BufferAllocator;
import io.daos.dfs.DaosFile;
import io.netty.buffer.ByteBuf;
import org.apache.hadoop.fs.FileSystem;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;

public abstract class DaosFileSource {

  protected final DaosFile daosFile;

  protected final long fileLen;

  protected final ByteBuf buffer;

  private final boolean selfManagedBuf;

  protected final int bufCapacity;

  protected int readSize;

  protected final FileSystem.Statistics stats;

  private long lastReadPos;  // file offset from which file data is read to buffer
  private long nextReadPos;  // next read position

  private long nextWritePos;

  private static final Logger LOG = LoggerFactory.getLogger(DaosFileSource.class);

  protected DaosFileSource(DaosFile daosFile, int bufCapacity, long fileLen, boolean append,
                           FileSystem.Statistics stats) {
    this.daosFile = daosFile;
    this.buffer = BufferAllocator.directNettyBuf(bufCapacity);
    selfManagedBuf = true;
    this.bufCapacity = buffer.capacity();
    this.fileLen = fileLen;
    this.stats = stats;
    if (append) {
      nextWritePos = fileLen;
    }
  }

  protected DaosFileSource(DaosFile daosFile, ByteBuf buffer, long fileLen, boolean append,
                           FileSystem.Statistics stats) {
    this.daosFile = daosFile;
    this.buffer = buffer;
    selfManagedBuf = false;
    this.bufCapacity = buffer.capacity();
    this.fileLen = fileLen;
    this.stats = stats;
    if (append) {
      nextWritePos = fileLen;
    }
  }

  public void setReadSize(int readSize) {
    this.readSize = readSize;
  }

  public void close() {
    this.daosFile.release();
    if (selfManagedBuf) {
      buffer.release();
    }
    closeMore();
  }

  protected void closeMore() {}

  public void write(int b) throws IOException {
    buffer.writeByte(b);
    if (buffer.writableBytes() == 0) {
      daosWrite();
    }
  }

  public void write(byte[] buf, int off, int len) throws IOException {
    if (buffer.writableBytes() >= len) {
      buffer.writeBytes(buf, off, len);
      if (buffer.writableBytes() == 0) {
        daosWrite();
      }
      return;
    }
    while (len > 0) {
      int length = Math.min(len, buffer.writableBytes());
      buffer.writeBytes(buf, off, length);
      if (buffer.writableBytes() == 0) {
        daosWrite();
      }
      len -= length;
      off += length;
    }
  }

  /**
   * write data in cache buffer to DAOS.
   */
  private void daosWrite() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("daosWrite : Write into this.path == " + this.daosFile.getPath());
    }
    long currentTime = 0;
    if (LOG.isDebugEnabled()) {
      currentTime = System.currentTimeMillis();
    }
    long writeSize = doWrite(nextWritePos);
    stats.incrementWriteOps(1);
    if (LOG.isDebugEnabled()) {
      LOG.debug("daosWrite : writing by daos_api spend time is : " +
          (System.currentTimeMillis() - currentTime) + " ; writing data size : " + writeSize + ".");
    }
    this.nextWritePos += writeSize;
    this.buffer.clear();
  }

  protected abstract int doWrite(long nextWritePos) throws IOException;

  public void flush() throws IOException {
    if (buffer.readableBytes() > 0) {
      daosWrite();
    }
  }

  public long nextReadPos() {
    return nextReadPos;
  }

  public void setNextReadPos(long targetPos) {
    this.nextReadPos = targetPos;
  }

  public int read(byte[] buf, int off, int len) throws IOException {
    int actualLen = 0;
    // check buffer overlay
    long start = lastReadPos;
    long end = lastReadPos + buffer.writerIndex();
    // Copy requested data from internal buffer to result array if possible
    if (nextReadPos >= start && nextReadPos < end) {
      buffer.readerIndex((int) (nextReadPos - start));
      int remaining = (int) (end - nextReadPos);

      // Want to read len bytes, and there is remaining bytes left in buffer, pick the smaller one
      actualLen = Math.min(remaining, len);
      buffer.readBytes(buf, off, actualLen);
      nextReadPos += actualLen;
      off += actualLen;
      len -= actualLen;
    }
    // Read data from DAOS to result array
    actualLen += readFromDaos(buf, off, len);
    // -1 : reach EOF
    return actualLen == 0 ? -1 : actualLen;
  }

  private int readFromDaos(byte[] buf, int off, int len) throws IOException {
    int numBytes = 0;
    while (len > 0 && (nextReadPos < fileLen)) {
      int actualLen = readFromDaos(len);
      if (actualLen == 0) {
        break;
      }
      // If we read 3 bytes but need 1, put 1; if we read 1 byte but need 3, put 1
      int lengthToPutInBuffer = Math.min(len, actualLen);
      buffer.readBytes(buf, off, lengthToPutInBuffer);
      numBytes += lengthToPutInBuffer;
      nextReadPos += lengthToPutInBuffer;
      off += lengthToPutInBuffer;
      len -= lengthToPutInBuffer;
    }
    return numBytes;
  }

  /**
   * Read data from DAOS and put into cache buffer.
   */
  private int readFromDaos(int length) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("readFromDaos : read from daos ,filePos = {}", this.nextReadPos);
    }
    buffer.clear();

    length = Math.min(length, bufCapacity);
    length = Math.max(length, readSize);

    long currentTime = 0;
    if (LOG.isDebugEnabled()) {
      currentTime = System.currentTimeMillis();
    }

    int actualLen = doRead(this.nextReadPos, length);
    buffer.writerIndex(actualLen);
    lastReadPos = nextReadPos;
    stats.incrementReadOps(1);
    this.stats.incrementBytesRead(actualLen);
    if (LOG.isDebugEnabled()) {
      LOG.debug("readFromDaos :reading from daos_api spend time is :  " +
          (System.currentTimeMillis() - currentTime) + " ;" +
          " requested data size: " + length +
          " actual data size : " + actualLen);
    }
    return actualLen;
  }

  protected abstract int doRead(long nextReadPos, int length) throws IOException;

  public int writerIndex() {
    return buffer.writerIndex();
  }

  public int readerIndex() {
    return buffer.readerIndex();
  }
}
