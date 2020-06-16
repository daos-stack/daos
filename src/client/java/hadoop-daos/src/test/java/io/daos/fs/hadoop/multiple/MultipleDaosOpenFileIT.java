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

package io.daos.fs.hadoop.multiple;

import io.daos.fs.hadoop.DaosFSFactory;
import org.apache.hadoop.fs.FileSystem;
import org.apache.hadoop.fs.Path;
import org.junit.Before;
import org.junit.Test;

import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

public class MultipleDaosOpenFileIT {

  FileSystem fs;

  @Before
  public void setup() throws IOException {
    fs = DaosFSFactory.getFS();
  }

  @Test
  public void testConcurrentRead() throws Exception{
    String path = "/test/data";
    DataOutputStream os = fs.create(new Path(path));
    for (int i=0; i<100000; i++) {
      os.write("abcdddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd".getBytes());
    }
    os.close();

    List<ReadThread> list = new ArrayList<>();
    int num = 20;
    for (int i=0; i<num; i++) {
      list.add(new ReadThread(fs, path));
    }
    for (int i=0; i<num; i++) {
      list.get(i).start();
    }
    for (int i=0; i<num; i++) {
      list.get(i).join();
    }
  }

  private static class ReadThread extends Thread {
    String path;
    FileSystem fs;
    ReadThread(FileSystem fs, String path){
      this.fs = fs;
      this.path = path;
    }
    @Override
    public void run() {
      byte[] bytes = new byte[100000];
      Random random = new Random();
      try(DataInputStream dis = fs.open(new Path(path))){
        while(dis.read(bytes)>0){
          try {
            Thread.sleep(random.nextInt(100));
          }catch (InterruptedException e){
            e.printStackTrace();
          }
        }
      }catch (IOException e){
        e.printStackTrace();
      }
    }
  }
}
