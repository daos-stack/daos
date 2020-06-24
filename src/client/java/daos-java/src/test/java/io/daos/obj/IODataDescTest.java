package io.daos.obj;

import org.junit.Assert;
import org.junit.Test;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public class IODataDescTest {

  @Test
  public void testKeyLength() throws Exception {
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
  public void testAkeyLength() throws Exception {
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
}
