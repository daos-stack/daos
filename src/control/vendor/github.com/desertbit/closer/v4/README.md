# Closer - A simple, thread-safe closer

[![GoDoc](https://godoc.org/github.com/desertbit/closer/v4?status.svg)](https://godoc.org/github.com/desertbit/closer/v4)
[![Go Report Card](https://goreportcard.com/badge/github.com/desertbit/closer/v4)](https://goreportcard.com/report/github.com/desertbit/closer/v4)
[![coverage](https://codecov.io/gh/desertbit/closer/branch/master/graph/badge.svg)](https://codecov.io/gh/desertbit/closer/branch/master)
[![license](https://img.shields.io/github/license/desertbit/closer.svg)](https://opensource.org/licenses/MIT)

This package aims to provide a simple and performance oriented mechanism to manage the graceful and reliable shutdown of an application, or parts of it.  

It can also be a handy alternative to the context package, though it does not solve the problem that common go libraries only accept context as a valid cancellation method. Therefore, you are only able to cancel "in-between" slow operations.

```
go get github.com/desertbit/closer/v4
```

### Examples
Check out the sample program for a good overview of this package's functionality.
##### Closing
Let us assume you want a server that should close its connection once it gets closed. We close the connection in the `onClose()` method of the server's closer and demonstrate that it does not matter how often you call `Close()`, the connection is closed exactly once.

```go
type Server struct {
    closer.Closer // Embedded
    conn net.Conn
}

func New() *Server {
    // ...
    s := &Server {
        Closer: closer.New(),
        conn: conn,
    }
    closer.Hook(s, func(h closer.H) {
        h.OnCloseWithErr(s.onClose)
    })
    return s
}

func (s *server) onClose() error {
    return s.conn.Close()
}

func main() {
    s := New()
    // ...

    // The s.onClose function will be called only once.
    s.Close()
    s.Close()
}
```

##### OneWay
Now we want an application that (among other things) connects as a client to a remote server. In case the connection is interrupted, the app should continue to run and not fail. But if the app itself closes, of course we want to take down the client connection as well.
```go
type App struct {
    closer.Closer
}

func NewApp() *App {
    return &App{
        Closer: closer.New()
    }
}

type Client struct {
    closer.Closer
    conn net.Conn
}

func NewClient(cl closer.Closer, conn net.Conn) (c *Client) {
    closer.Hook(s, func(h closer.H) {
        c = &Client{
            Closer: cl,
            conn: conn,
        }
        h.OnCloseWithErr(func() error {
            return c.conn.Close()
        })
    })
    return c
}

func main() {
    a := NewApp()
    // Close c, when a closes, but do not close a, when c closes.
    c := NewClient(closer.OneWay(a))
    
    c.Close()
    // App still alive.
}
```

##### TwoWay
Of course, there is the opposite to the OneWay closer that closes its parent as well. If we take the example from before, we can simply exchange the closer that is passed to the client.
```go
//...

func main() {
    a := NewApp()
    // Close c, when a closes, and close a, when c closes.
    c := NewClient(closer.TwoWay(a))
    
    c.Close()
    // App has been closed.
}
```
