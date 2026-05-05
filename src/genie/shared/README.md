# GENIE Shared Library API

This interface exposes a small C ABI for random-access decompression from MPEG-G
bitstreams. Current scope is `.mgb` input and `.fastq` output.

## Build

Configure with dependencies visible to CMake. If BSC support is needed in the
shared library, `BSC_LIBRARY` must point at shared `libbsc.so`; non-PIC
`libbsc.a` cannot be linked into `libgenie.so`.

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH=/mnt/vmshare/genie/dependencies/install \
  -DBSC_LIBRARY=/mnt/vmshare/genie/dependencies/install/lib/libbsc.so \
  -DBSC_INCLUDE_DIR=/mnt/vmshare/genie/dependencies/install/include/libbsc \
  -DLZMA_INCLUDE_DIR=/usr/include \
  -DHTSLIB_INCLUDE_DIR=/mnt/vmshare/genie/dependencies/install/include \
  -DHTSlib_LIBRARY=/mnt/vmshare/genie/dependencies/install/lib/libhts.so \
  "-DCMAKE_CXX_FLAGS=-I/mnt/vmshare/genie/dependencies/install/include/libbsc -I/mnt/vmshare/genie/dependencies/install/include"

cmake --build build --target genie-shared -j
```

Output library:

```text
build/lib/libgenie.so
```

Public header:

```text
src/genie/shared/api.h
```

## Functions

```c
uint8_t GenieGetAccessUnitCount(const char* input_file,
                                uint64_t* output_count);
```

Counts access units in an `.mgb` file.

Parameters:
- `input_file`: path to `.mgb`.
- `output_count`: receives access unit count.

Returns `GENIE_SHARED_SUCCESS` on success.

```c
uint8_t GenieDecompressAccessUnit(const char* input_file,
                                  uint64_t access_unit_id,
                                  const char* output_file,
                                  const char* reference_file,
                                  const char* working_dir,
                                  uint64_t threads);
```

Decompresses one selected access unit from `.mgb` into `.fastq`.

Parameters:
- `input_file`: path to `.mgb`.
- `access_unit_id`: MPEG-G AU id from AU header.
- `output_file`: output `.fastq` path.
- `reference_file`: optional `.fa`, `.fasta`, or `.mgb` reference path; pass
  `NULL` if not needed. If `NULL`, sidecar `input.mgb.json` is checked for a
  `file://` FASTA URI.
- `working_dir`: optional temp directory for decoder work files; `NULL` uses
  current directory.
- `threads`: decoder thread count; `0` means `1`.

Returns `GENIE_SHARED_SUCCESS` on success.

```c
const char* GenieSharedStrerror(uint8_t code);
```

Returns static text for a return code.

```c
uint8_t GenieDecompressAccessUnitToFastq(const char* input_file,
                                         uint64_t access_unit_id,
                                         const char* reference_file,
                                         const char* working_dir,
                                         uint64_t threads,
                                         char** output_data,
                                         uint64_t* output_size);
```

Decompresses one selected access unit from `.mgb` into an allocated FASTQ
buffer. Buffer is null-terminated, but `output_size` is authoritative length.
Release with `GenieFree`.

```c
void GenieFree(void* ptr);
```

Frees buffers returned by this library.

## Return Codes

```c
GENIE_SHARED_SUCCESS                 0
GENIE_SHARED_ACCESS_UNIT_NOT_FOUND   7
GENIE_SHARED_INVALID_PARAMETER       13
GENIE_SHARED_INVALID_BITSTREAM       14
GENIE_SHARED_UNLISTED_ERROR          15
```

## Example

```c
#include <stdint.h>
#include <stdio.h>

#include "genie/shared/api.h"

int main(void) {
  uint64_t count = 0;
  uint8_t rc = GenieGetAccessUnitCount("input.mgb", &count);
  if (rc != GENIE_SHARED_SUCCESS) {
    fprintf(stderr, "count failed: %s\n", GenieSharedStrerror(rc));
    return 1;
  }

  rc = GenieDecompressAccessUnit("input.mgb", 0, "au0.fastq",
                                 NULL, "tmp", 4);
  if (rc != GENIE_SHARED_SUCCESS) {
    fprintf(stderr, "decompress failed: %s\n", GenieSharedStrerror(rc));
    return 1;
  }

  printf("access units: %llu\n", (unsigned long long)count);
  return 0;
}
```

Memory-output example:

```c
char* fastq = NULL;
uint64_t size = 0;
uint8_t rc = GenieDecompressAccessUnitToFastq(
    "input.mgb", 0, NULL, "tmp", 4, &fastq, &size);
if (rc == GENIE_SHARED_SUCCESS) {
  fwrite(fastq, 1, size, stdout);
  GenieFree(fastq);
}
```

Compile example:

```sh
c++ example.cc -I/mnt/vmshare/genie/src \
  -L/mnt/vmshare/genie/build/lib -lgenie \
  -Wl,-rpath,/mnt/vmshare/genie/build/lib
```

## Python FFI

Python wrapper lives at:

```text
src/genie/shared/genie_ffi.py
```

It uses `cdlml.GLIBCPreloadedCDLL`, so returned C buffers remain valid in the
Python process and can be freed with `GenieFree`.

Install dependency in repo venv:

```sh
~/.pyenv/versions/3.14.4/bin/python3 -m venv venv
venv/bin/python3 -m pip -v install --find-links "https://kamilbur.github.io/cdlml/simple/cdlml" cdlml
```

Example:

```python
from pathlib import Path
import sys

sys.path.insert(0, "src/genie/shared")
from genie_ffi import Genie

genie = Genie("build/lib/libgenie.so")
print(genie.access_unit_count("input.mgb"))

fastq_bytes = genie.decompress_access_unit_to_bytes(
    "input.mgb", 0, working_dir="tmp", threads=4
)
fastq_text = fastq_bytes.decode("utf-8")

genie.decompress_access_unit_to_file(
    "input.mgb", 0, "au0.fastq", working_dir="tmp", threads=4
)
```
