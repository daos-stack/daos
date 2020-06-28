package io.daos.obj;

import io.daos.Constants;
import org.junit.Assert;
import org.junit.Test;

import java.nio.ByteBuffer;
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
    try {
      new IOKeyDesc(akey);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("should not exceed " + Short.MAX_VALUE));
  }

  @Test
  public void testNumberOfKeysZero() throws Exception {
    IllegalArgumentException ee = null;
    try {
      new IOKeyDesc("akey", 0);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("nbrOfKeys should be at least 1"));
  }

  @Test
  public void testEncodeDkeys() throws Exception {
    IOKeyDesc desc = new IOKeyDesc(null, 10, 64);
    desc.encode();
    desc.encode();
    Assert.assertEquals(4, desc.getDescBuffer().position());
    desc.getDescBuffer().position(0);
    Assert.assertEquals(0, desc.getDescBuffer().getInt());
    Assert.assertEquals(129, desc.getAnchorBuffer().capacity());
    Assert.assertEquals(164, desc.getDescBuffer().capacity());
    Assert.assertEquals(640, desc.getKeyBuffer().capacity());
  }

  @Test
  public void testEncodeAkeys() throws Exception {
    IOKeyDesc desc = new IOKeyDesc("dkey2345");
    desc.encode();
    desc.encode();
    ByteBuffer descBuffer = desc.getDescBuffer();
    Assert.assertEquals(14, descBuffer.position());
    descBuffer.position(0);
    Assert.assertEquals(0, descBuffer.getInt());
    Assert.assertEquals(8, descBuffer.getShort());
    byte bytes[] = new byte[8];
    descBuffer.get(bytes);
    Assert.assertTrue(Arrays.equals("dkey2345".getBytes(Constants.KEY_CHARSET), bytes));
    Assert.assertEquals(129, desc.getAnchorBuffer().capacity());
    Assert.assertEquals(2062, desc.getDescBuffer().capacity());
    Assert.assertEquals(8192, desc.getKeyBuffer().capacity());
  }

  @Test
  public void testParseResultEmpty() throws Exception {
    IOKeyDesc desc = new IOKeyDesc("dkey2345");
    desc.encode();
    ByteBuffer descBuffer = desc.getDescBuffer();
    descBuffer.position(0);
    descBuffer.putInt(0);
    List<String> keys = desc.parseResult();
    Assert.assertTrue(keys.isEmpty());
    Assert.assertTrue(desc.getResultKeys().isEmpty());
  }

  @Test
  public void testParseResultKey2Big() throws Exception {
    IOKeyDesc desc = new IOKeyDesc("dkey2345");
    desc.encode();
    ByteBuffer descBuffer = desc.getDescBuffer();
    descBuffer.position(0);
    descBuffer.putInt(1);
    descBuffer.position(descBuffer.position() + Constants.ENCODED_LENGTH_KEY + desc.getDkeyBytes().length);
    descBuffer.putLong(120);
    desc.getAnchorBuffer().position(0);
    desc.getAnchorBuffer().put(Constants.KEY_LIST_CODE_KEY2BIG);
    List<String> keys = desc.parseResult();
    Assert.assertTrue(keys.isEmpty());
    Assert.assertTrue(desc.getResultKeys().isEmpty());
    Assert.assertEquals(121, desc.getSuggestedKeyLen());
  }

  private IOKeyDesc parseResult(byte anchorStat) throws Exception {
    IOKeyDesc desc = new IOKeyDesc("dkey2345");
    desc.encode();
    ByteBuffer descBuffer = desc.getDescBuffer();
    ByteBuffer keyBuffer = desc.getKeyBuffer();
    descBuffer.position(0);
    descBuffer.putInt(2);
    descBuffer.position(descBuffer.position() + Constants.ENCODED_LENGTH_KEY + desc.getDkeyBytes().length);
    desc.getAnchorBuffer().position(0);
    desc.getAnchorBuffer().put(anchorStat);
    byte key1[] = "akey1".getBytes(Constants.KEY_CHARSET);
    byte key2[] = "akey2".getBytes(Constants.KEY_CHARSET);
    keyBuffer.position(0);
    descBuffer.putLong(key1.length);
    descBuffer.position(descBuffer.position() + 6);
    descBuffer.putShort((short)0);
    descBuffer.putLong(key2.length);
    descBuffer.position(descBuffer.position() + 6);
    descBuffer.putShort((short)0);
    keyBuffer.put(key1).put(key2);
    desc.parseResult();
    return desc;
  }

  @Test
  public void testParseResultNormal() throws Exception {
    IOKeyDesc desc = parseResult(Constants.KEY_LIST_CODE_ANCHOR_END);
    List<String> keys = desc.getResultKeys();
    Assert.assertEquals(2, keys.size());
    Assert.assertTrue(keys.contains("akey1"));
    Assert.assertTrue(keys.contains("akey2"));
  }

  @Test
  public void testContinueList() throws Exception {
    IOKeyDesc desc = parseResult(Constants.KEY_LIST_CODE_REACH_LIMIT);
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
    desc.getDescBuffer().position(0);
    Assert.assertEquals(0, desc.getDescBuffer().getInt());
  }
}
