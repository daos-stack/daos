package io.daos;

import io.netty.buffer.ByteBuf;
import org.apache.commons.lang.StringEscapeUtils;
import org.junit.Assert;
import org.junit.Test;

public class DaosUtilsTest {

  @Test
  public void normalizeLongPath() throws Exception {
    String path = "job_1581472776049_0003-1581473346405-" +
        "root-autogen%2D7.1%2DSNAPSHOT%2Djar%2Dwith%2Ddependencies.jar-" +
        "1581473454525-16-1-SUCCEEDED-default-1581473439146.jhist_tmp";
//    String path = "job%";
    DaosUtils.normalize(path);

    path = "job_1581472776049_0003-1581473346405-root-autogen%2D7.1%2DSNAPSHOT" +
        "%2Djar%2Dwith%2Ddependencies.jar-1581473454525-16-1-SUCCEEDED-" +
        "default-1581473439146.jhist_tmpjob_1581472776049_0003-1581473346405-" +
        "root-autogen%2D7.1%2DSNAPSHOT%2Djar%2Dwith%2Ddependen412345";
    DaosUtils.normalize(path);

    path = "job_1581472776049_0003-1581473346405-root-autogen%2D7.1%2DSNAPSHOT" +
        "%2Djar%2Dwith%2Ddependencies.jar-1581473454525-16-1-SUCCEEDED-" +
        "default-1581473439146.jhist_tmpjob_1581472776049_0003-1581473346405-" +
        "root-autogen%2D7.1%2DSNAPSHOT%2Djar%2Dwith%2Ddependen4123456";
    try {
      DaosUtils.normalize(path);
    } catch (IllegalArgumentException e) {
      return;
    }
    throw new Exception("normalize should fail since length exceeds 255");
  }

  @Test
  public void testReplaceForwardSlash() {
    String path = "\\abc\\de\\f\\";
    Assert.assertEquals("/abc/de/f", DaosUtils.normalize(path));

    path = "\\abc\\de\\f\\gh.i\\\\jkl\\\\";
    Assert.assertEquals("/abc/de/f/gh.i/jkl", DaosUtils.normalize(path));
  }

  @Test
  public void testReplaceMultipleSlash() {
    String path = "//abc//de/f//";
    Assert.assertEquals("/abc/de/f", DaosUtils.normalize(path));

    path = "/abc/de////f/gh.i//jkl/";
    Assert.assertEquals("/abc/de/f/gh.i/jkl", DaosUtils.normalize(path));
  }

  @Test
  public void testEmptyPath() {
    String path = null;
    Assert.assertEquals("", DaosUtils.normalize(path));
    path = "";
    Assert.assertEquals("", DaosUtils.normalize(path));
    path = " ";
    Assert.assertEquals("", DaosUtils.normalize(path));
  }

  @Test
  public void testValidCharacter() {
    String path = "/";
    Assert.assertEquals("/", DaosUtils.normalize(path));
    path = "a0Ab1B_-";
    Assert.assertEquals("a0Ab1B_-", DaosUtils.normalize(path));
    path = "/a0Ab1B_-/123456.7890/XYZ/_-/";
    Assert.assertEquals("/a0Ab1B_-/123456.7890/XYZ/_-", DaosUtils.normalize(path));
  }

  @Test
  public void testValidIllegalCharacterWhiteSpace() {
    String path = "/abc /def";
    DaosUtils.normalize(path);
  }

  @Test
  public void testValidIllegalCharacterQuestionMark() {
    String path = "abc?";
    DaosUtils.normalize(path);
  }

  @Test
  public void testSplitPath() {
    String path = "/";
    String[] pc = DaosUtils.parsePath(DaosUtils.normalize(path));
    Assert.assertEquals(1, pc.length);
    Assert.assertEquals("/", pc[0]);

    path = "abc";
    pc = DaosUtils.parsePath(DaosUtils.normalize(path));
    Assert.assertEquals(1, pc.length);
    Assert.assertEquals("abc", pc[0]);

    path = "abc/";
    pc = DaosUtils.parsePath(DaosUtils.normalize(path));
    Assert.assertEquals(1, pc.length);
    Assert.assertEquals("abc", pc[0]);

    path = "abc/XYZ";
    pc = DaosUtils.parsePath(DaosUtils.normalize(path));
    Assert.assertEquals(2, pc.length);
    Assert.assertEquals("abc", pc[0]);
    Assert.assertEquals("XYZ", pc[1]);

    path = "/abc/XYZ/5.TU-/ABC_";
    pc = DaosUtils.parsePath(DaosUtils.normalize(path));
    Assert.assertEquals(2, pc.length);
    Assert.assertEquals("/abc/XYZ/5.TU-", pc[0]);
    Assert.assertEquals("ABC_", pc[1]);
  }

  @Test
  public void testValue() throws Exception {
    Assert.assertTrue(02 == 2);
  }

  @Test
  public void testStringEncode() throws Exception {
    ByteBuf buf = BufferAllocator.objBufWithNativeOrder(100);
    long start = System.nanoTime();
    for (int i = 0; i < 125; i++) {
      for (int j = 0; j < 1000; j++) {
        buf.writeBytes((i + "000").getBytes("UTF-8"));
        buf.writeBytes((j + "000").getBytes("UTF-8"));
        buf.clear();
      }
    }
    System.out.println((System.nanoTime() - start)/1000000);
  }

  @Test
  public void testStringEncode2() throws Exception {
    ByteBuf buf = BufferAllocator.objBufWithNativeOrder(100);
    long start = System.nanoTime();
    for (int i = 0; i < 125; i++) {
      for (int j = 0; j < 1000; j++) {
        buf.clear();
        writeToBuffer(i + "000", buf);
        writeToBuffer(j + "000", buf);
      }
    }
    System.out.println((System.nanoTime() - start)/1000000);
    System.out.println(buf.writerIndex());
  }

  private void writeToBuffer(String s, ByteBuf buf) {
    for (int i = 0; i < s.length(); i++) {
      buf.writeShort(s.charAt(i));
    }
  }

  @Test
  public void testString() throws Exception {
    ByteBuf buf = BufferAllocator.objBufWithNativeOrder(100);
    String s = "0实时热点1";
    for (int i = 0; i < s.length(); i++) {
      buf.writeShort(s.charAt(i));
      System.out.println((int)(s.charAt(i)));
    }
    StringBuilder sb = new StringBuilder();
    while (buf.readableBytes() > 0) {
      char c = buf.readChar();
      sb.append(c);
      System.out.println((int)c);
    }
    System.out.println(sb.toString());
  }

  @Test
  public void testUuidLength() {
    String id = DaosUtils.randomUUID();
    Assert.assertEquals(16, id.length());
  }

  @Test
  public void testEscapeUnsValue() throws Exception {
    String value = "ab:c=:def=";
    String evalue = DaosUtils.escapeUnsValue(value);
    Assert.assertEquals("ab\\u003ac\\u003d\\u003adef\\u003d", evalue);
    Assert.assertEquals(value, StringEscapeUtils.unescapeJava(evalue));
  }

  @Test
  public void testBufferLeak() throws Exception {
    ByteBuf buff = BufferAllocator.directNettyBuf(10);
    ByteBuf buf = buff.order(Constants.DEFAULT_ORDER);
    if (buff == buf) {
      System.out.println("same");
    }
    Assert.assertEquals(1, buf.refCnt());
    buf.release();
    Assert.assertEquals(0, buf.refCnt());

    Assert.assertEquals(0, buff.refCnt());

  }
}
