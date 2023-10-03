/*
 * (C) Copyright 2018-2021 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-2-Clause-Patent
 */

package io.daos.fs.hadoop;

import java.io.IOException;
import java.io.OutputStream;

import com.jcraft.jsch.IO;
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

  /**
   * Constructor for self buffer management and non-append
   *
   * @param daosFile
   * @param writeBufferSize
   * @param stats
   * @param async
   * @throws IOException
   */
  protected DaosOutputStream(DaosFile daosFile,
                             final int writeBufferSize, FileSystem.Statistics stats,
                             boolean async) throws IOException {
    this(daosFile, BufferAllocator.directNettyBuf(writeBufferSize), stats, false, async, true);
  }

  /**
   * Constructor for self buffer management
   *
   * @param daosFile
   * @param writeBufferSize
   * @param stats
   * @param append
   * @param async
   * @throws IOException
   */
  protected DaosOutputStream(DaosFile daosFile,
                          final int writeBufferSize, FileSystem.Statistics stats,
                          boolean append, boolean async) throws IOException {
    this(daosFile, BufferAllocator.directNettyBuf(writeBufferSize), stats, append, async, true);
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
                          ByteBuf buffer, FileSystem.Statistics stats, boolean append, boolean async,
                          boolean selfBuffer) throws IOException {
    this.closed = false;
    this.source = async ? new DaosFileSourceAsync(daosFile, buffer,
        append ? daosFile.length() : 0, false, append, stats) :
        new DaosFileSourceSync(daosFile, buffer, append ? daosFile.length() : 0, append, stats);
    this.buffer = selfBuffer ? buffer : null;
    this.stats = stats;
    if (!buffer.hasMemoryAddress()) {
      throw new IllegalArgumentException("need direct buffer, but " + buffer.getClass().getName());
    }
  }

  /**
   * Constructor for external buffer management and non-append
   *
   * @param daosFile
   * @param buffer
   * @param stats
   * @param async
   * @throws IOException
   */
  protected DaosOutputStream(DaosFile daosFile,
                             ByteBuf buffer, FileSystem.Statistics stats,
                             boolean async) throws IOException {
    this(daosFile, buffer, stats, false, async, false);
  }

  /**
   * Constructor for external buffer management
   *
   * @param daosFile
   * DAOS file object
   * @param buffer
   * direct byte buffer
   * @param stats
   * Hadoop file system statistics
   */
  protected DaosOutputStream(DaosFile daosFile,
                             ByteBuf buffer, FileSystem.Statistics stats,
                             boolean append, boolean async) throws IOException {
    this(daosFile, buffer, stats, append, async, false);
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
