package io.daos;

import java.io.Closeable;
import java.io.IOException;

public interface ForceCloseable extends Closeable {

  void forceClose() throws IOException;
}
