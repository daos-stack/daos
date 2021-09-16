package io.daos.dfs;

import io.daos.BufferAllocator;
import io.daos.DaosEventQueue;
import io.netty.buffer.ByteBuf;
import org.junit.*;
import org.powermock.reflect.Whitebox;

public class IODfsDescIT {

  @BeforeClass
  public static void init() throws Exception {
    DaosEventQueue.getInstance(-1);
  }

  @Test
  public void testDescCreation() throws Exception {
    ByteBuf buf = BufferAllocator.directNettyBuf(100);
    DaosEventQueue eq = DaosEventQueue.getInstance(-1);
    long eqWrapHdl = eq.getEqWrapperHdl();
    IODfsDesc desc = new IODfsDesc(buf, eq);
    Assert.assertFalse(Whitebox.getInternalState(desc, "readOrWrite"));
    desc.setReadOrWrite(true);
    Assert.assertTrue(Whitebox.getInternalState(desc, "readOrWrite"));
    ByteBuf descBuf = desc.getDescBuffer();
    Assert.assertEquals(24, descBuf.writerIndex());
    descBuf.readerIndex(8);
    Assert.assertEquals(buf.memoryAddress(), descBuf.readLong());
    Assert.assertEquals(eqWrapHdl, descBuf.readLong());
    desc.release();
    buf.release();
  }

  @Test
  public void testDescEncode() throws Exception {
    ByteBuf buf = BufferAllocator.directNettyBuf(100);
    DaosEventQueue eq = DaosEventQueue.getInstance(-1);
    DaosEventQueue.Event e = eq.acquireEvent();
    IODfsDesc desc = new IODfsDesc(buf, eq);
    desc.setEvent(e);
    desc.encode(0, 100);
    ByteBuf descBuf = desc.getDescBuffer();
    descBuf.readerIndex(24);
    Assert.assertEquals(0, descBuf.readLong());
    Assert.assertEquals(100, descBuf.readLong());
    Assert.assertEquals(e.getId(), descBuf.readShort());
    desc.release();
    buf.release();
  }

  @Test(expected = IllegalStateException.class)
  public void testDescEncodeWithoutEvent() throws Exception {
    ByteBuf buf = BufferAllocator.directNettyBuf(100);
    DaosEventQueue eq = DaosEventQueue.getInstance(-1);
    IODfsDesc desc = new IODfsDesc(buf, eq);
    try {
      desc.encode(0, 100);
    } finally {
      desc.release();
      buf.release();
    }
  }

  @AfterClass
  public static void teardown() throws Exception {
    DaosEventQueue.destroy(Thread.currentThread().getId(), DaosEventQueue.getInstance(-1));
  }
}
