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

import com.intel.daos.client.DaosFile;
import org.apache.hadoop.fs.FileSystem;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;

/**
 * The output stream for Daos system.
 */
public class DaosOutputStream extends OutputStream {
  private static final Logger LOG = LoggerFactory.getLogger(DaosOutputStream.class);

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
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream : write single byte into daos");
    }
    checkNotClose();
    this.singleByte[0] = (byte) b;
    this.buffer.put(this.singleByte);
    if (!this.buffer.hasRemaining()) {
      try {
        daoswrite();
      } catch (IOException e) {
        throw new IOException(e.getMessage());
      }
    }
  }

  @Override
  public synchronized void write(byte[] b, int off, int len)
      throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream : write into daos , len = " + len);
    }
    checkNotClose();

    // validate write args
    if (b == null) {
      throw new NullPointerException("Null buffer");
    } else if (off < 0 || len < 0) {
      throw new IllegalArgumentException("offset/length is nagative , offset = " + off + ", length = " + len);
    } else if(len > (b.length - off)){
      throw new IndexOutOfBoundsException("requested more bytes than destination buffer size "
              +" : request length = " + len + ", with offset = " + off + ", buffer capacity =" + (b.length - off ));
    }

    //start
    try {
      if (!this.buffer.hasRemaining()) {
        daoswrite();
      }
      if(this.buffer.remaining()>= len){
        this.buffer.put(b,off,len);
      }else{
        int length = this.buffer.remaining();
        this.buffer.put(b,off,length);
        if (!this.buffer.hasRemaining()) {
          daoswrite();
        }
        write(b,off+length,(len-length));
      }
    } catch (IOException e) {
      throw new IOException(e.getMessage());
    }
  }

  /**
   * write data in cache buffer to DAOS.
   */
  private synchronized void daoswrite() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream : Write into this.path == " + this.path);
    }
    long currentTime = System.currentTimeMillis();
    long writeSize = -1;
    if (this.daosFile == null) {
      throw new NullPointerException("Null daosFile");
    }
    writeSize = this.daosFile.write(
      this.buffer, 0,this.position,
      this.buffer.position());
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosOutputStream : writing by daos_api spend time is : "
          + (System.currentTimeMillis() - currentTime)
          + " ; writing data size : " + writeSize + ".");
    }
    if(writeSize < 0){
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
      LOG.debug("DaosOutputStream close");
    }
    checkNotClose();
    try {
      if (this.buffer.position() > 0) {
        daoswrite();
      }
      if (this.daosFile != null) {
        this.daosFile.release();
      }
    } catch (IOException e) {
      LOG.error(e.getMessage());
      throw new IOException(e.getMessage());
    }
    if(this.buffer!=null){
      ((sun.nio.ch.DirectBuffer)this.buffer).cleaner().clean();
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
      try {
        daoswrite();
      } catch (IOException e) {
        LOG.error(e.getMessage());
        throw new IOException(e.getMessage());
      }
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
