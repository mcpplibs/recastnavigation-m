# recastnavigation

> Recast & Detour（游戏用的导航网格工具集：由三角形构建导航网格、寻路、群体、带障碍物的分块网格）的 C++23 模块封装。

[English](README.md) · **简体中文** · [Upstream](https://github.com/recastnavigation/recastnavigation) · [Releases](https://github.com/mcpplibs/recastnavigation-m/releases) · [mcpp](https://github.com/mcpp-community/mcpp) · [Index](https://github.com/mcpplibs/mcpp-index)

[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue.svg)](LICENSE)
[![mcpp-community](https://img.shields.io/badge/mcpp--community-mwrap-blue)](https://github.com/mcpp-community)
[![CI](https://github.com/mcpplibs/recastnavigation-m/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/mcpplibs/recastnavigation-m/actions/workflows/ci.yml)

上游的 C++ API 保持原样：`rcConfig`、`rcBuildPolyMesh`、`dtNavMesh`、`dtNavMeshQuery`、`dtCrowd`、`dtTileCache`
以及其他公开名字都是上游的实体，在上游声明它们的全局命名空间里。四个库（Recast、Detour、DetourCrowd、
DetourTileCache）逐字节取自 1.6.0 发布包，构建时不把 `a*b+c` 合并为 FMA、不用快速数学（`-ffp-contract=off`、
`-fno-fast-math`），且每种配置都相同（`NDEBUG`：不打开上游的断言）：同样的三角形给出同样的网格字节，同样的
群体给出同样的位置（测试中钉住哈希，CI 每一行都比对）。

## Modules

- `import mcpplibs.mwrap.recastnavigation;`：四个库头文件的全部公开名字——99 个类型、75 个非限定作用域枚举的枚举值、170 个函数（模板与重载各为一个名字）

> More Modules API -> [上游 README](upstream/recastnavigation/README.md)

## 快速开始

```sh
mcpp new path --template mcpplibs.mwrap.recastnavigation@1.6.0
cd path
mcpp run
```

默认模板由一块地面和横在其上的一堵墙构建导航网格，并打印从墙的一侧绕过墙端到另一侧的路线。

```toml
[dependencies]
mcpplibs.mwrap.recastnavigation = "1.6.0"

# 各平台得到同样的路线：Detour 的内联数学也在你的编译单元中编译。
[build]
cxxflags = ["-ffp-contract=off"]
```

```cpp
import std;
import mcpplibs.mwrap.recastnavigation;

int main() {
    rcContext context(false);
    rcHeightfield* solid = rcAllocHeightfield();   // rcCreateHeightfield、rcRasterizeTriangles … rcBuildPolyMesh
    dtNavMesh* mesh = dtAllocNavMesh();            // 用 dtCreateNavMeshData 的字节 init
    dtNavMeshQuery* query = dtAllocNavMeshQuery(); // findNearestPoly、findPath、findStraightPath、raycast
    dtFreeNavMeshQuery(query), dtFreeNavMesh(mesh), rcFreeHeightField(solid);
}
```

## 常量与宏

命名空间作用域的 52 个 `static const` 对象具有内部链接（每个包含者各有一份），模块无法导出。它们以原名、
原类型和原值放在 `mcpplibs::mwrap::recastnavigation` 中——`DT_VERTS_PER_POLYGON`、`DT_SUCCESS`、`DT_FAILURE`、
`DT_IN_PROGRESS`、`DT_PARTIAL_RESULT`、`DT_OFFMESH_CON_BIDIR`、`DT_MAX_AREAS`、`DT_CROWDAGENT_MAX_NEIGHBOURS`、
`RC_WALKABLE_AREA`、`RC_NULL_AREA`、`RC_PI` …… 状态辅助函数（`dtStatusSucceed`、`dtStatusFailed`、
`dtStatusDetail` ……）是函数，照常导出。

上游的宏是断言（`rcAssert`、`dtAssert`）、分支提示与 `RC_SIZE_MAX`，只在其自身源码中使用：没有 `.macros`
模块。64 位多边形引用（`DT_POLYREF64`）与虚函数查询过滤器（`DT_VIRTUAL_QUERYFILTER`）取上游默认：关闭。

## 封装范围

- 已封装：Recast、Detour、DetourCrowd、DetourTileCache 头文件的全部名字（共 396 个）。`tools/surface.py` 按 clang 读到的头文件推导出该列表。
- 不在此：DebugUtils（通过调试渲染器的回调绘制网格）、演示程序（RecastDemo）与上游测试。断言钩子（`rcAssertFailSetCustom`、`dtAssertFailSetCustom`）只在未定义 `NDEBUG` 的构建中存在；本包在每种配置都定义它。
- 分块缓存需要一个压缩器：`dtTileCacheCompressor` 是由程序实现的接口（上游演示用的 FastLZ 未收录）。
- 群体的自适应速度采样调用 `cosf` 与 `sinf`：这一处的结果取决于平台的 C 库。

## 验证

- `mcpp test`：由地面与墙构建导航网格，两次得到相同字节；绕墙的路径、其直线路径、沿网格被墙挡住的射线；四个智能体组成的群体穿过门洞走到另一侧；钉住网格字节与群体位置的哈希；改写的常量。
- `python3 tools/check_exports.py`：模块与 `tools/surface.json` 完全一致；给定 clang++（CI 的 LLVM 行必需）时再次从头文件推导记录并比对。
- `python3 tools/test_template.py`：从安装副本 `mcpp new`、严格构建并运行生成的项目。
- CI：Linux x86-64 与 arm64（GCC 16、LLVM 23），macOS 与 Windows（LLVM 23），各在 dev 与 release 配置下，以及 node 下的 wasm32——每一行都是同样的钉住哈希。

## 许可

封装为 Apache-2.0（mcpp-community）。Recast & Detour 为 zlib，© 2009 Mikko Mononen。上游的 README 与 License.txt
保存在 `upstream/recastnavigation/`，随程序的许可一并部署。来源：`upstream/documents.json`。
