package io.daos.fs.hadoop;

import java.nio.ByteBuffer;

public class DaosFileSourceAsync extends DaosFileSource {

  public DaosFileSourceAsync() {

  }

  public DaosFileSourceAsync(int bufferCapacity) {
    super();
  }

  public DaosFileSourceAsync(ByteBuffer buffer) {
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

  }

  @Override
  public int read(long nextReadPos, int length) {
    return 0;
  }
}
