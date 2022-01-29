package io.daos.obj;

import org.junit.After;
import org.junit.Assert;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mockito;
import org.powermock.api.mockito.PowerMockito;
import org.powermock.core.classloader.annotations.PowerMockIgnore;
import org.powermock.core.classloader.annotations.PrepareForTest;
import org.powermock.core.classloader.annotations.SuppressStaticInitializationFor;
import org.powermock.modules.junit4.PowerMockRunner;

import java.util.Collections;

@RunWith(PowerMockRunner.class)
@PowerMockIgnore("javax.management.*")
@PrepareForTest({DaosObject.class, DaosObjClient.class})
@SuppressStaticInitializationFor("io.daos.DaosClient")
public class DaosObjectTest {

  private DaosObject object;

  @Before
  public void setup() throws Exception {
    PowerMockito.mockStatic(DaosObjClient.class);
    DaosObjectId oid = new DaosObjectId();
    oid.encode(0L);
    DaosObjClient client = Mockito.mock(DaosObjClient.class);
    long contPtr = 12345;
    long address = oid.getBuffer().memoryAddress();
    long objPtr = 6789;
    Mockito.when(client.getContPtr()).thenReturn(contPtr);
    Mockito.when(client.openObject(contPtr, address, OpenMode.UNKNOWN.getValue())).thenReturn(objPtr);
    object = new DaosObject(client, oid);
  }

  @After
  public void teardown() throws Exception {
    if (object != null) {
      object.close();
    }
  }

  @Test
  public void testObjectIdEncode() throws Exception {
    DaosObjectId oid = new DaosObjectId();

    Exception ee = null;
    try {
      new DaosObject(Mockito.mock(DaosObjClient.class), oid);
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee instanceof IllegalArgumentException);
    Assert.assertTrue(ee.getMessage().contains("DAOS object ID should be encoded"));
  }

  @Test
  public void testPunchEmptyDkeys() throws Exception {
    Exception ee = null;
    try {
      object.open();
      object.punchDkeys(Collections.EMPTY_LIST);
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee instanceof DaosObjectException);
    Assert.assertTrue(ee.getMessage().contains("no dkeys specified when punch dkeys"));
  }

  @Test
  public void testPunchEmptyAkeys() throws Exception {
    Exception ee = null;
    try {
      object.open();
      object.punchAkeys("dkey1", Collections.EMPTY_LIST);
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee instanceof DaosObjectException);
    Assert.assertTrue(ee.getMessage().contains("no akeys specified when punch akeys"));
  }

  @Test
  public void testListAkeysWithNullDkey() throws Exception {
    Exception ee = null;
    try {
      object.open();
      IOKeyDesc desc = object.createKD(null);
      try {
        object.listAkeys(desc);
      } finally {
        desc.release();
      }
    } catch (Exception e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee instanceof DaosObjectException);
    Assert.assertTrue(ee.getMessage().contains("dkey is needed when list akeys"));
  }

  @Test
  public void testGetRecordSizeWithNullKey() throws Exception {
    IllegalArgumentException ee = null;
    try {
      object.open();
      object.getRecordSize(null, "akey");
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("dkey is blank"));
    ee = null;
    try {
      object.getRecordSize("dkey", null);
    } catch (IllegalArgumentException e) {
      ee = e;
    }
    Assert.assertNotNull(ee);
    Assert.assertTrue(ee.getMessage().contains("akey is blank"));
  }
}
