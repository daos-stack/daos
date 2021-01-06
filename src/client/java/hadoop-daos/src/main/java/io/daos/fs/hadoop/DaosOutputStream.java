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

  private long fileOffset;
  private boolean closed;
  private String path;
  private final DaosFileSource source;
  private final FileSystem.Statistics stats;

  private static final Logger LOG = LoggerFactory.getLogger(DaosOutputStream.class);

  public DaosOutputStream(DaosFile daosFile,
                          String path,
                          final int writeBufferSize, FileSystem.Statistics stats, boolean async) {
    this(daosFile, path, ByteBuffer.allocateDirect(writeBufferSize), stats, async);
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
                          ByteBuffer buffer, FileSystem.Statistics stats, boolean async) {
    this.path = path;
    this.closed = false;
    this.source = async ? new DaosFileSourceAsync(daosFile, buffer) :
        new DaosFileSourceSync(daosFile, buffer);
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
    source.write(b);
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

    source.write(buf, off, len);
  }

  @Override
  public synchronized void close() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream close");
    }
    if (closed) {
      return;
    }
    source.flush();
    source.close();
    super.close();
    this.closed = true;
  }

  @Override
  public synchronized void flush() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream flush");
    }
    checkNotClose();

    source.flush();
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
