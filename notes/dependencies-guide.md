# Where someone else's library comes from

*A from-first-principles guide to `find_package`, `FetchContent`, and the imported targets they hand you.*

This is a teaching document, not project documentation. It explains the two ways a CMake build acquires code it did not write, what an installed package actually consists of, and what changes when the dependency's own build system runs inside yours. The running example is this repository's single dependency — doctest — which is found if it is installed and downloaded if it is not. It assumes [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md), because everything a dependency gives you arrives as a usage requirement on a target.

**The shape of this document:**

- **§1** — the three ways to get code, and what each really trades.
- **§2** — what `find_package` finds: config packages, imported targets, and the file that creates them.
- **§3** — what `FetchContent` does differently, and the trap that follows from it.
- **§4–§5** — the hybrid this repo uses, and how a version request is answered.
- **§6** — four exercises, one of which reproduces a real breakage.
- **§7–§8** — the model in a paragraph, and where to go next.

---

## 1. Three ways to get code

**Installed on the machine.** Someone — a package manager, a `make install` — put headers and libraries in a system location, along with a small amount of CMake code describing them. `find_package(doctest)` looks for that description. Fast, no network, one copy shared between all your projects; but the version is whatever the machine has, and a colleague's machine may have another.

**Fetched by the build.** `FetchContent` downloads a pinned revision at configure time and builds it as part of your project. Every machine gets exactly the same version, and it works on a fresh CI runner with nothing pre-installed; but it needs a network on first configure, and the dependency's own CMake code now runs inside your build with all that implies (§3).

**Vendored.** The source is committed into your repository and added with `add_subdirectory`. Complete control and no network at all, at the price of carrying someone else's code in your history and updating it by hand. Common for single-header libraries, and a perfectly reasonable third answer that this repo does not use.

None dominates. The question to ask is who is expected to have what: a developer machine that can be set up once (installed), a CI runner that must be reproducible (fetched), or an air-gapped build (vendored).

## 2. What `find_package` actually finds

`find_package(doctest 2.5 QUIET)` looks, in this order, for two quite different things.

**Module mode** searches for `Finddoctest.cmake` — a finder written by *someone else*, usually CMake itself, that goes hunting for headers and libraries with `find_path` and `find_library`. It exists for dependencies that know nothing about CMake, like zlib.

**Config mode** searches for `doctestConfig.cmake` — a file shipped *by doctest*, written by its own build, in a known location such as `lib/cmake/doctest/`. When a project ships one, this is the good path: nobody is guessing where anything is, because the package described itself when it was installed.

The config file is small and worth reading in full, because it demystifies the whole mechanism:

```cmake
if(NOT TARGET doctest::doctest)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
    include("${CMAKE_CURRENT_LIST_DIR}/doctestTargets.cmake")
endif()
```

Two lines of work. The `include` creates an **imported target** — a target that describes something already built rather than something to build:

```cmake
add_library(doctest::doctest INTERFACE IMPORTED)
set_target_properties(doctest::doctest PROPERTIES
    INTERFACE_COMPILE_FEATURES "cxx_std_11"
    INTERFACE_INCLUDE_DIRECTORIES "${_IMPORT_PREFIX}/include")
```

That is the entire dependency: an `INTERFACE` target (nothing to compile — it is a header) carrying an include directory and a language requirement, both of which propagate to whoever links it. This is [`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) §2's table with the first column empty, and it is why `target_link_libraries(tests_rgbw PRIVATE doctest::doctest)` is all that is needed even though nothing is being *linked* in the ordinary sense.

The other line — `list(APPEND CMAKE_MODULE_PATH ...)` — is why `include(doctest)` works afterwards. The package ships a *helper script* alongside the target definition, and adds its own directory to the module search path so that a bare `include(doctest)` finds it. `doctest_discover_tests` comes from there; see [`testing-guide.md`](testing-guide.md) §4 for what it does.

## 3. What `FetchContent` does differently

```cmake
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.5.3
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(doctest)
```

`MakeAvailable` downloads the revision into `_deps/doctest-src` and then calls `add_subdirectory` on it. That second half is the part people underestimate: **the dependency's `CMakeLists.txt` now executes inside your project.** Its targets appear in your build, its options become your cache variables, its `project()` call runs, and — the one that bites — its `cmake_minimum_required` is evaluated by your CMake.

That last point is a live trap, not a hypothetical. CMake 4 removed compatibility with `cmake_minimum_required(VERSION <3.5)`, so a dependency pinned to a tag from a few years ago fails before it compiles a line:

```
CMake Error at _deps/doctest-src/CMakeLists.txt:1 (cmake_minimum_required):
  Compatibility with CMake < 3.5 has been removed from CMake.
```

doctest `v2.5.3` declares 3.14 and is fine; older doctest tags declare 3.0 and are not. The escape hatch (`-DCMAKE_POLICY_VERSION_MINIMUM=3.5`) exists and is worth knowing, but the durable fix is to pin a tag new enough to declare a supported minimum.

Pin a **tag or commit**, never a branch: `GIT_TAG main` makes your build's reproducibility depend on someone else's push. `GIT_SHALLOW TRUE` skips the history, which is most of the download.

## 4. The hybrid, and why CI takes the other branch

This repo does both:

```cmake
find_package(doctest 2.5 QUIET)
if(NOT doctest_FOUND)
    include(FetchContent)
    FetchContent_Declare(doctest ... GIT_TAG v2.5.3 ...)
    FetchContent_MakeAvailable(doctest)
    list(APPEND CMAKE_MODULE_PATH ${doctest_SOURCE_DIR}/scripts/cmake)
endif()
include(doctest)
```

A developer with doctest installed configures in a second with no network. A machine without it downloads a pinned copy. Both paths end with `doctest::doctest` defined and `doctest_discover_tests` available — the first because the config file appended its own directory to `CMAKE_MODULE_PATH` (§2), the second because the `if` branch appends the equivalent directory from the downloaded source. The single `include(doctest)` afterwards is what makes the two branches converge instead of duplicating the rest of the file.

The CI runners have no doctest installed, which means **CI exercises the fallback path on every run** while local development exercises the installed one. That is worth arranging deliberately: a fallback nobody takes is a fallback nobody knows is broken.

`QUIET` matters here. Without it, a machine with no doctest gets a scary "could not find" message during a configure that then succeeds anyway — training people to ignore CMake output, which is a bad habit to teach.

## 5. How a version request is answered

`find_package(doctest 2.5)` does not mean "exactly 2.5", and CMake does not decide what it means. The package ships a second file, `doctestConfigVersion.cmake`, whose job is to answer one question: *given that you have 2.5.3 and they asked for 2.5, is that acceptable?* It sets `PACKAGE_VERSION_COMPATIBLE` and CMake obeys.

doctest's is generated with the "same major version" policy: any version at least the one requested, provided the major number matches. So 2.5.3 satisfies a request for 2.5 or for 2.4, and fails a request for 2.6 — or for 1.0, where "any newer version" would have said yes. The practical consequence for the hybrid above is neat: asking for a version the machine does not have is not an error, it is simply `doctest_FOUND` staying false — and the fetch branch takes over.

---

## 6. Exercises

Four, on the host. Two need a network the first time.

### Exercise 1 — Watch `find_package` search

*Demonstrates §2: module mode, then config mode.*

```sh
cmake -S . -B /tmp/dbg --debug-find-pkg=doctest 2>&1 | head -12
```

```
find_package considered the following paths for Finddoctest.cmake:
  /opt/homebrew/share/cmake/Modules/Finddoctest.cmake
...
find_package considered the following locations for doctest's Config
```

Then read what it eventually found:

```sh
cat /opt/homebrew/lib/cmake/doctest/doctestConfig.cmake
grep -A4 'add_library' /opt/homebrew/lib/cmake/doctest/doctestTargets.cmake
```

**Proves:** the dependency is four lines of CMake and an imported `INTERFACE` target carrying an include directory. There is nothing else to it — no library to link, no magic.

### Exercise 2 — Force the fallback and compare

*Demonstrates §4: the path CI takes.*

```sh
rm -rf /tmp/fc && cmake -S . -B /tmp/fc -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_doctest=ON
ls /tmp/fc/_deps
cmake --build /tmp/fc >/dev/null && ctest --test-dir /tmp/fc -j | grep 'tests passed'
```

```
doctest-build  doctest-src  doctest-subbuild
100% tests passed out of …
```

The count is the same as in your normal build directory — every case, once per configuration.

**Proves:** the two acquisition paths are interchangeable from the build's point of view — same target name, same results, case for case — and `CMAKE_DISABLE_FIND_PACKAGE_<name>` lets you rehearse the CI path locally without uninstalling anything.

### Exercise 3 — Ask for a version that does not exist

*Demonstrates §5.*

In the root `CMakeLists.txt`, change the call to `find_package(doctest 99 QUIET)`:

```sh
rm -rf /tmp/v99 && cmake -S . -B /tmp/v99 -G Ninja | tail -2
ls /tmp/v99/_deps
```

The configure succeeds, and `_deps/` shows it downloaded doctest instead. **Revert it.**

**Proves:** a version request is answered by the package's own version file, and a "no" is not an error — it is `doctest_FOUND` staying false, which is exactly what the hybrid's `if` is testing.

### Exercise 4 — Reproduce the CMake 4 trap

*Demonstrates §3: the dependency's CMake code runs in your build.*

Change `GIT_TAG v2.5.3` to `GIT_TAG v2.4.11`, then force the fetch path:

```sh
rm -rf /tmp/old && cmake -S . -B /tmp/old -G Ninja -DCMAKE_DISABLE_FIND_PACKAGE_doctest=ON
```

```
CMake Error at /tmp/old/_deps/doctest-src/CMakeLists.txt:1 (cmake_minimum_required):
  Compatibility with CMake < 3.5 has been removed from CMake.
  ...
  Or, add -DCMAKE_POLICY_VERSION_MINIMUM=3.5 to try configuring anyway.
```

**Revert it.** Note the error is in *their* file, at line 1, reported by *your* configure.

**Proves:** `FetchContent_MakeAvailable` is `add_subdirectory` on someone else's project, so their minimum-version declaration, their options and their targets all become facts about your build. Pinning an old tag pins their CMake requirements too.

---

## 7. The model in one paragraph

A CMake build acquires outside code in one of three ways — installed and found, downloaded and built in place, or vendored into the repository — and the choice is really about who must be able to build you: a set-up developer machine, a bare CI runner, or a machine with no network. `find_package` prefers *config mode*, where the dependency ships a small CMake file describing itself; that file creates an **imported target** whose usage requirements — an include directory, a language feature, sometimes a helper script added to `CMAKE_MODULE_PATH` — are the entire dependency as far as your build is concerned. `FetchContent` instead downloads a pinned revision and calls `add_subdirectory` on it, so the dependency's own CMake code executes inside yours: its targets, its options and its `cmake_minimum_required` all become yours, which is why pinning an old tag can fail on a new CMake before anything compiles. Doing both — find first, fetch if absent — gives fast offline configures locally and reproducible ones in CI, provided the two branches converge on the same target name and the same helper, and provided somebody actually exercises the fallback.

## 8. Where to go next

- **[`cmake-usage-requirements-guide.md`](cmake-usage-requirements-guide.md) §2–§3** — what an imported target's `INTERFACE_*` properties are, in the general form.
- **[`testing-guide.md`](testing-guide.md) §4** — what the helper script from §2 does once you have it.
- **The root `CMakeLists.txt`, the top of the `STRIPSEQ_BUILD_TESTS` block** — the lines before `enable_testing()`, which contain every idea in this guide.
- **`/opt/homebrew/lib/cmake/doctest/`** (or wherever your package manager puts it) — five short files: the config, the version file, the targets, and the two helper scripts, `doctest.cmake` and the `doctestAddTests.cmake` it runs after each link. Reading them once removes most of the mystery from `find_package` permanently.
- **CMake's [`cmake-packages(7)`](https://cmake.org/cmake/help/latest/manual/cmake-packages.7.html)** — the reference half, including how to *ship* a config package.
