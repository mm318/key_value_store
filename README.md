# Concurrent and Peristent Key-Value Store


## Brief Description

This is an example implementation of a key-value store.

Features:
- Concurrent, uses atomics and memory fences for reads, and uses mutexes for writes
- Strongly consistent, writes take effect as immediately as possible
- Persistent, the store is backed by an `mmap()`'d file


## Build

Requires zig and a generally POSIX-compliant operating system
```bash
cd <"key_value_store" root dir>
zig build
```
(Tested on Ubuntu 20.04 with zig 0.13.0.)


## Run

Basic test, multiple threads writing their names to the same set of keys
```bash
# from "key_value_store" root dir
zig-out/bin/kv_basic_test
```

Stress test, multiple threads pummeling the key-value store with reads and writes with random values ranging from 8 bytes to 900 kilobytes
```bash
# from "key_value_store" root dir
zig build -Doptimize=ReleaseSafe run
```

If desired, reset the persistent state by deleting the generated `kvstore.bin` file in the present working directory.


## TODOs

- Improve memory allocation for key-value pairs from file-backed buffer, reduce memory fragmentation
- Support logging reads and writes
- Support resizing (growing) key-value storage size
- Support erasing from key-value store
