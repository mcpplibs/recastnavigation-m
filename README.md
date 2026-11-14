# recastnavigation

> Recast & Detour, the navigation-mesh toolset for games (navmeshes built from triangles, path finding, crowds, tiled meshes with obstacles), through C++23 modules.

**English** · [简体中文](README.zh.md) · [Upstream](https://github.com/recastnavigation/recastnavigation) · [Releases](https://github.com/mcpplibs/recastnavigation-m/releases) · [mcpp](https://github.com/mcpp-community/mcpp) · [Index](https://github.com/mcpplibs/mcpp-index)

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![mcpp-community](https://img.shields.io/badge/mcpp--community-mwrap-blue)](https://github.com/mcpp-community)
[![CI](https://github.com/mcpplibs/recastnavigation-m/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpplibs/recastnavigation-m/actions/workflows/ci.yml)

The upstream C++ API is unchanged: `rcConfig`, `rcBuildPolyMesh`, `dtNavMesh`,
`dtNavMeshQuery`, `dtCrowd`, `dtTileCache` and every other public name are the
upstream entities, in the global namespace where upstream declares them. The
four libraries (Recast, Detour, DetourCrowd, DetourTileCache) are vendored byte
for byte from the 1.6.0 release and built with no contraction of `a*b+c` into
an FMA and no fast math (`-ffp-contract=off`, `-fno-fast-math`), the same in
every profile (`NDEBUG`: upstream's asserts off): the same triangles give the
same mesh bytes, and the same crowd the same places (a pinned hash in the
tests, compared on every CI row).

## Modules

- `import mcpplibs.mwrap.recastnavigation;`: every public name of the four libraries' headers — 99 types, 75 enumerators of unscoped enumerations and 170 functions (templates and overloads under one name each)

> More Modules API -> [upstream's README](upstream/recastnavigation/README.md)

## Getting started

```sh
mcpp new path --template mcpplibs.mwrap.recastnavigation@1.6.0
cd path
mcpp run
```

The default template builds a navigation mesh from a floor with a wall across
it and prints the way from one side of the wall to the other, around its end.

```toml
[dependencies]
mcpplibs.mwrap.recastnavigation = "1.6.0"

# For the same path everywhere: Detour's inline math is compiled in your units too.
[build]
cxxflags = ["-ffp-contract=off"]
```

```cpp
import std;
import mcpplibs.mwrap.recastnavigation;

int main() {
    rcContext context(false);
    rcHeightfield* solid = rcAllocHeightfield();   // rcCreateHeightfield, rcRasterizeTriangles … rcBuildPolyMesh
    dtNavMesh* mesh = dtAllocNavMesh();            // init with dtCreateNavMeshData's bytes
    dtNavMeshQuery* query = dtAllocNavMeshQuery(); // findNearestPoly, findPath, findStraightPath, raycast
    dtFreeNavMeshQuery(query), dtFreeNavMesh(mesh), rcFreeHeightField(solid);
}
```

## Constants and macros

Fifty-two `static const` objects at namespace scope have internal linkage
(each includer has its own) and a module cannot export them. They are in
`mcpplibs::mwrap::recastnavigation` under their names, with their types and
values — `DT_VERTS_PER_POLYGON`, `DT_SUCCESS`, `DT_FAILURE`, `DT_IN_PROGRESS`,
`DT_PARTIAL_RESULT`, `DT_OFFMESH_CON_BIDIR`, `DT_MAX_AREAS`,
`DT_CROWDAGENT_MAX_NEIGHBOURS`, `RC_WALKABLE_AREA`, `RC_NULL_AREA`, `RC_PI` …
The status helpers (`dtStatusSucceed`, `dtStatusFailed`, `dtStatusDetail` …) are
functions and exported.

Upstream's macros are its asserts (`rcAssert`, `dtAssert`), branch hints and
`RC_SIZE_MAX`, spelled by its own sources only: there is no `.macros` module.
The 64-bit polygon references (`DT_POLYREF64`) and the virtual query filter
(`DT_VIRTUAL_QUERYFILTER`) are upstream's defaults: off.

## What is and is not wrapped

- Wrapped: every name of the headers of Recast, Detour, DetourCrowd and DetourTileCache (396 in all). `tools/surface.py` derives the list from the headers as clang reads them.
- Not here: DebugUtils (drawing a mesh through a debug renderer's callbacks), the demo (RecastDemo) and upstream's tests. The assert hooks (`rcAssertFailSetCustom`, `dtAssertFailSetCustom`) exist only in a build without `NDEBUG`; the package builds with it in every profile.
- A tile cache needs a compressor: `dtTileCacheCompressor` is an interface the program implements (upstream's demo uses FastLZ, which is not vendored).
- The crowd's adaptive velocity sampling calls `cosf` and `sinf`: its results follow the platform's C library there.

## Verification

- `mcpp test`: a navmesh built from a floor and a wall, the same bytes twice; a path around the wall, its straight path, a ray along the mesh stopped by the wall; a crowd of four walked through the doorway to the far side; the mesh's bytes and the crowd's places pinned; the adapted constants.
- `python3 tools/check_exports.py`: the module is exactly `tools/surface.json`; given a clang++ (required on CI's LLVM row) the record is derived again from the headers and compared.
- `python3 tools/test_template.py`: `mcpp new` from an installed copy, a strict build, and a run of the generated project.
- CI: Linux x86-64 and arm64 (GCC 16, LLVM 23), macOS and Windows (LLVM 23), each in the dev and release profiles, and wasm32 under node — the same pinned hashes on every row.

## License

The wrapper is Apache-2.0 (mcpp-community). Recast & Detour is zlib, © 2009
Mikko Mononen. Upstream's README and License.txt are kept in
`upstream/recastnavigation/` and deployed with a program's licenses.
Provenance: `upstream/documents.json`.
