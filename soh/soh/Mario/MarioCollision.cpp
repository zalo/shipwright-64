#ifdef __EMSCRIPTEN__

#include "MarioCollision.h"

#include <stdio.h>
#include <vector>

extern "C" {
#include "z64.h"
#include "z64bgcheck.h"
#include "macros.h"
}

// ---------------------------------------------------------------------------
// Surface type mapping: OoT surface material → SM64 terrain/surface type
// ---------------------------------------------------------------------------

// OoT surface material values (lower byte of SurfaceType.data[0])
// from z64bgcheck.h / scene documentation
enum OotSurfaceMat : uint8_t {
    OOT_MAT_NORMAL   = 0x00,
    OOT_MAT_SAND     = 0x02,
    OOT_MAT_STONE    = 0x03,
    OOT_MAT_SHALLOW  = 0x09, // shallow water
    OOT_MAT_WATER    = 0x0F, // deep water
    OOT_MAT_LAVA     = 0x05,
    OOT_MAT_ICE      = 0x0B,
    OOT_MAT_SNOW     = 0x0C,
};

// SM64 surface types (from decomp include/surface_terrains.h)
enum Sm64SurfaceType : int16_t {
    SM64_SURFACE_DEFAULT          = 0x0000,
    SM64_SURFACE_BURNING          = 0x0001,
    SM64_SURFACE_WATER            = 0x000D,
    SM64_SURFACE_FLOWING_WATER    = 0x000E,
    SM64_SURFACE_SHALLOW_WATER    = 0x0029,
    SM64_SURFACE_ICE              = 0x00B0,
    SM64_SURFACE_DEATH_PLANE      = 0x001A,
    SM64_SURFACE_HARD             = 0x0001,
    SM64_SURFACE_NOISE_DEFAULT    = 0x004C,
};

// SM64 terrain types
enum Sm64Terrain : uint16_t {
    SM64_TERRAIN_GRASS   = 0x0000,
    SM64_TERRAIN_STONE   = 0x0001,
    SM64_TERRAIN_SNOW    = 0x0002,
    SM64_TERRAIN_SAND    = 0x0003,
    SM64_TERRAIN_SPOOKY  = 0x0004,
    SM64_TERRAIN_WATER   = 0x0005,
    SM64_TERRAIN_SLIDE   = 0x0006,
};

static uint16_t MapTerrain(uint8_t mat) {
    switch (mat) {
        case OOT_MAT_SAND:    return SM64_TERRAIN_SAND;
        case OOT_MAT_STONE:   return SM64_TERRAIN_STONE;
        case OOT_MAT_SNOW:    return SM64_TERRAIN_SNOW;
        case OOT_MAT_SHALLOW:
        case OOT_MAT_WATER:   return SM64_TERRAIN_WATER;
        default:              return SM64_TERRAIN_GRASS;
    }
}

static int16_t MapSurfaceType(uint8_t mat) {
    switch (mat) {
        case OOT_MAT_LAVA:    return SM64_SURFACE_BURNING;
        case OOT_MAT_ICE:     return SM64_SURFACE_ICE;
        case OOT_MAT_SHALLOW: return SM64_SURFACE_SHALLOW_WATER;
        case OOT_MAT_WATER:   return SM64_SURFACE_WATER;
        default:              return SM64_SURFACE_DEFAULT;
    }
}

// ---------------------------------------------------------------------------
// Append all triangles from a CollisionHeader into the surface list
// ---------------------------------------------------------------------------

void MarioCollision::AppendFromHeader(std::vector<SM64Surface>& out,
                                      CollisionHeader* header) {
    if (!header || header->numPolygons <= 0 || !header->vertices || !header->polygons) {
        return;
    }

    out.reserve(out.size() + (size_t)header->numPolygons);

    for (int i = 0; i < header->numPolygons; i++) {
        CollisionPoly* poly = &header->polygons[i];

        // Vertex indices are in the lower 13 bits of vtxData
        int vi0 = poly->vtxData[0] & 0x1FFF;
        int vi1 = poly->vtxData[1] & 0x1FFF;
        int vi2 = poly->vtxData[2] & 0x1FFF;

        if (vi0 >= header->numVertices ||
            vi1 >= header->numVertices ||
            vi2 >= header->numVertices) {
            continue; // corrupt polygon, skip
        }

        Vec3s* v0 = &header->vertices[vi0];
        Vec3s* v1 = &header->vertices[vi1];
        Vec3s* v2 = &header->vertices[vi2];

        // Determine surface material from the scene's SurfaceType list
        uint8_t mat = 0;
        if (header->surfaceTypeList && poly->type < 0x1FF) {
            SurfaceType* st = &header->surfaceTypeList[poly->type & 0xFF];
            mat = (uint8_t)(st->data[0] & 0xFF);
        }

        SM64Surface surf{};
        surf.type    = MapSurfaceType(mat);
        surf.force   = 0;
        surf.terrain = MapTerrain(mat);

        // SM64Surface.vertices is int32_t[3][3]
        surf.vertices[0][0] = (int32_t)v0->x;
        surf.vertices[0][1] = (int32_t)v0->y;
        surf.vertices[0][2] = (int32_t)v0->z;
        surf.vertices[1][0] = (int32_t)v1->x;
        surf.vertices[1][1] = (int32_t)v1->y;
        surf.vertices[1][2] = (int32_t)v1->z;
        surf.vertices[2][0] = (int32_t)v2->x;
        surf.vertices[2][1] = (int32_t)v2->y;
        surf.vertices[2][2] = (int32_t)v2->z;

        out.push_back(surf);
    }
}

// ---------------------------------------------------------------------------
// Build the full surface list for the current scene
// ---------------------------------------------------------------------------

std::vector<SM64Surface> MarioCollision::BuildSurfacesFromScene(PlayState* play) {
    std::vector<SM64Surface> surfaces;
    surfaces.reserve(8192);

    // CollisionContext.colHeader is the scene's static collision header
    // (defined at offset 0x00 in CollisionContext, z64bgcheck.h)
    CollisionHeader* staticHeader = play->colCtx.colHeader;
    if (staticHeader) {
        AppendFromHeader(surfaces, staticHeader);
    }

    // Dynamic background actors (moving platforms, large objects) are in
    // play->colCtx.dyna — handled via sm64_surface_object_* in a future phase.

    printf("[Mario] BuildSurfacesFromScene: %zu surfaces from static collision header.\n",
           surfaces.size());
    return surfaces;
}

// ---------------------------------------------------------------------------
// Dynamic surface object helpers
// ---------------------------------------------------------------------------

uint32_t MarioCollision::CreateDynamicObject(CollisionHeader* header,
                                              float x, float y, float z, float ry) {
    std::vector<SM64Surface> surfs;
    AppendFromHeader(surfs, header);
    if (surfs.empty()) return UINT32_MAX;

    SM64SurfaceObject obj{};
    obj.transform.position[0] = x;
    obj.transform.position[1] = y;
    obj.transform.position[2] = z;
    obj.transform.eulerRotation[0] = 0.0f;
    obj.transform.eulerRotation[1] = ry;
    obj.transform.eulerRotation[2] = 0.0f;
    obj.surfaceCount = (uint32_t)surfs.size();
    obj.surfaces     = surfs.data();

    return sm64_surface_object_create(&obj);
}

void MarioCollision::UpdateDynamicObject(uint32_t objId,
                                          float x, float y, float z, float ry) {
    if (objId == UINT32_MAX) return;
    SM64ObjectTransform t{};
    t.position[0] = x;
    t.position[1] = y;
    t.position[2] = z;
    t.eulerRotation[1] = ry;
    sm64_surface_object_move(objId, &t);
}

void MarioCollision::DeleteDynamicObject(uint32_t objId) {
    if (objId != UINT32_MAX) {
        sm64_surface_object_delete(objId);
    }
}

#endif // __EMSCRIPTEN__
