package io.daos.obj;

import io.daos.Constants;
import io.netty.buffer.ByteBuf;
import org.junit.Assert;
import org.junit.Test;

import java.util.Arrays;
import java.util.List;

public class IOKeyDescTest {

  @Test
  public void testKeyLengthExceedLen() throws Exception {
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
    IOKeyDesc desc = null;
    try {
      desc = new IOKeyDesc(akey);
    } catch (IllegalArgumentException e) {
      ee = e;
    } finally {
      if (desc != null) {
        desc.release();
      }
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("should not exceed " + Short.MAX_VALUE));
  }

  @Test
  public void testNumberOfKeysZero() throws Exception {
    IllegalArgumentException ee = null;
    IOKeyDesc desc = null;
    try {
      desc = new IOKeyDesc("akey", 0);
    } catch (IllegalArgumentException e) {
      ee = e;
    } finally {
      if (desc != null) {
        desc.release();
      }
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("nbrOfKeys should be at least 1"));
  }

  @Test
  public void testEncodeDkeys() throws Exception {
    IOKeyDesc desc = new IOKeyDesc(null, 10, 64);
    try {
      desc.encode();
      desc.encode();
      Assert.assertEquals(4, desc.getDescBuffer().writerIndex());
      desc.getDescBuffer().readerIndex(0);
      Assert.assertEquals(0, desc.getDescBuffer().readInt());
      Assert.assertEquals(129, desc.getAnchorBuffer().capacity());
      Assert.assertEquals(124, desc.getDescBuffer().capacity());
      Assert.assertEquals(640, desc.getKeyBuffer().capacity());
    } finally {
      desc.release();
    }
  }

  @Test
  public void testEncodeAkeys() throws Exception {
    IOKeyDesc desc = new IOKeyDesc("dkey2345");
    try {
      desc.encode();
      desc.encode();
      ByteBuf descBuffer = desc.getDescBuffer();
      Assert.assertEquals(14, descBuffer.writerIndex());
      descBuffer.readerIndex(0);
      Assert.assertEquals(0, descBuffer.readInt());
      Assert.assertEquals(8, descBuffer.readShort());
      byte bytes[] = new byte[8];
      descBuffer.readBytes(bytes);
      Assert.assertTrue(Arrays.equals("dkey2345".getBytes(Constants.KEY_CHARSET), bytes));
      Assert.assertEquals(129, desc.getAnchorBuffer().capacity());
      Assert.assertEquals(1550, desc.getDescBuffer().capacity());
      Assert.assertEquals(8192, desc.getKeyBuffer().capacity());
    } finally {
      desc.release();
    }
  }

  @Test
  public void testParseResultEmpty() throws Exception {
    IOKeyDesc desc = new IOKeyDesc("dkey2345");
    try {
      desc.encode();
      ByteBuf descBuffer = desc.getDescBuffer();
      descBuffer.writerIndex(0);
      descBuffer.writeInt(0);
      List<String> keys = desc.parseResult();
      Assert.assertTrue(keys.isEmpty());
      Assert.assertTrue(desc.getResultKeys().isEmpty());
    } finally {
      desc.release();
    }
  }

  @Test
  public void testParseResultKey2Big() throws Exception {
    IOKeyDesc desc = new IOKeyDesc("dkey2345");
    try {
      desc.encode();
      ByteBuf descBuffer = desc.getDescBuffer();
      descBuffer.writerIndex(0);
      descBuffer.writeInt(1);
      descBuffer.writerIndex(descBuffer.writerIndex() + Constants.ENCODED_LENGTH_KEY + desc.getDkeyBytes().length);
      descBuffer.writeLong(120);
      desc.getAnchorBuffer().writerIndex(0);
      desc.getAnchorBuffer().writeByte(Constants.KEY_LIST_CODE_KEY2BIG);
      List<String> keys = desc.parseResult();
      Assert.assertTrue(keys.isEmpty());
      Assert.assertTrue(desc.getResultKeys().isEmpty());
      Assert.assertEquals(121, desc.getSuggestedKeyLen());
    } finally {
      desc.release();
    }
  }

  private IOKeyDesc parseResult(byte anchorStat) throws Exception {
    IOKeyDesc desc = new IOKeyDesc("dkey2345");
    desc.encode();
    ByteBuf descBuffer = desc.getDescBuffer();
    ByteBuf keyBuffer = desc.getKeyBuffer();
    descBuffer.writerIndex(0);
    descBuffer.writeInt(2);
    descBuffer.writerIndex(descBuffer.writerIndex() + Constants.ENCODED_LENGTH_KEY + desc.getDkeyBytes().length);
    desc.getAnchorBuffer().writerIndex(0);
    desc.getAnchorBuffer().writeByte(anchorStat);
    byte key1[] = "akey1".getBytes(Constants.KEY_CHARSET);
    byte key2[] = "akey2".getBytes(Constants.KEY_CHARSET);
    keyBuffer.writerIndex(0);
    descBuffer.writeLong(key1.length);
    descBuffer.writerIndex(descBuffer.writerIndex() + 4);
    descBuffer.writeLong(key2.length);
    descBuffer.writerIndex(descBuffer.writerIndex() + 4);
    keyBuffer.writeBytes(key1).writeBytes(key2);
    desc.parseResult();
    return desc;
  }

  @Test
  public void testParseResultNormal() throws Exception {
    IOKeyDesc desc = parseResult(Constants.KEY_LIST_CODE_ANCHOR_END);
    try {
      List<String> keys = desc.getResultKeys();
      Assert.assertEquals(2, keys.size());
      Assert.assertTrue(keys.contains("akey1"));
      Assert.assertTrue(keys.contains("akey2"));
    } finally {
      desc.release();
    }
  }

  @Test
  public void testContinueList() throws Exception {
    IOKeyDesc desc = parseResult(Constants.KEY_LIST_CODE_REACH_LIMIT);
    try {
      List<String> keys = desc.getResultKeys();
      Assert.assertEquals(2, keys.size());
      Assert.assertTrue(keys.contains("akey1"));
      Assert.assertTrue(keys.contains("akey2"));
      Assert.assertTrue(!desc.reachEnd());
      IllegalStateException ee = null;
      try {
        desc.encode();
      } catch (IllegalStateException e) {
        ee = e;
      }
      Assert.assertNotNull(ee);
      Assert.assertTrue(ee.getMessage().contains("result is parsed. cannot encode again"));
      // continue listing
      desc.continueList();
      Assert.assertNull(desc.getResultKeys());
      desc.encode();
      desc.getDescBuffer().readerIndex(0);
      Assert.assertEquals(0, desc.getDescBuffer().readInt());
    } finally {
      desc.release();
    }
  }
}
