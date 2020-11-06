/**
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

package io.daos.fs.hadoop;

import io.daos.dfs.DaosFile;
import org.apache.hadoop.fs.FileSystem;
import org.junit.Assert;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.stubbing.Answer;

import java.io.IOException;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.concurrent.atomic.AtomicInteger;

import static org.mockito.Mockito.*;

public class DaosInputStreamTest {

  @Test(expected = IllegalArgumentException.class)
  public void testNewDaosInputStreamNonDirectBuffer() throws Exception {
    DaosFile file = mock(DaosFile.class);
    new DaosInputStream(file, null, ByteBuffer.allocate(10), 10);
  }

  @Test
  public void testNewDaosInputStreamWithBiggerPreloadSize() throws Exception {
    DaosFile file = mock(DaosFile.class);
    DaosInputStream is = new DaosInputStream(file, null, ByteBuffer.allocateDirect(25), 20);
    Assert.assertEquals(20, is.getReadSize());
  }

  @Test(expected = IllegalArgumentException.class)
  public void testReadLenIllegal() throws Exception {
    DaosFile file = mock(DaosFile.class);
    DaosInputStream is = new DaosInputStream(file, null, 10, 10);
    byte[] buf = new byte[10];
    is.read(buf, 0, -10);
  }

  @Test(expected = IllegalArgumentException.class)
  public void testReadOffsetIllegal() throws Exception {
    DaosFile file = mock(DaosFile.class);
    DaosInputStream is = new DaosInputStream(file, null, 10, 10);
    byte[] buf = new byte[10];
    is.read(buf, -1, 10);
  }

  @Test
  public void testRead0Len() throws Exception {
    DaosFile file = mock(DaosFile.class);
    DaosInputStream is = new DaosInputStream(file, null, 10, 10);
    byte[] buf = new byte[10];
    int len = is.read(buf, 0, 0);
    Assert.assertEquals(0, len);
  }

  private void readFromDaosOnce(int bufferCap, int readSize, int requestLen, long readLen) throws Exception {
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);
    long actualReadLen = requestLen < readSize ? readSize : requestLen;
    when(file.read(any(ByteBuffer.class), anyLong(), anyLong(), eq(actualReadLen))).thenReturn(actualReadLen);
    when(file.length()).thenReturn(200L);

    ByteBuffer buffer = ByteBuffer.allocateDirect(bufferCap);
    ByteBuffer sbuffer = spy(buffer);
    DaosInputStream is = new DaosInputStream(file, stats, sbuffer, readSize);
    byte[] buf = new byte[requestLen];
    int len = is.read(buf, 0, requestLen);

    verify(file, times(1)).read(any(ByteBuffer.class), anyLong(), anyLong(), eq(actualReadLen));

    ArgumentCaptor<Integer> offSetCap = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times(1)).get(eq(buf), offSetCap.capture(), eq(requestLen));

    Assert.assertEquals(0, offSetCap.getValue().intValue());

    Assert.assertEquals(requestLen < readSize ? readSize : requestLen, is.getBuffer().limit());
    Assert.assertEquals(requestLen, len);
    Assert.assertEquals(requestLen, is.getBuffer().position());
    Assert.assertEquals(requestLen, is.getPos());
  }

  @Test
  public void testReadFromDaosLessThanReadSize() throws Exception {
    readFromDaosOnce(100, 50, 20, 20);
  }

  @Test
  public void testReadFromDaosEqualToReadSize() throws Exception {
    readFromDaosOnce(100, 50, 50, 50);
  }

  @Test
  public void testReadFromDaosGreaterThanReadSize() throws Exception {
    readFromDaosOnce(100, 50, 80, 80);
  }

  @Test
  public void testReadFromDaosEqualToBufferCap() throws Exception {
    readFromDaosOnce(100, 50, 100, 100);
  }

  private void readFromDaosMultipleTimes(int requestLen, long actualRemainingLen, int readTimes) throws Exception {
    int bufferCap = 100;
    int preloadSize = 50;
    DaosFile file = mock(DaosFile.class);
    when(file.length()).thenReturn(1000L);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);

    ByteBuffer buffer = ByteBuffer.allocateDirect(bufferCap);
    ByteBuffer sbuffer = spy(buffer);
    DaosInputStream is = new DaosInputStream(file, stats, sbuffer, preloadSize);
    byte[] buf = new byte[requestLen];

    long actualReadLen1 = 100;
    when(file.read(any(ByteBuffer.class), anyLong(), anyLong(), eq(actualReadLen1))).thenReturn(actualReadLen1);

    long actualReadLen2 = actualRemainingLen < preloadSize ? preloadSize : actualRemainingLen;
    when(file.read(any(ByteBuffer.class), anyLong(), anyLong(), eq(actualReadLen2))).thenReturn(actualReadLen2);

    int len = is.read(buf, 0, requestLen);

    verify(file, times(readTimes)).read(any(ByteBuffer.class), anyLong(), anyLong(), eq(actualReadLen1));
    verify(file, times(1)).read(any(ByteBuffer.class), anyLong(), anyLong(), eq(actualReadLen2));

    ArgumentCaptor<Integer> offSetCap = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times((readTimes + 1))).get(eq(buf), offSetCap.capture(), anyInt());

    int mod = requestLen % bufferCap;
    int remain = mod > preloadSize ? mod : preloadSize;
    Assert.assertEquals(remain, is.getBuffer().limit());
    Assert.assertEquals(requestLen, len);
    Assert.assertEquals(mod, is.getBuffer().position());

    Integer expect[] = new Integer[readTimes + 1];
    for (int i = 0; i < expect.length; i++) {
      expect[i] = i * 100;
    }
    Assert.assertTrue(Arrays.equals(expect, offSetCap.getAllValues().toArray()));
    Assert.assertEquals(requestLen, is.getPos());
  }

  @Test
  public void testReadFromDaosGreaterThanBufferCapAndRemainingLessThanPreload() throws Exception {
    readFromDaosMultipleTimes(249, 49, 2);
  }

  @Test
  public void testReadFromDaosGreaterThanBufferCapAndRemainingEqualToPreload() throws Exception {
    readFromDaosMultipleTimes(350, 50, 3);
  }

  @Test
  public void testReadFromDaosGreaterThanBufferCapAndRemainingGreaterThanPreload() throws Exception {
    readFromDaosMultipleTimes(463, 63, 4);
  }

  @Test
  public void testSeekAndSkip() throws Exception {
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);

    when(file.length()).thenReturn((long) 500);
    DaosInputStream is = new DaosInputStream(file, stats, 100, 50);
    Assert.assertEquals(0, is.getPos());

    is.seek(200);
    Assert.assertEquals(200, is.getPos());

    is.skip(200);
    Assert.assertEquals(400, is.getPos());
  }

  @Test
  public void testReadAllFromCache() throws Exception {
    int bufferCap = 100;
    int preloadSize = 100;
    int requestLen = 50;

    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);

    when(file.length()).thenReturn((long) 500);
    ByteBuffer buffer = ByteBuffer.allocateDirect(bufferCap);
    ByteBuffer sbuffer = spy(buffer);
    DaosInputStream is = new DaosInputStream(file, stats, sbuffer, preloadSize);

    sbuffer.limit(preloadSize);
    byte[] buf = new byte[100];
    when(file.read(any(ByteBuffer.class), anyLong(), anyLong(), anyLong())).thenThrow(new IOException());

    int len = is.read(buf, 0, requestLen);
    Assert.assertEquals(requestLen, len);
    ArgumentCaptor<Integer> offSetCap = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times(1)).get(eq(buf), offSetCap.capture(), eq(requestLen));
    Assert.assertEquals(0, offSetCap.getValue().intValue());

    is.seek(30);
    len = is.read(buf, 1, 60);
    Assert.assertEquals(60, len);
    offSetCap = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times(1)).get(eq(buf), offSetCap.capture(), eq(60));
    Assert.assertEquals(1, offSetCap.getValue().intValue());

    is.seek(71);
    len = is.read(buf, 4, 4);
    Assert.assertEquals(4, len);
    offSetCap = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times(1)).get(eq(buf), offSetCap.capture(), eq(4));
    Assert.assertEquals(4, offSetCap.getValue().intValue());

    verify(file, times(0)).read(any(ByteBuffer.class), anyLong(), anyLong(), anyLong());

    Assert.assertEquals(preloadSize, is.getBuffer().limit());
    Assert.assertEquals(75, is.getBuffer().position());
    Assert.assertEquals(75, is.getPos());
  }

  @Test
  public void testReadPartialFromCache() throws Exception {
    int bufferCap = 100;
    int preloadSize = 80;
    int requestLen = 320;

    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);

    when(file.length()).thenReturn((long) 500);
    ByteBuffer buffer = ByteBuffer.allocateDirect(bufferCap);
    ByteBuffer sbuffer = spy(buffer);
    DaosInputStream is = new DaosInputStream(file, stats, sbuffer, preloadSize);

    byte[] buf = new byte[requestLen];
    when(file.read(any(ByteBuffer.class), eq(0L), anyLong(), eq(100L))).thenReturn(100L);
    when(file.read(any(ByteBuffer.class), eq(0L), anyLong(), eq(80L))).thenReturn(80L);

    int len = is.read(buf, 0, requestLen);

    verify(file, times(3)).read(any(ByteBuffer.class), eq(0L), anyLong(),
        eq(100L));
    verify(file, times(1)).read(any(ByteBuffer.class), eq(0L), anyLong(),
        eq(80L));
    Assert.assertEquals(requestLen, len);

    when(file.read(any(ByteBuffer.class), eq(0L), anyLong(), eq(80L))).thenReturn(80L);
    len = is.read(buf, 0, 137);

    verify(file, times(3)).read(any(ByteBuffer.class), eq(0L), anyLong(),
        eq(100L));
    verify(file, times(2)).read(any(ByteBuffer.class), eq(0L), anyLong(),
        eq(80L));

    ArgumentCaptor<Integer> offSetCap = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times(3)).get(eq(buf), offSetCap.capture(), eq(100));
    Assert.assertEquals(200, offSetCap.getAllValues().get(2).intValue());
    ArgumentCaptor<Integer> offSetCap2 = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times(1)).get(eq(buf), offSetCap2.capture(), eq(20));
    Assert.assertEquals(300, offSetCap2.getValue().intValue());

    ArgumentCaptor<Integer> offSetCap3 = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times(1)).get(eq(buf), offSetCap3.capture(), eq(60));
    Assert.assertEquals(0, offSetCap3.getValue().intValue());
    ArgumentCaptor<Integer> offSetCap4 = ArgumentCaptor.forClass(Integer.class);
    verify(sbuffer, times(1)).get(eq(buf), offSetCap4.capture(), eq(77));
    Assert.assertEquals(60, offSetCap4.getValue().intValue());

    Assert.assertEquals(preloadSize, is.getBuffer().limit());
    Assert.assertEquals(137, len);
    Assert.assertEquals(77, is.getBuffer().position());
    Assert.assertEquals(457, is.getPos());
  }

  @Test
  public void testReadSingleFromCache() throws Exception {
    int bufferCap = 100;
    int preloadSize = 80;

    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);

    when(file.length()).thenReturn((long) 500);
    ByteBuffer buffer = ByteBuffer.allocateDirect(bufferCap);
    ByteBuffer sbuffer = spy(buffer);
    DaosInputStream is = new DaosInputStream(file, stats, sbuffer, preloadSize);

    sbuffer.limit(preloadSize - 10);
    sbuffer.put("abcdef".getBytes());
    is.seek(5);

    int b = is.read();
    Assert.assertEquals('f', (byte) b);
  }

  @Test
  public void testReadSingleFromDaos() throws Exception {
    int bufferCap = 100;
    int preloadSize = 80;

    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);

    when(file.length()).thenReturn((long) 500);

    ByteBuffer buffer = ByteBuffer.allocateDirect(bufferCap);
    ByteBuffer sbuffer = spy(buffer);
    DaosInputStream is = new DaosInputStream(file, stats, sbuffer, preloadSize);

    Answer<Long> answer = (invocation) -> {
      sbuffer.limit(1);
      sbuffer.put((byte) 'a');
      sbuffer.flip();
      return 80L;
    };
    when(file.read(any(ByteBuffer.class), anyLong(), anyLong(), eq(80L))).thenAnswer(answer);

    int b = is.read();

    Assert.assertEquals('a', (byte) b);
    verify(file, times(1)).read(any(ByteBuffer.class), anyLong(), anyLong(), eq(80L));
    verify(sbuffer, times(1)).get(any(byte[].class), eq(0), eq(1));
  }

  @Test
  public void testAvailable() throws Exception {
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);

    when(file.length()).thenReturn((long) 500);
    ByteBuffer buffer = ByteBuffer.allocateDirect(100);
    ByteBuffer sbuffer = spy(buffer);
    DaosInputStream is = new DaosInputStream(file, stats, sbuffer, 80);
    is.seek(452);
    Assert.assertEquals(48, is.available());
    is.close();
  }

  private DaosInputStream close() throws Exception {
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = mock(FileSystem.Statistics.class);

    when(file.length()).thenReturn((long) 500);
    ByteBuffer buffer = ByteBuffer.allocateDirect(100);
    ByteBuffer sbuffer = spy(buffer);
    DaosInputStream is = new DaosInputStream(file, stats, sbuffer, 80);
    is.close();
    is.close();
    verify(file, times(1)).release();
    return is;
  }

  @Test
  public void testClose() throws Exception {
    close();
  }

  @Test(expected = IOException.class)
  public void testOperationAfterClose() throws Exception {
    DaosInputStream is = close();
    is.read(null, 0, 0);
  }

  @Test
  public void testBufferedAndNonBufferedRead() throws IOException {
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = new FileSystem.Statistics("daos:///");

    int bufferSize = 7;
    ByteBuffer internalBuffer = ByteBuffer.allocateDirect(bufferSize);
    byte[] data = new byte[]{19, 49, 89, 64, 20, 19, 1, 2, 3};

    doAnswer(
        invocationOnMock -> {
          ByteBuffer buffer = (ByteBuffer) invocationOnMock.getArguments()[0];
          long bufferOffset = (long) invocationOnMock.getArguments()[1];
          long len = (long) invocationOnMock.getArguments()[3];
          long i;
          for (i = bufferOffset; i < bufferOffset + len; i++) {
            buffer.put((int) i, data[(int) (i - bufferOffset)]);
          }
          buffer.limit((int) i);
          return len;
        })
        .when(file)
        .read(any(ByteBuffer.class), anyLong(), anyLong(), anyLong());
    doReturn((long) data.length).when(file).length();

    boolean[] trueFalse = new boolean[]{true, false};

    for (int j = 0; j < 2; j++) {
      boolean bufferedReadEnabled = trueFalse[j];
      int preloadSize = bufferedReadEnabled ? bufferSize : 0;
      DaosInputStream input = new DaosInputStream(file, stats, internalBuffer, preloadSize);
      int readSize = 4;
      byte[] answer = new byte[readSize];
      input.read(answer, 0, readSize);
      byte[] expect = new byte[readSize];
      for (int i = 0; i < readSize; i++) {
        expect[i] = data[i];
      }
      Assert.assertArrayEquals(expect, answer);

      boolean shouldThemEqual = bufferedReadEnabled;
      for (int i = readSize; i < data.length && i < internalBuffer.limit(); i++) {
        // If enabled buffered read, the internal buffer of DataInputStream should be fully filled
        // Otherwise, DaosInputStream's internal buffer is not filled for non-target part
        Assert.assertEquals(shouldThemEqual, (internalBuffer.get(i) == data[i]));
      }
    }
  }

  @Test
  public void testReadLengthLargerThanBufferSize() throws IOException {
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = new FileSystem.Statistics("daos:///");

    int bufferSize = 7;
    ByteBuffer internalBuffer = ByteBuffer.allocateDirect(bufferSize);
    byte[] data = new byte[]{19, 49, 89, 64, 20, 19, 1, 2, 3};

    doAnswer(
        invocationOnMock -> {
          ByteBuffer buffer = (ByteBuffer) invocationOnMock.getArguments()[0];
          long bufferOffset = (long) invocationOnMock.getArguments()[1];
          long fileOffset = (long) invocationOnMock.getArguments()[2];
          long len = (long) invocationOnMock.getArguments()[3];
          if (len > buffer.capacity() - bufferOffset) {
            throw new IOException(
                String.format("buffer (%d) has no enough space start at %d for reading %d bytes from file",
                    buffer.capacity(), bufferOffset, len));
          }
          long actualRead = 0;
          for (long i = bufferOffset; i < bufferOffset + len &&
              (i - bufferOffset + fileOffset) < data.length; i++) {
            buffer.put((int) i, data[(int) (i - bufferOffset + fileOffset)]);
            actualRead++;
          }
          return actualRead;
        })
        .when(file)
        .read(any(ByteBuffer.class), anyLong(), anyLong(), anyLong());
    doReturn((long) data.length).when(file).length();

    boolean[] trueFalse = new boolean[]{true, false};
    for (int j = 0; j < 2; j++) {
      int readSize = trueFalse[j] ? bufferSize : 5;
      DaosInputStream input = new DaosInputStream(file, stats, internalBuffer, readSize);
      byte[] answer = new byte[readSize];
      input.read(answer, 0, readSize);
      byte[] expect = new byte[readSize];
      for (int i = 0; i < readSize; i++) {
        expect[i] = data[i];
      }
      Assert.assertArrayEquals(expect, answer);

      for (int i = readSize; i < data.length && i < internalBuffer.limit(); i++) {
        // If enabled buffered read, the internal buffer of DataInputStream should be fully filled
        // Otherwise, DaosInputStream's internal buffer is not filled for non-target part
        Assert.assertEquals(trueFalse[j], (internalBuffer.get(i) == data[i]));
      }
    }
  }

  @Test
  public void testCallingInputStreamReadMultiTimes() throws IOException {
    DaosFile file = mock(DaosFile.class);
    FileSystem.Statistics stats = new FileSystem.Statistics("daos:///");

    byte[] data = new byte[]{19, 49, 89, 64, 20, 19, 1, 2, 3};
    int bufferSize = data.length;
    ByteBuffer internalBuffer = ByteBuffer.allocateDirect(bufferSize);
    AtomicInteger timesReadFromDaos = new AtomicInteger(0);

    doAnswer(
        invocationOnMock -> {
          ByteBuffer buffer = (ByteBuffer) invocationOnMock.getArguments()[0];
          int times = timesReadFromDaos.incrementAndGet();
          Assert.assertTrue("Read to internal buffer more than once!", times <= 1);
          long bufferOffset = (long) invocationOnMock.getArguments()[1];
          long fileOffset = (long) invocationOnMock.getArguments()[2];
          long len = (long) invocationOnMock.getArguments()[3];
          if (len > buffer.capacity() - bufferOffset) {
            throw new IOException(
                String.format("buffer (%d) has no enough space start at %d for reading %d bytes from file",
                    buffer.capacity(), bufferOffset, len));
          }
          long actualRead = 0;
          for (long i = bufferOffset; i < bufferOffset + len &&
              (i - bufferOffset + fileOffset) < data.length; i++) {
            buffer.put((int) i, data[(int) (i - bufferOffset + fileOffset)]);
            actualRead++;
          }
          return actualRead;
        })
        .when(file)
        .read(any(ByteBuffer.class), anyLong(), anyLong(), anyLong());
    doReturn((long) data.length).when(file).length();

    DaosInputStream input = new DaosInputStream(file, stats, internalBuffer, bufferSize);
    int readSize = 3;
    byte[] answer = new byte[bufferSize];
    input.read(answer, 0, readSize);
    input.read(answer, readSize, bufferSize - readSize);
    byte[] expect = data;
    Assert.assertArrayEquals(expect, answer);
  }
}
