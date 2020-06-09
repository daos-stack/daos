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

package io.daos.fs.hadoop;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;

import io.daos.dfs.DaosFile;

import org.apache.hadoop.fs.FileSystem;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import sun.nio.ch.DirectBuffer;

/**
 * The output stream for Daos system.
 */
public class DaosOutputStream extends OutputStream {
  private static final Logger LOG = LoggerFactory.getLogger(DaosOutputStream.class);

  private ByteBuffer buffer;
  private long fileOffset;
  private boolean closed;
  private String path;
  private final DaosFile daosFile;
  private final FileSystem.Statistics stats;

  public DaosOutputStream(DaosFile daosFile,
                          String path,
                          final int writeBufferSize, FileSystem.Statistics stats) {
    this(daosFile, path, ByteBuffer.allocateDirect(writeBufferSize), stats);
  }

  /**
   * Constructor with daosFile, file path, direct byte buffer and Hadoop file system statistics.
   * @param daosFile
   * DAOS file object
   * @param path
   * file path
   * @param buffer
   * direct byte buffer
   * @param stats
   * Hadoop file system statistics
   */
  public DaosOutputStream(DaosFile daosFile,
                          String path,
                          ByteBuffer buffer, FileSystem.Statistics stats) {
    this.path = path;
    this.daosFile = daosFile;
    this.closed = false;
    this.buffer = buffer;
    this.stats = stats;
    if (!(buffer instanceof DirectBuffer)) {
      throw new IllegalArgumentException("need instance of direct buffer, but " + buffer.getClass().getName());
    }
  }

  /**
   * Write one byte.
   */
  @Override
  public synchronized void write(int b) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream : write single byte into daos");
    }
    checkNotClose();
    this.buffer.put((byte) b);
    if (!this.buffer.hasRemaining()) {
      daosWrite();
    }
  }

  @Override
  public synchronized void write(byte[] buf, int off, int len)
          throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream : write byte array into daos , len = " + len);
    }
    checkNotClose();

    // validate write args
    if (off < 0 || len < 0) {
      throw new IllegalArgumentException("offset/length is negative , offset = " + off + ", length = " + len);
    }
    if (len > (buf.length - off)) {
      throw new IndexOutOfBoundsException("requested more bytes than destination buffer size " +
              " : request length = " + len + ", with offset = " + off + ", buffer capacity =" + (buf.length - off));
    }

    if (this.buffer.remaining() >= len) {
      this.buffer.put(buf, off, len);
      if (!this.buffer.hasRemaining()) {
        daosWrite();
      }
      return;
    }
    while (len > 0) {
      int length = Math.min(len, this.buffer.remaining());
      this.buffer.put(buf, off, length);
      if (!this.buffer.hasRemaining()) {
        daosWrite();
      }
      len -= length;
      off += length;
    }
  }

  /**
   * write data in cache buffer to DAOS.
   */
  private synchronized void daosWrite() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream : Write into this.path == " + this.path);
    }
    long currentTime = 0;
    if (LOG.isDebugEnabled()) {
      currentTime = System.currentTimeMillis();
    }
    long writeSize = this.daosFile.write(
            this.buffer, 0, this.fileOffset,
            this.buffer.position());
    stats.incrementWriteOps(1);
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream : writing by daos_api spend time is : " +
              (System.currentTimeMillis() - currentTime) + " ; writing data size : " + writeSize + ".");
    }
    this.fileOffset += writeSize;
    this.buffer.clear();
  }

  @Override
  public synchronized void close() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream close");
    }
    if (closed) {
      return;
    }
    if (this.buffer.position() > 0) {
      daosWrite();
    }
    if (this.daosFile != null) {
      this.daosFile.release();
    }
    if (this.buffer != null) {
      ((sun.nio.ch.DirectBuffer) this.buffer).cleaner().clean();
      this.buffer = null;
    }
    super.close();
    this.closed = true;
  }

  @Override
  public synchronized void flush() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream flush");
    }
    checkNotClose();

    if (this.buffer.position() > 0) {
      daosWrite();
    }
    super.flush();
  }

  private void checkNotClose() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream checkNotClose");
    }
    if (this.closed) {
      throw new IOException("Stream is closed!");
    }
  }
}
