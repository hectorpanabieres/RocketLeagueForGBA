/**
 * @file models.c
 * @brief 3D Model definitions and procedural generators.
 */

#include "models.h"
#include <math.h>
#include <string.h>

/* Large mutable model buffers do not need the GBA's scarce 32 KiB IWRAM.
 * Keeping them in EWRAM leaves enough room for the runtime stack. */
#define EWRAM_MODEL_DATA __attribute__((section(".ewram"), aligned(4)))

/* ==============================================================================
   1. CUBE MODEL (8 Vertices, 12 Faces)
   ============================================================================== */
static const Vector3 cube_vertices[8] = {
    { -INT_TO_FP(25), -INT_TO_FP(25), -INT_TO_FP(25) }, // 0
    {  INT_TO_FP(25), -INT_TO_FP(25), -INT_TO_FP(25) }, // 1
    {  INT_TO_FP(25),  INT_TO_FP(25), -INT_TO_FP(25) }, // 2
    { -INT_TO_FP(25),  INT_TO_FP(25), -INT_TO_FP(25) }, // 3
    { -INT_TO_FP(25), -INT_TO_FP(25),  INT_TO_FP(25) }, // 4
    {  INT_TO_FP(25), -INT_TO_FP(25),  INT_TO_FP(25) }, // 5
    {  INT_TO_FP(25),  INT_TO_FP(25),  INT_TO_FP(25) }, // 6
    { -INT_TO_FP(25),  INT_TO_FP(25),  INT_TO_FP(25) }  // 7
};

static const Face cube_faces[12] = {
    // Front face (v0, v3, v2, v1)
    { 0, 3, 2, 2 }, { 0, 2, 1, 2 },
    // Back face (v5, v6, v7, v4)
    { 5, 6, 7, 2 }, { 5, 7, 4, 2 },
    // Left face (v4, v7, v3, v0)
    { 4, 7, 3, 2 }, { 4, 3, 0, 2 },
    // Right face (v1, v2, v6, v5)
    { 1, 2, 6, 2 }, { 1, 6, 5, 2 },
    // Top face (v3, v7, v6, v2)
    { 3, 7, 6, 2 }, { 3, 6, 2, 2 },
    // Bottom face (v4, 0, 1, v5)
    { 4, 0, 1, 2 }, { 4, 1, 5, 2 }
};

Mesh cube_mesh = {
    "CUBE",
    8,
    12,
    cube_vertices,
    cube_faces
};

/* ==============================================================================
   2. PYRAMID MODEL (5 Vertices, 8 Faces)
   ============================================================================== */
static const Vector3 pyramid_vertices[5] = {
    { -INT_TO_FP(30), -INT_TO_FP(20), -INT_TO_FP(30) }, // 0: Bottom-Left-Back
    {  INT_TO_FP(30), -INT_TO_FP(20), -INT_TO_FP(30) }, // 1: Bottom-Right-Back
    {  INT_TO_FP(30), -INT_TO_FP(20),  INT_TO_FP(30) }, // 2: Bottom-Right-Front
    { -INT_TO_FP(30), -INT_TO_FP(20),  INT_TO_FP(30) }, // 3: Bottom-Left-Front
    {  0,              INT_TO_FP(40),  0              }  // 4: Apex
};

static const Face pyramid_faces[8] = {
    // Base (0, 3, 2, 1)
    { 0, 3, 2, 4 }, { 0, 2, 1, 4 },
    // Front face (3, 2, 4)
    { 3, 2, 4, 4 },
    // Right face (2, 1, 4)
    { 2, 1, 4, 4 },
    // Back face (1, 0, 4)
    { 1, 0, 4, 4 },
    // Left face (0, 3, 4)
    { 0, 3, 4, 4 }
};

Mesh pyramid_mesh = {
    "PYRAMID",
    5,
    8,
    pyramid_vertices,
    pyramid_faces
};

/* ==============================================================================
   3. SPACE FIGHTER MODEL (12 Vertices, 16 Faces)
   ============================================================================== */
static const Vector3 spaceship_vertices[12] = {
    { 0,              0,              INT_TO_FP(50)  }, // 0: Nose cone tip
    { -INT_TO_FP(15), -INT_TO_FP(5),  INT_TO_FP(10)  }, // 1: Cockpit left front
    {  INT_TO_FP(15), -INT_TO_FP(5),  INT_TO_FP(10)  }, // 2: Cockpit right front
    {  0,              INT_TO_FP(15),  INT_TO_FP(10)  }, // 3: Canopy top
    { -INT_TO_FP(40), -INT_TO_FP(10), -INT_TO_FP(25) }, // 4: Left wing joint
    {  INT_TO_FP(40), -INT_TO_FP(10), -INT_TO_FP(25) }, // 5: Right wing joint
    { -INT_TO_FP(90), -INT_TO_FP(15), -INT_TO_FP(40) }, // 6: Left wingtip
    {  INT_TO_FP(90), -INT_TO_FP(15), -INT_TO_FP(40) }, // 7: Right wingtip
    { -INT_TO_FP(15), -INT_TO_FP(10), -INT_TO_FP(45) }, // 8: Left engine nozzle
    {  INT_TO_FP(15), -INT_TO_FP(10), -INT_TO_FP(45) }, // 9: Right engine nozzle
    {  0,              INT_TO_FP(25),  -INT_TO_FP(40) }, // 10: Tail fin top
    {  0,              0,              -INT_TO_FP(40) }  // 11: Fuselage rear center
};

static const Face spaceship_faces[16] = {
    // Nose cone
    { 0, 3, 1, 6 }, { 0, 2, 3, 6 },
    { 0, 1, 11, 6 }, { 0, 11, 2, 6 },
    // Canopy/Fuselage upper
    { 3, 10, 1, 6 }, { 3, 2, 10, 6 },
    // Fuselage rear / tail fin
    { 10, 11, 1, 6 }, { 2, 11, 10, 6 },
    // Left wing
    { 1, 4, 6, 6 }, { 4, 11, 6, 6 },
    // Right wing
    { 2, 7, 5, 6 }, { 5, 7, 11, 6 },
    // Engines/Back flat
    { 4, 8, 11, 6 }, { 5, 11, 9, 6 },
    // Lower belly
    { 1, 11, 4, 6 }, { 2, 5, 11, 6 }
};

Mesh spaceship_mesh = {
    "SPACE SHIP",
    12,
    16,
    spaceship_vertices,
    spaceship_faces
};

/* ==============================================================================
   4. DYNAMIC PROCEDURAL TORUS & SPHERE
   ============================================================================== */
#define TORUS_RINGS 12
#define TORUS_SEGMENTS 12
#define TORUS_VCOUNT (TORUS_RINGS * TORUS_SEGMENTS)
#define TORUS_FCOUNT (TORUS_RINGS * TORUS_SEGMENTS * 2)

static Vector3 torus_verts[TORUS_VCOUNT] EWRAM_MODEL_DATA;
static Face torus_faces[TORUS_FCOUNT] EWRAM_MODEL_DATA;

Mesh torus_mesh = {
    "TORUS",
    TORUS_VCOUNT,
    TORUS_FCOUNT,
    torus_verts,
    torus_faces
};

#define WALLS_VCOUNT 8
#define WALLS_FCOUNT 8
static Vector3 walls_verts[WALLS_VCOUNT] = {
    { INT_TO_FP(-120), INT_TO_FP(0), INT_TO_FP(-160) },
    { INT_TO_FP(120), INT_TO_FP(0), INT_TO_FP(-160) },
    { INT_TO_FP(120), INT_TO_FP(0), INT_TO_FP(160) },
    { INT_TO_FP(-120), INT_TO_FP(0), INT_TO_FP(160) },
    { INT_TO_FP(-120), INT_TO_FP(50), INT_TO_FP(-160) },
    { INT_TO_FP(120), INT_TO_FP(50), INT_TO_FP(-160) },
    { INT_TO_FP(120), INT_TO_FP(50), INT_TO_FP(160) },
    { INT_TO_FP(-120), INT_TO_FP(50), INT_TO_FP(160) },
};

static Face walls_faces[WALLS_FCOUNT] = {
    { 0, 1, 5, 2 }, // Base color 2 (Dark Green/Blue)
    { 0, 5, 4, 2 },
    { 1, 2, 6, 2 },
    { 1, 6, 5, 2 },
    { 2, 3, 7, 2 },
    { 2, 7, 6, 2 },
    { 3, 0, 4, 2 },
    { 3, 4, 7, 2 },
};

Mesh walls_mesh = {
    "WALLS",
    WALLS_VCOUNT,
    WALLS_FCOUNT,
    walls_verts,
    walls_faces
};

#define SPHERE_VCOUNT 42
#define SPHERE_FCOUNT 80

/*
 * The gameplay ball has a 14-unit radius.  This is a once-subdivided
 * icosahedron (an icosphere): 42 evenly distributed vertices and 80 small
 * triangles.  It is deliberately one subdivision deep rather than a UV
 * sphere, because its triangles stay similarly sized at the poles and the
 * renderer only has to transform 42 vertices per frame.  The mesh radius is
 * 21 units and the caller draws it at 2/3 scale, preserving the existing
 * physical and visual radius of 14 units.
 */
static const Vector3 sphere_verts[SPHERE_VCOUNT] = {
    { INT_TO_FP(-11), INT_TO_FP(17), INT_TO_FP(0) },
    { INT_TO_FP(11), INT_TO_FP(17), INT_TO_FP(0) },
    { INT_TO_FP(-11), INT_TO_FP(-17), INT_TO_FP(0) },
    { INT_TO_FP(11), INT_TO_FP(-17), INT_TO_FP(0) },
    { INT_TO_FP(0), INT_TO_FP(-11), INT_TO_FP(17) },
    { INT_TO_FP(0), INT_TO_FP(11), INT_TO_FP(17) },
    { INT_TO_FP(0), INT_TO_FP(-11), INT_TO_FP(-17) },
    { INT_TO_FP(0), INT_TO_FP(11), INT_TO_FP(-17) },
    { INT_TO_FP(17), INT_TO_FP(0), INT_TO_FP(-11) },
    { INT_TO_FP(17), INT_TO_FP(0), INT_TO_FP(11) },
    { INT_TO_FP(-17), INT_TO_FP(0), INT_TO_FP(-11) },
    { INT_TO_FP(-17), INT_TO_FP(0), INT_TO_FP(11) },
    { INT_TO_FP(-16), INT_TO_FP(10), INT_TO_FP(6) },
    { INT_TO_FP(-10), INT_TO_FP(6), INT_TO_FP(16) },
    { INT_TO_FP(-6), INT_TO_FP(16), INT_TO_FP(10) },
    { INT_TO_FP(6), INT_TO_FP(16), INT_TO_FP(10) },
    { INT_TO_FP(0), INT_TO_FP(21), INT_TO_FP(0) },
    { INT_TO_FP(6), INT_TO_FP(16), INT_TO_FP(-10) },
    { INT_TO_FP(-6), INT_TO_FP(16), INT_TO_FP(-10) },
    { INT_TO_FP(-10), INT_TO_FP(6), INT_TO_FP(-16) },
    { INT_TO_FP(-16), INT_TO_FP(10), INT_TO_FP(-6) },
    { INT_TO_FP(-21), INT_TO_FP(0), INT_TO_FP(0) },
    { INT_TO_FP(10), INT_TO_FP(6), INT_TO_FP(16) },
    { INT_TO_FP(16), INT_TO_FP(10), INT_TO_FP(6) },
    { INT_TO_FP(-10), INT_TO_FP(-6), INT_TO_FP(16) },
    { INT_TO_FP(0), INT_TO_FP(0), INT_TO_FP(21) },
    { INT_TO_FP(-16), INT_TO_FP(-10), INT_TO_FP(-6) },
    { INT_TO_FP(-16), INT_TO_FP(-10), INT_TO_FP(6) },
    { INT_TO_FP(0), INT_TO_FP(0), INT_TO_FP(-21) },
    { INT_TO_FP(-10), INT_TO_FP(-6), INT_TO_FP(-16) },
    { INT_TO_FP(16), INT_TO_FP(10), INT_TO_FP(-6) },
    { INT_TO_FP(10), INT_TO_FP(6), INT_TO_FP(-16) },
    { INT_TO_FP(16), INT_TO_FP(-10), INT_TO_FP(6) },
    { INT_TO_FP(10), INT_TO_FP(-6), INT_TO_FP(16) },
    { INT_TO_FP(6), INT_TO_FP(-16), INT_TO_FP(10) },
    { INT_TO_FP(-6), INT_TO_FP(-16), INT_TO_FP(10) },
    { INT_TO_FP(0), INT_TO_FP(-21), INT_TO_FP(0) },
    { INT_TO_FP(-6), INT_TO_FP(-16), INT_TO_FP(-10) },
    { INT_TO_FP(6), INT_TO_FP(-16), INT_TO_FP(-10) },
    { INT_TO_FP(10), INT_TO_FP(-6), INT_TO_FP(-16) },
    { INT_TO_FP(16), INT_TO_FP(-10), INT_TO_FP(-6) },
    { INT_TO_FP(21), INT_TO_FP(0), INT_TO_FP(0) },
};

/* Palette-white ball material.  The renderer maps this marker through a
 * near-white diffuse ramp so the smooth silhouette also has surface depth. */
static const Face sphere_faces[SPHERE_FCOUNT] = {
    { 0, 12, 14, 130 }, { 11, 13, 12, 130 }, { 5, 14, 13, 130 }, { 12, 13, 14, 130 },
    { 0, 14, 16, 130 }, { 5, 15, 14, 130 }, { 1, 16, 15, 130 }, { 14, 15, 16, 130 },
    { 0, 16, 18, 130 }, { 1, 17, 16, 130 }, { 7, 18, 17, 130 }, { 16, 17, 18, 130 },
    { 0, 18, 20, 130 }, { 7, 19, 18, 130 }, { 10, 20, 19, 130 }, { 18, 19, 20, 130 },
    { 0, 20, 12, 130 }, { 10, 21, 20, 130 }, { 11, 12, 21, 130 }, { 20, 21, 12, 130 },
    { 1, 15, 23, 130 }, { 5, 22, 15, 130 }, { 9, 23, 22, 130 }, { 15, 22, 23, 130 },
    { 5, 13, 25, 130 }, { 11, 24, 13, 130 }, { 4, 25, 24, 130 }, { 13, 24, 25, 130 },
    { 11, 21, 27, 130 }, { 10, 26, 21, 130 }, { 2, 27, 26, 130 }, { 21, 26, 27, 130 },
    { 10, 19, 29, 130 }, { 7, 28, 19, 130 }, { 6, 29, 28, 130 }, { 19, 28, 29, 130 },
    { 7, 17, 31, 130 }, { 1, 30, 17, 130 }, { 8, 31, 30, 130 }, { 17, 30, 31, 130 },
    { 3, 32, 34, 130 }, { 9, 33, 32, 130 }, { 4, 34, 33, 130 }, { 32, 33, 34, 130 },
    { 3, 34, 36, 130 }, { 4, 35, 34, 130 }, { 2, 36, 35, 130 }, { 34, 35, 36, 130 },
    { 3, 36, 38, 130 }, { 2, 37, 36, 130 }, { 6, 38, 37, 130 }, { 36, 37, 38, 130 },
    { 3, 38, 40, 130 }, { 6, 39, 38, 130 }, { 8, 40, 39, 130 }, { 38, 39, 40, 130 },
    { 3, 40, 32, 130 }, { 8, 41, 40, 130 }, { 9, 32, 41, 130 }, { 40, 41, 32, 130 },
    { 4, 33, 25, 130 }, { 9, 22, 33, 130 }, { 5, 25, 22, 130 }, { 33, 22, 25, 130 },
    { 2, 35, 27, 130 }, { 4, 24, 35, 130 }, { 11, 27, 24, 130 }, { 35, 24, 27, 130 },
    { 6, 37, 29, 130 }, { 2, 26, 37, 130 }, { 10, 29, 26, 130 }, { 37, 26, 29, 130 },
    { 8, 39, 31, 130 }, { 6, 28, 39, 130 }, { 7, 31, 28, 130 }, { 39, 28, 31, 130 },
    { 9, 41, 23, 130 }, { 8, 30, 41, 130 }, { 1, 23, 30, 130 }, { 41, 30, 23, 130 },
};

Mesh sphere_mesh = {
    "DODECAHEDRON_BALL",
    SPHERE_VCOUNT,
    SPHERE_FCOUNT,
    sphere_verts,
    sphere_faces
};

/* The first twelve icosphere vertices form a compact distant-ball silhouette. */
static const Face far_ball_faces[20] = {
    { 0, 11, 5, 130 },
    { 0, 5, 1, 130 },
    { 0, 1, 7, 130 },
    { 0, 7, 10, 130 },
    { 0, 10, 11, 130 },
    { 1, 5, 9, 130 },
    { 5, 11, 4, 130 },
    { 11, 10, 2, 130 },
    { 10, 7, 6, 130 },
    { 7, 1, 8, 130 },
    { 3, 9, 4, 130 },
    { 3, 4, 2, 130 },
    { 3, 2, 6, 130 },
    { 3, 6, 8, 130 },
    { 3, 8, 9, 130 },
    { 4, 9, 5, 130 },
    { 2, 4, 11, 130 },
    { 6, 2, 10, 130 },
    { 8, 6, 7, 130 },
    { 9, 8, 1, 130 },
};
static Vector3 far_ball_normals[20] EWRAM_MODEL_DATA;
static Mesh far_ball_mesh = {"DISTANT_BALL",12,20,sphere_verts,far_ball_faces,far_ball_normals};

const Mesh *car_gameplay_mesh(int model, int distance_sq) {
    if(performance_mode==2)return car_speed_models[model];
    return performance_mode || distance_sq > 240*240 ? car_far_models[model] : car_match_models[model];
}
const Mesh *ball_gameplay_mesh(int distance_sq) {
    return distance_sq > (280*280) ? &far_ball_mesh : &sphere_mesh;
}

void init_dynamic_models(void) {
    /* ----------------------------------------------------
       Torus Generator
       ---------------------------------------------------- */
    fixed r_major = INT_TO_FP(30);
    fixed r_minor = INT_TO_FP(12);
    
    int t_idx = 0;
    for (int ring = 0; ring < TORUS_RINGS; ring++) {
        int u_angle = (ring * 256 / TORUS_RINGS) << 8;
        fixed cos_u = lu_cos(u_angle) >> 4; // Convert Tonc 4.12 to 8.8
        fixed sin_u = lu_sin(u_angle) >> 4;
        
        for (int seg = 0; seg < TORUS_SEGMENTS; seg++) {
            int v_angle = (seg * 256 / TORUS_SEGMENTS) << 8;
            fixed cos_v = lu_cos(v_angle) >> 4;
            fixed sin_v = lu_sin(v_angle) >> 4;
            
            fixed dist = r_major + ((r_minor * cos_v) >> 8);
            torus_verts[t_idx].x = (dist * cos_u) >> 8;
            torus_verts[t_idx].y = (dist * sin_u) >> 8;
            torus_verts[t_idx].z = (r_minor * sin_v) >> 8;
            t_idx++;
        }
    }
    
    // Generate faces
    t_idx = 0;
    for (int ring = 0; ring < TORUS_RINGS; ring++) {
        int next_ring = (ring + 1) % TORUS_RINGS;
        for (int seg = 0; seg < TORUS_SEGMENTS; seg++) {
            int next_seg = (seg + 1) % TORUS_SEGMENTS;
            
            int v1 = ring * TORUS_SEGMENTS + seg;
            int v2 = ring * TORUS_SEGMENTS + next_seg;
            int v3 = next_ring * TORUS_SEGMENTS + seg;
            int v4 = next_ring * TORUS_SEGMENTS + next_seg;
            
            // Triangle 1: CCW order
            torus_faces[t_idx].v1 = v1;
            torus_faces[t_idx].v2 = v3;
            torus_faces[t_idx].v3 = v2;
            torus_faces[t_idx].base_color = 3; // Blue
            t_idx++;
            
            // Triangle 2: CCW order
            torus_faces[t_idx].v1 = v2;
            torus_faces[t_idx].v2 = v3;
            torus_faces[t_idx].v3 = v4;
            torus_faces[t_idx].base_color = 3; // Blue
            t_idx++;
        }
    }


}

#include "car_models.inc"

/* ==============================================================================
   6. SOCCER BALL MODEL (6 Vertices, 8 Faces - Octahedron for speed)
   ============================================================================== */
static const Vector3 ball_vertices[6] = {
    { 0,              INT_TO_FP(14),  0              }, // 0: Top
    { 0,              -INT_TO_FP(14), 0              }, // 1: Bottom
    {  INT_TO_FP(14), 0,              0              }, // 2: Right
    { -INT_TO_FP(14), 0,              0              }, // 3: Left
    { 0,              0,              INT_TO_FP(14)  }, // 4: Front
    { 0,              0,              -INT_TO_FP(14) }  // 5: Back
};

static const Face ball_faces[8] = {
    { 0, 2, 4, 0 }, // Top-Right-Front
    { 0, 4, 3, 0 }, // Top-Front-Left
    { 0, 3, 5, 0 }, // Top-Left-Back
    { 0, 5, 2, 0 }, // Top-Back-Right
    { 1, 4, 2, 0 }, // Bottom-Front-Right
    { 1, 3, 4, 0 }, // Bottom-Left-Front
    { 1, 5, 3, 0 }, // Bottom-Back-Left
    { 1, 2, 5, 0 }  // Bottom-Right-Back
};

Mesh ball_mesh = {
    "BALL",
    6,
    8,
    ball_vertices,
    ball_faces
};

/* ==============================================================================
   7. GOAL POST MODEL (8 Vertices, 10 Faces)
   ============================================================================== */
static const Vector3 goal_vertices[8] = {
    { -INT_TO_FP(45), 0,              0 },             // 0: bottom-left front
    {  INT_TO_FP(45), 0,              0 },             // 1: bottom-right front
    {  INT_TO_FP(45),  INT_TO_FP(30), 0 },             // 2: top-right front
    { -INT_TO_FP(45),  INT_TO_FP(30), 0 },             // 3: top-left front
    { -INT_TO_FP(45), 0,              INT_TO_FP(20) }, // 4: bottom-left back
    {  INT_TO_FP(45), 0,              INT_TO_FP(20) }, // 5: bottom-right back
    {  INT_TO_FP(45),  INT_TO_FP(30), INT_TO_FP(20) }, // 6: top-right back
    { -INT_TO_FP(45),  INT_TO_FP(30), INT_TO_FP(20) }  // 7: top-left back
};

static const Face goal_faces[10] = {
    // Back net panel
    { 4, 7, 6, 0 }, { 4, 6, 5, 0 },
    // Top net panel
    { 3, 7, 6, 0 }, { 3, 6, 2, 0 },
    // Left net panel
    { 0, 3, 7, 0 }, { 0, 7, 4, 0 },
    // Right net panel
    { 1, 5, 6, 0 }, { 1, 6, 2, 0 },
    // Bottom anchors
    { 0, 4, 5, 0 }, { 0, 5, 1, 0 }
};

Mesh goal_mesh = {
    "GOAL POST",
    8,
    10,
    goal_vertices,
    goal_faces
};

/* ==============================================================================
   8. HOCKEY PUCK MODEL (flat cylinder with twelve-sided silhouette)
   ============================================================================== */
#define PUCK_R  INT_TO_FP(12)  /* radius */
#define PUCK_H  INT_TO_FP(4)   /* half-height */

static const Vector3 puck_vertices[26] = {
    { 3072, 1024, 0 },
    { 2660, 1024, 1536 },
    { 1536, 1024, 2660 },
    { 0, 1024, 3072 },
    { -1536, 1024, 2660 },
    { -2660, 1024, 1536 },
    { -3072, 1024, 0 },
    { -2660, 1024, -1536 },
    { -1536, 1024, -2660 },
    { 0, 1024, -3072 },
    { 1536, 1024, -2660 },
    { 2660, 1024, -1536 },
    { 3072, -1024, 0 },
    { 2660, -1024, 1536 },
    { 1536, -1024, 2660 },
    { 0, -1024, 3072 },
    { -1536, -1024, 2660 },
    { -2660, -1024, 1536 },
    { -3072, -1024, 0 },
    { -2660, -1024, -1536 },
    { -1536, -1024, -2660 },
    { 0, -1024, -3072 },
    { 1536, -1024, -2660 },
    { 2660, -1024, -1536 },
    { 0, 1024, 0 },
    { 0, -1024, 0 },
};
static const Face puck_faces[48] = {
    { 24, 1, 0, 0 },
    { 25, 12, 13, 0 },
    { 0, 13, 12, 0 },
    { 0, 1, 13, 0 },
    { 24, 2, 1, 0 },
    { 25, 13, 14, 0 },
    { 1, 14, 13, 0 },
    { 1, 2, 14, 0 },
    { 24, 3, 2, 0 },
    { 25, 14, 15, 0 },
    { 2, 15, 14, 0 },
    { 2, 3, 15, 0 },
    { 24, 4, 3, 0 },
    { 25, 15, 16, 0 },
    { 3, 16, 15, 0 },
    { 3, 4, 16, 0 },
    { 24, 5, 4, 0 },
    { 25, 16, 17, 0 },
    { 4, 17, 16, 0 },
    { 4, 5, 17, 0 },
    { 24, 6, 5, 0 },
    { 25, 17, 18, 0 },
    { 5, 18, 17, 0 },
    { 5, 6, 18, 0 },
    { 24, 7, 6, 0 },
    { 25, 18, 19, 0 },
    { 6, 19, 18, 0 },
    { 6, 7, 19, 0 },
    { 24, 8, 7, 0 },
    { 25, 19, 20, 0 },
    { 7, 20, 19, 0 },
    { 7, 8, 20, 0 },
    { 24, 9, 8, 0 },
    { 25, 20, 21, 0 },
    { 8, 21, 20, 0 },
    { 8, 9, 21, 0 },
    { 24, 10, 9, 0 },
    { 25, 21, 22, 0 },
    { 9, 22, 21, 0 },
    { 9, 10, 22, 0 },
    { 24, 11, 10, 0 },
    { 25, 22, 23, 0 },
    { 10, 23, 22, 0 },
    { 10, 11, 23, 0 },
    { 24, 0, 11, 0 },
    { 25, 23, 12, 0 },
    { 11, 12, 23, 0 },
    { 11, 0, 12, 0 },
};
Mesh puck_mesh = { "PUCK",26,48,puck_vertices,puck_faces };

/* Placeholder stadium_arena_mesh (empty box) */
Mesh stadium_arena_mesh = {
    "STADIUM",
    8,
    8,
    walls_verts,
    walls_faces
};

/* ==============================================================================
   INIT_MESH_NORMALS — pre-compute normalized face normals for static meshes
   ============================================================================== */
#define MAX_NORM_FACES 64


static Vector3 sphere_normals[SPHERE_FCOUNT] EWRAM_MODEL_DATA;
static Vector3 puck_normals[48] EWRAM_MODEL_DATA;
static Vector3 goal_normals[10] EWRAM_MODEL_DATA;

static void compute_normals(const Vector3 *verts, const Face *faces, int nfaces, Vector3 *out) {
    for (int i = 0; i < nfaces; i++) {
        const Vector3 *v0 = &verts[faces[i].v1];
        const Vector3 *v1 = &verts[faces[i].v2];
        const Vector3 *v2 = &verts[faces[i].v3];

        /* Edge vectors (FP 8.8, >> 8 to keep within 32-bit) */
        int32_t ex1 = (v1->x - v0->x) >> 4;
        int32_t ey1 = (v1->y - v0->y) >> 4;
        int32_t ez1 = (v1->z - v0->z) >> 4;
        int32_t ex2 = (v2->x - v0->x) >> 4;
        int32_t ey2 = (v2->y - v0->y) >> 4;
        int32_t ez2 = (v2->z - v0->z) >> 4;

        /* Cross product (stay in 32-bit by keeping >> 4 factors) */
        int32_t nx = (ey1 * ez2 - ez1 * ey2) >> 8;
        int32_t ny = (ez1 * ex2 - ex1 * ez2) >> 8;
        int32_t nz = (ex1 * ey2 - ey1 * ex2) >> 8;

        /* Keep small spoiler/support normals: shifting again here used to
         * collapse thin faces to zero and give them a fake forward normal. */
        int32_t len = fast_sqrt(nx*nx + ny*ny + nz*nz);

        if (len > 0) {
            /* Store as 8.8 fixed (multiply by 256/len) */
            out[i].x = (nx * 256) / len;
            out[i].y = (ny * 256) / len;
            out[i].z = (nz * 256) / len;
        } else {
            out[i].x = 0; out[i].y = 0; out[i].z = INT_TO_FP(1);
        }
    }
}

void init_mesh_normals(void) {
    compute_normals(car_vertices, car_faces, car_mesh.face_count, car_normals);
    compute_normals(opp_car_vertices, opp_car_faces, opp_car_mesh.face_count, opp_car_normals);
    compute_normals(rover_vertices, rover_faces, rover_mesh.face_count, rover_normals);
    for (int model=0;model<CAR_MODEL_COUNT;model++) {
        const Mesh *near=car_match_models[model], *far=car_far_models[model];
        const Mesh *speed=car_speed_models[model];
        compute_normals(speed->vertices,speed->faces,speed->face_count,(Vector3 *)speed->face_normals);
        compute_normals(near->vertices,near->faces,near->face_count,(Vector3 *)near->face_normals);
        compute_normals(far->vertices,far->faces,far->face_count,(Vector3 *)far->face_normals);
    }
    compute_normals(sphere_verts,far_ball_faces,20,far_ball_normals);
    compute_normals(sphere_verts,      sphere_faces,   SPHERE_FCOUNT, sphere_normals);
    compute_normals(puck_vertices,     puck_faces,     48,            puck_normals);
    compute_normals(goal_vertices,     goal_faces,     10,            goal_normals);

    car_mesh.face_normals      = car_normals;
    opp_car_mesh.face_normals  = opp_car_normals;
    sphere_mesh.face_normals   = sphere_normals;
    puck_mesh.face_normals     = puck_normals;
    goal_mesh.face_normals     = goal_normals;
}

/* Cached once from actual mesh bounds, including wheels and bodywork. */
static Vector3 car_centers[CAR_MODEL_COUNT];
static int car_centers_ready;

Vector3 car_render_position(int model, Vector3 pos, int yaw, const int32_t rotation[9]) {
    if (!car_centers_ready) {
        for (int m = 0; m < CAR_MODEL_COUNT; ++m) {
            const Mesh *mesh = car_models[m];
            Vector3 lo = mesh->vertices[0], hi = lo;
            for (int i = 1; i < mesh->vertex_count; ++i) {
                Vector3 v = mesh->vertices[i];
                if (v.x < lo.x) lo.x = v.x;
                if (v.y < lo.y) lo.y = v.y;
                if (v.z < lo.z) lo.z = v.z;
                if (v.x > hi.x) hi.x = v.x;
                if (v.y > hi.y) hi.y = v.y;
                if (v.z > hi.z) hi.z = v.z;
            }
            car_centers[m] = (Vector3){(lo.x + hi.x) / 2, (lo.y + hi.y) / 2, (lo.z + hi.z) / 2};
        }
        car_centers_ready = 1;
    }
    Vector3 c = car_centers[model];
    int32_t sy = custom_sin_lut[yaw & 255], cy = custom_cos_lut[yaw & 255];
    /* Translation = ground origin + yaw * center - rotation * center. */
    pos.x += ((cy * c.x + sy * c.z) >> 12) -
             ((rotation[0] * c.x + rotation[1] * c.y + rotation[2] * c.z) >> 12);
    pos.y += c.y - ((rotation[3] * c.x + rotation[4] * c.y + rotation[5] * c.z) >> 12);
    pos.z += ((-sy * c.x + cy * c.z) >> 12) -
             ((rotation[6] * c.x + rotation[7] * c.y + rotation[8] * c.z) >> 12);
    return pos;
}
