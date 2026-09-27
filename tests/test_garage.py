"""Exercise actual car meshes and showroom renderer on host; write preview PPMs."""
from pathlib import Path
import subprocess, tempfile, os, re
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    d=Path(tmp)
    for name in ('achievements.h','achievements.c','ball_sprite.inc','engine3d.h','engine3d.c','render.h','render.c','models.h','models.c','car_models.inc','stadium.c','stadium.h'):
        s=(Path(os.environ['STADIUM_SOURCE']) if name=='stadium.c' and 'STADIUM_SOURCE' in os.environ else root/name).read_text()
        if name=='engine3d.c':
            s='int test_disable_flat_fastpath;\nlong test_vertices,test_faces,test_projections;\n'+s
            s=s.replace('if(face_mode==RENDER_FLAT && model_vertices', 'if(!test_disable_flat_fastpath && face_mode==RENDER_FLAT && model_vertices')
            s=s.replace('int project_vertex_world(Vector3 world_pos, int *sx, int *sy) {','int project_vertex_world(Vector3 world_pos, int *sx, int *sy) { ++test_projections;')
            s=s.replace('    /* Reject unsupported meshes before transforming', '    if(mesh){test_vertices+=mesh->vertex_count;test_faces+=mesh->face_count;}\n    /* Reject unsupported meshes before transforming')
        if name=='engine3d.c' and os.environ.get('STADIUM_PROFILE'):
            s='long profile_project, profile_lines, profile_meshes;\n'+s
            s=s.replace('int project_vertex_world(Vector3 world_pos, int *sx, int *sy) {', 'int project_vertex_world(Vector3 world_pos, int *sx, int *sy) { ++profile_project;')
            s=s.replace('void draw_world_line(Vector3 a, Vector3 b, u8 color) {', 'void draw_world_line(Vector3 a, Vector3 b, u8 color) { ++profile_lines;')
            s=s.replace('int render_mode) {\n    /* Reject unsupported', 'int render_mode) {\n    ++profile_meshes;\n    /* Reject unsupported')
        s=re.sub(r'__attribute__\(\(section\("\.iwram"\), target\("arm"\), long_call\)\)', '',s)
        (d/name).write_text(s)
    (d/'tonc.h').write_text('''#pragma once
#include <stdint.h>
#include <math.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32;
extern long test_vertices,test_faces,test_projections;
extern u16 pal_bg_mem[256];
extern u32 REG_DISPCNT, REG_BG2PA, REG_BG2PD, REG_BG2PB, REG_BG2PC, REG_BG2X, REG_BG2Y;
extern u32 REG_DMA3SAD,REG_DMA3DAD,REG_DMA3CNT;
#define DCNT_MODE4 0
#define DCNT_BG2 0
#define DCNT_PAGE 0
#define DMA_ENABLE 0
#define DMA_32 0
#define REG_VCOUNT 160
#define VBlankIntrWait() ((void)0)
static inline int lu_sin(int a) { return lround(sin(a*6.283185307179586/65536)*4096); }
static inline int lu_cos(int a) { return lround(cos(a*6.283185307179586/65536)*4096); }
static inline void memcpy32(void *p,const void *s,int n) { u32 *q=p; const u32 *r=s; while(n--) *q++=*r++; }
static inline void memset32(void *p,u32 v,int n) { u32 *q=p; while(n--) *q++=v; }
''')
    main=(root/'main.c').read_text()
    state=main[main.index('static int garage_side'):main.index('static int pause_selection')]
    garage=main[main.index('static void draw_garage'):main.index('static void draw_menu_screen')]
    hud=main[main.index("static void draw_hud_box"):main.index("static void draw_ball_indicator")]
    menus=main[main.index("static int menu_text_width"):main.index("static void draw_centered_text_line")]
    shadows=main[main.index("static void draw_car_shadow"):main.index("static void radar_point")]
    hud_text=main[main.index('static void draw_hud_text'):main.index('static void draw_hud_box')]
    match_hud=main[main.index('static void draw_ball_indicator'):main.index('/* --- Main Application Frame logic')]
    pitch=main[main.index('static const Vector3 center_circle_pts'):main.index('/* Small floodlight')]
    radar=main[main.index('static void radar_point'):main.index('/* --- Big Minimap')]
    pitch_constants='\n'.join(line for line in main.splitlines() if line.startswith(('#define STADIUM_', '#define CAGE_', '#define GOAL_HALF_WIDTH', '#define GOAL_HEIGHT')))
    car_code=main[main.index('typedef struct {\n    Vector3 pos;\n    Vector3 vel;'):main.index('\n\ntypedef struct {\n    Vector3 pos;\n    Vector3 vel;')]
    boost_code=main[main.index('static void apply_player_boost'):main.index('/* --- Physics Core Logic --- */')]
    ball_physics=main[main.index('void update_ball_physics'):main.index('/* --- Sphere-to-Sphere Car-Ball Collision')]
    collisions=main[main.index('/* --- Sphere-to-Sphere Car-Ball Collision --- */'):main.index('/* --- Fast Atan2')]
    physics=main[main.index('void update_car_physics'):main.index('void update_ball_physics')]
    particle_code=main[main.index('typedef struct {',main.index('/* --- Particles --- */')):main.index('enum {',main.index('/* --- Particles --- */'))]
    particle_spawn=main[main.index('void spawn_boost_particle'):main.index('void spawn_skid_particle')]
    constants='\n'.join(line for line in main.splitlines() if line.startswith(('#define FLIP_', '#define BOOST_ACCEL', '#define MAX_DRIVE_SPEED', '#define ACCEL_RATE', '#define GRAVITY', '#define DRAG_COEFF', '#define CAR_RADIUS', '#define BALL_RADIUS', '#define PUCK_', '#define JUMP_FORCE')))
    harness='''static void audio_impact(int strength){(void)strength;}
#include "achievements.c"
#include "models.h"
#include "stadium.h"
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
u16 pal_bg_mem[256];
u32 REG_DISPCNT, REG_BG2PA, REG_BG2PD, REG_BG2PB, REG_BG2PC, REG_BG2X, REG_BG2Y;
u32 REG_DMA3SAD,REG_DMA3DAD,REG_DMA3CNT;
static void draw_centered_text_line(const char *s,int y,u8 c) {draw_string(s,(240-strlen(s)*8)/2,y,c);}
static void save_preview(const char *name) {
    FILE *out=fopen(name,"wb"); assert(out);
    fprintf(out,"P6\\n240 160\\n255\\n");
    for(int i=0;i<240*160;i++) {
        unsigned c=pal_bg_mem[frame_buffer[i]];
        fputc((c&31)*255/31,out); fputc(((c>>5)&31)*255/31,out); fputc(((c>>10)&31)*255/31,out);
    }
    fclose(out);
}
'''+ 'static int animation_ticks=2;\n'+state+hud+menus+'static int is_hockey_match;\n'+shadows+garage+constants+'\n'+car_code+particle_code+particle_spawn+'''
static Car player,opponent;
static int enable_opponent=1;
static struct {Vector3 pos,vel;} ball;
static int game_state, show_boost_alert;
enum {STATE_TUTORIAL=99,STATE_REPLAY,STATE_TRAINING,STATE_TRAINING_GOAL,STATE_GOAL};
enum {TUTORIAL_AIM_SHOT=1};
static int current_tutorial_stage,screen_shake;
static struct {int objective;} tutorial_stages[1];
static void spawn_explosion(Vector3 p,u8 c){}
static void spawn_goal_celebration(Vector3 p,u8 c){}
static int measured_fps=30,control_scheme;
static int score_blue=2,score_orange=1,match_timer=3600,cam_mode,scoring_team=3,state_timer=100;
static struct {int boosted,goal_scored;} tutorial_progress;
'''+pitch_constants+'\n'+pitch+radar+boost_code+physics+ball_physics+collisions+hud_text+match_hud+'''
int main(void) {
    init_3d_engine(); init_dynamic_models(); init_mesh_normals();
    /* Compare the flat fast path with the general clipper, including clipped
       near-camera geometry and off-screen edges through a full rotation. */
    {
        extern int test_disable_flat_fastpath;
        static u8 reference[240*160];
        performance_mode=2;
        for(int yaw=0;yaw<256;yaw+=16)for(int distance=12;distance<=160;distance+=37) {
            set_camera((Vector3){0,8*256,-distance*256},0,0);
            clear_screen(128);test_disable_flat_fastpath=1;
            int slow=draw_model_world(car_far_models[0],(Vector3){0,0,0},yaw,0,0,256,3,RENDER_FLAT);
            memcpy(reference,frame_buffer,sizeof(reference));
            clear_screen(128);test_disable_flat_fastpath=0;
            int fast=draw_model_world(car_far_models[0],(Vector3){0,0,0},yaw,0,0,256,3,RENDER_FLAT);
            assert(fast==slow && !memcmp(reference,frame_buffer,sizeof(reference)));
        }
        performance_mode=0;
    }
    /* Mixed grass/ramp footprint keeps each surface's own shadow tint. */
    for(int i=0;i<12;i++)frame_buffer[i]=i<4?135:i<8?154:SHADOW_RAMP_START+3;
    fast_span_fill(frame_buffer,SHADOW_GRASS_EDGE*0x01010101u,12);
    for(int i=0;i<4;i++)assert(frame_buffer[i]==SHADOW_GRASS_EDGE);
    for(int i=4;i<12;i++)assert(frame_buffer[i]==SHADOW_RAMP_START+3);
    fast_span_fill(frame_buffer,SHADOW_GRASS_CORE*0x01010101u,12);
    for(int i=4;i<12;i++)assert(frame_buffer[i]==SHADOW_RAMP_START+2);
    performance_mode=0;
    {
        Car c={0};apply_air_rotation(&c,1,-1);
        assert(c.visual_pitch==3 && c.visual_roll==252);
        apply_air_rotation(&c,-1,1);assert(!c.visual_pitch && !c.visual_roll);
        draw_achievements(2);save_preview("achievements.ppm");
        assert(frame_buffer[27*240+83]==131);
        achievement_add(ACH_GOAL,1);achievement_add(ACH_TEN_GOALS,4);achievement_add(ACH_WALL,90);
        draw_achievements(4);save_preview("achievements-progress.ppm");
        for(int i=0;i<ACH_COUNT;i++) {
            draw_achievements(i);
            int x=7+(i%6)*38,y=27+(i/6)*20;
            assert(frame_buffer[y*240+x]==131);
        }
        for(int i=0;i<ACH_COUNT;i++)achievement_add(i,achievement_targets[i]);
        draw_achievements(23);save_preview("achievements-complete.ppm");
        achievements_init(0);
        int speeds[2];
        for(int hockey=0;hockey<2;hockey++) {
            is_hockey_match=hockey;
            ball.pos=(Vector3){0,hockey?PUCK_HALF_HEIGHT:BALL_RADIUS,0};
            ball.vel=(Vector3){0,0,8*256};
            for(int i=0;i<20;i++)update_ball_physics();
            speeds[hockey]=ball.vel.z;
            assert(ball.pos.y==(hockey?PUCK_HALF_HEIGHT:BALL_RADIUS));
        }
        assert(speeds[1]>speeds[0]);
        ball.pos=(Vector3){0,8*256,0};ball.vel=(Vector3){0,-3*256,0};
        for(int i=0;i<5;i++)update_ball_physics();
        assert(ball.pos.y==PUCK_HALF_HEIGHT && ball.vel.y==0);
        c=(Car){0};c.speed=4*256;c.is_on_ground=1;
        ball.pos=(Vector3){0,PUCK_HALF_HEIGHT,20*256};ball.vel=(Vector3){0,0,0};
        assert(check_car_ball_collision(&c));assert(ball.vel.z>0 && ball.vel.y==0);
        assert(ball.pos.y==PUCK_HALF_HEIGHT);
        is_hockey_match=0;
    }
    /* Controlled touches: one speed-proportional impulse, no launch bonuses. */
    for(int speed=1;speed<=20;speed++) {
        Car c={0};c.pos.y=BALL_RADIUS;c.speed=speed*256;c.is_on_ground=1;
        ball.pos=(Vector3){0,BALL_RADIUS,26*256};ball.vel=(Vector3){0,0,0};
        assert(check_car_ball_collision(&c));
        assert(ball.vel.z>=speed*256 && ball.vel.z<=speed*308);
        assert(ball.vel.z<=22*256 && ball.vel.y==0);
    }
    {
        Car c={0};c.pos.y=BALL_RADIUS;c.speed=4*256;
        ball.pos=(Vector3){0,BALL_RADIUS,26*256};ball.vel=(Vector3){3*256,0,6*256};
        assert(check_car_ball_collision(&c));
        assert(ball.vel.x==3*256 && ball.vel.z==6*256); /* separating */
        ball.pos=(Vector3){0,BALL_RADIUS,26*256};ball.vel=(Vector3){3*256,0,0};
        assert(check_car_ball_collision(&c));assert(ball.vel.x==3*256); /* tangent */
        c=(Car){0};c.pos.y=BALL_RADIUS+20*256;c.vel.y=-4*256;
        ball.pos=(Vector3){0,BALL_RADIUS,0};ball.vel=(Vector3){0,0,0};
        assert(check_car_ball_collision(&c));assert(ball.vel.y<0 && ball.pos.y==BALL_RADIUS); /* downward hit stays above floor */
        c=(Car){0};c.pos.y=BALL_RADIUS;c.vel=(Vector3){40*256,40*256,40*256};
        ball.pos=(Vector3){14*256,BALL_RADIUS+14*256,14*256};ball.vel=(Vector3){0,0,0};
        assert(check_car_ball_collision(&c));
        int x=ball.vel.x/16,y=ball.vel.y/16,z=ball.vel.z/16;
        assert(x*x+y*y+z*z<=(22*16+1)*(22*16+1));
        ball.pos=(Vector3){0,BALL_RADIUS,0};ball.vel=(Vector3){0,-64,0};
        update_ball_physics();assert(ball.pos.y==BALL_RADIUS && ball.vel.y==0);
    }
    /* Even almost coincident diagonal contacts have unit-length normals. */
    for(int x=-32;x<=32;x++)for(int y=-32;y<=32;y++) {
        Vector3 n=contact_normal(x,y,17);
        assert(n.x*n.x+n.y*n.y+n.z*n.z<=256*256);
        Vector3 opposite=contact_normal(-x,-y,-17);
        assert(n.x==-opposite.x && n.y==-opposite.y && n.z==-opposite.z);
    }
    /* Soft floor contacts settle, but deliberate upward motion survives. */
    {
        is_hockey_match=0;
        ball.pos=(Vector3){0,BALL_RADIUS,0};ball.vel=(Vector3){0,-220,0};
        update_ball_physics();assert(ball.pos.y==BALL_RADIUS && ball.vel.y==0);
        for(int i=0;i<60;i++)update_ball_physics();
        assert(ball.pos.y==BALL_RADIUS && ball.vel.y==0);
        ball.pos=(Vector3){0,BALL_RADIUS-100,0};ball.vel=(Vector3){0,64,0};
        update_ball_physics();assert(ball.pos.y==BALL_RADIUS && ball.vel.y>0);
        ball.pos=(Vector3){0,BALL_RADIUS,0};ball.vel=(Vector3){0,-4*256,0};
        update_ball_physics();assert(ball.vel.y>2*256);
    }
    /* Mirrored contacts must impart identical speed in opposite directions. */
    {
        int result[2];
        for(int side=0;side<2;side++) {
            int sign=side?1:-1;Car c={0};c.pos.y=BALL_RADIUS;c.vel.x=sign*4*256;
            ball.pos=(Vector3){sign*26*256,BALL_RADIUS,0};ball.vel=(Vector3){0,0,0};
            assert(check_car_ball_collision(&c));result[side]=ball.vel.x;
            assert(ball.pos.y>=BALL_RADIUS);
        }
        assert(result[0]==-result[1]);
    }
    /* Bulk horizontal strokes must preserve inclusive endpoints and clipping. */
    for(int direction=0;direction<2;direction++) for(int y=0;y<160;y+=17) {
        memset(frame_buffer,0,240*160);
        draw_line(direction?300:-40,y,direction?-40:300,y,130);
        for(int row=0;row<160;row++)for(int x=0;x<240;x++)
            assert(frame_buffer[row*240+x]==(row==y?130:0));
        memset(frame_buffer,0,240*160);
        draw_line(direction?117:11,y,direction?11:117,y,130);
        for(int x=0;x<240;x++)assert(frame_buffer[y*240+x]==(x>=11 && x<=117?130:0));
    }
    /* Steering softens at speed, builds progressively and stops on release. */
    {
        Car slow={0},fast={0};slow.is_on_ground=fast.is_on_ground=1;
        fast.speed=12*256;
        steer_car(&slow,1,0,0);assert(slow.yaw==0);
        assert(slow.steer_velocity==36 && slow.steer_fraction==36);
        for(int i=0;i<19;i++)steer_car(&slow,1,0,0);
        for(int i=0;i<20;i++)steer_car(&fast,1,0,0);
        assert(slow.yaw>fast.yaw && fast.yaw>0);
        assert(slow.yaw<=11 && fast.yaw<=5);
        int yaw=slow.yaw,fraction=slow.steer_fraction;steer_car(&slow,0,0,0);
        assert(slow.yaw==yaw && !slow.steer_velocity && slow.steer_fraction==fraction);
        steer_car(&slow,-1,0,1);assert(slow.yaw==yaw);
        Car grip={0};grip.is_on_ground=1;grip.vel=(Vector3){8*256,0,12*256};
        grip_car(&grip,0,0);assert(grip.vel.x==5*256 && grip.vel.z==12*256);
        grip_car(&grip,0,1);assert(grip.vel.z<12*256);
        Car drift={0};drift.is_on_ground=1;drift.vel=(Vector3){8*256,0,12*256};
        grip_car(&drift,1,0);assert(drift.vel.x==8*256 && drift.vel.z==12*256);
        drift.is_on_ground=0;grip_car(&drift,0,1);
        assert(drift.vel.x==8*256 && drift.vel.z==12*256);
    }
    /* Fractional steering moves both the mesh and camera without whole-angle jumps. */
    {
        Car c={0};int32_t before[9],after[9];build_car_rotation(&c,before);
        c.steer_fraction=128;build_car_rotation(&c,after);
        assert(after[2]>before[2]);
        chase_turn_velocity=0;assert(follow_chase_heading(0,&c)>0);
        steer_car(&c,0,0,0);assert(c.steer_fraction==128);
    }
    /* Fuel controls thrust strength; diagonal velocity shares one cap. */
    {
        player=(Car){0};player.boost=310;player.boost_requested=1;
        apply_player_boost();int full=player.vel.z;
        player=(Car){0};player.boost=31;player.boost_requested=1;
        apply_player_boost();assert(player.vel.z>0 && player.vel.z<full/8 && !player.boost);
        Vector3 velocity={19*256,19*256,19*256};int cap=198*256/10;
        limit_boost_velocity(&velocity,cap);
        assert(velocity.x==velocity.y && velocity.y==velocity.z);
        assert(velocity.x*velocity.x+velocity.y*velocity.y+velocity.z*velocity.z<=cap*cap);
        velocity=(Vector3){0,0,10*256};limit_boost_velocity(&velocity,cap);
        assert(velocity.z==10*256);
    }
    /* Recovery is explicit, gradual, and disabled in the air. */
    {
        Car c={0};c.is_on_ground=1;c.visual_roll=128;c.settle_delay=4;
        assist_ground_recovery(&c,0);assert(c.visual_roll==128 && c.settle_delay==4);
        assist_ground_recovery(&c,1);assert(c.visual_roll!=0 && !c.settle_delay);
        for(int i=0;i<80;i++)assist_ground_recovery(&c,1);
        assert(!car_needs_recovery(&c));
        c.is_on_ground=0;c.visual_roll=128;
        assist_ground_recovery(&c,1);assert(c.visual_roll==128);
    }
    /* 50% game time: 120 displayed frames advance 120 simulation ticks,
       with identical displayed travel on regular and delayed frames. */
    {
        unsigned phase=0;int steps_total=0;
        Vector3 previous={0,0,0},current={0,0,0},last={-256,0,0};
        for(int frame=0;frame<120;frame++) {
            int steps=scaled_simulation_steps(2,&phase);steps_total+=steps;
            for(int i=0;i<steps;i++){previous=current;current.x+=256;}
            Vector3 shown=blend_position(previous,current,phase*64);
            assert(shown.x-last.x==256);last=shown;
        }
        assert(steps_total==120 && !phase);
        assert(blend_heading(255*256,256,128)==0);
        phase=0;steps_total=0;
        for(int i=0;i<240;i++)steps_total+=scaled_simulation_steps(1,&phase);
        assert(steps_total==120 && !phase);
    }
    /* Blended rotation stays rigid, including diagonal flips and yaw wrap. */
    for(int yaw=0;yaw<256;yaw+=32)for(int fraction=64;fraction<256;fraction+=64) {
        Car before={0},after={0};before.yaw=yaw;after.yaw=(yaw+32)&255;
        after.flip_timer=FLIP_DURATION_TICKS-3*FLIP_STEP_TICKS;
        after.flip_pitch_dir=1;after.flip_roll_dir=-1;
        int32_t matrix[9];blend_car_rotation(&before,&after,fraction,matrix);
        for(int col=0;col<3;col++) {
            int length=0;for(int row=0;row<3;row++)length+=matrix[row*3+col]*matrix[row*3+col];
            assert(abs(length-4096*4096)<70000);
            for(int other=col+1;other<3;other++) {
                int dot=0;for(int row=0;row<3;row++)dot+=matrix[row*3+col]*matrix[row*3+other];
                assert(abs(dot)<20000);
            }
        }
    }
    /* Camera interpolation takes the short path, without overshoot or stalls. */
    for(int start=0;start<256;start+=8)for(int target=0;target<256;target+=8) {
        int heading=start*256;
        for(int step=0;step<192;step++) {
            int next=smooth_camera_heading(heading,target);
            int delta=(next-heading)&65535;if(delta>32768)delta-=65536;
            assert(abs(delta)<=3*256);
            int old=(target*256-heading)&65535;if(old>32768)old-=65536;
            int remaining=(target*256-next)&65535;if(remaining>32768)remaining-=65536;
            assert(abs(remaining)<=abs(old));heading=next;
        }
        assert(heading==target*256);
    }
    assert(smooth_camera_heading(0,1)>0 && smooth_camera_heading(0,1)<256);
    /* Chase follows immediately with only a small eased remainder. */
    {
        Car c={0};c.yaw=1;chase_turn_velocity=0;
        int heading=follow_chase_heading(0,&c);
        assert(heading>=224 && heading<256);
        for(int i=0;i<8;i++)heading=follow_chase_heading(heading,&c);
        assert(heading==256 && !chase_turn_velocity);
        c.yaw=16;heading=0;chase_turn_velocity=0;
        for(int i=0;i<16;i++) {
            heading=follow_chase_heading(heading,&c);
            assert(heading<=16*256);
        }
        assert(heading==16*256);
        c.yaw=1;heading=255*256;chase_turn_velocity=0;
        for(int i=0;i<8;i++)heading=follow_chase_heading(heading,&c);
        assert(heading==256 && !chase_turn_velocity);
    }

    for(int angle=0;angle<256;angle++) {
        assert(camera_sin_q8(angle*256)==custom_sin_lut[angle]);
        int a=camera_sin_q8(angle*256), b=camera_sin_q8((angle+1)*256);
        int mid=camera_sin_q8(angle*256+128);
        assert(abs(mid-(a+b)/2)<=1);
    }
    /* Holding jump extends lift; releasing it permanently ends this boost. */
    {
        int peak[2]={0,0};
        for(int held=0;held<2;held++) {
            Car c={0};c.is_on_ground=1;jump_from_surface(&c,(JUMP_FORCE*3)/5);
            for(int i=0;i<100;i++) {
                extend_jump(&c,held);update_car_physics(&c,0);
                if(c.pos.y>peak[held])peak[held]=c.pos.y;
            }
            assert(c.is_on_ground);
        }
        assert(peak[1]>peak[0]);
        Car c={0};c.is_on_ground=1;jump_from_surface(&c,3*256);
        extend_jump(&c,0);fixed vy=c.vel.y;
        extend_jump(&c,1);assert(c.vel.y==vy && c.jump_hold==0);
    }
    /* Surface changes preserve the impact attitude, including upside-down poses. */
    for(int wall=-2;wall<=2;wall++)for(int pitch=0;pitch<256;pitch+=32)for(int roll=0;roll<256;roll+=32) {
        Car c={0};c.yaw=32;c.visual_pitch=pitch;c.visual_roll=roll;
        c.surface_wall=wall;c.surface_angle=wall?64:0;
        int32_t before[9],after[9];build_car_rotation(&c,before);
        retain_landing_rotation(&c,0,0);c.surface_wall=c.surface_angle=0;
        build_car_rotation(&c,after);
        for(int i=0;i<9;i++){if(abs(before[i]-after[i])>=300)fprintf(stderr,"wall %d pitch %d roll %d new %d %d %d matrix %d: %d / %d\\n",wall,pitch,roll,c.yaw,c.visual_pitch,c.visual_roll,i,before[i],after[i]);assert(abs(before[i]-after[i])<300);}
    }
    {
        Car c={0};c.pos.y=80*256;c.visual_pitch=32;c.visual_roll=96;
        for(int i=0;i<4;i++)update_car_physics(&c,0);
        assert(c.visual_pitch==32 && c.visual_roll==96); /* no aerial auto-righting */
        c.pos.y=256;c.vel.y=-2*256;update_car_physics(&c,0);
        assert(c.is_on_ground && c.visual_pitch && c.visual_roll && c.settle_delay==4);
        int pitch=c.visual_pitch,roll=c.visual_roll;
        for(int i=0;i<4;i++)update_car_physics(&c,0);
        assert(c.visual_pitch==pitch && c.visual_roll==roll);
        update_car_physics(&c,0);
        assert(abs(c.visual_pitch-pitch)<=3 && abs(c.visual_roll-roll)<=3);
        for(int i=0;i<64;i++)update_car_physics(&c,0);
        assert(c.visual_pitch==0 && c.visual_roll==0);
        int peak[2]={0,0};
        for(int stronger=0;stronger<2;stronger++) {
            c=(Car){0};c.is_on_ground=1;
            jump_from_surface(&c,stronger?(JUMP_FORCE*3)/5:3*256);
            for(int i=0;i<100;i++) {update_car_physics(&c,0);if(c.pos.y>peak[stronger])peak[stronger]=c.pos.y;}
            assert(c.is_on_ground);
        }
        assert(peak[1]>peak[0]*17/10);
    }
    /* Fast is the shipped default; detailed assets remain available in garage. */
    int detail_faces=car_gameplay_mesh(0,100*100)->face_count;
    performance_mode=1;
    assert(car_gameplay_mesh(0,100*100)->face_count<detail_faces);
    assert(ball_gameplay_mesh(100*100)->face_count==80);
    assert(ball_gameplay_mesh(300*300)->face_count==20);
    assert(ball_gameplay_mesh(60*60)->face_count==80);
    long net_cost[2];
    for(int mode=0;mode<2;mode++) {
        performance_mode=mode;test_projections=0;
        for(int yaw=0;yaw<256;yaw+=16) {
            Vector3 camera={0,35*256,0};set_camera(camera,yaw,0);
            draw_stadium_hex_walls(camera,STADIUM_WIDTH,STADIUM_LENGTH,STADIUM_HEIGHT);
        }
        net_cost[mode]=test_projections;
    }
    assert(net_cost[1]<net_cost[0]*3/5);
    printf("NET PROJECTIONS: detailed %ld, fast %ld\\n",net_cost[0],net_cost[1]);
    performance_mode=0;
    /* Frustum boxes reject hidden geometry but retain visible and crossing edges. */
    set_camera((Vector3){0,0,0},0,0);
    assert(!world_bounds_visible((Vector3){-10*256,-10*256,-100*256},(Vector3){10*256,10*256,-50*256}));
    assert(!world_bounds_visible((Vector3){300*256,0,50*256},(Vector3){310*256,10*256,60*256}));
    assert(world_bounds_visible((Vector3){-20*256,-20*256,4*256},(Vector3){20*256,20*256,20*256}));
    for(int yaw=0;yaw<256;yaw+=16) {
        set_camera((Vector3){0,0,0},yaw,0);
        for(int x=-500;x<=500;x+=50)for(int z=-500;z<=500;z+=50) {
            Vector3 p={x*256,20*256,z*256};int px,py;
            if(project_vertex_world(p,&px,&py) && px>=0 && px<240 && py>=0 && py<160)
                assert(world_bounds_visible((Vector3){p.x-256,p.y-256,p.z-256},(Vector3){p.x+256,p.y+256,p.z+256}));
        }
    }
    /* Far geometry halves face work and reduces transforms, even on cached draws. */
    set_camera_lookat((Vector3){0,35*256,0},(Vector3){0,30*256,459*256},0);
    test_vertices=test_faces=0;
    draw_stadium_curves((Vector3){0,35*256,0},STADIUM_WIDTH,STADIUM_LENGTH,GOAL_HALF_WIDTH);
    assert(test_vertices>0 && test_vertices<=60 && test_faces<=48);
    test_vertices=test_faces=0;
    draw_stadium_curves((Vector3){STADIUM_WIDTH-64*256,35*256,0},STADIUM_WIDTH,STADIUM_LENGTH,GOAL_HALF_WIDTH);
    assert(test_vertices>0 && test_vertices<=68 && test_faces<=56);
    /* Every garage effect animates, is distinct, and ends without residual pixels. */
    unsigned fingerprints[3];
    for(int style=0;style<3;style++) {
        unsigned previous=0;
        for(int age=10;age<=40;age+=30) {
            clear_screen(2);draw_goal_effect(120,72,age,style,146,35);
            unsigned hash=2166136261u;
            for(int i=0;i<240*160;i++)hash=(hash^frame_buffer[i])*16777619u;
            assert(age==10 || hash!=previous);previous=hash;
        }
        fingerprints[style]=previous;
        char name[40];sprintf(name,"goal-effect-%d.ppm",style);save_preview(name);
        garage_row=2;garage_goal[garage_side]=style;draw_garage();
        sprintf(name,"garage-effect-%d.ppm",style);save_preview(name);
        clear_screen(2);draw_goal_effect(120,72,96,style,146,35);
        for(int i=0;i<240*160;i++)assert(frame_buffer[i]==2);
    }
    assert(fingerprints[0]!=fingerprints[1] && fingerprints[1]!=fingerprints[2] && fingerprints[0]!=fingerprints[2]);
    garage_row=0;
    /* Drive from flat ground through every ramp, jump out, and drive back down. */
    for(int axis=1;axis<=2;axis++) for(int sign=-1;sign<=1;sign+=2) {
        Car c={0};c.is_on_ground=1;c.can_double_jump=1;
        c.yaw=axis==1?(sign>0?64:192):(sign>0?0:128);
        if(axis==1)c.pos.x=sign*(STADIUM_WIDTH-WALL_CURVE_RADIUS-20*256);
        else {c.pos.z=sign*(STADIUM_LENGTH-WALL_CURVE_RADIUS-20*256);c.pos.x=180*256;}
        int climbed=0,saw_curve=0;
        for(int frame=0;frame<240;frame++) {
            c.speed+=ACCEL_RATE;if(c.speed>MAX_DRIVE_SPEED)c.speed=MAX_DRIVE_SPEED;
            update_car_physics(&c,0);
            assert(abs(c.pos.x)<=STADIUM_WIDTH && abs(c.pos.z)<=STADIUM_LENGTH);
            if(c.surface_angle>0 && c.surface_angle<64)saw_curve=1;
            if(c.pos.y>80*256) {climbed=1;break;}
        }
        assert(climbed && saw_curve && c.is_on_ground);
        assert(c.surface_wall==axis*sign && c.surface_angle==64);
        Car wall_car=c;
        Vector3 normal=stadium_surface_vector((Vector3){0,256,0},c.surface_wall,c.surface_angle);
        jump_from_surface(&c,3*256);
        for(int frame=0;frame<5;frame++)update_car_physics(&c,0);
        assert(!c.is_on_ground && c.can_double_jump);
        fixed separation=FP_MUL(c.pos.x-wall_car.pos.x,normal.x)+FP_MUL(c.pos.z-wall_car.pos.z,normal.z);
        assert(separation>8*256); /* jump leaves the wall, not just upward */
        c=wall_car;
        for(int frame=0;frame<150;frame++) {c.speed=-4*256;update_car_physics(&c,0);}
        assert(c.pos.y==0 && c.is_on_ground && c.surface_angle==0);
        /* Wall turbo adds tangent speed instead of detaching the car. */
        player=wall_car;player.boost=50*256;player.boost_requested=1;
        fixed old_speed=player.speed;
        apply_player_boost();
        assert(player.is_on_ground && player.speed>old_speed);
    }
    /* Both directions around every vertical corner retain forward travel. */
    for(int sx=-1;sx<=1;sx+=2)for(int sz=-1;sz<=1;sz+=2)for(int from=1;from<=2;from++) {
        Car c={0};c.is_on_ground=1;c.surface_angle=64;
        c.surface_wall=from==1?sx:sz*2;
        c.pos=(Vector3){sx*(STADIUM_WIDTH-(from==2?8*256:0)),80*256,
            sz*(STADIUM_LENGTH-(from==1?8*256:0))};
        c.yaw=from==1?(sz>0?0:128):(sx>0?64:192);
        for(int frame=0;frame<16;frame++){c.speed=4*256;update_car_physics(&c,0);}
        assert(c.surface_wall==(from==1?sz*2:sx));
        assert(from==1?abs(c.pos.x)<STADIUM_WIDTH-20*256:abs(c.pos.z)<STADIUM_LENGTH-20*256);
    }
    /* A wall shot inherits upward driving speed; height separates cars. */
    {
        Car c={0};c.pos=(Vector3){STADIUM_WIDTH,80*256,0};
        c.surface_wall=1;c.surface_angle=64;c.is_on_ground=1;c.yaw=64;c.speed=4*256;
        ball.pos=(Vector3){STADIUM_WIDTH-10*256,100*256,0};ball.vel=(Vector3){0,0,0};
        assert(check_car_ball_collision(&c));assert(ball.vel.y>0);
        Car low={0},high={0};low.pos.x=high.pos.x=STADIUM_WIDTH;high.pos.y=100*256;
        check_car_car_collision(&low,&high);
        assert(low.pos.y==0 && high.pos.y==100*256);
        high.pos.y=10*256;high.vel.y=-2*256;
        check_car_car_collision(&low,&high);
        assert(low.pos.y<0 && high.pos.y>10*256);
    }
    /* Car impacts conserve momentum, ignore argument order, and cannot add energy. */
    {
        Car a={0},b={0};a.pos.x=-10*256;b.pos.x=10*256;
        a.vel=(Vector3){6*256,0,2*256};b.vel=(Vector3){-2*256,0,2*256};
        Car reversed_a=a,reversed_b=b;
        check_car_car_collision(&a,&b);
        check_car_car_collision(&reversed_b,&reversed_a);
        assert(a.vel.x+b.vel.x==4*256);
        assert(a.vel.z==2*256 && b.vel.z==2*256);
        assert(a.vel.x==reversed_a.vel.x && b.vel.x==reversed_b.vel.x);
        assert(a.vel.x*a.vel.x+b.vel.x*b.vel.x<=40*256*256);
        int av=a.vel.x,bv=b.vel.x;check_car_car_collision(&a,&b);
        assert(a.vel.x==av && b.vel.x==bv); /* separating contacts don't bounce again */
        a=(Car){0};b=(Car){0};check_car_car_collision(&a,&b);
        assert(a.pos.x<0 && b.pos.x>0);
    }
    /* Goal mouths stay flat and traversable; above the opening is a wall. */
    for(int sign=-1;sign<=1;sign+=2) {
        Vector3 p={0,0,sign*(STADIUM_LENGTH+10*256)},normal;
        int wall,angle;
        stadium_surface_contact(&p,0,0,STADIUM_WIDTH,STADIUM_LENGTH,GOAL_HALF_WIDTH,GOAL_HEIGHT,&wall,&angle,&normal);
        assert(p.z==sign*(STADIUM_LENGTH+10*256) && wall==0);
        p.y=GOAL_HEIGHT+10*256;
        assert(stadium_surface_contact(&p,0,0,STADIUM_WIDTH,STADIUM_LENGTH,GOAL_HALF_WIDTH,GOAL_HEIGHT,&wall,&angle,&normal));
        assert(p.z==sign*STADIUM_LENGTH && angle==64);
    }
    /* A ball on the curve rests one radius inward from the wheel surface. */
    {
        Vector3 p={STADIUM_WIDTH-5*256,14*256,0},n;int w,a;
        assert(stadium_surface_contact(&p,14*256,0,STADIUM_WIDTH,STADIUM_LENGTH,GOAL_HALF_WIDTH,GOAL_HEIGHT,&w,&a,&n));
        assert(p.y>14*256 && p.x<STADIUM_WIDTH-14*256 && n.x<0 && n.y>0);
    }
    player=(Car){0};
    /* Gameplay detail levels remain valid and reduce both transforms and faces. */
    for(int model=0;model<3;model++) {
        const Mesh *levels[4]={car_models[model],car_gameplay_mesh(model,100*100),car_gameplay_mesh(model,300*300),car_speed_models[model]};
        for(int level=1;level<4;level++) {
            const Mesh *mesh=levels[level];
            assert(mesh->vertex_count<=levels[level-1]->vertex_count);
            assert(mesh->face_count<=levels[level-1]->face_count);
            for(int f=0;f<mesh->face_count;f++) {
                Face face=mesh->faces[f];
                assert(face.v1<mesh->vertex_count && face.v2<mesh->vertex_count && face.v3<mesh->vertex_count);
                Vector3 a=mesh->vertices[face.v1],b=mesh->vertices[face.v2],c=mesh->vertices[face.v3];
                int x1=(b.x-a.x)/256,y1=(b.y-a.y)/256,z1=(b.z-a.z)/256;
                int x2=(c.x-a.x)/256,y2=(c.y-a.y)/256,z2=(c.z-a.z)/256;
                assert(y1*z2-z1*y2 || z1*x2-x1*z2 || x1*y2-y1*x2);
                for(int v=0;v<3;v++) assert(face.uv[v][0]<64 && face.uv[v][1]<64);
            }
            set_camera_lookat((Vector3){0,30*256,-90*256},(Vector3){0,10*256,0},0);
            for(int angle=0;angle<256;angle+=8) {
                clear_screen(135);
                assert(draw_model_world(mesh,(Vector3){0,0,0},angle,angle,angle,256,3,RENDER_TEXTURED)>0);
            }
        }
    }
    assert(ball_gameplay_mesh(100*100)->vertex_count==42);
    assert(ball_gameplay_mesh(300*300)->vertex_count==12);
    assert(ball_gameplay_mesh(300*300)->face_count==20);
    for(int yaw=0;yaw<256;yaw+=8) {
        set_camera((Vector3){0,0,0},yaw,0);
        assert(!world_sphere_visible((Vector3){0,0,0},256));
        Vector3 front={custom_sin_fp[yaw]*100,0,custom_cos_fp[yaw]*100};
        assert(world_sphere_visible(front,14*256));
        front.x=-front.x;front.z=-front.z;
        assert(!world_sphere_visible(front,14*256));
    }
    set_camera((Vector3){0,0,0},0,0);
    assert(world_sphere_visible((Vector3){110*256,0,100*256},14*256)); /* edge overlap */
    assert(!world_sphere_visible((Vector3){200*256,0,100*256},14*256));
    assert(!world_sphere_visible((Vector3){0,200*256,100*256},14*256));
    /* Actual thrust must follow the visible nose throughout all diagonal flips. */
    for(int yaw=0;yaw<256;yaw+=32) for(int pitch=-1;pitch<=1;pitch++) for(int roll=-1;roll<=1;roll++) {
        if(!pitch && !roll) continue;
        for(int ticks=1;ticks<=50;ticks++) {
            player=(Car){0}; player.yaw=yaw; player.flip_timer=ticks;
            player.flip_pitch_dir=pitch; player.flip_roll_dir=roll;
            player.pos=(Vector3){0,80*256,0}; player.boost=100*256; player.boost_requested=1;
            int32_t rotation[9]; build_car_rotation(&player,rotation);
            /* An orthonormal matrix preserves unit-length forward at all angles. */
            double length=sqrt((double)rotation[2]*rotation[2]+(double)rotation[5]*rotation[5]+(double)rotation[8]*rotation[8]);
            assert(fabs(length-4096)<10);
            memset(particles,0,sizeof(particles));
            apply_player_boost();
            assert(player.vel.x==FP_MUL(rotation[2]>>4,(256*1848)/1000));
            assert(player.vel.y==FP_MUL(rotation[5]>>4,(256*1848)/1000));
            assert(player.vel.z==FP_MUL(rotation[8]>>4,(256*1848)/1000));
            assert(!player.boost_requested && player.boost==100*256-310);
            for(int i=0;i<3;i++) {
                assert(particles[i].life==15);
                int dot=particles[i].vel.x*(rotation[2]>>4)+particles[i].vel.y*(rotation[5]>>4)+particles[i].vel.z*(rotation[8]>>4);
                assert(dot<0); /* Exhaust always travels away from the nose. */
            }
        }
    }
    for(int m=0;m<3;m++) {
        const Mesh *mesh=car_models[m];
        assert(mesh->vertex_count<=256 && mesh->face_count<=320);
        for(int i=0;i<mesh->face_count;i++) {
            Face f=mesh->faces[i];
            assert(f.v1<mesh->vertex_count && f.v2<mesh->vertex_count && f.v3<mesh->vertex_count);
            Vector3 a=mesh->vertices[f.v1],b=mesh->vertices[f.v2],c=mesh->vertices[f.v3];
            int x1=(b.x-a.x)/256,y1=(b.y-a.y)/256,z1=(b.z-a.z)/256;
            int x2=(c.x-a.x)/256,y2=(c.y-a.y)/256,z2=(c.z-a.z)/256;
            assert(y1*z2-z1*y2 || z1*x2-x1*z2 || x1*y2-y1*x2);
            for(int v=0;v<3;v++) assert(f.uv[v][0]<64 && f.uv[v][1]<64);
        }
        for(int side=0;side<2;side++) for(int paint=0;paint<3;paint++) {
            garage_side=side; garage_model[side]=m; garage_paint[side]=paint;
            for(int frame=0;frame<768;frame++) draw_garage();
            char path[80]; snprintf(path,sizeof(path),"garage-%d-%d-%d.ppm",m,side,paint);
            FILE *out=fopen(path,"wb"); fprintf(out,"P6\\n240 160\\n255\\n");
            for(int i=0;i<240*160;i++) {unsigned c=pal_bg_mem[frame_buffer[i]];
                fputc((c&31)*255/31,out); fputc(((c>>5)&31)*255/31,out); fputc(((c>>10)&31)*255/31,out);}
            fclose(out);
        }
    }
    /* Every orientation must leave the visible bounds center stationary. */
    for (int model=0; model<CAR_MODEL_COUNT; ++model) {
        const Mesh *mesh=car_models[model];
        Vector3 lo=mesh->vertices[0], hi=lo;
        for(int i=1;i<mesh->vertex_count;++i) {
            Vector3 v=mesh->vertices[i];
            if(v.x<lo.x) lo.x=v.x; if(v.x>hi.x) hi.x=v.x;
            if(v.y<lo.y) lo.y=v.y; if(v.y>hi.y) hi.y=v.y;
            if(v.z<lo.z) lo.z=v.z; if(v.z>hi.z) hi.z=v.z;
        }
        Vector3 center={(lo.x+hi.x)/2,(lo.y+hi.y)/2,(lo.z+hi.z)/2};
        Vector3 origin={123*256,42*256,-76*256};
        for(int yaw=0;yaw<256;yaw+=17) for(int a=0;a<256;++a) {
            int32_t base[9], rot[9];
            build_model_rotation(yaw,0,0,base);
            build_model_rotation(yaw,a,(a*3)&255,rot);
            Vector3 p=car_render_position(model,origin,yaw,rot);
            int c[3]={center.x,center.y,center.z};
            int o[3]={origin.x,origin.y,origin.z}, t[3]={p.x,p.y,p.z};
            for(int axis=0;axis<3;++axis) {
                int expected=o[axis]+((base[axis*3]*c[0]+base[axis*3+1]*c[1]+base[axis*3+2]*c[2])>>12);
                int actual=t[axis]+((rot[axis*3]*c[0]+rot[axis*3+1]*c[1]+rot[axis*3+2]*c[2])>>12);
                assert(actual==expected);
            }
            p=car_render_position(model,origin,yaw,base);
            assert(p.x==origin.x && p.y==origin.y && p.z==origin.z);
        }
    }
    /* Actual menu fill stays opaque and safely clips at screen edges. */
    clear_screen(31); draw_menu_text_box("PLAY",82,131,136);
    assert(frame_buffer[81*240+54]==8);
    draw_menu_text_box("BOTTOM",155,130,300);
    draw_menu_text_box("TOP",0,130,136);
    /* Ground footprints survive yaw, height, camera cropping and both pitches. */
    for(int ice=0;ice<2;ice++) {
        is_hockey_match=ice;
        for(int h=0;h<=160;h+=20) for(int yaw=0;yaw<256;yaw+=16) {
            clear_screen(ice?14:135);
            set_camera_lookat((Vector3){0,30*256,-80*256},(Vector3){0,0,0},0);
            draw_car_shadow((Vector3){0,h*256,0},yaw,1);
            int pixels=0;
            for(int i=0;i<240*160;i++) pixels+=frame_buffer[i]!=(ice?14:135);
            assert(pixels>0);
            for(int i=0;i<240*160;i++) assert(frame_buffer[i]!=132);
            draw_ball_ground_shadow((Vector3){0,h*256,0});
            for(int i=0;i<240*160;i++) assert(frame_buffer[i]!=132);
        }
        active_pitch_mode=ice;
        if(ice) init_hockey_pitch_texture(); else init_pitch_texture();
        set_camera_lookat((Vector3){0,35*256,-100*256},(Vector3){0,15*256,0},0);
        draw_environment_background(ice?14:128);
        if(ice) for(int i=0;i<240*160;i++) {
            assert(frame_buffer[i]!=16 && frame_buffer[i]!=48 && frame_buffer[i]!=83);
        }
        draw_stadium_crowd((Vector3){0,35*256,-100*256},336*256,504*256);
        draw_soccer_pitch((Vector3){0,35*256,-100*256});
        draw_car_shadow((Vector3){-16*256,0,0},32,0);
        draw_ball_ground_shadow((Vector3){25*256,14*256,0});
        draw_model_world(car_gameplay_mesh(0,100*100),(Vector3){-16*256,0,0},32,0,0,256,3,RENDER_TEXTURED);
        draw_model_world(&sphere_mesh,(Vector3){25*256,14*256,0},0,0,0,170,130,RENDER_FLAT);
        draw_stadium_goal(459*256,109*256,75*256,ice?28:131);
        save_preview(ice?"scene-ice.ppm":"scene-grass.ppm");
        u8 untouched[5]={frame_buffer[3*240+3],frame_buffer[3*240+86],frame_buffer[3*240+171],frame_buffer[142*240+3],frame_buffer[154*240+170]};
        player.pos=(Vector3){-16*256,0,0};player.boost=74*256;
        ball.pos=(Vector3){25*256,14*256,0};opponent.pos=(Vector3){30*256,0,190*256};
        draw_radar();draw_match_hud();
        assert(frame_buffer[3*240+3]==untouched[0] && frame_buffer[3*240+86]==untouched[1]);
        assert(frame_buffer[3*240+171]==untouched[2] && frame_buffer[142*240+3]==untouched[3]);
        assert(frame_buffer[154*240+170]==untouched[4]);
        if(!ice) save_preview("transparent-hud.ppm");
        u8 goal_corner=frame_buffer[52*240+39];draw_goal_celebration_panel();
        assert(frame_buffer[52*240+39]==goal_corner);
        if(!ice) save_preview("transparent-goal.ppm");
        if(!ice) {
            {
                Vector3 eye={180*256,70*256,-105*256};
                set_camera_lookat(eye,(Vector3){300*256,36*256,0},0);
                draw_environment_background(128);
                draw_stadium_hex_walls(eye,STADIUM_WIDTH,STADIUM_LENGTH,STADIUM_HEIGHT);
                draw_stadium_curves(eye,STADIUM_WIDTH,STADIUM_LENGTH,GOAL_HALF_WIDTH);
                Car wall_car={0};wall_car.pos=(Vector3){STADIUM_WIDTH,52*256,0};
                wall_car.yaw=64;wall_car.surface_wall=1;wall_car.surface_angle=64;
                int32_t rot[9];build_car_rotation(&wall_car,rot);
                draw_model_world_mat(car_gameplay_mesh(0,100*100),surface_car_position(0,&wall_car,rot),rot,256,6,RENDER_TEXTURED);
                save_preview("wall-driving.ppm");
            }
            set_camera_lookat((Vector3){-90*256,55*256,270*256},(Vector3){0,35*256,459*256},0);
            draw_environment_background(128);
            draw_stadium_goal(459*256,109*256,75*256,131);
            save_preview("goal-design.ppm");
            for(int charge=0;charge<=100;charge+=25) {
                memset(frame_buffer,128,240*160);
                fast_draw_boost(charge,212,136);
                assert(frame_buffer[146*240+212]==128); /* open center outside glyph */
                assert(frame_buffer[155*240+193]==128); /* no rectangular backing */
                assert(frame_buffer[115*240+212]==(charge>=50?131:149));
                char name[32];sprintf(name,"boost-%d.ppm",charge);save_preview(name);
            }
        }
        set_camera((Vector3){0,0,0},0,0);
        int ix,iy;
        assert(!world_target_indicator((Vector3){0,0,100*256},&ix,&iy));
        assert(world_target_indicator((Vector3){200*256,0,50*256},&ix,&iy) && ix>120);
        assert(world_target_indicator((Vector3){-200*256,0,50*256},&ix,&iy) && ix<120);
        assert(world_target_indicator((Vector3){0,0,-50*256},&ix,&iy) && iy>72);
        for(int x=-600;x<=600;x+=100) for(int z=-600;z<=600;z+=100) {
            if(world_target_indicator((Vector3){x*256,80*256,z*256},&ix,&iy))
                assert(ix>=14 && ix<=226 && iy>=30 && iy<=114);
        }
    }
    is_hockey_match=active_pitch_mode=0; init_pitch_texture();
    for(int yaw=0;yaw<256;yaw+=16) {
        Vector3 camera={0,35*256,0};
        set_camera(camera,yaw,0); draw_environment_background(128);
        draw_stadium_crowd(camera,336*256,504*256);
        draw_soccer_pitch(camera);
        if(yaw==32) save_preview("stadium.ppm");
    }
    /* Crossing the near plane must clip net segments safely, even outside walls. */
    for(int x=-400;x<=400;x+=200) {
        Vector3 camera={x*256,30*256,490*256};
        set_camera(camera,64,0);
        draw_soccer_pitch(camera);
        draw_stadium_crowd(camera,336*256,504*256);
    }
    for(int frame=0;frame<10;frame++) {
        int ticks=frame*6; if(ticks>50) ticks=50;
        int angle=(ticks*256/50)&255;
        int32_t rotation[9]; build_dodge_rotation(32,1,1,angle,rotation);
        Vector3 pos={0,20*256,0};
        set_camera_lookat((Vector3){0,40*256,-100*256},(Vector3){0,25*256,0},0);
        draw_environment_background(128);
        draw_car_shadow(pos,32,0);
        draw_model_world_mat(car_gameplay_mesh(0,100*100),car_render_position(0,pos,32,rotation),rotation,256,3,RENDER_TEXTURED);
        char name[40]; snprintf(name,sizeof(name),"flip-%02d.ppm",frame); save_preview(name);
    }
    for(int detail=0;detail<2;detail++) {
        for(int model=0;model<3;model++) {
            set_camera_lookat((Vector3){0,29*256,-68*256},(Vector3){0,9*256,0},0);
            clear_screen(144);
            const Mesh *mesh=detail?car_far_models[model]:car_match_models[model];
            draw_model_world(mesh,(Vector3){0,0,0},32,0,0,256,3,RENDER_TEXTURED);
            char name[40];snprintf(name,sizeof(name),"lod-%d-%d.ppm",model,detail);save_preview(name);
        }
        set_camera_lookat((Vector3){0,10*256,-80*256},(Vector3){0,0,0},0);
        clear_screen(144);
        assert(draw_soccer_ball((Vector3){0,0,0},detail*64,0));
        save_preview(detail?"ball-far.ppm":"ball-near.ppm");
    }
    /* The sprite silhouette is round and invariant as its panels rotate;
       it must not send any vertices or faces through the mesh renderer. */
    set_camera((Vector3){0,0,0},0,0);
    long sphere_vertices=test_vertices,sphere_faces=test_faces;
    unsigned char silhouette[240*160];
    for(int phase=0;phase<16;phase++) {
        clear_screen(144);
        assert(draw_soccer_ball((Vector3){0,0,80*256},phase*16,0));
        for(int y=0;y<160;y++) for(int x=0;x<240;x++) {
            int i=y*240+x;
            int filled=frame_buffer[i]!=144;
            if(!phase) silhouette[i]=filled;
            else assert(filled==silhouette[i]);
            assert(filled==(frame_buffer[y*240+239-x]!=144));
            assert(filled==(frame_buffer[(159-y)*240+x]!=144));
        }
    }
    assert(test_vertices==sphere_vertices && test_faces==sphere_faces);
    /* Exercise all screen edges, tiny sizes, and close-up fallback with ASan. */
    for(int z=28;z<=600;z+=29)
        for(int x=-200;x<=200;x+=40)
            for(int y=-120;y<=120;y+=40)
                assert(draw_soccer_ball((Vector3){x*256,y*256,z*256},x,y));
    assert(!draw_soccer_ball((Vector3){0,0,27*256},0,0));
    clear_screen(144);
    assert(draw_soccer_ball((Vector3){0,0,-80*256},0,0));
    for(int i=0;i<240*160;i++) assert(frame_buffer[i]==144);
    int32_t identity[9]={4096,0,0,0,4096,0,0,0,4096};
    Mesh invalid=*car_models[0]; invalid.vertex_count=1;
    assert(draw_model_world_mat(&invalid,(Vector3){0,0,0},identity,256,3,5)==0);
    /* Compare both playable detail modes at the same camera and positions. */
    for(int mode=0;mode<3;mode++) {
        performance_mode=mode;active_pitch_mode=0;
        Vector3 camera={0,35*256,-100*256};
        set_camera_lookat(camera,(Vector3){0,15*256,0},0);
        draw_environment_background(128);
        if(mode!=2)draw_stadium_crowd(camera,CAGE_WIDTH,CAGE_LENGTH);
        draw_soccer_pitch(camera);
        draw_stadium_curves(camera,STADIUM_WIDTH,STADIUM_LENGTH,GOAL_HALF_WIDTH);
        draw_stadium_goal(STADIUM_LENGTH,GOAL_HALF_WIDTH,GOAL_HEIGHT,131);
        draw_model_world(car_gameplay_mesh(0,100*100),(Vector3){-16*256,0,0},32,0,0,256,3,mode?RENDER_ACCENTS:RENDER_TEXTURED);
        draw_soccer_ball((Vector3){25*256,14*256,0},0,0);
        player.boost=74*256;draw_match_hud();
        save_preview(mode==2?"speed-mode.ppm":mode?"fast-mode.ppm":"detailed-mode.ppm");
    }
    draw_link_lobby(0,0,0);save_preview("link-lobby.ppm");
    draw_link_lobby(1,1,1);save_preview("link-waiting.ppm");
    {
        is_hockey_match=1;active_pitch_mode=1;init_hockey_pitch_texture();
        Vector3 camera={0,35*256,-100*256};
        set_camera_lookat(camera,(Vector3){0,10*256,0},0);
        draw_environment_background(14);
        draw_stadium_crowd(camera,CAGE_WIDTH,CAGE_LENGTH);
        draw_soccer_pitch(camera);
        draw_stadium_curves(camera,STADIUM_WIDTH,STADIUM_LENGTH,GOAL_HALF_WIDTH);
        draw_stadium_goal(STADIUM_LENGTH,GOAL_HALF_WIDTH,GOAL_HEIGHT,28);
        draw_model_world(&puck_mesh,(Vector3){25*256,PUCK_HALF_HEIGHT,0},0,0,0,256,149,RENDER_FLAT);
        draw_model_world(car_gameplay_mesh(0,100*100),(Vector3){-16*256,0,0},32,0,0,256,3,RENDER_TEXTURED);
        draw_match_hud();save_preview("hockey-updated.ppm");
    }
    puts("PASS: stronger jumps, preserved landing attitude, gradual recovery, three animated garage effects, reduced ramp LOD work, four-wall climbing/jumps/descent, corner traversal, goal clearance, wall boost, gameplay LODs, frustum culling, 18 loadouts, complete rotations, fixed car pivots, diagonal thrust/exhaust, stadium camera sweep, mesh/UV validity, invalid mesh rejection");
}
'''
    if os.environ.get('STADIUM_PROFILE'):
        harness=harness.replace('    puts("PASS:', '''    extern long profile_project,profile_lines,profile_meshes;
    profile_project=profile_lines=profile_meshes=0;
    for(int yaw=0;yaw<256;yaw+=16) {
        Vector3 camera={0,35*256,0}; set_camera(camera,yaw,0);
        draw_stadium_crowd(camera,336*256,504*256);
        draw_soccer_pitch(camera);
    }
    printf("STADIUM / 16 VIEWS: %ld projections, %ld clipped world lines, %ld mesh draws\\n",profile_project,profile_lines,profile_meshes);
    puts("PASS:''')
    (d/'test.c').write_text(harness)
    subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-Wno-pointer-to-int-cast','-I',str(d),*[str(d/f) for f in ('test.c','engine3d.c','render.c','models.c','stadium.c')],'-lm','-o',str(d/'test')],check=True)
    output=Path(os.environ.get("GARAGE_PREVIEW_DIR", str(d/"previews"))); output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(d/'test')],cwd=output,check=True,env={**os.environ,'ASAN_OPTIONS':'detect_leaks=0','UBSAN_OPTIONS':'halt_on_error=1'})
