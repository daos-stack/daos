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

public class DaosFilePathTest {

  @Test
  public void testTwoLevelPathFromRoot() {
    DaosFile file = new DaosFile("/root", 0, null);
    Assert.assertEquals("/", file.getParentPath());
    Assert.assertEquals("root", file.getName());
    Assert.assertEquals("/root", file.getPath());
  }

  @Test
  public void testRoot(){
    DaosFile file = new DaosFile("/", 0, null);
    Assert.assertEquals("", file.getParentPath());
    Assert.assertEquals("/", file.getName());
    Assert.assertEquals("/", file.getPath());
  }

  @Test
  public void testOneLevelWithoutRoot(){
    DaosFile file = new DaosFile("abc", 0, null);
    Assert.assertEquals("", file.getParentPath());
    Assert.assertEquals("abc", file.getName());
    Assert.assertEquals("abc", file.getPath());
  }

  @Test
  public void testMultiLevelWithoutRoot(){
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
  public void testMultiLevelWithRoot(){
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
  public void testWithParent(){
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
