# cxx-frontend

A snapshot of the parser library from Roberto Raggi's C++ frontend,
https://github.com/robertoraggi/cplusplus.

This is the successor to the front end in `../cplusplus`, which Qt Creator has
used since 2008. It is vendored here so that the two can be compared and the
built-in code model can be moved over piece by piece. Nothing in Qt Creator
depends on it yet; the target is built only when `QTC_ENABLE_CXX_FRONTEND` is
on, and the only consumer so far is `tests/auto/cxxfrontend`, which measures
how the new parser copes with the corpora of the existing
`tests/auto/cplusplus` suites.

## Provenance

| | |
|---|---|
| Upstream | https://github.com/robertoraggi/cplusplus |
| Revision | a35126486c245a96f6212a55f91cdbf2753b4847 (2026-09-09) |
| Imported from | `src/parser/cxx` |
| License | MIT, see `LICENSE` |

## Differences from upstream

The tree under `cxx/` is an unmodified copy. Only the build description
differs: `CMakeLists.txt` here replaces upstream's `src/parser/CMakeLists.txt`,
and it leaves out `cxx/flatbuffers/` — AST serialization is not needed, so the
library is compiled with `CXX_NO_FLATBUFFERS`.

Keeping `cxx/` free of local edits is deliberate: it is what makes re-importing
a newer upstream a plain copy. Fixes belong upstream, not here.

## Updating

    scripts/updateCxxFrontend.sh <path-to-cplusplus-checkout>

The script copies the sources, refreshes the revision recorded above, and
prints the file list to add to `CMakeLists.txt` and `cxx-frontend.qbs` if
upstream gained or lost a source file.

## Requirements

The library needs C++23, while the rest of Qt Creator is built as C++20. The
standard is raised for this target alone, through
`target_compile_features(cxx-frontend PUBLIC cxx_std_23)`.
