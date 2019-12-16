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
import org.apache.hadoop.fs.FSInputStream;
import org.apache.hadoop.fs.FileSystem;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import java.io.EOFException;
import java.io.IOException;
import java.nio.ByteBuffer;

/**
 * The input stream for Daos system.
 */
public class DaosInputStream extends FSInputStream {
  private static final Logger LOG = LoggerFactory.getLogger(DaosInputStream.class);
  private final DaosFile daosFile;

  private long pos = 0;      //the current offset from input stream
  private long filePos = 0;  //the current offset from the start of the file
  private long readSize = 0;  // the size of data read from DAOS
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
      LOG.debug("DaosInputStream : seek specific position pos = " + this.pos
              + " ; targetPos = " + targetPos
              + " ; filePos= " + this.filePos);
    }
    checkNotClose();

    // validate
    if(this.filePos < this.pos){
      throw new EOFException("filePos Cannot after pos ,filePos :" + filePos +" ; pos: " + this.pos);
    }
    if (targetPos < 0) {
      throw new EOFException("Cannot seek to negative position " + targetPos);
    }
    if (this.contentLength < targetPos) {
      throw new EOFException("Cannot seek after EOF ,contentLength :" + contentLength +" ; position: " + this.pos);
    }

    // start
    if (this.filePos == targetPos) {
      return;
    }else if(this.filePos < targetPos){
      this.pos = targetPos;
      this.filePos = targetPos;
      this.buffer.clear();
      this.isRead=false;
    }else {
      long bufoffsetPos =  this.filePos - this.readSize;
      if(bufoffsetPos < 0){
        throw new EOFException("Cannot seek from buffer after EOF");
      }else if (bufoffsetPos > targetPos) {
        this.pos = targetPos;
        this.filePos = targetPos;
        this.buffer.clear();
        this.isRead=false;
      } else{
        this.filePos = targetPos;
        int newbufPos = (int)(targetPos-bufoffsetPos);
        this.buffer.position(newbufPos);
      }
    }
  }

  @Override
  public long skip(long len) throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosInputStream : skip specify length : %s",len);
    }
    if (len > 0){
      long curPos = getPos();
      long fileLen = this.contentLength;
      if(len+curPos > fileLen){
        len = fileLen - curPos;
      }
      seek(curPos+len);
      return len;
    }
    return len < 0 ? -1 : 0;
  }


  private void checkNotClose() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosInputStream checkNotClose");
    }
    if (this.closed) {
      throw new IOException("Stream is closed!");
    }
  }

  @Override
  public synchronized long getPos() throws IOException {
    checkNotClose();
    return this.pos;
  }

  // Used by unit tests.
  public long getFilePos() {
    return this.filePos;
  }

  @Override
  public boolean seekToNewSource(long targetPos) throws IOException {
    checkNotClose();
    return false;
  }

  /**
   * Reads the next byte of data from the input stream.
   */
  @Override
  public synchronized int read() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosInputStream :Reads the next byte of data from the input stream");
    }
    checkNotClose();
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
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosInputStream : read from daos , contentLength = " + this.contentLength + " ; filePos = " +this.filePos);
    }
    checkNotClose();
    long rsize= 0;

    // validation code
    if (buf == null) {
      throw new NullPointerException("Null buffer");
    } else if (off < 0 || len < 0) {
      throw new IllegalArgumentException("offset/length is nagative , offset = " + off + ", length = " + len);
    } else if(len > buf.length - off){
      throw new IndexOutOfBoundsException("requested more bytes than destination buffer size "
              +" : request length = " + len + ", with offset = " + off + ", buffer capacity =" + (buf.length - off ));
    } else if (this.filePos < 0 || this.pos < 0 ) {
      throw new EOFException("pos/filePos is negative , pos = " + this.pos + ", filePos = " + this.filePos );
    } else if (len == 0) {
      return 0;
    }

    if(this.contentLength <= 0 || streamAvailable() <= 0 || blockAvailable() < 0 || (this.pos > this.filePos)){
      if (LOG.isDebugEnabled()) {
        LOG.debug("DaosInputStream : contentLength = %s , pos = %s , filePos = %s , streamAvailable = %s , blockAvailable =%s  " , this.contentLength, this.pos, this.filePos, streamAvailable(), blockAvailable());
      }
      return -1;
    }

    // check cache buffer 
    if ((this.buffer.position() == 0) && (!this.isRead)) {
      long readS = readFormDaos();
      if (readS == 0) {
        return -1;
      }
    }

    // start
    if(streamAvailable()>= 0){
      if(remainLen()>len){
        this.buffer.get(buf, off, len);
        this.pos += len;
        rsize = len;
        if (this.stats != null) {
          this.stats.incrementBytesRead(len);
        }
      }else{
        long readSizeTmp = remainLen();
        this.buffer.get(buf, off, (int)readSizeTmp);
        this.pos += readSizeTmp;
        rsize = readSizeTmp;
        if (this.stats != null) {
          this.stats.incrementBytesRead(readSizeTmp);
        }
        this.isRead = false;
        this.buffer.clear();
        read(buf,(int)(off+readSizeTmp) ,(int)(len-readSizeTmp));
      }
    }

    return (int)(rsize == 0 ? -1 : rsize);
  }

  private long  remainLen() {
    return this.readSize - this.buffer.position();
  }

  /**
   * Read data from DAOS and put into cache buffer.
   */
  private synchronized long readFormDaos() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosInputStream : read from daos ,filePos = %s" ,this.filePos);
    }
    long currentTime = System.currentTimeMillis();
    if (this.daosFile == null) {
      throw new NullPointerException("Null daosFile");
    }
    this.readSize = this.daosFile.read(this.buffer,0,this.filePos,buffer.capacity() );
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosInputStream :reading from daos_api spend time is :  "
          + (System.currentTimeMillis()-currentTime)
          + " ; read data size : " + readSize
          + "; buffer size : "+ this.buffer.capacity()
          +".");
    }
    if (this.readSize < 0) {
      throw new IOException("read failed , rc = " + readSize);
    }
    this.filePos += this.readSize;
    this.isRead = true;
    if (this.stats != null) {
      this.stats.incrementReadOps(1);
    }
    return this.readSize;
  }

  @Override
  public synchronized void close() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosInputStream close : FileSystem.Statistics = %s" ,this.stats.toString());
    }
    if (this.closed) {
      return;
    }
    this.closed = true;
    this.daosFile.release();

    if(this.buffer!=null){
      ((sun.nio.ch.DirectBuffer)this.buffer).cleaner().clean();
      this.buffer = null;
    }
    super.close();
  }

  @Override
  public synchronized int available() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosInputStream available");
    }
    return streamAvailable();
  }

  private synchronized int streamAvailable() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosInputStream streamAvailable");
    }
    checkNotClose();
    long remaining = this.contentLength - this.pos;
    if (remaining > Integer.MAX_VALUE) {
      return Integer.MAX_VALUE;
    }
    return (int) remaining;
  }

  private synchronized int blockAvailable() throws IOException {
    if (LOG.isDebugEnabled()) {
      LOG.debug("DaosInputStream blockAvailable");
    }
    checkNotClose();
    long remaining = this.contentLength - this.filePos;
    if (remaining > Integer.MAX_VALUE) {
      return Integer.MAX_VALUE;
    }
    return (int) remaining;
  }
}
