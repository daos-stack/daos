/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.intel.daos.hadoop.fs;

import com.intel.daos.DaosFile;
import com.intel.daos.DaosNativeException;
import org.apache.commons.logging.Log;
import org.apache.commons.logging.LogFactory;
import org.apache.hadoop.fs.FileSystem;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;

/**
 * The output stream for Daos system.
 */
public class DaosOutputStream extends OutputStream {
  private static final Log LOG =
      LogFactory.getLog(DaosOutputStream.class);
  private FileSystem.Statistics stats;
  private ByteBuffer buffer;
  private final byte[] singleByte = new byte[1];
  private long position = 0;
  private boolean closed;
  private String path;
  private final DaosFile daosFile;

  public DaosOutputStream(DaosFile daosFile,
      String path,
      FileSystem.Statistics stats,
      final int writeSize) {
    this.path = path;
    this.stats = stats;
    this.daosFile = daosFile;
    this.closed = false;
    this.buffer = ByteBuffer.allocateDirect(writeSize);
  }

  /**
   * Write one byte.
   */
  @Override
  public synchronized void write(int b) throws IOException {
    this.singleByte[0] = (byte) b;
    this.buffer.put(this.singleByte);
    if (!this.buffer.hasRemaining()) {
      try {
        daoswrite();
      } catch (DaosNativeException e) {
        LOG.error(e.getMessage());
        throw new IOException(e.getMessage());
      }
    }
  }

  @Override
  public synchronized void write(byte[] b, int off, int len)
      throws IOException {
    if (this.closed) {
      throw new IOException("Stream is closed!");
    } else if (b == null) {
      throw new NullPointerException();
    }
    for (int i = 0; i < len; i++) {
      write(b[off + i]);
    }
  }

  private synchronized void daoswrite() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("Write into this.path == " + this.path);
    }
    LOG.info("Write into this.path == " + this.path);

    long currentTime = System.currentTimeMillis();
    int writeSize = -1;
    if (this.daosFile == null) {
      throw new IOException("");
    }
    writeSize = this.daosFile.write(this.position,
      this.buffer,
      this.buffer.position());
    if (LOG.isDebugEnabled()) {
      LOG.debug("writing by daos_api spend time is : "
          + (System.currentTimeMillis() - currentTime)
          + " ; writing data size : " + writeSize + ".");
    }
    LOG.info("writing by daos_api spend time is : "
            + (System.currentTimeMillis() - currentTime)
            + " ; writing data size : " + writeSize + ".");
    if(writeSize < 0){
      LOG.error("write failed , rc = " + writeSize);
      throw new IOException("write failed , rc = " + writeSize);
    }
    this.position += writeSize;
    if (this.stats != null) {
      this.buffer.clear();
      this.stats.incrementBytesWritten(writeSize);
      this.stats.incrementWriteOps(1);
    }
  }

  @Override
  public synchronized void close() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("close");
    }
    if (this.closed) {
      throw new IOException("Stream is closed!");
    }
    this.closed = true;
    try {
      if (this.buffer.position() > 0) {
        daoswrite();
      }
      if (this.daosFile != null) {
        this.daosFile.close();
      }
    } catch (DaosNativeException e) {
      LOG.error(e.getMessage());
      throw new IOException(e.getMessage());
    }
    if(this.buffer!=null){
      ((sun.nio.ch.DirectBuffer)this.buffer).cleaner().clean();
      this.buffer = null;
    }
    super.close();
  }

  @Override
  public synchronized void flush() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("flush");
    }
    if (this.closed) {
      throw new IOException("Stream is closed!");
    }
    if (this.buffer.position() > 0) {
      try {
        daoswrite();
      } catch (DaosNativeException e) {
        LOG.error(e.getMessage());
        throw new IOException(e.getMessage());
      }
    }
    super.flush();
  }
}
