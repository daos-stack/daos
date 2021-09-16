/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import java.io.IOException;
import java.io.OutputStream;

import io.daos.BufferAllocator;
import io.daos.dfs.DaosFile;

import io.netty.buffer.ByteBuf;
import org.apache.hadoop.fs.FileSystem;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * The output stream for Daos system.
 */
public class DaosOutputStream extends OutputStream {

  private boolean closed;
  private final DaosFileSource source;
  private final FileSystem.Statistics stats;
  private final ByteBuf buffer;

  private static final Logger LOG = LoggerFactory.getLogger(DaosOutputStream.class);

  protected DaosOutputStream(DaosFile daosFile,
                          final int writeBufferSize, FileSystem.Statistics stats, boolean async) {
    this(daosFile, BufferAllocator.directNettyBuf(writeBufferSize), stats, async, true);
  }

  /**
   * Constructor with daosFile, file path, direct byte buffer and Hadoop file system statistics.
   *
   * @param daosFile
   * DAOS file object
   * @param buffer
   * direct byte buffer
   * @param stats
   * Hadoop file system statistics
   * @param selfBuffer
   * is self managed buffer?
   */
  private DaosOutputStream(DaosFile daosFile,
                          ByteBuf buffer, FileSystem.Statistics stats, boolean async, boolean selfBuffer) {
    this.closed = false;
    this.source = async ? new DaosFileSourceAsync(daosFile, buffer, 0, false, stats) :
        new DaosFileSourceSync(daosFile, buffer, 0, stats);
    this.buffer = selfBuffer ? buffer : null;
    this.stats = stats;
    if (!buffer.hasMemoryAddress()) {
      throw new IllegalArgumentException("need direct buffer, but " + buffer.getClass().getName());
    }
  }

  /**
   * Constructor with daosFile, file path, direct byte buffer and Hadoop file system statistics.
   *
   * @param daosFile
   * DAOS file object
   * @param buffer
   * direct byte buffer
   * @param stats
   * Hadoop file system statistics
   */
  protected DaosOutputStream(DaosFile daosFile,
                          ByteBuf buffer, FileSystem.Statistics stats, boolean async) {
    this(daosFile, buffer, stats, async, false);
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
    if (this.buffer != null) {
      this.buffer.release();
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

  public DaosFileSource getSource() {
    return source;
  }
}
