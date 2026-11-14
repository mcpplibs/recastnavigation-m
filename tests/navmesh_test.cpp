// A navigation mesh through import mcpplibs.mwrap.recastnavigation: built by Recast from a floor with a wall
// across it, loaded into Detour, asked for a path around the wall, a straight path, the nearest point and a
// ray along the mesh; a crowd walked to a target through a doorway. The mesh's bytes and the crowd's places
// after the walk are pinned, the same on every platform CI builds.
#include <gtest/gtest.h>

import std;
import mcpplibs.mwrap.recastnavigation;

namespace nav = mcpplibs::mwrap::recastnavigation;

namespace {

// A 20 m square floor at y 0, and a wall 0.4 m thick and 2 m high across x = 0 from z = -10 to z = 6 (a
// doorway from z = 6 to 10), as triangles.
struct Soup {
    std::vector<float> vertices;
    std::vector<int> triangles;
    void quad(std::array<float, 3> a, std::array<float, 3> b, std::array<float, 3> c, std::array<float, 3> d) {
        const int base = static_cast<int>(vertices.size() / 3);
        for (const auto& corner : {a, b, c, d}) vertices.insert(vertices.end(), corner.begin(), corner.end());
        triangles.insert(triangles.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    // A box from `low` to `high`, its faces wound counter-clockwise seen from outside.
    void box(std::array<float, 3> low, std::array<float, 3> high) {
        const auto [x0, y0, z0] = low;
        const auto [x1, y1, z1] = high;
        quad({x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0});  // top
        quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1});  // bottom
        quad({x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0});  // -z
        quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1});  // +z
        quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0});  // -x
        quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1});  // +x
    }
};
Soup scene() {
    Soup soup;
    soup.quad({-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10});
    soup.box({-0.2f, 0, -10}, {0.2f, 2, 6});
    return soup;
}

// The fingerprint of bytes: FNV-1a, 64 bits.
std::uint64_t fnv(std::span<const unsigned char> bytes, std::uint64_t hash = 0xcbf29ce484222325ull) {
    for (const unsigned char byte : bytes) hash = (hash ^ byte) * 0x100000001b3ull;
    return hash;
}
std::uint64_t fnv(std::span<const float> values, std::uint64_t hash = 0xcbf29ce484222325ull) {
    return fnv(std::as_bytes(values).size() ? std::span(reinterpret_cast<const unsigned char*>(values.data()), values.size_bytes()) : std::span<const unsigned char>{}, hash);
}

// Recast's pipeline for one tile, as upstream's solo-mesh sample runs it: the triangles rasterised into a
// heightfield, what an agent cannot stand on or pass under filtered out, regions, contours, polygons and the
// detail mesh; then Detour's data of it.
std::vector<unsigned char> build(const Soup& soup) {
    rcConfig config{};
    config.cs = 0.2f;
    config.ch = 0.1f;
    config.walkableSlopeAngle = 45;
    config.walkableHeight = 20;  // 2 m in cells of 0.1
    config.walkableClimb = 4;    // 0.4 m
    config.walkableRadius = 2;   // 0.4 m in cells of 0.2
    config.maxEdgeLen = 60;
    config.maxSimplificationError = 1.3f;
    config.minRegionArea = 8 * 8;
    config.mergeRegionArea = 20 * 20;
    config.maxVertsPerPoly = 6;
    config.detailSampleDist = 1.2f;
    config.detailSampleMaxError = 0.1f;
    const int vertex_count = static_cast<int>(soup.vertices.size() / 3), triangle_count = static_cast<int>(soup.triangles.size() / 3);
    rcCalcBounds(soup.vertices.data(), vertex_count, config.bmin, config.bmax);
    rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);

    rcContext context(false);
    rcHeightfield* solid = rcAllocHeightfield();
    EXPECT_TRUE(rcCreateHeightfield(&context, *solid, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch));
    std::vector<unsigned char> areas(static_cast<std::size_t>(triangle_count), 0);
    rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, soup.vertices.data(), vertex_count, soup.triangles.data(), triangle_count, areas.data());
    EXPECT_TRUE(rcRasterizeTriangles(&context, soup.vertices.data(), vertex_count, soup.triangles.data(), areas.data(), triangle_count, *solid, config.walkableClimb));
    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *solid);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *solid);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *solid);
    rcCompactHeightfield* compact = rcAllocCompactHeightfield();
    EXPECT_TRUE(rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *solid, *compact));
    rcFreeHeightField(solid);
    EXPECT_TRUE(rcErodeWalkableArea(&context, config.walkableRadius, *compact));
    EXPECT_TRUE(rcBuildDistanceField(&context, *compact));
    EXPECT_TRUE(rcBuildRegions(&context, *compact, 0, config.minRegionArea, config.mergeRegionArea));
    rcContourSet* contours = rcAllocContourSet();
    EXPECT_TRUE(rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours));
    rcPolyMesh* mesh = rcAllocPolyMesh();
    EXPECT_TRUE(rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *mesh));
    rcPolyMeshDetail* detail = rcAllocPolyMeshDetail();
    EXPECT_TRUE(rcBuildPolyMeshDetail(&context, *mesh, *compact, config.detailSampleDist, config.detailSampleMaxError, *detail));
    rcFreeCompactHeightfield(compact);
    rcFreeContourSet(contours);
    for (int poly = 0; poly < mesh->npolys; ++poly)
        if (mesh->areas[poly] == nav::RC_WALKABLE_AREA) mesh->flags[poly] = 1;

    dtNavMeshCreateParams params{};
    params.verts = mesh->verts;
    params.vertCount = mesh->nverts;
    params.polys = mesh->polys;
    params.polyAreas = mesh->areas;
    params.polyFlags = mesh->flags;
    params.polyCount = mesh->npolys;
    params.nvp = mesh->nvp;
    params.detailMeshes = detail->meshes;
    params.detailVerts = detail->verts;
    params.detailVertsCount = detail->nverts;
    params.detailTris = detail->tris;
    params.detailTriCount = detail->ntris;
    params.walkableHeight = 2.0f;
    params.walkableRadius = 0.4f;
    params.walkableClimb = 0.4f;
    std::copy_n(mesh->bmin, 3, params.bmin);
    std::copy_n(mesh->bmax, 3, params.bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;
    unsigned char* data = nullptr;
    int size = 0;
    EXPECT_TRUE(dtCreateNavMeshData(&params, &data, &size));
    rcFreePolyMesh(mesh);
    rcFreePolyMeshDetail(detail);
    std::vector<unsigned char> out(data, data + size);
    dtFree(data);
    return out;
}

// The mesh loaded: Detour takes a copy it frees.
dtNavMesh* load(const std::vector<unsigned char>& bytes) {
    dtNavMesh* mesh = dtAllocNavMesh();
    auto* data = static_cast<unsigned char*>(dtAlloc(static_cast<int>(bytes.size()), DT_ALLOC_PERM));
    std::ranges::copy(bytes, data);
    EXPECT_TRUE(dtStatusSucceed(mesh->init(data, static_cast<int>(bytes.size()), DT_TILE_FREE_DATA)));
    return mesh;
}

}  // namespace

TEST(Navmesh, BuiltQueriedWalked) {
    const std::vector<unsigned char> bytes = build(scene());
    ASSERT_GT(bytes.size(), 0u);
    EXPECT_EQ(bytes, build(scene())) << "the same triangles, the same mesh";
    dtNavMesh* mesh = load(bytes);
    dtNavMeshQuery* query = dtAllocNavMeshQuery();
    ASSERT_TRUE(dtStatusSucceed(query->init(mesh, 2048)));
    dtQueryFilter filter;
    const float extents[3] = {1, 2, 1};

    // From one side of the wall to the other: around its end, through the doorway.
    const float start[3] = {-5, 0, -5}, end[3] = {5, 0, -5};
    dtPolyRef start_ref = 0, end_ref = 0;
    float start_at[3], end_at[3];
    ASSERT_TRUE(dtStatusSucceed(query->findNearestPoly(start, extents, &filter, &start_ref, start_at)));
    ASSERT_TRUE(dtStatusSucceed(query->findNearestPoly(end, extents, &filter, &end_ref, end_at)));
    ASSERT_NE(start_ref, 0u);
    std::array<dtPolyRef, 256> polys{};
    int poly_count = 0;
    ASSERT_TRUE(dtStatusSucceed(query->findPath(start_ref, end_ref, start_at, end_at, &filter, polys.data(), &poly_count, 256)));
    std::array<float, 3 * 32> corners{};
    int corner_count = 0;
    ASSERT_TRUE(dtStatusSucceed(query->findStraightPath(start_at, end_at, polys.data(), poly_count, corners.data(), nullptr, nullptr, &corner_count, 32)));
    float through = -100;
    for (int corner = 0; corner < corner_count; ++corner) through = std::max(through, corners[3 * corner + 2]);
    EXPECT_GE(corner_count, 3) << "it turns at the doorway";
    EXPECT_GT(through, 6.0f) << "the path passes the wall's end at z 6";

    // A ray along the mesh straight at the wall stops before it.
    float hit = 0, normal[3];
    dtPolyRef ray_polys[64];
    int ray_count = 0;
    ASSERT_TRUE(dtStatusSucceed(query->raycast(start_ref, start_at, end_at, &filter, &hit, normal, ray_polys, &ray_count, 64)));
    EXPECT_GT(hit, 0.4f);
    EXPECT_LT(hit, 0.5f) << "the wall (less the agent's radius) is a little under half way";

    // A crowd of four walked to the far side; then their places, pinned.
    dtCrowd* crowd = dtAllocCrowd();
    ASSERT_TRUE(crowd->init(8, 0.4f, mesh));
    dtCrowdAgentParams agent{};
    agent.radius = 0.4f;
    agent.height = 2;
    agent.maxAcceleration = 8;
    agent.maxSpeed = 3.5f;
    agent.collisionQueryRange = agent.radius * 12;
    agent.pathOptimizationRange = agent.radius * 30;
    agent.separationWeight = 2;
    agent.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE | DT_CROWD_SEPARATION;
    agent.obstacleAvoidanceType = 3;
    for (int index = 0; index < 4; ++index) {
        const float at[3] = {-5, 0, -6.0f + static_cast<float>(index)};
        ASSERT_EQ(crowd->addAgent(at, &agent), index);
    }
    for (int index = 0; index < 4; ++index) EXPECT_TRUE(crowd->requestMoveTarget(index, end_ref, end_at));
    for (int step = 0; step < 600; ++step) crowd->update(1.0f / 60, nullptr);
    std::vector<float> places;
    for (int index = 0; index < 4; ++index) {
        const dtCrowdAgent* walker = crowd->getAgent(index);
        places.insert(places.end(), walker->npos, walker->npos + 3);
        EXPECT_LT(dtVdist2D(walker->npos, end_at), 2.5f) << index << " reached the far side (four of radius 0.4 crowd the target)";
    }
    std::printf("mesh %zu bytes %016llx, crowd %016llx\n", bytes.size(), static_cast<unsigned long long>(fnv(bytes)), static_cast<unsigned long long>(fnv(places)));
    EXPECT_EQ(fnv(bytes), 0xd50409b3475ce9afull) << "the mesh's bytes";
    EXPECT_EQ(fnv(places), 0x040f1c330fce843bull) << "the crowd's places";

    dtFreeCrowd(crowd);
    dtFreeNavMeshQuery(query);
    dtFreeNavMesh(mesh);
}

TEST(Navmesh, Constants) {
    static_assert(nav::DT_VERTS_PER_POLYGON == 6);
    static_assert(nav::RC_WALKABLE_AREA == 63);
    static_assert(nav::DT_SUCCESS == 1u << 30);
    EXPECT_EQ(nav::RC_PI, 3.14159265f);
    EXPECT_TRUE(dtStatusFailed(nav::DT_FAILURE));
}
