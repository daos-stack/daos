package io.daos.fs.hadoop;

import java.io.IOException;
import java.nio.ByteBuffer;

public class DaosFileSourceSync extends DaosFileSource {

  private ByteBuffer buffer;

  public DaosFileSourceSync(int bufferCap) {
    buffer = ByteBuffer.allocateDirect(bufferCap);
    buffer.limit(0);
  }

  public DaosFileSourceSync(ByteBuffer buffer) {
    super();
  }

  @Override
  public long limit() {
    return 0;
  }

  @Override
  public void position(int i) {

  }

  @Override
  public void get(byte[] buf, int off, int actualLen) {
  }

  @Override
  public void clear() {

  }

  @Override
  public void close() {
    this.daosFile.release();
    ((sun.nio.ch.DirectBuffer) this.buffer).cleaner().clean();
    this.buffer = null;
  }

  @Override
  public int read(long nextReadPos, int length) {
    int actualLen = (int)this.daosFile.read(this.buffer, 0, this.nextReadPos, length);
    buffer.limit(actualLen);
    return actualLen;
  }

  @Override
  public void write(int b) {
    this.buffer.put((byte) b);
    if (!this.buffer.hasRemaining()) {
      daosWrite();
    }
  }

  @Override
  public void write(byte[] buf, int off, int len) {
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

  @Override
  public void flush() {
    if (this.buffer.position() > 0) {
      daosWrite();
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
}
