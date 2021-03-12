package io.daos;

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
}
