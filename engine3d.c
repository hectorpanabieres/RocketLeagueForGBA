/**
 * @file engine3d.c
 * @brief Ultra-optimized 3D engine implementation for the GBA (Mode 4).
 */

#include "engine3d.h"
#include <string.h>

int performance_mode=2;

/* --- Shared EWRAM Frame Buffer --- */
u8 frame_buffer[240 * 160] __attribute__((section(".ewram"), aligned(4)));

/* Rendering is synchronous and non-reentrant. Share scratch storage instead
 * of putting 5-6 KB of vertex arrays on the small IWRAM user stack per draw. */
static Vector3 model_vertices[256] __attribute__((section(".ewram"), aligned(4)));
static int screen_x[256] __attribute__((section(".ewram"), aligned(4)));
static int screen_y[256] __attribute__((section(".ewram"), aligned(4)));
static int z_proj[256] __attribute__((section(".ewram"), aligned(4)));

/* --- Look-Up Tables --- */
fixed custom_div_lut[2048] __attribute__((section(".ewram"), aligned(4)));
int32_t custom_sin_lut[256] __attribute__((aligned(4)));
int32_t custom_cos_lut[256] __attribute__((aligned(4)));
int16_t custom_sin_fp[256] __attribute__((section(".ewram"), aligned(4)));
int16_t custom_cos_fp[256] __attribute__((section(".ewram"), aligned(4)));
u8 custom_sqrt_lut[4096] __attribute__((section(".ewram"), aligned(4)));
u8 font_pixel_lut[42][8][8] __attribute__((section(".ewram"), aligned(4)));

/* --- Pitch Texture (shared for soccer + hockey) --- */
u8 pitch_texture[512][256] __attribute__((section(".ewram"), aligned(4)));
u8 car_texture[64][64] __attribute__((section(".ewram"), aligned(4)));
int active_pitch_mode = 0; /* 0=soccer, 1=hockey */

/* Code-native 64x64 atlas: eight 16x32 panels. Zero keeps the lit
 * team paint, so one texture works for both blue and orange cars. */
/* High precision reciprocal samples, interpolated in fractional depth. */
static u32 projection_reciprocal[1025] __attribute__((section(".ewram"),aligned(4)));

static void init_car_texture(void) {
    for (int v = 0; v < 64; ++v) for (int u = 0; u < 64; ++u) {
        int tile = (v / 32) * 4 + u / 16;
        int x = u & 15, y = v & 31;
        u8 c = 0;
        switch (tile) {
        case 0: /* Hood/deck: twin ivory racing stripes and inset vents. */
            if (x == 5 || x == 6 || x == 9 || x == 10) c = 14;
            if ((x == 2 || x == 13) && y > 5 && y < 15) c = (y&1)?4:8;
            if (y==23 && x>1 && x<14 && c==0)c=10;
            break;
        case 1: /* Side: swept decal above the dark rocker panel. */
            if (y > 24) c = 4;
            if (y >= 18 - x / 3 && y <= 20 - x / 3) c = 14;
            if (x == 8 && y > 5 && y < 15) c = 5;
            break;
        case 2: /* Front: paired lamps and grille. */
            if (y > 21) c = 4;
            if (x > 4 && x < 11 && y > 13 && y < 24) c = (y & 3) ? 3 : 8;
            if ((x < 4 || x > 11) && y > 7 && y < 16) c = 130;
            break;
        case 3: /* Rear: tail lamps and twin exhausts. */
            if (y > 21) c = 4;
            if ((x < 4 || x > 11) && y > 7 && y < 15) c = 30;
            if ((x == 4 || x == 11) && y > 24 && y < 29) c = 10;
            break;
        case 4: /* Glass with a broad reflection and painted surround. */
            if (x > 1 && x < 14 && y > 3 && y < 28) {
                int reflection = 10 + x / 2;
                c = y < reflection - 3 ? 145 : (y <= reflection ? 140 : 144);
                if (x == 2 || y == 27) c = 7;
            }
            break;
        case 5: { /* Tyre and five-spoke-style silver hub. */
            int dx = (x - 8) * 2, dy = y - 16;
            int r = dx * dx + dy * dy;
            c = 3;
            if (r < 85) c = (dx > -3 && dx < 3) ||
                (dy > -3 && dy < 3) || (dx - dy > -3 && dx - dy < 3) ? 12 : 6;
            if (r < 10) c = 14;
            break;
        }
        case 6: c = (((x+(y>>1)) & 7) < 2) ? 8 : 4; break; /* Underbody and tyre tread. */
        default: break; /* Plain team paint. */
        }
        car_texture[v][u] = c;
    }
}

void init_pitch_texture(void) {
    /* Only mowing bands belong in the low-cost floor sampler. Pitch markings
       use the clipped world-space pass, so camera tilt cannot produce a second,
       offset circle or doubled boundary lines. */
    for (int z=0;z<512;z++) {
        u8 stripe=(z/24)&1;
        memset(pitch_texture[z],stripe,256);
    }
}

/* Hockey ice texture — writes into the shared pitch_texture.
   tex value 0 = white ice
   tex value 1 = light-blue ice stripe
   tex value 2 = red line
   tex value 3 = blue line
*/
void init_hockey_pitch_texture(void) {
    for (int z = 0; z < 512; z++) {
        for (int x = 0; x < 256; x++) {
            u8 tex = 0; /* white ice default */

            /* Wider stripe shading for smoother ice feel */
            if ((z / 48) & 1) tex = 1;

            /* Red centre line at midfield (texture z≈256) */
            if (z >= 253 && z <= 258) tex = 2;

            /* Blue lines at z≈±120 → texture z≈136 and z≈376 */
            if ((z >= 133 && z <= 138) || (z >= 373 && z <= 378)) tex = 3;

            /* Outer boundary (red) */
            if (x >= 228 && x <= 232 && z <= 390) tex = 2;
            if (z >= 386 && z <= 390 && x <= 232) tex = 2;

            /* Centre face-off circle (r≈35, red) */
            int cx = x, cz = z - 256;
            int cr2 = cx*cx + cz*cz;
            if (cr2 > 32*32 && cr2 < 36*36) tex = 2;

            /* Zone face-off circles (r≈24, red) */
            int fx1 = x - 128;
            int fz1a = z - 106; int fr1a = fx1*fx1 + fz1a*fz1a;
            if (fr1a > 22*22 && fr1a < 26*26) tex = 2;
            int fz1b = z - 406; int fr1b = fx1*fx1 + fz1b*fz1b;
            if (fr1b > 22*22 && fr1b < 26*26) tex = 2;

            /* Goal creases (small red D arcs) */
            int gz1 = z - 30;
            int gcr1 = x*x + gz1*gz1;
            if (gcr1 > 28*28 && gcr1 < 32*32 && gz1 >= 0) tex = 2;
            int gz2 = z - 482;
            int gcr2 = x*x + gz2*gz2;
            if (gcr2 > 28*28 && gcr2 < 32*32 && gz2 <= 0) tex = 2;

            pitch_texture[z][x] = tex;
        }
    }
}


/* --- Fast Math Utils --- */
IWRAM_CODE int32_t fast_sqrt(int32_t val) {
    if (val <= 0) return 0;
    if (val < 4096) return custom_sqrt_lut[val];
    
    int shift = 0;
    while (val >= 4096) {
        val >>= 2;
        shift++;
    }
    return ((int32_t)custom_sqrt_lut[val]) << shift;
}

/* --- Custom 8x8 Bitmap Font moved to render.c --- */

#include "ball_sprite.inc"

/* --- Camera state variables & Precomputed Matrix --- */
static Vector3 camera_pos;
static int32_t cam_cos_y, cam_sin_y;
static int32_t cam_cos_x, cam_sin_x;
static int32_t cam_m00, cam_m02;
static int32_t cam_m10, cam_m11, cam_m12;
static int32_t cam_m20, cam_m21, cam_m22;

/* --- Fast 32-bit Word Span Filler moved to render.c --- */

/* --- Engine Initialization & Palette Generation --- */
void init_3d_engine(void) {
    // Enable Mode 4 and BG 2 (bitmap layer)
    REG_DISPCNT = DCNT_MODE4 | DCNT_BG2;

    // Palette Colors definition in RGB5(R, G, B) - max 31
    // 8 Base colors to generate 16 gradients each:
    struct { u8 r, g, b; } base_colors[8] = {
        {31, 31, 31}, // 0: White/Gray
        {31, 0,  0 }, // 1: Red
        {0,  31, 0 }, // 2: Green
        {0,  10, 31}, // 3: Blue
        {31, 31, 0 }, // 4: Yellow
        {0,  31, 31}, // 5: Cyan
        {31, 16, 0 }, // 6: Orange
        {25, 0,  25}  // 7: Purple
    };

    // Populate palette index 0 to 127 with shades
    for (int c = 0; c < 8; ++c) {
        for (int s = 0; s < 16; ++s) {
            u16 r = (base_colors[c].r * s) / 15;
            u16 g = (base_colors[c].g * s) / 15;
            u16 b = (base_colors[c].b * s) / 15;
            pal_bg_mem[c * 16 + s] = RGB5(r, g, b);
        }
    }

    // Palette setups for system elements (128-145)
    pal_bg_mem[128] = RGB5(5, 12, 26);  // Sky blue (deep)
    pal_bg_mem[129] = RGB5(0, 31, 16);  // HUD text (bright green)
    pal_bg_mem[130] = RGB5(31, 31, 31); // HUD White text
    pal_bg_mem[131] = RGB5(31, 24, 0);  // HUD highlight (gold)
    pal_bg_mem[132] = RGB5(0, 0, 0);    // Outlines (pure black)
    pal_bg_mem[133] = RGB5(0, 26, 12);  // Grid line color (bright teal/green)
    pal_bg_mem[134] = RGB5(3, 19, 5);   // Grass dark (far)
    pal_bg_mem[135] = RGB5(4, 25, 6);   // Grass light (near stripe)
    pal_bg_mem[136] = RGB5(3, 22, 5);   // Grass mid (medium distance)
    pal_bg_mem[137] = RGB5(31, 31, 0);  // Centre circle / lines (bright yellow)
    // Extra sky gradient bands (used by draw_environment_background)
    pal_bg_mem[138] = RGB5(5, 12, 26);  // Sky top (same as 128)
    pal_bg_mem[139] = RGB5(9, 18, 28);  // Sky mid-high
    pal_bg_mem[140] = RGB5(16, 22, 29); // Sky mid-low (near horizon)
    pal_bg_mem[141] = RGB5(22, 26, 30); // Horizon haze (brightest)
    pal_bg_mem[142] = RGB5(6, 28, 7);   // Grass light near (bright)
    pal_bg_mem[143] = RGB5(3, 17, 5);   // Grass very far (darkest)
    /* Menu UI: dark glass panels and their cool-blue highlight. */
    pal_bg_mem[144] = RGB5(1, 4, 11);
    pal_bg_mem[145] = RGB5(3, 9, 20);
    pal_bg_mem[146] = RGB5(7, 19, 31);
    pal_bg_mem[147] = RGB5(0, 16, 12);
    pal_bg_mem[148] = RGB5(31, 15, 2);
    pal_bg_mem[149] = RGB5(7, 7, 8);   // Menu label backing (dark grey)

    /* Ball-only diffuse ramp.  The upper end remains true white while the
       lower end adds a cool, readable stadium shadow without making the ball
       look like a grey object. */
    pal_bg_mem[150] = RGB5(12, 14, 17);
    pal_bg_mem[151] = RGB5(15, 17, 20);
    pal_bg_mem[152] = RGB5(18, 20, 23);
    pal_bg_mem[153] = RGB5(21, 23, 26);
    pal_bg_mem[154] = RGB5(24, 26, 28);
    pal_bg_mem[155] = RGB5(27, 28, 30);
    pal_bg_mem[156] = RGB5(30, 30, 31);
    pal_bg_mem[157] = RGB5(31, 31, 31);

    /* Soft surface-colored shadows avoid black cutouts on grass and ice. */
    pal_bg_mem[SHADOW_GRASS_EDGE] = RGB5(5, 16, 8);
    pal_bg_mem[SHADOW_GRASS_CORE] = RGB5(4, 12, 7);
    pal_bg_mem[SHADOW_ICE_EDGE] = RGB5(24, 27, 29);
    pal_bg_mem[SHADOW_ICE_CORE] = RGB5(18, 22, 25);
    pal_bg_mem[STAND_DARK] = RGB5(4, 7, 11);
    pal_bg_mem[STAND_LIGHT] = RGB5(6, 10, 14);
    pal_bg_mem[STAND_RAIL] = RGB5(13, 19, 23);
    pal_bg_mem[CROWD_BLUE] = RGB5(8, 18, 29);
    pal_bg_mem[CROWD_ORANGE] = RGB5(28, 16, 7);
    pal_bg_mem[CROWD_NEUTRAL] = RGB5(22, 24, 23);
    pal_bg_mem[WALL_HEX] = RGB5(12, 22, 23);
    pal_bg_mem[ICE_SURFACE_STRIPE] = RGB5(26, 29, 30);
    for (int i = 0; i < SKY_GRADIENT_COUNT; ++i) {
        pal_bg_mem[SKY_GRADIENT_START + i] = RGB5(
            5 + (17 * i + 7) / 15, 12 + (14 * i + 7) / 15,
            26 + (4 * i + 7) / 15);
    }

    for(int i=0;i<8;i++)pal_bg_mem[SHADOW_RAMP_START+i]=RGB5(12+i*2,12+i*2,12+i*2);

    /* Dedicated smooth white/cool-gray ramp for the spherical ball sprite. */
    for (int i=0;i<32;i++) {
        int shade=7+(24*i)/31;
        pal_bg_mem[192+i]=RGB5(shade,shade,shade<30?shade+1:31);
    }

    for(int i=0;i<8;i++)pal_bg_mem[233+i]=RGB5(10+i,22+i,31); /* clear hockey sky */

    pal_bg_mem[232]=RGB5(4,24,6); /* subtle grass grain beside index 135 */

    for(int i=1;i<=1024;i++)projection_reciprocal[i]=16777216u/i;

    // Pre-calculate Division LUT (1 / i) in 16.16 fixed point
    custom_div_lut[0] = 0;
    for (int i = 1; i < 2048; i++) {
        custom_div_lut[i] = (1 << 16) / i;
    }

    // Pre-calculate Sin/Cos LUTs mapped from 0-255 in IWRAM & EWRAM
    for (int i = 0; i < 256; i++) {
        int tonc_angle = i << 8;
        custom_sin_lut[i] = lu_sin(tonc_angle);
        custom_cos_lut[i] = lu_cos(tonc_angle);
        custom_sin_fp[i] = (int16_t)(custom_sin_lut[i] >> 4);
        custom_cos_fp[i] = (int16_t)(custom_cos_lut[i] >> 4);
    }

    // Pre-calculate Sqrt LUT for [0..4095]
    for (int i = 0; i < 4096; i++) {
        int r = 0;
        while ((r + 1) * (r + 1) <= i) {
            r++;
        }
        custom_sqrt_lut[i] = (u8)r;
    }

    // Initialize 2D rendering systems
    init_render();
    
    // Procedurally generate soccer pitch texture
    init_pitch_texture();
    init_car_texture();

    // Setup BG2 Affine registers for hardware upscaling
    REG_BG2PA = 256 / RENDER_SCALE; // dx
    REG_BG2PD = 256 / RENDER_SCALE; // dy
    REG_BG2PB = 0; // dmx
    REG_BG2PC = 0; // dmy
    REG_BG2X = 0;
    REG_BG2Y = 0;
}

/* Soccer stripes are uniform runs, not a two-dimensional texture lookup.
 * Split exactly at mirrored 48-world-unit boundaries; never alter markings. */
static void draw_grass_span(u8 *dst, int count, fixed z, fixed step, const u8 colors[4]) {
    const int band=24*512;
    while(count>0) {
        int absolute=z<0?-z:z;
        int stripe=absolute/band;
        int run=count;
        if(step) {
            int gap;
            int speed=step<0?-step:step;
            if(step>0) gap=z>=0?(stripe+1)*band-z:-stripe*band+1-z;
            else gap=z<=0?z+(stripe+1)*band:z-stripe*band+1;
            run=(gap+speed-1)/speed;
            if(run<1)run=1;
            if(run>count)run=count;
        }
        fast_span_fill(dst,(u32)colors[stripe&1]*0x01010101u,run);
        dst+=run;count-=run;z+=step*run;
    }
}

/* Share one reciprocal between X/Y, then correct truncation exactly.
 * The usual coordinates need no fallback and retain integer-division pixels. */
static inline int project_quotient(int numerator, int depth, int reciprocal) {
    if(numerator <= -1073741824 || numerator >= 1073741824 || depth>1073741824)
        return numerator/depth;
    int value=(int)(((int64_t)numerator*reciprocal)/1073741824LL);
    int remainder=numerator-value*depth;
    if(remainder>=depth) ++value;
    else if(remainder<=-depth) --value;
    return value;
}
static IWRAM_CODE __attribute__((noinline)) void project_camera_point(fixed x, fixed y, fixed z, int *sx, int *sy) {
    if(performance_mode==2 && z>=32*FP_SCALE && z<1024*FP_SCALE) {
        int whole=z>>8,fraction=z&255;
        int a=projection_reciprocal[whole],b=projection_reciprocal[whole+1];
        int reciprocal=a+(b-a)*fraction/256;
        *sx=(int)(((int64_t)x*120*reciprocal)/4294967296LL)+120;
        *sy=(int)(((int64_t)-y*120*reciprocal)/4294967296LL)+80;
        return;
    }
    /* Preserve fractional depth: integer-depth lookup caused visible jitter. */
    int reciprocal=1073741824/z;
    *sx=project_quotient(x*120,z,reciprocal)+120/RENDER_SCALE;
    *sy=project_quotient(-y*120,z,reciprocal)+80/RENDER_SCALE;
}

/* --- Buffer Management --- */
/* --- clear_screen moved to render.c --- */

/**
 * @brief Ultra-optimized 32-bit pseudo-raycast floor renderer.
 */
IWRAM_CODE void draw_environment_background(u8 sky_color) {
    int horizon_y = 80;
    if (cam_cos_x != 0) {
        horizon_y = 80 + ((cam_sin_x * 120) / cam_cos_x);
    }

    int start_y = horizon_y;
    if (start_y < 0) start_y = 0;
    if (start_y > 160) start_y = 160;

    if(performance_mode==2) {
        /* Eight smooth sky bands and a low-contrast grass weave use bulk word
           fills, avoiding a per-pixel texture sampler in the Speed preset. */
        if(sky_color==14) {
            for(int band=0;band<8;band++) {
                int top=start_y*band/8,bottom=start_y*(band+1)/8;
                memset32(frame_buffer+top*240,(233u+band)*0x01010101u,(bottom-top)*60);
            }
            memset32(frame_buffer+start_y*240,14u*0x01010101u,(160-start_y)*60);
        } else {
            for(int band=0;band<8;band++) {
                int top=start_y*band/8,bottom=start_y*(band+1)/8;
                u32 color=SKY_GRADIENT_START+2+band*13/7;
                memset32(frame_buffer+top*240,color*0x01010101u,(bottom-top)*60);
            }
            for(int y=start_y;y<160;y++) {
                u32 grass=(y&2)?0x8787e887u:0xe8878787u;
                memset32(frame_buffer+y*240,grass,60);
            }
        }
        return;
    }

    int aligned_start_y = (start_y + 1) & ~1;
    if (aligned_start_y > 160) aligned_start_y = 160;

    int out_start_y = aligned_start_y / RENDER_SCALE;

    // Sixteen sky shades blend smoothly into the existing horizon haze
    // Keep the hockey sky blue and visually separate from the white ice.
    int is_hockey = (sky_color == 14);
    if (out_start_y > 0) {
        int sky_rows = out_start_y;
        for (int y = 0; y < sky_rows; y++) {
            u8 band;
            if (is_hockey) {
                band = 233+(y*7)/(sky_rows>1?sky_rows-1:1); /* clear blue above white ice */
            } else {
                int t = (y * (SKY_GRADIENT_COUNT - 1)) / (sky_rows > 1 ? sky_rows - 1 : 1);
                band = SKY_GRADIENT_START + t;
            }
            u32 w = (u32)band | ((u32)band<<8) | ((u32)band<<16) | ((u32)band<<24);
            memset32(&frame_buffer[y * 240], w, RENDER_WIDTH / 4);
        }
    }

    if (out_start_y >= RENDER_HEIGHT) return;

    fixed fwd_x = cam_sin_y >> 4;
    fixed fwd_z = cam_cos_y >> 4;
    fixed right_x = cam_cos_y >> 4;
    fixed right_z = -(cam_sin_y >> 4);

    fixed cam_h = camera_pos.y;
    if (cam_h <= 0) cam_h = FP_ONE;

    u32 fog_word = (is_hockey ? 14u : 141u) * 0x01010101u;
    int y_step = (RENDER_SCALE == 1) ? 2 : 1;

    for (int y_out = out_start_y; y_out < RENDER_HEIGHT; y_out += y_step) {
        int y_virtual = y_out * RENDER_SCALE;
        int offset = y_virtual - horizon_y;
        if (offset < 2) {
            memset32(&frame_buffer[y_out * 240], fog_word, RENDER_WIDTH / 4);
            if (y_step == 2 && y_out + 1 < RENDER_HEIGHT) memset32(&frame_buffer[(y_out + 1) * 240], fog_word, RENDER_WIDTH / 4);
            continue;
        }

        fixed distance;
        if (offset < 2048) {
            // Split the >> 16 shift into two >> 8 shifts to prevent 32-bit integer overflow when cam_h is high and offset is small
            distance = (((cam_h * 120) >> 8) * custom_div_lut[offset]) >> 8;
        } else {
            distance = (cam_h * 120) / offset;
        }

        if (distance > 400 * FP_SCALE) {
            memset32(&frame_buffer[y_out * 240], fog_word, RENDER_WIDTH / 4);
            if (y_step == 2 && y_out + 1 < RENDER_HEIGHT) memset32(&frame_buffer[(y_out + 1) * 240], fog_word, RENDER_WIDTH / 4);
            continue;
        }

        fixed step_x = (right_x * distance) / 120;
        fixed step_z = (right_z * distance) / 120;

        int32_t wdx = (fwd_x * distance) - (step_x * 120);
        int32_t wdz = (fwd_z * distance) - (step_z * 120);

        fixed wx = camera_pos.x + (fixed)(wdx >> 8);
        fixed wz = camera_pos.z + (fixed)(wdz >> 8);

        fixed dx = (step_x >> 8) * RENDER_SCALE;
        fixed dz = (step_z >> 8) * RENDER_SCALE;

        u8 palette_map[4];
        if (active_pitch_mode == 1) {
            /* Hockey ice palette */
            palette_map[0] = 14;  /* white ice */
            palette_map[1] = ICE_SURFACE_STRIPE; /* pale blue, not the near-black cyan ramp */
            palette_map[2] = 28;  /* visible red markings */
            palette_map[3] = 60;  /* visible blue markings */
        } else if (distance > 200 * FP_SCALE) {
            /* Very far: darkest grass, muted lines (blends into fog) */
            palette_map[0] = 143; // very dark grass (far)
            palette_map[1] = 134; // slightly lighter far stripe
            palette_map[2] = 13;  // readable distant chalk
            palette_map[3] = 97;  // boost pad orange (dim)
        } else if (distance > 120 * FP_SCALE) {
            /* Mid-far: dark grass */
            palette_map[0] = 134; // dark grass
            palette_map[1] = 136; // mid-dark stripe
            palette_map[2] = 14;  // mid-distance chalk
            palette_map[3] = 98;  // boost pad orange mid
        } else if (distance > 50 * FP_SCALE) {
            /* Mid: medium green */
            palette_map[0] = 136; // grass mid
            palette_map[1] = 135; // grass light mid
            palette_map[2] = 13;  // lines (light gray)
            palette_map[3] = 106; // orange mid-near
        } else {
            /* Near: richest greens, bright white lines */
            palette_map[0] = 135; // bright grass near dark stripe
            palette_map[1] = 142; // bright grass light stripe
            palette_map[2] = 130; // lines white
            palette_map[3] = 108; // boost pad orange bright (6*16+12)
        }

        int sx_min = 0;
        int sx_max = RENDER_WIDTH - 1;

        if (dx > 0) {
            int t = (-78336 - wx) / dx;
            if (t > sx_min) sx_min = t;
            t = (78336 - wx) / dx;
            if (t < sx_max) sx_max = t;
        } else if (dx < 0) {
            int t = (78336 - wx) / dx;
            if (t > sx_min) sx_min = t;
            t = (-78336 - wx) / dx;
            if (t < sx_max) sx_max = t;
        } else if (wx < -78336 || wx > 78336) {
            sx_max = -1;
        }

        if (dz > 0) {
            int t = (-117504 - wz) / dz;
            if (t > sx_min) sx_min = t;
            t = (117504 - wz) / dz;
            if (t < sx_max) sx_max = t;
        } else if (dz < 0) {
            int t = (117504 - wz) / dz;
            if (t > sx_min) sx_min = t;
            t = (-117504 - wz) / dz;
            if (t < sx_max) sx_max = t;
        } else if (wz < -117504 || wz > 117504) {
            sx_max = -1;
        }

        if (sx_min < 0) sx_min = 0;
        if (sx_max >= RENDER_WIDTH) sx_max = RENDER_WIDTH - 1;

        u8 *dst = &frame_buffer[y_out * 240];
        u8 ob_color = palette_map[0];
        u32 out_of_bounds_color = (u32)ob_color * 0x01010101u; // Continuous grass instead of void
        // 1. Left out-of-bounds
        if (sx_min > 0) {
            int count = sx_min > RENDER_WIDTH ? RENDER_WIDTH : sx_min;
            fast_span_fill(dst, out_of_bounds_color, count);
        }

        // 2. In-bounds checkerboard
        if (sx_max >= sx_min) {
            wx += sx_min * dx;
            wz += sx_min * dz;
            u8 *p = dst + sx_min;
            int count = sx_max - sx_min + 1;
            u8 (*tex)[256] = pitch_texture;

            if (!active_pitch_mode) {
                draw_grass_span(p,count,wz,dz,palette_map);
                count=0;
            }
            // Hockey keeps its detailed two-dimensional ice texture.
            while (count >= 4) {
                int32_t wx0 = wx; int32_t wz0 = wz;
                int32_t wx1 = wx + dx; int32_t wz1 = wz + dz;
                int32_t wx2 = wx1 + dx; int32_t wz2 = wz1 + dz;
                int32_t wx3 = wx2 + dx; int32_t wz3 = wz2 + dz;
                wx = wx3 + dx; wz = wz3 + dz;

                int tx0 = ((wx0 < 0 ? -wx0 : wx0) >> 9) & 255;
                int tz0 = ((wz0 < 0 ? -wz0 : wz0) >> 9) & 511;
                int tx1 = ((wx1 < 0 ? -wx1 : wx1) >> 9) & 255;
                int tz1 = ((wz1 < 0 ? -wz1 : wz1) >> 9) & 511;
                int tx2 = ((wx2 < 0 ? -wx2 : wx2) >> 9) & 255;
                int tz2 = ((wz2 < 0 ? -wz2 : wz2) >> 9) & 511;
                int tx3 = ((wx3 < 0 ? -wx3 : wx3) >> 9) & 255;
                int tz3 = ((wz3 < 0 ? -wz3 : wz3) >> 9) & 511;

                p[0] = palette_map[tex[tz0][tx0]];
                p[1] = palette_map[tex[tz1][tx1]];
                p[2] = palette_map[tex[tz2][tx2]];
                p[3] = palette_map[tex[tz3][tx3]];

                p += 4;
                count -= 4;
            }
            while (count > 0) {
                int32_t wx_abs = wx < 0 ? -wx : wx;
                int32_t wz_abs = wz < 0 ? -wz : wz;
                int tx = (wx_abs >> 9) & 255;
                int tz = (wz_abs >> 9) & 511;
                *p++ = palette_map[tex[tz][tx]];
                wx += dx; wz += dz;
                count--;
            }
        }

        // 3. Right out-of-bounds
        if (sx_max < RENDER_WIDTH - 1) {
            int start = sx_max + 1;
            if (start < 0) start = 0;
            if (start < RENDER_WIDTH) {
                fast_span_fill(dst + start, out_of_bounds_color, RENDER_WIDTH - start);
            }
        }

        if (y_step == 2 && y_out + 1 < RENDER_HEIGHT) {
            memcpy32(&frame_buffer[(y_out + 1) * 240], &frame_buffer[y_out * 240], RENDER_WIDTH / 4);
        }
    }
}

/* --- swap_buffers, primitives, and fonts moved to render.c --- */

/* --- Math Utils --- */
int32_t int_sqrt(int32_t val) {
    if(val<=0)return 0;
    unsigned value=(unsigned)val,root=0,bit=1u<<30;
    while(bit>value)bit>>=2;
    while(bit) {
        if(value>=root+bit){value-=root+bit;root=(root>>1)+bit;}
        else root>>=1;
        bit>>=2;
    }
    return (int32_t)root;
}

/* Mesh materials use palette groups 0..7.  Values above that are literal
 * palette indices (for example the black wheel material at index 132). */
static inline u8 material_shade(u8 material, int intensity) {
    /* Palette white (130) is the ball's surface material.  Convert its
       directional-light intensity into the dedicated near-white ramp. */
    if (material == 130) {
        if (intensity < 4) intensity = 4;
        if (intensity > 15) intensity = 15;
        return (u8)(150 + ((intensity - 4) * 7 + 5) / 11);
    }
    return material <= 7 ? (u8)((material << 4) + intensity) : material;
}

static inline u8 material_wire_color(u8 material) {
    return material <= 7 ? (u8)((material << 4) + 12) : material;
}

/* --- 3D Projection Pipeline (Standalone Model Rendering) --- */
int draw_model(const Mesh *mesh, int angle_x, int angle_y, fixed scale, fixed z_offset, int render_mode) {
    int visible_faces = 0;

    int32_t sin_x = custom_sin_lut[angle_x & 255];
    int32_t cos_x = custom_cos_lut[angle_x & 255];
    int32_t sin_y = custom_sin_lut[angle_y & 255];
    int32_t cos_y = custom_cos_lut[angle_y & 255];

    int32_t r00 = cos_y;
    int32_t r01 = (-sin_x * sin_y) >> 12;
    int32_t r02 = (cos_x * sin_y) >> 12;

    int32_t r10 = 0;
    int32_t r11 = cos_x;
    int32_t r12 = sin_x;

    int32_t r20 = -sin_y;
    int32_t r21 = (-sin_x * cos_y) >> 12;
    int32_t r22 = (cos_x * cos_y) >> 12;

    int32_t r20_light = r20;
    int32_t r21_light = r21;
    int32_t r22_light = r22;

    if (scale != FP_ONE) {
        r00 = FP_MUL(r00, scale); r01 = FP_MUL(r01, scale); r02 = FP_MUL(r02, scale);
        r10 = FP_MUL(r10, scale); r11 = FP_MUL(r11, scale); r12 = FP_MUL(r12, scale);
        r20 = FP_MUL(r20, scale); r21 = FP_MUL(r21, scale); r22 = FP_MUL(r22, scale);
    }


    
    int v_count = mesh->vertex_count;
    if (v_count > 256) v_count = 256;

    for (int i = 0; i < v_count; i++) {
        Vector3 v = mesh->vertices[i];

        fixed x2 = (r00 * v.x + r01 * v.y + r02 * v.z) >> 12;
        fixed y2 = (r10 * v.x + r11 * v.y + r12 * v.z) >> 12;
        fixed z2 = (r20 * v.x + r21 * v.y + r22 * v.z) >> 12;

        model_vertices[i].x = x2;
        model_vertices[i].y = y2;
        model_vertices[i].z = z2;

        fixed z_cam = z2 + z_offset;
        z_proj[i] = z_cam;
        
        if (z_cam > 10 * FP_SCALE) {
            screen_x[i] = ((x2 * 120) / z_cam) + (120 / RENDER_SCALE);
            screen_y[i] = ((-y2 * 120) / z_cam) + (80 / RENDER_SCALE);
        } else {
            screen_x[i] = -999;
            screen_y[i] = -999;
        }
    }

    for (int i = 0; i < mesh->face_count; i++) {
        Face f = mesh->faces[i];
        
        if (z_proj[f.v1] <= 10 * FP_SCALE || z_proj[f.v2] <= 10 * FP_SCALE || z_proj[f.v3] <= 10 * FP_SCALE) {
            continue;
        }

        int x0 = screen_x[f.v1], y0 = screen_y[f.v1];
        int x1 = screen_x[f.v2], y1 = screen_y[f.v2];
        int x2 = screen_x[f.v3], y2 = screen_y[f.v3];

        // 2D Screen-space Backface Culling
        int cross = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (cross <= 0) {
            continue;
        }

        // Screen Frustum Rejection
        if ((x0 < 0 && x1 < 0 && x2 < 0) ||
            (x0 >= RENDER_WIDTH && x1 >= RENDER_WIDTH && x2 >= RENDER_WIDTH) ||
            (y0 < 0 && y1 < 0 && y2 < 0) ||
            (y0 >= RENDER_HEIGHT && y1 >= RENDER_HEIGHT && y2 >= RENDER_HEIGHT)) {
            continue;
        }

        visible_faces++;

        int intensity;
        if (mesh->face_normals) {
            fixed nx = mesh->face_normals[i].x;
            fixed ny = mesh->face_normals[i].y;
            fixed nz = mesh->face_normals[i].z;
            fixed nz_cam = (r20_light * nx + r21_light * ny + r22_light * nz) >> 12;
            intensity = (-nz_cam * 15) >> 8;
        } else {
            Vector3 v1_3d = model_vertices[f.v1];
            Vector3 v2_3d = model_vertices[f.v2];
            Vector3 v3_3d = model_vertices[f.v3];

            fixed e1_x = v2_3d.x - v1_3d.x;
            fixed e1_y = v2_3d.y - v1_3d.y;
            fixed e1_z = v2_3d.z - v1_3d.z;

            fixed e2_x = v3_3d.x - v1_3d.x;
            fixed e2_y = v3_3d.y - v1_3d.y;
            fixed e2_z = v3_3d.z - v1_3d.z;

            fixed nx = (e1_y * e2_z - e1_z * e2_y) >> FP_SHIFT;
            fixed ny = (e1_z * e2_x - e1_x * e2_z) >> FP_SHIFT;
            fixed nz = (e1_x * e2_y - e1_y * e2_x) >> FP_SHIFT;

            int32_t len_sq = nx*nx + ny*ny + nz*nz;
            int32_t len = int_sqrt(len_sq);
            
            int32_t norm_z = 0;
            if (len > 0) {
                if (len < 2048) {
                    norm_z = (nz * 256 * custom_div_lut[len]) >> 16;
                } else {
                    norm_z = (nz * 256) / len; 
                }
            }
            intensity = (-norm_z * 15) >> 8;
        }
        if (intensity < 0) intensity = 0;
        if (intensity > 15) intensity = 15;

        u8 color = material_shade(f.base_color, intensity);

        int is_fully_on_screen = (x0 >= 0 && x1 >= 0 && x2 >= 0 &&
                                  x0 < RENDER_WIDTH && x1 < RENDER_WIDTH && x2 < RENDER_WIDTH &&
                                  y0 >= 0 && y1 >= 0 && y2 >= 0 &&
                                  y0 < RENDER_HEIGHT && y1 < RENDER_HEIGHT && y2 < RENDER_HEIGHT);

        if (render_mode == RENDER_POINT_CLOUD) {
            draw_point(x0, y0, color);
            draw_point(x1, y1, color);
            draw_point(x2, y2, color);
        }
        else if (render_mode == RENDER_WIREFRAME) {
            u8 wire_color = material_wire_color(f.base_color);
            draw_line(x0, y0, x1, y1, wire_color);
            draw_line(x1, y1, x2, y2, wire_color);
            draw_line(x2, y2, x0, y0, wire_color);
        }
        else if (render_mode == RENDER_FLAT) {
            if (is_fully_on_screen) {
                draw_triangle_flat_unclipped(x0, y0, x1, y1, x2, y2, color);
            } else {
                draw_triangle_flat_clipped(x0, y0, x1, y1, x2, y2, color);
            }
        }
        else if (render_mode == RENDER_OUTLINED) {
            if (is_fully_on_screen) {
                draw_triangle_flat_unclipped(x0, y0, x1, y1, x2, y2, color);
            } else {
                draw_triangle_flat_clipped(x0, y0, x1, y1, x2, y2, color);
            }
            draw_line(x0, y0, x1, y1, 132);
            draw_line(x1, y1, x2, y2, 132);
            draw_line(x2, y2, x0, y0, 132);
        }
    }

    return visible_faces;
}

/* --- Camera and World-Space Pipeline Implementation --- */
/* Interpolate the existing trig table: smooth camera motion without floats. */
int camera_sin_q8(int angle) {
    int index=(angle>>8)&255, fraction=angle&255;
    int a=custom_sin_lut[index],b=custom_sin_lut[(index+1)&255];
    return a+((b-a)*fraction)/256;
}
int camera_cos_q8(int angle) {return camera_sin_q8(angle+64*256);}
void set_camera(Vector3 pos,int yaw,int pitch) {
    set_camera_q8(pos,yaw*256,pitch*256);
}
void set_camera_q8(Vector3 pos,int yaw,int pitch) {
    camera_pos=pos;
    cam_cos_y=camera_cos_q8(yaw);cam_sin_y=camera_sin_q8(yaw);
    cam_cos_x=camera_cos_q8(pitch);cam_sin_x=camera_sin_q8(pitch);

    // Precompute Camera Matrix Components (4.12 fixed point)
    cam_m00 = cam_cos_y;
    cam_m02 = -cam_sin_y;
    cam_m10 = (-cam_sin_x * cam_sin_y) >> 12;
    cam_m11 = cam_cos_x;
    cam_m12 = (-cam_sin_x * cam_cos_y) >> 12;
    cam_m20 = (cam_cos_x * cam_sin_y) >> 12;
    cam_m21 = cam_sin_x;
    cam_m22 = (cam_cos_x * cam_cos_y) >> 12;
}

void set_camera_lookat(Vector3 pos, Vector3 target, int pitch) {
    camera_pos = pos;
    
    fixed dx = target.x - pos.x;
    fixed dz = target.z - pos.z;
    
    fixed dx_s = dx >> 4;
    fixed dz_s = dz >> 4;
    int32_t len_sq = dx_s*dx_s + dz_s*dz_s;
    int32_t len = int_sqrt(len_sq);
    
    if (len > 0) {
        fixed dir_x = (dx_s * 4096) / len;
        fixed dir_z = (dz_s * 4096) / len;
        
        cam_cos_y = dir_z;
        cam_sin_y = dir_x;
        
        /* Auto-compute pitch to look up/down at the target. */
        fixed dy = target.y - pos.y;
        pitch = (int)(((int64_t)dy * 41 * 256) / (len * 16));
    } else {
        cam_cos_y = 4096;
        cam_sin_y = 0;
        pitch = 0;
    }
    
    cam_cos_x = camera_cos_q8(pitch);
    cam_sin_x = camera_sin_q8(pitch);

    cam_m00 = cam_cos_y;
    cam_m02 = -cam_sin_y;
    cam_m10 = (-cam_sin_x * cam_sin_y) >> 12;
    cam_m11 = cam_cos_x;
    cam_m12 = (-cam_sin_x * cam_cos_y) >> 12;
    cam_m20 = (cam_cos_x * cam_sin_y) >> 12;
    cam_m21 = cam_sin_x;
    cam_m22 = (cam_cos_x * cam_cos_y) >> 12;
}

/* A camera-relative offscreen pointer, bounded clear of the scoreboard/HUD. */
int world_target_indicator(Vector3 pos, int *sx, int *sy) {
    fixed x=pos.x-camera_pos.x,y=pos.y-camera_pos.y,z=pos.z-camera_pos.z;
    int cx=(x*cam_m00+z*cam_m02)>>20;
    int cy=(x*cam_m10+y*cam_m11+z*cam_m12)>>20;
    int cz=(x*cam_m20+y*cam_m21+z*cam_m22)>>20;
    int px=cx,py=cz<8?(cz<0?-cz:8):-cy;
    if(cz>=8) {
        if(cx>-cz && cx<cz && 3*cy>-2*cz && 3*cy<2*cz) return 0;
    }
    int ax=px<0?-px:px,ay=py<0?-py:py;
    int scale=ax*42>ay*106?ax*42:ay*106;
    if(!scale) return 0;
    *sx=120+px*4452/scale;
    *sy=72+py*4452/scale;
    return 1;
}

/* Conservative sphere/frustum rejection avoids transforming off-screen meshes. */
int world_sphere_visible(Vector3 pos, fixed radius) {
    fixed x=pos.x-camera_pos.x, y=pos.y-camera_pos.y, z=pos.z-camera_pos.z;
    fixed cx=(x*cam_m00+z*cam_m02)>>12;
    fixed cy=(x*cam_m10+y*cam_m11+z*cam_m12)>>12;
    fixed cz=(x*cam_m20+y*cam_m21+z*cam_m22)>>12;
    if (cz+radius < 8*FP_SCALE) return 0;
    if (cx > cz+2*radius || -cx > cz+2*radius) return 0;
    if (3*cy > 2*cz+4*radius || -3*cy > 2*cz+4*radius) return 0;
    return 1;
}

/* Conservative box/frustum rejection for long stadium strips. A sphere
   around a whole wall is too large to reject the wall behind the camera. */
int world_bounds_visible(Vector3 lo, Vector3 hi) {
    unsigned outside=31;
    for(int i=0;i<8;i++) {
        fixed x=((i&1)?hi.x:lo.x)-camera_pos.x;
        fixed y=((i&2)?hi.y:lo.y)-camera_pos.y;
        fixed z=((i&4)?hi.z:lo.z)-camera_pos.z;
        fixed cx=(x*cam_m00+z*cam_m02)>>12;
        fixed cy=(x*cam_m10+y*cam_m11+z*cam_m12)>>12;
        fixed cz=(x*cam_m20+y*cam_m21+z*cam_m22)>>12;
        /* Expand by one screen pixel plus two world units so integer
           projection cannot turn a rejected edge into a visible pixel. */
        const int pad=2*FP_SCALE;
        unsigned mask=(cz<8*FP_SCALE-pad?1:0) |
            (120*cx>121*cz+120*pad?2:0) | (-120*cx>121*cz+120*pad?4:0) |
            (120*cy>81*cz+120*pad?8:0) | (-120*cy>81*cz+120*pad?16:0);
        outside &= mask;
        if(!outside)return 1;
    }
    return 0;
}

/* Clip world-space wirework at the near plane before the 2D viewport clip. */
IWRAM_CODE void draw_world_line(Vector3 a, Vector3 b, u8 color) {
    Vector3 points[2]={a,b};
    for(int i=0;i<2;i++) {
        fixed x=points[i].x-camera_pos.x, y=points[i].y-camera_pos.y, z=points[i].z-camera_pos.z;
        points[i]=(Vector3){(x*cam_m00+z*cam_m02)>>12,
                            (x*cam_m10+y*cam_m11+z*cam_m12)>>12,
                            (x*cam_m20+y*cam_m21+z*cam_m22)>>12};
    }
    a=points[0]; b=points[1];
    const fixed near=8*FP_SCALE;
    if(a.z<near && b.z<near) return;
    if(a.z<near || b.z<near) {
        int t=((near-a.z)*256)/(b.z-a.z);
        Vector3 clipped={a.x+((b.x-a.x)*t)/256,a.y+((b.y-a.y)*t)/256,near};
        if(a.z<near) a=clipped; else b=clipped;
    }
    int ax,ay,bx,by;
    project_camera_point(a.x,a.y,a.z,&ax,&ay);
    project_camera_point(b.x,b.y,b.z,&bx,&by);
    draw_line(ax,ay,bx,by,color);
}

IWRAM_CODE int project_vertex_world(Vector3 world_pos, int *sx, int *sy) {
    fixed rx = world_pos.x - camera_pos.x;
    fixed ry = world_pos.y - camera_pos.y;
    fixed rz = world_pos.z - camera_pos.z;
    
    fixed x_cam = (rx * cam_m00 + rz * cam_m02) >> 12;
    fixed y_cam = (rx * cam_m10 + ry * cam_m11 + rz * cam_m12) >> 12;
    fixed z_cam = (rx * cam_m20 + ry * cam_m21 + rz * cam_m22) >> 12;
    
    if (z_cam > 8 * FP_SCALE) {
        project_camera_point(x_cam,y_cam,z_cam,sx,sy);
        return 1;
    }
    return 0;
}

/* Camera-facing sphere impostor, drawn in the same painter order as cars.
 * Return 0 only for extreme close-ups, where the clipped mesh is safer.
 * UV steps use integer additions; lighting and panel animation live in ROM. */
IWRAM_CODE int draw_soccer_ball(Vector3 pos, int yaw, int pitch) {
    fixed rx=pos.x-camera_pos.x, ry=pos.y-camera_pos.y, rz=pos.z-camera_pos.z;
    fixed x=(rx*cam_m00+rz*cam_m02)>>12;
    fixed y=(rx*cam_m10+ry*cam_m11+rz*cam_m12)>>12;
    fixed z=(rx*cam_m20+ry*cam_m21+rz*cam_m22)>>12;
    if(z<=0) return 1;
    if(z<28*FP_SCALE) return 0;
    int sx,sy;
    project_camera_point(x,y,z,&sx,&sy);
    int radius=(14*FP_SCALE*120+z/2)/z;
    if(radius<1) radius=1;
    int left=sx-radius,top=sy-radius,diameter=radius*2;
    int x0=left<0?0:left,y0=top<0?0:top;
    int x1=left+diameter,y1=top+diameter;
    if(x1>RENDER_WIDTH) x1=RENDER_WIDTH;
    if(y1>RENDER_HEIGHT) y1=RENDER_HEIGHT;
    if(x0>=x1 || y0>=y1) return 1;
    const u8 *tex=ball_sprite[((yaw+pitch)&255)>>2];
    int step=(64<<16)/diameter;
    int v=(y0-top)*step+step/2;
    for(int py=y0;py<y1;py++,v+=step) {
        const u8 *row=tex+(v>>16)*64;
        u8 *dst=frame_buffer+py*RENDER_WIDTH+x0;
        int u=(x0-left)*step+step/2;
        for(int px=x0;px<x1;px++,u+=step) {
            u8 color=row[u>>16];
            if(color) *dst=color;
            dst++;
        }
    }
    return 1;
}

/* --- Near-plane triangle clipping helper ---
 * Clips a triangle against the near plane z_cam > NEAR and produces
 * 0, 1, or 2 output triangles written into out_tris[0..1].
 * Returns number of output triangles (0-2).
 * All coords are in camera space (x_cam, y_cam, z_cam).
 */
#define NEAR_PLANE (8 * FP_SCALE)

typedef struct { fixed x, y, z; int u, v; } ClipV;

static inline ClipV lerp_clip(ClipV a, ClipV b, fixed t) {
    ClipV r;
    r.x = a.x + ((b.x - a.x) * t >> 8);
    r.y = a.y + ((b.y - a.y) * t >> 8);
    r.z = a.z + ((b.z - a.z) * t >> 8);
    r.u = a.u + ((b.u - a.u) * t >> 8);
    r.v = a.v + ((b.v - a.v) * t >> 8);
    return r;
}

typedef struct { ClipV v[3]; } ClipTri;

static int clip_tri_near(ClipV v0, ClipV v1, ClipV v2, ClipTri *out) {
    /* Most faces are entirely in front: avoid temporary polygon construction. */
    if(v0.z>=NEAR_PLANE && v1.z>=NEAR_PLANE && v2.z>=NEAR_PLANE) {
        out[0].v[0]=v0;out[0].v[1]=v1;out[0].v[2]=v2;
        return 1;
    }
    if(v0.z<NEAR_PLANE && v1.z<NEAR_PLANE && v2.z<NEAR_PLANE) return 0;
    /* Walk edges in source order: regrouping inside/outside vertices reverses
       winding for some cases and makes clipped panels disappear under culling. */
    ClipV verts[3] = {v0, v1, v2}, polygon[4];
    int count = 0;
    for (int i = 0; i < 3; ++i) {
        ClipV a = verts[i], b = verts[(i + 1) % 3];
        int a_in = a.z >= NEAR_PLANE, b_in = b.z >= NEAR_PLANE;
        if (a_in) polygon[count++] = a;
        if (a_in != b_in) {
            fixed t = (fixed)(((int64_t)(NEAR_PLANE - a.z) * 256) / (b.z - a.z));
            ClipV hit = lerp_clip(a, b, t);
            hit.z = NEAR_PLANE;
            polygon[count++] = hit;
        }
    }
    for (int i = 1; i + 1 < count; ++i) {
        out[i - 1].v[0] = polygon[0];
        out[i - 1].v[1] = polygon[i];
        out[i - 1].v[2] = polygon[i + 1];
    }
    return count >= 3 ? count - 2 : 0;
}

static inline void project_clip_v(ClipV cv, int *sx, int *sy) {
    if (cv.z < 1) cv.z = 1;
    project_camera_point(cv.x,cv.y,cv.z,sx,sy);
}

/* --- World-space directional light: ~45° above, from front-right ---
 * Direction (normalised, 8.8 fixed): (0.5, 0.8, -0.3)
 */
#define LIGHT_X  128  /* 0.5  * 256 */
#define LIGHT_Y  205  /* 0.8  * 256 */
#define LIGHT_Z  -77  /* -0.3 * 256 */

/* The renderer has no Z buffer, so sort a model's faces back-to-front before
 * drawing.  The largest mesh currently has 288 faces (the tutorial torus). */
#define MAX_SORTED_FACES 320
static u16 face_order[MAX_SORTED_FACES] __attribute__((section(".ewram"), aligned(4)));
static fixed face_depth[MAX_SORTED_FACES] __attribute__((section(".ewram"), aligned(4)));


void build_model_rotation(int yaw, int pitch, int roll, int32_t mod_m[9]) {
    int32_t sin_x = custom_sin_lut[pitch & 255];
    int32_t cos_x = custom_cos_lut[pitch & 255];
    int32_t sin_y = custom_sin_lut[yaw & 255];
    int32_t cos_y = custom_cos_lut[yaw & 255];
    int32_t sin_z = custom_sin_lut[roll & 255];
    int32_t cos_z = custom_cos_lut[roll & 255];

    int32_t sy_sx = (sin_y * sin_x) >> 12;
    int32_t cy_sx = (cos_y * sin_x) >> 12;

    mod_m[0] = (cos_y * cos_z + sy_sx * sin_z) >> 12;
    mod_m[1] = (-cos_y * sin_z + sy_sx * cos_z) >> 12;
    mod_m[2] = (sin_y * cos_x) >> 12;
    mod_m[3] = (cos_x * sin_z) >> 12;
    mod_m[4] = (cos_x * cos_z) >> 12;
    mod_m[5] = -sin_x;
    mod_m[6] = (-sin_y * cos_z + cy_sx * sin_z) >> 12;
    mod_m[7] = (sin_y * sin_z + cy_sx * cos_z) >> 12;
    mod_m[8] = (cos_y * cos_x) >> 12;

}

IWRAM_CODE int draw_model_world(const Mesh *mesh, Vector3 pos, int yaw, int pitch, int roll, fixed scale, int color_override, int render_mode) {
    int32_t mod_m[9];
    build_model_rotation(yaw, pitch, roll, mod_m);
    return draw_model_world_mat(mesh, pos, mod_m, scale, color_override, render_mode);
}

IWRAM_CODE int draw_model_world_mat(const Mesh *mesh, Vector3 pos, const int32_t mod_m[9], fixed scale, int color_override, int render_mode) {
    /* Reject unsupported meshes before transforming or indexing scratch arrays. */
    if (!mesh || !mesh->vertices || !mesh->faces || !mod_m ||
        mesh->vertex_count > 256 || mesh->face_count > MAX_SORTED_FACES)
        return 0;
    for (int i = 0; i < mesh->face_count; ++i) {
        Face f = mesh->faces[i];
        if (f.v1 >= mesh->vertex_count || f.v2 >= mesh->vertex_count ||
            f.v3 >= mesh->vertex_count) return 0;
    }

    int visible_faces = 0;

    int32_t mod_m00 = mod_m[0]; int32_t mod_m01 = mod_m[1]; int32_t mod_m02 = mod_m[2];
    int32_t mod_m10 = mod_m[3]; int32_t mod_m11 = mod_m[4]; int32_t mod_m12 = mod_m[5];
    int32_t mod_m20 = mod_m[6]; int32_t mod_m21 = mod_m[7]; int32_t mod_m22 = mod_m[8];

    // Combined Rotation Matrix R_total = R_cam * R_model
    int32_t r00 = (cam_m00 * mod_m00 + cam_m02 * mod_m20) >> 12;
    int32_t r01 = (cam_m00 * mod_m01 + cam_m02 * mod_m21) >> 12;
    int32_t r02 = (cam_m00 * mod_m02 + cam_m02 * mod_m22) >> 12;

    int32_t r10 = (cam_m10 * mod_m00 + cam_m11 * mod_m10 + cam_m12 * mod_m20) >> 12;
    int32_t r11 = (cam_m10 * mod_m01 + cam_m11 * mod_m11 + cam_m12 * mod_m21) >> 12;
    int32_t r12 = (cam_m10 * mod_m02 + cam_m11 * mod_m12 + cam_m12 * mod_m22) >> 12;

    int32_t r20 = (cam_m20 * mod_m00 + cam_m21 * mod_m10 + cam_m22 * mod_m20) >> 12;
    int32_t r21 = (cam_m20 * mod_m01 + cam_m21 * mod_m11 + cam_m22 * mod_m21) >> 12;
    int32_t r22 = (cam_m20 * mod_m02 + cam_m21 * mod_m12 + cam_m22 * mod_m22) >> 12;

    // Incorporate model scaling into rotation matrix
    if (scale != FP_ONE) {
        r00 = FP_MUL(r00, scale); r01 = FP_MUL(r01, scale); r02 = FP_MUL(r02, scale);
        r10 = FP_MUL(r10, scale); r11 = FP_MUL(r11, scale); r12 = FP_MUL(r12, scale);
        r20 = FP_MUL(r20, scale); r21 = FP_MUL(r21, scale); r22 = FP_MUL(r22, scale);
    }

    // Translation vector in Camera Space: T = R_cam * (pos - camera_pos)
    fixed rx = pos.x - camera_pos.x;
    fixed ry = pos.y - camera_pos.y;
    fixed rz = pos.z - camera_pos.z;

    fixed tx = (rx * cam_m00 + rz * cam_m02) >> 12;
    fixed ty = (rx * cam_m10 + ry * cam_m11 + rz * cam_m12) >> 12;
    fixed tz = (rx * cam_m20 + ry * cam_m21 + rz * cam_m22) >> 12;



    int v_count = mesh->vertex_count;
    if (v_count > 256) v_count = 256;

    // Phase 1: Vertex Transformation (Single Combined Multiply) & Fast Projection
    for (int i = 0; i < v_count; i++) {
        Vector3 v = mesh->vertices[i];

        fixed x_cam = ((r00 * v.x + r01 * v.y + r02 * v.z) >> 12) + tx;
        fixed y_cam = ((r10 * v.x + r11 * v.y + r12 * v.z) >> 12) + ty;
        fixed z_cam = ((r20 * v.x + r21 * v.y + r22 * v.z) >> 12) + tz;

        model_vertices[i].x = x_cam;
        model_vertices[i].y = y_cam;
        model_vertices[i].z = z_cam;

        if (z_cam > 8 * FP_SCALE) {
            project_camera_point(x_cam,y_cam,z_cam,&screen_x[i],&screen_y[i]);
        } else {
            screen_x[i] = -999;
            screen_y[i] = -999;
        }
    }

    /* Phase 2: painter sort faces from furthest to nearest.  Sorting on the
       camera-space centroid prevents body panels, wheels, and the ball from
       drawing through one another on the GBA's framebuffer renderer. */
    int face_count = 0;
    for (int i = 0; i < mesh->face_count; i++) {
        Face f = mesh->faces[i];
        fixed z0 = model_vertices[f.v1].z, z1 = model_vertices[f.v2].z, z2 = model_vertices[f.v3].z;
        if (z0 < NEAR_PLANE && z1 < NEAR_PLANE && z2 < NEAR_PLANE) continue;
        /* Cull before sorting/lighting. Crossing triangles still take the full
         * clipping path, preserving their interpolated UVs and winding. */
        if (z0 > NEAR_PLANE && z1 > NEAR_PLANE && z2 > NEAR_PLANE) {
            int x0 = screen_x[f.v1], x1 = screen_x[f.v2], x2 = screen_x[f.v3];
            int y0 = screen_y[f.v1], y1 = screen_y[f.v2], y2 = screen_y[f.v3];
            if ((x1-x0)*(y2-y0) - (y1-y0)*(x2-x0) <= 0) continue;
            if ((x0 < 0 && x1 < 0 && x2 < 0) ||
                (x0 >= RENDER_WIDTH && x1 >= RENDER_WIDTH && x2 >= RENDER_WIDTH) ||
                (y0 < 0 && y1 < 0 && y2 < 0) ||
                (y0 >= RENDER_HEIGHT && y1 >= RENDER_HEIGHT && y2 >= RENDER_HEIGHT)) continue;
        }
        fixed depth = (z0 + z1 + z2) / 3;
        int j = face_count - 1;
        while (j >= 0 && face_depth[j] < depth) {
            face_order[j + 1] = face_order[j];
            face_depth[j + 1] = face_depth[j];
            j--;
        }
        face_order[j + 1] = (u16)i;
        face_depth[j + 1] = depth;
        face_count++;
    }

    /* Transform the light once into model space, rather than rotating every
     * visible face normal into world space (nine multiplies per face). */
    fixed light_x = (mod_m00 * LIGHT_X + mod_m10 * LIGHT_Y + mod_m20 * LIGHT_Z) >> 12;
    fixed light_y = (mod_m01 * LIGHT_X + mod_m11 * LIGHT_Y + mod_m21 * LIGHT_Z) >> 12;
    fixed light_z = (mod_m02 * LIGHT_X + mod_m12 * LIGHT_Y + mod_m22 * LIGHT_Z) >> 12;

    // Phase 3: face processing (culling, lighting, rasterizing)
    for (int sorted = 0; sorted < face_count; sorted++) {
        int i = face_order[sorted];
        Face f = mesh->faces[i];

        /* --- Lighting: world-space directional light dot product --- */
        int intensity;
        if (mesh->face_normals) {
            const Vector3 *normal = &mesh->face_normals[i];
            fixed dot = (normal->x * light_x + normal->y * light_y + normal->z * light_z) >> 8;
            /* Stadium fill light keeps unlit paint readable during a flip. */
            intensity = 10 + ((dot * 5) >> 8);
        } else {
            intensity = 8; /* flat fallback */
        }
        if (intensity < 4) intensity = 4;
        if (intensity > 15) intensity = 15;

        /* Color overrides tint painted panels, while direct palette materials
           such as the tyres remain black. */
        u8 base_col = (color_override >= 0 && f.base_color <= 7)
            ? (u8)color_override : f.base_color;
        u8 color = material_shade(base_col, intensity);

        int face_mode=render_mode;
        if(render_mode==RENDER_ACCENTS) {
            int tile=(f.uv[0][1]/32)*4+f.uv[0][0]/16;
            face_mode=tz>48*FP_SCALE && (tile==0 || tile==2 || tile==3 || tile==4 || tile==5 || tile==6)?RENDER_TEXTURED:RENDER_FLAT;
        }
        /* The sorting pass already culled fully projected faces. Flat cars
           need neither UV copying nor the general near-plane clipper here. */
        if(face_mode==RENDER_FLAT && model_vertices[f.v1].z>NEAR_PLANE &&
           model_vertices[f.v2].z>NEAR_PLANE && model_vertices[f.v3].z>NEAR_PLANE) {
            int x0=screen_x[f.v1],y0=screen_y[f.v1];
            int x1=screen_x[f.v2],y1=screen_y[f.v2];
            int x2=screen_x[f.v3],y2=screen_y[f.v3];
            if((unsigned)x0<240 && (unsigned)x1<240 && (unsigned)x2<240 &&
               (unsigned)y0<160 && (unsigned)y1<160 && (unsigned)y2<160)
                draw_triangle_flat_unclipped(x0,y0,x1,y1,x2,y2,color);
            else draw_triangle_flat_clipped(x0,y0,x1,y1,x2,y2,color);
            visible_faces++;
            continue;
        }

        /* --- Near-plane clipping --- */
        ClipV cv0 = { model_vertices[f.v1].x, model_vertices[f.v1].y, model_vertices[f.v1].z, f.uv[0][0], f.uv[0][1] };
        ClipV cv1 = { model_vertices[f.v2].x, model_vertices[f.v2].y, model_vertices[f.v2].z, f.uv[1][0], f.uv[1][1] };
        ClipV cv2 = { model_vertices[f.v3].x, model_vertices[f.v3].y, model_vertices[f.v3].z, f.uv[2][0], f.uv[2][1] };

        ClipTri clipped[2];
        int n_tris = clip_tri_near(cv0, cv1, cv2, clipped);
        if (n_tris == 0) continue;

        for (int ti = 0; ti < n_tris; ti++) {
            int x0, y0, x1, y1, x2, y2;
            if (n_tris == 1 && ti == 0 &&
                cv0.z > NEAR_PLANE && cv1.z > NEAR_PLANE && cv2.z > NEAR_PLANE) {
                /* Unclipped fast path: use precomputed screen coords */
                x0 = screen_x[f.v1]; y0 = screen_y[f.v1];
                x1 = screen_x[f.v2]; y1 = screen_y[f.v2];
                x2 = screen_x[f.v3]; y2 = screen_y[f.v3];
            } else {
                project_clip_v(clipped[ti].v[0], &x0, &y0);
                project_clip_v(clipped[ti].v[1], &x1, &y1);
                project_clip_v(clipped[ti].v[2], &x2, &y2);
            }

            /* 2D screen-space backface culling */
            int cross = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
            if (cross <= 0) continue;

            /* Screen frustum rejection */
            if ((x0 < 0 && x1 < 0 && x2 < 0) ||
                (x0 >= RENDER_WIDTH && x1 >= RENDER_WIDTH && x2 >= RENDER_WIDTH) ||
                (y0 < 0 && y1 < 0 && y2 < 0) ||
                (y0 >= RENDER_HEIGHT && y1 >= RENDER_HEIGHT && y2 >= RENDER_HEIGHT)) continue;

            visible_faces++;

            int is_fully = (x0 >= 0 && x1 >= 0 && x2 >= 0 &&
                            x0 < RENDER_WIDTH && x1 < RENDER_WIDTH && x2 < RENDER_WIDTH &&
                            y0 >= 0 && y1 >= 0 && y2 >= 0 &&
                            y0 < RENDER_HEIGHT && y1 < RENDER_HEIGHT && y2 < RENDER_HEIGHT);

            if (face_mode == RENDER_FLAT) {
                if (is_fully) draw_triangle_flat_unclipped(x0,y0,x1,y1,x2,y2,color);
                else          draw_triangle_flat_clipped(x0,y0,x1,y1,x2,y2,color);
            } else if (face_mode == RENDER_TEXTURED) {
                int u0 = clipped[ti].v[0].u; int v0 = clipped[ti].v[0].v;
                int u1 = clipped[ti].v[1].u; int v1 = clipped[ti].v[1].v;
                int u2 = clipped[ti].v[2].u; int v2 = clipped[ti].v[2].v;
                if (n_tris == 1 && ti == 0 &&
                    cv0.z > NEAR_PLANE && cv1.z > NEAR_PLANE && cv2.z > NEAR_PLANE) {
                    u0 = f.uv[0][0]; v0 = f.uv[0][1]; u1 = f.uv[1][0]; v1 = f.uv[1][1]; u2 = f.uv[2][0]; v2 = f.uv[2][1];
                }
                if (is_fully) draw_triangle_textured_unclipped(x0,y0,u0,v0, x1,y1,u1,v1, x2,y2,u2,v2, car_texture, color);
                else          draw_triangle_textured_clipped(x0,y0,u0,v0, x1,y1,u1,v1, x2,y2,u2,v2, car_texture, color);
            } else if (render_mode == RENDER_OUTLINED || render_mode == RENDER_OUTLINED_WHITE) {
                if (is_fully) draw_triangle_flat_unclipped(x0,y0,x1,y1,x2,y2,color);
                else          draw_triangle_flat_clipped(x0,y0,x1,y1,x2,y2,color);
                
                u8 outline_color = (render_mode == RENDER_OUTLINED_WHITE) ? 130 : 0; // 130 is white, 0 is black
                draw_line(x0,y0,x1,y1,outline_color);
                draw_line(x1,y1,x2,y2,outline_color);
                draw_line(x2,y2,x0,y0,outline_color);
            } else if (render_mode == RENDER_WIREFRAME) {
                u8 wc = material_wire_color(base_col);
                draw_line(x0,y0,x1,y1,wc);
                draw_line(x1,y1,x2,y2,wc);
                draw_line(x2,y2,x0,y0,wc);
            } else if (render_mode == RENDER_POINT_CLOUD) {
                draw_point(x0,y0,color);
                draw_point(x1,y1,color);
                draw_point(x2,y2,color);
            }
        }
    }

    return visible_faces;
}

void build_dodge_rotation(int yaw, int pitch_dir, int roll_dir, int angle, int32_t mod_m[9]) {
    if (!pitch_dir || !roll_dir) {
        build_model_rotation(yaw, pitch_dir * angle, roll_dir * angle, mod_m);
        return;
    }
    int32_t c = custom_cos_lut[angle];
    int32_t s = custom_sin_lut[angle];
    int32_t v = 4096 - c;

    // Normalized diagonal axis components (0.707 * 4096 = 2896)
    int32_t ux = pitch_dir * 2896;
    int32_t uz = roll_dir * 2896;

    int32_t r_flip[9];
    r_flip[0] = ((ux * ux >> 12) * v >> 12) + c;
    r_flip[1] = (-uz * s) >> 12;
    r_flip[2] = ((ux * uz >> 12) * v) >> 12;

    r_flip[3] = (uz * s) >> 12;
    r_flip[4] = c;
    r_flip[5] = (-ux * s) >> 12;

    r_flip[6] = ((ux * uz >> 12) * v) >> 12;
    r_flip[7] = (ux * s) >> 12;
    r_flip[8] = ((uz * uz >> 12) * v >> 12) + c;

    // Base Yaw matrix
    int32_t sy = custom_sin_lut[yaw & 255];
    int32_t cy = custom_cos_lut[yaw & 255];

    // Combined Mod matrix: Ry * R_flip
    mod_m[0] = (cy * r_flip[0] + sy * r_flip[6]) >> 12;
    mod_m[1] = (cy * r_flip[1] + sy * r_flip[7]) >> 12;
    mod_m[2] = (cy * r_flip[2] + sy * r_flip[8]) >> 12;

    mod_m[3] = r_flip[3];
    mod_m[4] = r_flip[4];
    mod_m[5] = r_flip[5];

    mod_m[6] = (-sy * r_flip[0] + cy * r_flip[6]) >> 12;
    mod_m[7] = (-sy * r_flip[1] + cy * r_flip[7]) >> 12;
    mod_m[8] = (-sy * r_flip[2] + cy * r_flip[8]) >> 12;

}
