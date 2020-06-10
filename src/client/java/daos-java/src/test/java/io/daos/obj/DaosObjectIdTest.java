package io.daos.obj;

import io.daos.DaosObjectType;
import org.junit.Assert;
import org.junit.Test;

public class DaosObjectIdTest {

  @Test
  public void testEncodeEmptyObjectId() throws Exception {
    DaosObjectId id = new DaosObjectId();
    id.encode(0, DaosObjectType.OC_SX, 0);
    Assert.assertTrue(id.getHigh() != 0);
    Assert.assertTrue(id.getLow() == 0);
  }

  @Test
  public void testEncodeObjectId() throws Exception {
    DaosObjectId id = new DaosObjectId(345, 1024);
    id.encode(0, DaosObjectType.OC_SX, 0);
    Assert.assertTrue(id.getHigh() != 0);
    Assert.assertTrue(id.getLow() != 0);
  }
}
