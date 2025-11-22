# How to Test

This wiki page describes how to build and run tests on Linux. It should eventually provide guidance on how to add tests for a new feature.

## Build Tests

Can be built with the `-t` flag for `build_linux.sh`:

```
build_linux.sh -t
```

(or `-ter` or `-stb` etc).

When running `build_linux.sh` with `-t`, make sure you always include the `-e` or `-b` flag if you built the binary with them, otherwise you'll rebuild all of OrcaSlicer again before the tests are ready.

Test binaries will then appear under `build/tests` or `build-dbginfo/tests` or `build-dbg/tests`. As of this writing, not all tests will be built.

### Faster Test Write-Build-Run Loop

For rebuilding after changes, you can look into `build_linux.sh` for the cmake command which triggers the build and adapt it to running independently. You'll be able to use something like:

```
# Obviously only use the appropriate one
BUILD_CONFIG=Release
BUILD_CONFIG=RelWithDebInfo
cd $BUILD_DIR # build or build-dbginfo probably

cmake --build . --config $BUILD_CONFIG --target tests/all
# or
cmake --build . --config $BUILD_CONFIG --target libslic3r_tests
# etc
```

If you change any CMakeLists.txt file, you'll need to rerun the configuration generation first. Change to the build directory and run `cmake ..` hopefully works without destroying the cache. Rerunning `build_linux.sh -t{eb}` is the most comprehensive way, but then everything has to be rebuilt. You'll need this if you added a new cpp file, for example.

## Run Unit Tests

### Run All

```
cd $BUILD_DIR # build or build-dbginfo probably
ctest --test-dir tests
```

### Run a Specific Set

```
cd $BUILD_DIR # build or build-dbginfo probably
ctest --test-dir tests/slic3rutils
```
