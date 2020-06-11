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
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.IOException;

public class DaosFsClientIT {

  private static String poolId;
  private static String contId;

  @BeforeClass
  public static void setup()throws Exception{
    poolId = System.getProperty("pool_id", DaosFsClientTestBase.DEFAULT_POOL_ID);
    contId = System.getProperty("cont_id", DaosFsClientTestBase.DEFAULT_CONT_ID);
  }

  @Test
  public void testCreateFsClientFromSpecifiedContainer() throws Exception{
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try{
      client = builder.build();
      Assert.assertTrue(client != null);
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
  }

  @Test
  public void testCreateFsClientFromRootContainer() throws Exception{
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId);
    DaosFsClient client = null;
    try{
      client = builder.build();
      Assert.assertTrue(client != null);
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
  }

  @Test
  public void testFsClientCachePerPoolAndContainer()throws Exception{
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    DaosFsClient client2[] = new DaosFsClient[1];
    try {
      client = builder.build();
      Thread thread = new Thread(){
        @Override
        public void run(){
          try {
            DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
            builder.poolId(poolId).containerId(contId);
            client2[0] = builder.build();
          }catch (IOException e){
            e.printStackTrace();
          }
        }
      };
      thread.start();
      thread.join();
      Assert.assertEquals(client, client2[0]);
    } finally {
      client.disconnect();
    }
  }

  @Test
  public void testDeleteSuccessful()throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try{
      client = builder.build();
      Assert.assertTrue(client != null);
      client.delete("/ddddddd/zyx", true);
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
  }

  @Test
  public void testMultipleMkdirs()throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try{
      client = builder.build();
      Assert.assertTrue(client != null);
      client.mkdir("/mkdirs/1", true);
      client.mkdir("/mkdirs/1", true);
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
  }

  @Test(expected = IOException.class)
  public void testMultipleMkdir()throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try{
      client = builder.build();
      Assert.assertTrue(client != null);
      client.mkdir("/mkdir/1", false);
      client.mkdir("/mkdir/1", false);
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
  }

  @Test(expected = IllegalArgumentException.class)
  public void testMoveWithOpenDirsIllegalSrcName() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try{
      String fileName = "srcFile/zb";
      client = builder.build();
      client.move(0, fileName, 0, "destFile");
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
  }

  @Test(expected = IllegalArgumentException.class)
  public void testMoveWithOpenDirsIllegalDestName() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try{
      String fileName = "srcFile";
      client = builder.build();
      client.move(0, fileName, 0, "/destFile");
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
  }

  @Test
  public void testMoveWithOpenDirs() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try{
      String fileName = "srcFile";
      client = builder.build();
      DaosFile srcDir = client.getFile("/mdir1");
      srcDir.mkdirs();
      DaosFile srcFile = client.getFile(srcDir, fileName);
      srcFile.createNewFile();

      DaosFile destDir = client.getFile("/mdir2");
      destDir.mkdirs();
      String destFileName = "destFile";
      client.move(srcDir.getObjId(), fileName, destDir.getObjId(), destFileName);
      Assert.assertFalse(srcFile.exists());
      Assert.assertTrue(client.getFile(destDir, destFileName).exists());
    }finally {
      if(client != null){
        client.disconnect();
      }
    }
  }

  @Test
  public void testFsClientReferenceOne() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    try{
      client = builder.build();
      Assert.assertEquals(1, client.getRefCnt());
    }finally {
      if(client != null){
        client.disconnect();
        Assert.assertEquals(0, client.getRefCnt());
      }
    }
    Exception ee = null;
    try {
      client.incrementRef();
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertTrue(ee instanceof IllegalStateException);
  }

  @Test
  public void testFsClientReferenceMore() throws Exception {
    DaosFsClient.DaosFsClientBuilder builder = new DaosFsClient.DaosFsClientBuilder();
    builder.poolId(poolId).containerId(contId);
    DaosFsClient client = null;
    int cnt = 0;
    try{
      client = builder.build();
      cnt = client.getRefCnt();
      builder.build();
      Assert.assertEquals(cnt + 1, client.getRefCnt());
      client.disconnect();
      client.incrementRef();
      Assert.assertEquals(cnt + 1, client.getRefCnt());
      client.decrementRef();
    } catch (Exception e){
      e.printStackTrace();
    }finally {
      if(client != null){
        client.disconnect();
        Assert.assertEquals(cnt - 1, client.getRefCnt());
      }
    }
  }
}
