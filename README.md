# Concurrent and Peristent Key-Value Store


## Brief Description

This is an example implementation of a key-value store.

Features:
- Concurrency, uses atomics and memory fences for reads, and uses mutexes for writes
- Strongly consistent, writes take effect as immediately as possible
- Persistent, the store is backed by an `mmap()`'d file


## Build

Requires CMake, a C++14 compiler, and a generally POSIX-compliant operating system
```
mkdir <build dir>
cd <build dir>
cmake -DCMAKE_BUILD_TYPE=Release <path to "key_value_store" root dir>
make
```
(Tested on Ubuntu 20.04.)


## Run

Basic test, multiple threads writing their names to the same set of keys
```
<build dir>/bin/kv_basic_test
```

Stress test, multiple threads pummeling the key-value store with reads and writes with random values ranging from 8 bytes to 900 kilobytes
```
<build dir>/bin/kv_stress_test
```

If desired, reset the persistent state by deleting the generated `kvstore.bin` file in the present working directory.


## TODOs

- Improve memory allocation for key-value pairs from file-backed buffer, reduce memory fragmentation
- Support logging reads and writes
- Support resizing (growing) key-value storage size
- Support erasing from key-value store
