package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import org.junit.Assert;
import org.junit.Test;

import java.nio.ByteBuffer;
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
    ByteBuffer buffer = BufferAllocator.directBuffer(10);
    buffer.position(buffer.limit());
    buffer.flip();
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
    ByteBuffer byteBuffer = ByteBuffer.allocate(10);
    try {
      entry.get(byteBuffer);
    } catch (UnsupportedOperationException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("only support for fetch"));
  }

  @Test
  public void testEncodeBeforeSetGlobalBuffer() throws Exception {
    IllegalStateException ee = null;
    IODataDesc.Entry entry = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
      10, 10);
    ByteBuffer descBuffer  = BufferAllocator.directBuffer(100);
    try {
      entry.encode(descBuffer);
    } catch (IllegalStateException e) {
      ee = e;
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
    ByteBuffer globalBuffer  = BufferAllocator.directBuffer(100);
    entry.setGlobalDataBuffer(globalBuffer);
    Assert.assertEquals(14, globalBuffer.position());
    try {
      entry.setGlobalDataBuffer(globalBuffer);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("global buffer is set already."));
    // update, need padding
    ByteBuffer valueBuffer = BufferAllocator.directBuffer(8);
    valueBuffer.position(valueBuffer.limit());
    valueBuffer.flip();
    IODataDesc.Entry entry2 = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
      10, valueBuffer);
    ByteBuffer globalBuffer2  = BufferAllocator.directBuffer(100);
    entry2.setGlobalDataBuffer(globalBuffer2);
    Assert.assertEquals(14, globalBuffer2.position());
    try {
      entry2.setGlobalDataBuffer(globalBuffer2);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("global buffer is set already."));
    // update no padding
    ByteBuffer valueBuffer2 = BufferAllocator.directBuffer(30);
    valueBuffer2.position(valueBuffer2.limit());
    valueBuffer2.flip();
    IODataDesc.Entry entry3 = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
      10, valueBuffer2);
    ByteBuffer globalBuffer3  = BufferAllocator.directBuffer(100);
    entry3.setGlobalDataBuffer(globalBuffer3);
    Assert.assertEquals(34, globalBuffer3.position());
  }

  @Test
  public void testEncode() throws Exception {
    IllegalArgumentException ee = null;
    // array value
    ByteBuffer globalBuffer  = BufferAllocator.directBuffer(14);
    ByteBuffer descBuffer  = BufferAllocator.directBuffer(30);
    IODataDesc.Entry entry = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
      10, 10);
    entry.setGlobalDataBuffer(globalBuffer);
    entry.encode(descBuffer);
    Assert.assertEquals(23, descBuffer.position());
    descBuffer.position(0);
    Assert.assertEquals(4, descBuffer.getShort());
    byte keyBytes[] = new byte[4];
    descBuffer.get(keyBytes);
    Assert.assertTrue(Arrays.equals("akey".getBytes(Constants.KEY_CHARSET), keyBytes));
    Assert.assertEquals(IODataDesc.IodType.ARRAY.getValue(), descBuffer.get());
    Assert.assertEquals(10, descBuffer.getInt());
    Assert.assertEquals(1, descBuffer.getInt());
    Assert.assertEquals(1, descBuffer.getInt());
    // single value
    globalBuffer.clear();
    descBuffer.clear();
    entry = new IODataDesc.Entry("akey", IODataDesc.IodType.SINGLE, 10,
      0, 10);
    entry.setGlobalDataBuffer(globalBuffer);
    entry.encode(descBuffer);
    Assert.assertEquals(15, descBuffer.position());
  }

  @Test
  public void testGetDataWhenFetch() throws Exception {
    // single value
    ByteBuffer globalBuffer  = BufferAllocator.directBuffer(14);
    ByteBuffer descBuffer  = BufferAllocator.directBuffer(30);
    IODataDesc.Entry entry = new IODataDesc.Entry("akey", IODataDesc.IodType.SINGLE, 10,
      0, 10);
    entry.setGlobalDataBuffer(globalBuffer);
    entry.encode(descBuffer);
    Assert.assertEquals(15, descBuffer.position());
    globalBuffer.position(4);
    byte[] bytes = "1234567890".getBytes();
    globalBuffer.put(bytes);
    entry.setActualSize(10);
    // read to byte array
    byte[] bytes1 = new byte[10];
    entry.get(bytes1);
    Assert.assertTrue(Arrays.equals(bytes, bytes1));
    // read to bytebuffer
    ByteBuffer buffer = BufferAllocator.directBuffer(10);
    entry.get(buffer);
    buffer.flip();
    for (int i = 0; i < bytes.length; i++) {
      Assert.assertEquals(bytes[i], buffer.get());
    }
    // array value
    globalBuffer  = BufferAllocator.directBuffer(34);
    descBuffer  = BufferAllocator.directBuffer(30);
    entry = new IODataDesc.Entry("akey", IODataDesc.IodType.ARRAY, 10,
      0, 30);
    entry.setGlobalDataBuffer(globalBuffer);
    entry.encode(descBuffer);
    Assert.assertEquals(23, descBuffer.position());
    descBuffer.position(0);
    Assert.assertEquals(4, descBuffer.getShort());
    byte keyBytes[] = new byte[4];
    descBuffer.get(keyBytes);
    Assert.assertTrue(Arrays.equals("akey".getBytes(Constants.KEY_CHARSET), keyBytes));
    Assert.assertEquals(IODataDesc.IodType.ARRAY.getValue(), descBuffer.get());
    Assert.assertEquals(10, descBuffer.getInt());
    Assert.assertEquals(0, descBuffer.getInt());
    Assert.assertEquals(3, descBuffer.getInt());
    globalBuffer.position(4);
    bytes = "123456789012345678901234567890".getBytes();
    globalBuffer.put(bytes);
    entry.setActualSize(30);
    // read to byte array
    bytes1 = new byte[30];
    entry.get(bytes1);
    Assert.assertTrue(Arrays.equals(bytes, bytes1));
    // read to bytebuffer
    buffer = BufferAllocator.directBuffer(30);
    entry.get(buffer);
    buffer.flip();
    for (int i = 0; i < bytes.length; i++) {
      Assert.assertEquals(bytes[i], buffer.get());
    }
  }
}
