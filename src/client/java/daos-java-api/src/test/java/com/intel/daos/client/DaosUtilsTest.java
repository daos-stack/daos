/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.intel.daos.client;

import org.junit.Assert;
import org.junit.Test;

public class DaosUtilsTest {

  @Test
  public void testReplaceForwardSlash(){
    String path = "\\abc\\de\\f\\";
    Assert.assertEquals("/abc/de/f", DaosUtils.normalize(path));

    path = "\\abc\\de\\f\\gh.i\\\\jkl\\\\";
    Assert.assertEquals("/abc/de/f/gh.i/jkl", DaosUtils.normalize(path));
  }

  @Test
  public void testReplaceMultipleSlash(){
    String path = "//abc//de/f//";
    Assert.assertEquals("/abc/de/f", DaosUtils.normalize(path));

    path = "/abc/de////f/gh.i//jkl/";
    Assert.assertEquals("/abc/de/f/gh.i/jkl", DaosUtils.normalize(path));
  }

  @Test
  public void testEmptyPath(){
    String path = null;
    Assert.assertEquals("", DaosUtils.normalize(path));
    path = "";
    Assert.assertEquals("", DaosUtils.normalize(path));
    path = " ";
    Assert.assertEquals("", DaosUtils.normalize(path));
  }

  @Test
  public void testValidCharacter(){
    String path = "/";
    Assert.assertEquals("/", DaosUtils.normalize(path));
    path = "a0Ab1B_-";
    Assert.assertEquals("a0Ab1B_-", DaosUtils.normalize(path));
    path = "/a0Ab1B_-/123456.7890/XYZ/_-/";
    Assert.assertEquals("/a0Ab1B_-/123456.7890/XYZ/_-", DaosUtils.normalize(path));
  }

  @Test(expected = IllegalArgumentException.class)
  public void testValidIllegalCharacterWhiteSpace(){
    String path = "/abc /def";
    DaosUtils.normalize(path);
  }

  @Test(expected = IllegalArgumentException.class)
  public void testValidIllegalCharacterQuestionMark(){
    String path = "abc?";
    DaosUtils.normalize(path);
  }

  @Test
  public void testSplitPath(){
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
  public void testUuidLength(){
    String id = DaosUtils.randomUUID();
    Assert.assertEquals(16, id.length());
  }
}







