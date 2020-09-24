package io.daos.dfs;

import org.junit.Assert;
import org.junit.Test;

public class DaosFilePathTest {

  @Test
  public void testTwoLevelPathFromRoot() {
    DaosFile file = new DaosFile("/root", 0, null);
    Assert.assertEquals("/", file.getParentPath());
    Assert.assertEquals("root", file.getName());
    Assert.assertEquals("/root", file.getPath());
  }

  @Test
  public void testRoot() {
    DaosFile file = new DaosFile("/", 0, null);
    Assert.assertEquals("", file.getParentPath());
    Assert.assertEquals("/", file.getName());
    Assert.assertEquals("/", file.getPath());
  }

  @Test
  public void testOneLevelWithoutRoot() {
    DaosFile file = new DaosFile("abc", 0, null);
    Assert.assertEquals("", file.getParentPath());
    Assert.assertEquals("abc", file.getName());
    Assert.assertEquals("abc", file.getPath());
  }

  @Test
  public void testMultiLevelWithoutRoot() {
    DaosFile file = new DaosFile("abc/bcd", 0, null);
    Assert.assertEquals("abc", file.getParentPath());
    Assert.assertEquals("bcd", file.getName());
    Assert.assertEquals("abc/bcd", file.getPath());

    file = new DaosFile("abc/bcd/def", 0, null);
    Assert.assertEquals("abc/bcd", file.getParentPath());
    Assert.assertEquals("def", file.getName());
    Assert.assertEquals("abc/bcd/def", file.getPath());
  }

  @Test
  public void testMultiLevelWithRoot() {
    DaosFile file = new DaosFile("/abc/bcd/", 0, null);
    Assert.assertEquals("/abc", file.getParentPath());
    Assert.assertEquals("bcd", file.getName());
    Assert.assertEquals("/abc/bcd", file.getPath());

    file = new DaosFile("/abc/bcd/def", 0, null);
    Assert.assertEquals("/abc/bcd", file.getParentPath());
    Assert.assertEquals("def", file.getName());
    Assert.assertEquals("/abc/bcd/def", file.getPath());
  }

  @Test
  public void testWithParent() {
    DaosFile file = new DaosFile("/abc", "bcd/", 0, null);
    Assert.assertEquals("/abc", file.getParentPath());
    Assert.assertEquals("bcd", file.getName());
    Assert.assertEquals("/abc/bcd", file.getPath());

    file = new DaosFile("/abc/", "/bcd/def", 0, null);
    Assert.assertEquals("/abc/bcd", file.getParentPath());
    Assert.assertEquals("def", file.getName());
    Assert.assertEquals("/abc/bcd/def", file.getPath());
  }
}
