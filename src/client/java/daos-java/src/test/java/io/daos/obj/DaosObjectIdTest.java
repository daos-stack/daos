package io.daos.obj;

import io.daos.BufferAllocator;
import io.daos.Constants;
import io.netty.buffer.ByteBuf;
import org.junit.Assert;
import org.junit.Test;

public class DaosObjectIdTest {

  @Test
  public void testKeyEncoding() throws Exception {
    String s = "abc" + '\u90ed' + '\u5fb7' + '\u7eb2' + "&";
    byte[] utfBytes = s.getBytes(Constants.KEY_CHARSET);
    String decoded = new String(utfBytes, Constants.KEY_CHARSET);
    Assert.assertEquals(s, decoded);
  }

  @Test
  public void TestEncodeInteger() throws Exception {
    ByteBuf buffer = BufferAllocator.objBufWithNativeOrder(1);
    int value = 100;
    buffer.writeByte(value);
    byte b = buffer.readByte();
    Assert.assertEquals((int)b, value);
  }
}
