package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.netty.buffer.ByteBuf;
import org.junit.Assert;
import org.junit.Test;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

public class IODataDescTest {

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
      new IODataDesc(dkey, Collections.EMPTY_LIST, true);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("should not exceed " + Short.MAX_VALUE));
  }

  @Test
  public void testInconsistentAction() throws Exception {
    IllegalArgumentException ee = null;
    List<IODataDesc.Entry> list = new ArrayList<>();
    IODataDesc.Entry entry = new IODataDesc.Entry("akey1", IODataDesc.IodType.ARRAY, 10, 0,
      10);
    list.add(entry);
    try {
      new IODataDesc("dkey1", list, true);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("should be update"));
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
    try {
      new IODataDesc.Entry(akey, IODataDesc.IodType.ARRAY, 10, 0, 10);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("should not exceed " + Short.MAX_VALUE));
  }

  @Test
  public void testOffsetNotMultipleOfRecordSize() throws Exception {
    IllegalArgumentException ee = null;
    try {
      new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10, 9, 10);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("should be a multiple of recordSize"));
  }

  @Test
  public void testNonPositiveDataSize() throws Exception {
    IllegalArgumentException ee = null;
    try {
      new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10, 10, 0);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("need positive data size"));
    ee = null;
    try {
      new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10, 10, -1);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("need positive data size"));
  }

  @Test
  public void testSingleValueNonZeroOffset() throws Exception {
    IllegalArgumentException ee = null;
    try {
      new IODataDesc.Entry("akey", IODataDesc.IodType.SINGLE, 10, 10, 10);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("offset should be zero for"));
  }

  @Test
  public void testSingleValueDataSizeBiggerThanRecSize() throws Exception {
    IllegalArgumentException ee = null;
    try {
      new IODataDesc.Entry("akey", IODataDesc.IodType.SINGLE, 10, 0, 70);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("data size should be no more than record size for"));
  }

  @Test
  public void testInvalidIodType() throws Exception {
    IllegalArgumentException ee = null;
    try {
      new IODataDesc.Entry("akey", IODataDesc.IodType.NONE, 10, 10, 10);
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
    try {
      buffer.writerIndex(buffer.capacity());
      IODataDesc.Entry entry = new IODataDesc.Entry("akey", IODataDesc.IodType.SINGLE, 10,
          0, buffer);
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
        entry.getActualRecSize();
      } catch (UnsupportedOperationException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("only support for fetch"));
      ee = null;
      try {
        entry.setActualRecSize(10);
      } catch (UnsupportedOperationException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("only support for fetch"));
      ee = null;
      byte[] bytes = new byte[10];
      try {
        entry.get(bytes);
      } catch (UnsupportedOperationException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("only support for fetch"));
      ee = null;
      ByteBuf byteBuffer = BufferAllocator.objBufWithNativeOrder(10);
      try {
        entry.get(byteBuffer);
      } catch (UnsupportedOperationException e) {
        ee = e;
      } finally {
        byteBuffer.release();
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("only support for fetch"));
    } finally {
      buffer.release();
    }
  }

  @Test
  public void testEncodeBeforeSetGlobalBuffer() throws Exception {
    IllegalStateException ee = null;
    IODataDesc.Entry entry = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
      10, 10);
    ByteBuf descBuffer  = BufferAllocator.objBufWithNativeOrder(100);
    try {
      entry.encode(descBuffer);
    } catch (IllegalStateException e) {
      ee = e;
    } finally {
      descBuffer.release();
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("value buffer index is not set"));
  }

  @Test
  public void testSetGlobalBuffer() throws Exception {
    IllegalArgumentException ee = null;
    // fetch
    IODataDesc.Entry entry = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
      10, 10);
    ByteBuf globalBuffer  = BufferAllocator.objBufWithNativeOrder(100);
    try {
      entry.setGlobalDataBuffer(globalBuffer);
      Assert.assertEquals(14, globalBuffer.writerIndex());
      try {
        entry.setGlobalDataBuffer(globalBuffer);
      } catch (IllegalArgumentException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("global buffer is set already."));
    } finally {
      globalBuffer.release();
    }
    // update, need padding
    ByteBuf valueBuffer = BufferAllocator.objBufWithNativeOrder(8);
    valueBuffer.writerIndex(valueBuffer.capacity());
    IODataDesc.Entry entry2 = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
      10, valueBuffer);
    ByteBuf globalBuffer2  = BufferAllocator.objBufWithNativeOrder(100);
    try {
      entry2.setGlobalDataBuffer(globalBuffer2);
      Assert.assertEquals(14, globalBuffer2.writerIndex());
      try {
        entry2.setGlobalDataBuffer(globalBuffer2);
      } catch (IllegalArgumentException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("global buffer is set already."));
    } finally {
      valueBuffer.release();
      globalBuffer2.release();
    }
    // update no padding
    ByteBuf valueBuffer2 = BufferAllocator.objBufWithNativeOrder(30);
    valueBuffer2.writerIndex(valueBuffer2.capacity());
    IODataDesc.Entry entry3 = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
      10, valueBuffer2);
    ByteBuf globalBuffer3  = BufferAllocator.objBufWithNativeOrder(100);
    try {
      entry3.setGlobalDataBuffer(globalBuffer3);
      Assert.assertEquals(34, globalBuffer3.writerIndex());
    } finally {
      valueBuffer2.release();
      globalBuffer3.release();
    }
  }

  @Test
  public void testEncode() throws Exception {
    IllegalArgumentException ee = null;
    // array value
    ByteBuf globalBuffer  = BufferAllocator.objBufWithNativeOrder(14);
    ByteBuf descBuffer  = BufferAllocator.objBufWithNativeOrder(30);
    try {
      IODataDesc.Entry entry = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
          10, 10);
      entry.setGlobalDataBuffer(globalBuffer);
      entry.encode(descBuffer);
      Assert.assertEquals(23, descBuffer.writerIndex());
      descBuffer.readerIndex(0);
      Assert.assertEquals(4, descBuffer.readShort());
      byte keyBytes[] = new byte[4];
      descBuffer.readBytes(keyBytes);
      Assert.assertTrue(Arrays.equals("akey".getBytes(Constants.KEY_CHARSET), keyBytes));
      Assert.assertEquals(IODataDesc.IodType.ARRAY.getValue(), descBuffer.readByte());
      Assert.assertEquals(10, descBuffer.readInt());
      Assert.assertEquals(1, descBuffer.readInt());
      Assert.assertEquals(1, descBuffer.readInt());
      // single value
      globalBuffer.clear();
      descBuffer.clear();
      entry = new IODataDesc.Entry("akey", IODataDesc.IodType.SINGLE, 10,
          0, 10);
      entry.setGlobalDataBuffer(globalBuffer);
      entry.encode(descBuffer);
      Assert.assertEquals(15, descBuffer.writerIndex());
    } finally {
      globalBuffer.release();
      descBuffer.release();
    }
  }

  @Test
  public void testGetDataWhenFetch() throws Exception {
    // single value
    ByteBuf globalBuffer  = BufferAllocator.objBufWithNativeOrder(14);
    ByteBuf descBuffer  = BufferAllocator.objBufWithNativeOrder(30);
    try {
      IODataDesc.Entry entry = new IODataDesc.Entry("akey", IODataDesc.IodType.SINGLE, 10,
          0, 10);
      entry.setGlobalDataBuffer(globalBuffer);
      entry.encode(descBuffer);
      Assert.assertEquals(15, descBuffer.writerIndex());
      globalBuffer.writerIndex(4);
      byte[] bytes = "1234567890".getBytes();
      globalBuffer.writeBytes(bytes);
      entry.setActualSize(10);
      // read to byte array
      byte[] bytes1 = new byte[10];
      entry.get(bytes1);
      Assert.assertTrue(Arrays.equals(bytes, bytes1));
      // read to bytebuffer
      ByteBuf buffer = BufferAllocator.objBufWithNativeOrder(10);
      try {
        entry.get(buffer);
        for (int i = 0; i < bytes.length; i++) {
          Assert.assertEquals(bytes[i], buffer.readByte());
        }
      } finally {
        buffer.release();
      }
    } finally {
      globalBuffer.release();
      descBuffer.release();
    }
    // array value
    globalBuffer  = BufferAllocator.objBufWithNativeOrder(34);
    descBuffer  = BufferAllocator.objBufWithNativeOrder(30);
    try {
      IODataDesc.Entry entry = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
          0, 30);
      entry.setGlobalDataBuffer(globalBuffer);
      entry.encode(descBuffer);
      Assert.assertEquals(23, descBuffer.writerIndex());
      descBuffer.readerIndex(0);
      Assert.assertEquals(4, descBuffer.readShort());
      byte keyBytes[] = new byte[4];
      descBuffer.readBytes(keyBytes);
      Assert.assertTrue(Arrays.equals("akey".getBytes(Constants.KEY_CHARSET), keyBytes));
      Assert.assertEquals(IODataDesc.IodType.ARRAY.getValue(), descBuffer.readByte());
      Assert.assertEquals(10, descBuffer.readInt());
      Assert.assertEquals(0, descBuffer.readInt());
      Assert.assertEquals(3, descBuffer.readInt());
      globalBuffer.writerIndex(4);
      byte[] bytes = "123456789012345678901234567890".getBytes();
      globalBuffer.writeBytes(bytes);
      entry.setActualSize(30);
      // read to byte array
      byte[] bytes1 = new byte[30];
      entry.get(bytes1);
      Assert.assertTrue(Arrays.equals(bytes, bytes1));
      // read to bytebuffer
      ByteBuf buffer = BufferAllocator.objBufWithNativeOrder(30);
      try {
        entry.get(buffer);
        for (int i = 0; i < bytes.length; i++) {
          Assert.assertEquals(bytes[i], buffer.readByte());
        }
      } finally {
        buffer.release();
      }
    } finally {
      globalBuffer.release();
      descBuffer.release();
    }
  }
}
