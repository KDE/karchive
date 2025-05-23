<!-- SPDX-FileCopyrightText: 2025 Azhar Momin <azhar.momin@kdemail.net> -->
<!-- SPDX-License-Identifier: CC-BY-SA-4.0 -->

# KArchive OSS-Fuzz Integration

## Fuzzing Locally
Make sure you're using Clang (by setting `CC` and `CXX`), since fuzzing requires it. Then build KArchive with `BUILD_FUZZERS=ON` to generate the fuzzer binaries:
```sh
cmake -B build -DBUILD_FUZZERS=ON
cmake --build build
# Then run one of the fuzzer binaries:
./build/bin/fuzzers/kzip_fuzzer
```

## Testing OSS-Fuzz Integration
Testing OSS-Fuzz integration requires: Python & Docker

First clone the OSS-Fuzz repository:
```sh
git clone https://github.com/google/oss-fuzz.git
```

After navigating to the cloned repository, run the following command to build the fuzzers:
```sh
python3 infra/helper.py build_image karchive
python3 infra/helper.py build_fuzzers --sanitizer address karchive
```

This may take a while since it builds the whole QtBase dependency alongside with zlib, libzstd, liblzma, libcrypto, etc and KArchive itself. Once the build is completed, you can run the fuzzers using the following command:
```sh
python3 infra/helper.py run_fuzzer karchive kzip_fuzzer
```

The code for preparing the build lives in the `prepare_build.sh` script and the code for building the fuzzers lives in the `build_fuzzers.sh` script (which is also responsible for building the dependencies, creating the seed corpus and copying the dict file).

For more information on OSS-Fuzz, visit the [official website](https://google.github.io/oss-fuzz/).

## Integrating New Fuzzers

When you add a new archive format or compression device, you need to add a corresponding fuzzer for it. KArchive uses two main fuzzer types:
- `karchive_fuzzer.cc` for archive formats (like zip, tar, etc)
- `kcompressiondevice_fuzzer.cc` for compression devices (used through `KTar`)

### If You Are Adding a New Archive Format:
- Update `CMakeLists.txt` to include the new fuzzer, following the pattern of existing ones
- If the format needs extra dependencies, update `prepare_build.sh` accordingly
- Update `build_fuzzers.sh` to build those dependencies and include the new fuzzer

### If You Are Adding a New Compression Device:
- Do the same as above, but use `kcompressiondevice_fuzzer.cc` instead of `karchive_fuzzer.cc`
