package io.daos.dfs;

import io.daos.DaosEventQueue;
import io.netty.buffer.ByteBuf;

public class IODfsDesc implements DaosEventQueue.Attachment {
  @Override
  public void reuse() {

  }

  @Override
  public void ready() {

  }

  @Override
  public boolean alwaysBoundToEvt() {
    return false;
  }

  @Override
  public void release() {

  }

  public void encode() {
  }

  public ByteBuf getDescBuffer() {
  }
}
