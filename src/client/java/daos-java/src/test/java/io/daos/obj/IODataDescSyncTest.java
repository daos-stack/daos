package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.daos.DaosUtils;
import io.netty.buffer.ByteBuf;
import org.junit.Assert;
import org.junit.Test;
import org.powermock.reflect.Whitebox;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public class IODataDescSyncTest {

  @Test
  public void testKeyLengthZero() throws Exception {
    String dkey;
    int len = Short.MAX_VALUE + 1;
    StringBuilder sb = new StringBuilder();
    for (int i = 0; i < len; i++) {
      sb.append(i);
      if (sb.length() > len) {
        break;
      }
    }
    dkey = sb.toString();
    IllegalArgumentException ee = null;
    try {
      new IODataDescSync(dkey, IODataDescSync.IodType.ARRAY, 1, true);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("should not exceed " + Short.MAX_VALUE));
  }

  @Test
  public void testInconsistentAction() throws Exception {
    IllegalArgumentException ee = null;
    IODataDescSync desc = null;
    try {
      desc = new IODataDescSync("dkey1", IODataDescSync.IodType.ARRAY, 1, true);
      desc.addEntryForFetch("akey1", 0, 10);
    } catch (IllegalArgumentException e) {
      ee = e;
    } finally {
      desc.release();
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("It's desc for update"));
  }

  @Test
  public void testAkeyLengthExceedLimit() throws Exception {
    String akey;
    int len = Short.MAX_VALUE + 1;
    StringBuilder sb = new StringBuilder();
    for (int i = 0; i < len; i++) {
      sb.append(i);
      if (sb.length() > len) {
        break;
      }
    }
    akey = sb.toString();
    IllegalArgumentException ee = null;
    ByteBuf buffer = BufferAllocator.objBufWithNativeOrder(10);
    buffer.writeByte(1);
    IODataDescSync desc = null;
    try {
      desc = new IODataDescSync("dkey1", IODataDescSync.IodType.ARRAY, 1, true);
      desc.addEntryForUpdate(akey, 0, buffer);
    } catch (IllegalArgumentException e) {
      ee = e;
    } finally {
      desc.release();
      buffer.release();
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("should not exceed " + Short.MAX_VALUE));
  }

  @Test
  public void testOffsetNotMultipleOfRecordSize() throws Exception {
    IllegalArgumentException ee = null;
    IODataDescSync desc = null;
    try {
      desc = new IODataDescSync("dkey1", IODataDescSync.IodType.ARRAY, 10, false);
      desc.addEntryForFetch("akey", 9, 10);
    } catch (IllegalArgumentException e) {
      ee = e;
    } finally {
      desc.release();
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("should be a multiple of recordSize"));
  }

  @Test
  public void testNonPositiveDataSize() throws Exception {
    IllegalArgumentException ee = null;
    IODataDescSync desc = null;
    try {
      desc = new IODataDescSync("dkey1", IODataDescSync.IodType.ARRAY, 10, false);
      desc.addEntryForFetch("akey", 10, 0);
    } catch (IllegalArgumentException e) {
      ee = e;
    } finally {
      desc.release();
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("data size should be positive"));
    ee = null;
    try {
      desc = new IODataDescSync("dkey1", IODataDescSync.IodType.ARRAY, 10, false);
      desc.addEntryForFetch("akey", 10, -1);
    } catch (IllegalArgumentException e) {
      ee = e;
    } finally {
      desc.release();
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("data size should be positive"));
  }

  @Test
  public void testSingleValueNonZeroOffset() throws Exception {
    IllegalArgumentException ee = null;
    IODataDescSync desc = null;
    try {
      desc = new IODataDescSync("dkey1", IODataDescSync.IodType.SINGLE, 10, false);
      desc.addEntryForFetch("akey", 10, 10);
    } catch (IllegalArgumentException e) {
      ee = e;
    } finally {
      desc.release();
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("offset should be zero for"));
  }

  @Test
  public void testSingleValueDataSizeBiggerThanRecSize() throws Exception {
    IllegalArgumentException ee = null;
    IODataDescSync desc = null;
    try {
      desc = new IODataDescSync("dkey1", IODataDescSync.IodType.SINGLE, 10, false);
      desc.addEntryForFetch("akey", 0, 70);
    } catch (IllegalArgumentException e) {
      ee = e;
    } finally {
      desc.release();
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("data size should be no more than record size for"));
  }

  @Test
  public void testInvalidIodType() throws Exception {
    IllegalArgumentException ee = null;
    try {
      new IODataDescSync("dkey1", IODataDescSync.IodType.NONE, 10, false);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("need valid IodType, either"));
  }

  @Test
  public void testCallFetchMethodsWhenUpdate() throws Exception {
    UnsupportedOperationException ee = null;
    ByteBuf buffer = BufferAllocator.objBufWithNativeOrder(10);
    IODataDescSync desc = null;
    try {
      buffer.writerIndex(buffer.capacity());
      desc = new IODataDescSync("dkey1", IODataDescSync.IodType.SINGLE, 10, true);
      IODataDescSync.Entry entry = desc.addEntryForUpdate("akey", 0, buffer);
      try {
        entry.getActualSize();
      } catch (UnsupportedOperationException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("only support for fetch"));
      ee = null;
      try {
        entry.setActualSize(10);
      } catch (UnsupportedOperationException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("only support for fetch"));
      ee = null;
      try {
        ((IODataDescSync.SyncEntry)entry).getActualRecSize();
      } catch (UnsupportedOperationException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("only support for fetch"));
      ee = null;
      try {
        ((IODataDescSync.SyncEntry)entry).setActualRecSize(10);
      } catch (UnsupportedOperationException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("only support for fetch"));
      ee = null;
      try {
        entry.getFetchedData();
      } catch (UnsupportedOperationException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("only support for fetch"));
    } finally {
      desc.release();
    }
  }

  @Test
  public void testEncodeReusableDesc() throws Exception {
    // array value
//    ByteBuf buffer  = BufferAllocator.objBufWithNativeOrder(30);
//    buffer.writerIndex(30);
    IODataDescSync desc = null;
    try {
      desc = new IODataDescSync(10, 2, 100, IODataDescSync.IodType.ARRAY,
        10, true);
      checkEncoded(desc, IODataDescSync.IodType.ARRAY, true);
      // single value
      desc = new IODataDescSync(10, 2, 100, IODataDescSync.IodType.SINGLE,
        10, false);
      checkEncoded(desc, IODataDescSync.IodType.SINGLE, false);
    } finally {
//      buffer.release();
      desc.release();
    }
  }

  private void checkEncoded(IODataDescSync desc, IODataDescSync.IodType type, boolean update) throws Exception {
    desc.setDkey("dkey");
    for (int i = 0; i < 2; i++) {
      IODataDesc.Entry e = desc.getEntry(i);
      if (update) {
        e.getDataBuffer().writerIndex(30);
        ((IODataDescSync.SyncEntry)e).setKey("key" + i, 0, e.getDataBuffer());
      } else {
        ((IODataDescSync.SyncEntry)e).setKey("key" + i, 0, 10);
      }
    }
    desc.encode();
    ByteBuf descBuffer = desc.getDescBuffer();
    if (update) {
      Assert.assertEquals(93, descBuffer.writerIndex());
    } else {
      Assert.assertEquals(69, descBuffer.writerIndex());
      Assert.assertEquals(85, descBuffer.capacity());
    }
    // address
    descBuffer.readerIndex(0);
    Assert.assertEquals(0L, descBuffer.readLong());
    // max key len
    Assert.assertEquals(10, descBuffer.readShort());
    // nbr of akeys with data
    Assert.assertEquals(2, descBuffer.readShort());
    // type
    Assert.assertEquals(type.getValue(), descBuffer.readByte());
    // record size
    Assert.assertEquals(10, descBuffer.readInt());
    // dkey
    Assert.assertEquals(8, descBuffer.readShort());
    byte keyBytes[] = new byte[8];
    descBuffer.readBytes(keyBytes);
    Assert.assertTrue(Arrays.equals(DaosUtils.keyToBytes8("dkey"), keyBytes));
    // max key len - key length = 10 - 8
    descBuffer.readerIndex(descBuffer.readerIndex() + 10 - 8);
    // entries
    keyBytes = new byte[4];
    for (int i = 0; i < 2; i++) {
      Assert.assertEquals(4, descBuffer.readShort());
      descBuffer.readBytes(keyBytes);
      Assert.assertTrue(Arrays.equals(("key" + i).getBytes(StandardCharsets.UTF_8), keyBytes));
      descBuffer.readerIndex(descBuffer.readerIndex() + 10 - 4);
      if (type == IODataDescSync.IodType.ARRAY) {
        Assert.assertEquals(0, descBuffer.readLong());
        Assert.assertEquals(3, descBuffer.readInt());
      }
      descBuffer.readerIndex(descBuffer.readerIndex() + 8);
    }
    desc.release();
  }

  @Test
  public void testGetDataWhenFetch() throws Exception {
    // single value
    IODataDescSync desc = null;
    try {
      desc = new IODataDescSync("dkey", IODataDescSync.IodType.SINGLE, 10, false);
      IODataDescSync.Entry entry = desc.addEntryForFetch("akey", 0, 10);
      desc.encode();
      Assert.assertEquals(37, desc.getDescBuffer().writerIndex());
      ByteBuf descBuf = desc.getDescBuffer();
      descBuf.writeInt(8);
      descBuf.writeInt(8);
      desc.parseFetchResult();
      // read to byte array
      ByteBuf buf = entry.getFetchedData();
      Assert.assertEquals(0, buf.readerIndex());
      Assert.assertEquals(8, buf.writerIndex());
    } finally {
      if (desc != null) {
        desc.release();
      }
    }
    // array value
    try {
      desc = new IODataDescSync("dkey", IODataDescSync.IodType.ARRAY, 10, false);
      IODataDescSync.Entry entry = desc.addEntryForFetch("akey", 0, 30);
      desc.encode();
      ByteBuf descBuf = desc.getDescBuffer();
      Assert.assertEquals(49, descBuf.writerIndex());
      // not reusable
      descBuf.readerIndex(0);
      Assert.assertEquals(-1L, descBuf.readLong());
      // check iod type and record size
      Assert.assertEquals(IODataDescSync.IodType.ARRAY.getValue(), descBuf.readByte());
      Assert.assertEquals(10, descBuf.readInt());
      // check dkey and akey
      Assert.assertEquals(8, descBuf.readShort());
      byte keyBytes[] = new byte[8];
      descBuf.readBytes(keyBytes);
      Assert.assertTrue(Arrays.equals(DaosUtils.keyToBytes8("dkey"), keyBytes));
      keyBytes = new byte[4];
      Assert.assertEquals(4, descBuf.readShort());
      descBuf.readBytes(keyBytes);
      Assert.assertTrue(Arrays.equals("akey".getBytes(StandardCharsets.UTF_8), keyBytes));
      Assert.assertEquals(0, descBuf.readLong());
      Assert.assertEquals(3, descBuf.readInt());
      // parse
      descBuf.writeInt(30);
      descBuf.writeInt(10);
      desc.parseFetchResult();
      ByteBuf buf = entry.getFetchedData();
      Assert.assertEquals(0, buf.readerIndex());
      Assert.assertEquals(30, buf.writerIndex());
    } finally {
      if (desc != null) {
        desc.release();
      }
    }
  }

  @Test
  public void testDescDuplicate() throws Exception {
    IODataDescSync desc = new IODataDescSync("dkey", IODataDescSync.IodType.ARRAY, 10, false);
    IODataDescSync.Entry entry1 = desc.addEntryForFetch("akey", 0, 30);
    IODataDescSync.Entry entry2 = desc.addEntryForFetch("akey", 30, 30);
    IODataDescSync desc2 = desc.duplicate();
    IODataDescSync.Entry entry21 = desc2.getAkeyEntries().get(0);
    IODataDescSync.Entry entry22 = desc2.getAkeyEntries().get(1);
    Assert.assertEquals(desc.getNbrOfEntries(), desc2.getNbrOfEntries());
    assertEntries(entry1, entry21);
    assertEntries(entry2, entry22);
    Assert.assertEquals((Boolean)Whitebox.getInternalState(desc, "updateOrFetch"),
        (Boolean)Whitebox.getInternalState(desc2, "updateOrFetch"));
  }

  private void assertEntries(IODataDescSync.Entry entry1, IODataDescSync.Entry entry2) {
    Assert.assertEquals(entry1.getKey(), entry2.getKey());
    Assert.assertEquals(entry1.getOffset(), entry2.getOffset());
    Assert.assertEquals(entry1.getRequestSize(), entry2.getRequestSize());
  }

  @Test(expected = UnsupportedOperationException.class)
  public void testDescDuplicateUnsupported() throws Exception {
    IODataDescSync desc = new IODataDescSync(IODataDescSync.IodType.ARRAY, 1, true);
    desc.duplicate();
  }
}
