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
import org.apache.hadoop.fs.FSInputStream;
import org.apache.hadoop.fs.FileSystem;

import java.io.EOFException;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * The input stream for Daos system.
 */
public class DaosInputStream extends FSInputStream {
  private static final Log LOG =
      LogFactory.getLog(DaosInputStream.class);
  private final DaosFile daosFile;

  private long pos = 0;      //the current offset from input stream
  private long filePos = 0;  //the current offset from the start of the file
  private int readSize = 0;  // the size of data read from DAOS
  private FileSystem.Statistics stats;
  private ByteBuffer buffer;
  private boolean closed;
  private byte[] singleByte = new byte[1];
  private long contentLength;
  private boolean isRead = false;

  public DaosInputStream(DaosFile daosFile,
      FileSystem.Statistics stats,
      final long contentLength,
      final int readsize) {
    this.daosFile = daosFile;
    this.closed = false;
    this.buffer = ByteBuffer.allocateDirect(readsize);
    this.contentLength = contentLength;
    this.stats = stats;
  }

  @Override
  public synchronized void seek(long targetPos) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("seek specific position pos = " + this.pos
              + " ; targetPos = " + targetPos
              + " ; filePos= " + this.filePos);
    }
    LOG.info("seek specific position pos = " + this.pos
            + " ; targetPos = " + targetPos
            + " ; filePos= " + this.filePos);
    if (targetPos < 0) {
      throw new EOFException("Cannot seek to negative offset");
    }
    if (this.contentLength < targetPos) {
      throw new EOFException("Cannot seek after EOF");
    }
    checkNotClose();
    if (this.filePos == targetPos) {
      return;
    }
    if (this.pos < targetPos) {
      this.pos = targetPos;
      this.filePos = targetPos;
      this.buffer.clear();
      this.isRead=false;
    } else {
      long bufOffset =  this.pos - this.readSize;
      if(bufOffset < 0){
        throw new EOFException("Cannot seek from buffer after EOF");
      }else if (bufOffset <= targetPos) {
        int newPosition = (int) (targetPos - bufOffset);
        this.buffer.position(newPosition);
        this.filePos = targetPos;
      } else if (bufOffset > targetPos) {
        this.filePos = targetPos;
        this.buffer.clear();
        this.isRead=false;
      } else {
        LOG.error("seek specific position ,pos = " + pos
                + " ; targetPos = " + targetPos
                + " ; filePos= "+ filePos);
      }
    }
  }

  @Override
  public long skip(long n) throws IOException {
    if (n > 0){
      long curPos = getPos();
      long fileLen = this.contentLength;
      if(n+curPos > fileLen){
        n = fileLen - curPos;
      }
      seek(curPos+n);
      return n;
    }
    return n < 0 ? -1 : 0;
  }


  private void checkNotClose() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("checkNotClose");
    }
    if (this.closed) {
      throw new IOException("Stream is closed!");
    }
  }

  @Override
  public long getPos() {
    return this.filePos;
  }

  @Override
  public boolean seekToNewSource(long targetPos) {
    return false;
  }

  /**
   * Reads the next byte of data from the input stream.
   */
  @Override
  public synchronized int read() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("Reads the next byte of data from the input stream");
    }
    LOG.info("Reads the next byte of data from the input stream");

    if (this.singleByte == null) {
      this.singleByte = new byte[1];
    }
    read(singleByte, 0, 1);
    return this.singleByte[0] & 0xff;
  }

  /**
   * Read the entire buffer.
   */
  @Override
  public synchronized int read(byte[] buf, int off, int len)
      throws IOException {
    LOG.info("read from daos ,this.contentLength = " + this.contentLength + " ; this.filePos = " +this.filePos);
    int rSize= 0;
    if (buf == null) {
      throw new NullPointerException();
    } else if (off < 0 || len < 0 || len > buf.length - off) {
      throw new IndexOutOfBoundsException();
    } else if (len == 0) {
      return 0;
    }
    if ((this.buffer.position() == 0) && (!this.isRead)) {
      int readS = readFormDaos();
      if (readS == 0) {
        return -1;
      }
    }
    if(available()==0){
      return 0;
    } else if(this.contentLength > this.filePos){
      int remain =available();
      if(remain<0){
        LOG.info("Exceeding file length !! ");
        return -1;
      }else if(remain>len){
        if(hasRemainLen()>len){
          this.buffer.get(buf, off, len);
          this.filePos+=len;
          rSize = len;
          if (this.stats != null) {
            this.stats.incrementBytesRead(len);
          }
        }else{
          int readSizeTmp = hasRemainLen();
          this.buffer.get(buf, off, readSizeTmp);
          this.filePos+=readSizeTmp;
          rSize = readSizeTmp;
          if (this.stats != null) {
            this.stats.incrementBytesRead(readSizeTmp);
          }
          this.isRead = false;
          this.buffer.clear();
        }
      }else{
        if(hasRemainLen()>remain){
          this.buffer.get(buf, off, remain);
          this.filePos+=remain;
          rSize = remain;
          if (this.stats != null) {
            this.stats.incrementBytesRead(remain);
          }
        }else{
          int readSizeTmp = hasRemainLen();
          this.buffer.get(buf, off, readSizeTmp);
          this.filePos+=readSizeTmp;
          rSize = readSizeTmp;
          if (this.stats != null) {
            this.stats.incrementBytesRead(readSizeTmp);
          }
          this.isRead = false;
          this.buffer.clear();
        }
      }
    }else{
      if (LOG.isDebugEnabled()) {
        LOG.debug("Exceeding file length !! ");
      }
      LOG.info("Exceeding file length !! ");
      return -1;
    }
    return rSize == 0 ? -1 : rSize;
  }

  private int hasRemainLen() {
    return this.readSize - this.buffer.position();
  }


  private int readFormDaos() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("read from daos ,offset = " + this.pos);
    }
    LOG.info("read from daos ,offset = " + this.pos);

    long currentTime = System.currentTimeMillis();
    try {
      this.readSize = this.daosFile
        .read(this.pos, this.buffer);
    } catch (DaosNativeException e) {
      LOG.error(e.getMessage());
      throw new IOException(e.getMessage());
    }
    if (LOG.isDebugEnabled()) {
      LOG.debug("reading from daos_api spend time is :  "
          + (System.currentTimeMillis()-currentTime)
          + " ; read data size : " + readSize
          + "; buffer size : "+ this.buffer.capacity()
          +".");
    }
    LOG.info("reading from daos_api spend time is :  "
            + (System.currentTimeMillis()-currentTime)
            + " ; read data size : " + readSize
            + "; buffer size : "+ this.buffer.capacity()
            +".");
    if (readSize < 0) {
      throw new IOException("read failed , rc = " + readSize);
    } else if (this.readSize > 0) {
      this.pos += this.readSize;
      this.isRead = true;
      if (this.stats != null) {
        this.stats.incrementReadOps(1);
      }
    }
    return this.readSize;
  }

  @Override
  public synchronized void close() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("close , FileSystem.Statistics = " + this.stats.toString());
    }
    LOG.info("close , FileSystem.Statistics = " + this.stats.toString());

    if (this.closed) {
      return;
    }
    this.closed = true;
    try {
      this.daosFile.close();
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
  public int available() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("available");
    }
    checkNotClose();
    long remaining = this.contentLength - this.filePos;
    if (remaining > Integer.MAX_VALUE) {
      return Integer.MAX_VALUE;
    }
    return (int) remaining;
  }
}
