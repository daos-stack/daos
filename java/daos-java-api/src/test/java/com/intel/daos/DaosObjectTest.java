/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * <p>
 * http://www.apache.org/licenses/LICENSE-2.0
 * <p>
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package com.intel.daos;

import com.intel.daos.*;
import org.junit.AfterClass;
import org.junit.BeforeClass;
import org.junit.Test;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Random;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

/**
 * Daos Object API Test.
 */
public class DaosObjectTest {
  private static int kb = 1 << 10;
  private static int mb = 1 << 20;
  private static int gb = 1 << 30;

  private static DaosKeyValue kv;
  private static DaosArray array;
  private static DaosPool pool;
  private static DaosContainer container;

  private static void printArray(byte[] buf, int offset, int number) {
    System.out.println("Printing array of length: " + buf.length);
    for (int i = offset; i < offset + number; i++) {
      System.out.printf("%d ", buf[i]);
    }
    System.out.println();
  }

  private String sizeToString(long size) {
    String str;
    if (size / kb < 1024) {
      str = size / kb + " KB";
    } else if (size / mb < 1024){
      str = size / mb + " MB";
    } else {
      str = size / gb + " GB";
    }
    return str;
  }

  @BeforeClass
  public static void openObjects()
      throws DaosNativeException, DaosJavaException, IOException {
    DaosSession session = DaosSession.getInstance();
    pool = session.createAndGetPool(
        50*1024*1024,
        0,
        DaosPoolMode.DAOS_PC_RW);
    container = pool.getContainer(
        pool.getUuid(),
        DaosContainerMode.DAOS_COO_RW,
    true);
    kv = container.getKV(1, DaosObjectMode.DAOS_OO_RW.getValue(),
      0, DaosObjClass.OC_SX.getValue());
    array = container.getArray(2, DaosObjectMode.DAOS_OO_RW.getValue(),
      0, DaosObjClass.OC_SX.getValue());
  }

  @Test
  public void kvTest() throws IOException {
    int size = 4 * mb;
    String dkey = "1", akey = "sync";
    Random rand = new Random();
    ByteBuffer buffer = ByteBuffer.allocateDirect(size);
    ByteBuffer result = ByteBuffer.allocateDirect(size);
    byte[] buf1 = new byte[size];
    rand.nextBytes(buf1);
    buffer.put(buf1);
    long single;
    long start = System.nanoTime();
    kv.put(dkey, akey, buffer);
    long middle = System.nanoTime();
    single = kv.get(dkey, akey, result);
    long end = System.nanoTime();
    assertEquals(single, size);
    byte[] buf2 = new byte[size];
    result.get(buf2);
    assertArrayEquals(buf1, buf2);
    System.out.printf("dkey %s: Update single value(%s) cost %.6f seconds\n",
        dkey, sizeToString(size), (middle - start) / 1000000000.0);
    System.out.printf("dkey %s: Fetch single value(%s) cost %.6f seconds\n",
        dkey, sizeToString(size), (end - middle) / 1000000000.0);
  }

  @Test
  public void arrayTest() throws IOException {
    int single = 4;
    int number = mb;
    String dkey = "1", akey = "sync";
    int size = single * number;
    Random rand = new Random();
    ByteBuffer buffer = ByteBuffer.allocateDirect(size);
    ByteBuffer result = ByteBuffer.allocateDirect(size);
    byte[] buf1 = new byte[size];
    rand.nextBytes(buf1);
    buffer.put(buf1);
    long start = System.nanoTime();
    array.write(dkey, akey, 0, single, buffer);
    long middle = System.nanoTime();
    long singleSize = array.read(dkey, akey, 0, number, result);
    assertEquals(singleSize, single);
    long end = System.nanoTime();
    byte[] buf2 = new byte[size];
    result.get(buf2);
    assertArrayEquals(buf1, buf2);
    System.out.printf("dkey %s: Update Array value(%s) cost %.6f seconds\n",
        dkey, sizeToString(size), (middle - start) / 1000000000.0);
    System.out.printf("dkey %s: Fetch Array value(%s) cost %.6f seconds\n",
        dkey, sizeToString(size), (end - middle) / 1000000000.0);
  }

  /**
   * Now async functions work if and only if you call waitAll.
   * @throws DaosNativeException
   */
  @Test
  public void asyncTest() throws IOException {
    DaosEventQueue writeQueue = new DaosEventQueue();
    DaosEventQueue readQueue = new DaosEventQueue();
    int single = 4;
    int number = mb;
    String dkey = "2", akey = "async";
    int size = single * number;
    Random rand = new Random();
    ByteBuffer kvBuffer = ByteBuffer.allocateDirect(size);
    ByteBuffer arrayBuffer = ByteBuffer.allocateDirect(size);
    ByteBuffer kvResult = ByteBuffer.allocateDirect(size);
    ByteBuffer arrayResult = ByteBuffer.allocateDirect(size);
    byte[] buf1 = new byte[size];
    rand.nextBytes(buf1);
    kvBuffer.put(buf1);
    arrayBuffer.put(buf1);
    long start = System.nanoTime();
    kv.put(dkey, akey, kvBuffer, writeQueue);
    array.write(dkey, akey, 0, single, arrayBuffer, writeQueue);
    writeQueue.waitAll();
    long middle = System.nanoTime();
    kv.get(dkey, akey, kvResult, readQueue);
    array.read(dkey, akey, 0, number, arrayResult, readQueue);
    readQueue.waitAll();
    long end = System.nanoTime();
    byte[] buf2 = new byte[size];
    arrayResult.get(buf2);
    assertArrayEquals(buf1, buf2);
    System.out.printf("dkey %s: Async update(%s) cost %.6f seconds\n",
        dkey, sizeToString(size * 2), (middle - start) / 1000000000.0);
    System.out.printf("dkey %s: Async fetch value(%s) cost %.6f seconds\n",
        dkey, sizeToString(size * 2), (end - middle) / 1000000000.0);
  }

  @Test
  public void multiThreadTest()
      throws InterruptedException, IOException {
    ExecutorService exec = Executors.newFixedThreadPool(4);
    int size = 1 * mb;
    String akey = "multithread";
    Random rand = new Random(1);
    for (int i = 0; i < 4; i++) {
      switch (i % 2) {
      case 0:
        exec.submit(() -> {
          try {
            ByteBuffer buffer = ByteBuffer.allocateDirect(size);
            byte[] buf = new byte[size];
            rand.nextBytes(buf);
            buffer.put(buf);
            kv.put("1", akey, buffer);
            System.out.printf("Thread %d: update dkey 1\n",
                Thread.currentThread().getId());
          } catch (Exception e) {
            e.printStackTrace();
          }
        });
        break;
      case 1:
        exec.submit(() -> {
          try {
            ByteBuffer buffer = ByteBuffer.allocateDirect(size);
            byte[] buf = new byte[size];
            rand.nextBytes(buf);
            buffer.put(buf);
            array.write("1", akey, 0, 8, buffer);
            System.out.printf("Thread %d: update dkey 2\n",
                Thread.currentThread().getId());
          } catch (Exception e) {
            e.printStackTrace();
          }
        });
      default:
        break;
      }
    }
    exec.shutdown();
    exec.awaitTermination(30, TimeUnit.SECONDS);
    String[] dkeys = kv.listDkey();
    for (String dkey : dkeys) {
      String[] akeys = kv.listAkey(dkey);
      for (String key : akeys) {
        System.out.printf("dkey %s akey %s\n", dkey, key);
      }
    }
  }

  @AfterClass
  public static void closeSession() {
    if(!container.closeCont()){
      System.err.println("close contianer fail");
    }
    if(!pool.disconPool()){
      System.err.println("disconnect pool fail");
    }
    if(!pool.destroyPool()){
      System.err.println("destroy pool fail");
    }
  }

}
