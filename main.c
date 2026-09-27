/**
 * @file main.c
 * @brief 3D GBA Rocket League Game implementation.
 */

#include <tonc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "engine3d.h"
#include "models.h"
#include "stadium.h"
#include "link.h"
#include "achievements.h"
#include "audio.h"
#include "coverart.h"

/* --- Game State Definitions --- */
typedef enum {
    STATE_START_SCREEN,
    STATE_TITLE,
    STATE_MENU_PLAY,
    STATE_MENU_TRAINING,
    STATE_MENU_SETTINGS,
    STATE_MENU_GARAGE,
    STATE_MENU_ACHIEVEMENTS,
    STATE_MENU_LINK,
    STATE_PLAY,
    STATE_PAUSED,
    STATE_HOCKEY,
    STATE_GOAL,
    STATE_REPLAY,
    STATE_GAMEOVER,
    STATE_TUTORIAL,
    STATE_TUTORIAL_BRIEFING,
    STATE_TUTORIAL_COMPLETE,
    STATE_TRAINING_INIT,
    STATE_TRAINING,
    STATE_TRAINING_GOAL
} GameState;

/* --- Tutorial & Training Structures --- */
typedef enum {
    TUTORIAL_DRIVE_GATE,
    TUTORIAL_STEER_GATES,
    TUTORIAL_BOOST_GATE,
    TUTORIAL_JUMP_GATE,
    TUTORIAL_AERIAL_GATE,
    TUTORIAL_AIM_SHOT
} TutorialObjective;

typedef struct {
    Vector3 target_pos;
    int target_yaw;
    Vector3 car_start_pos;
    int car_start_yaw;
    Vector3 ball_start_pos;
    TutorialObjective objective;
    const char* title;
    const char* instruction;
} TutorialStage;

typedef struct {
    int accelerated;
    int steered;
    int boosted;
    int jumped;
    int double_jumped;
    int ball_touched;
    int goal_scored;
} TutorialProgress;

typedef struct {
    Vector3 ball_start_pos;
    Vector3 ball_start_vel;
    Vector3 car_start_pos;
    int car_start_yaw;
    int time_limit; // in frames
    const char* hint;
} TrainingLevel;

#define NUM_STEERING_GATES 3
#define NUM_TUTORIAL_STAGES 6

static const Vector3 tutorial_steering_gates[NUM_STEERING_GATES] = {
    { -45 * 256, 24 * 256, -145 * 256 },
    {  45 * 256, 24 * 256,  -25 * 256 },
    {   0, 24 * 256, 95 * 256 }
};

static const TutorialStage tutorial_stages[NUM_TUTORIAL_STAGES] = {
    {
        { 0, 24 * 256, -190 * 256 }, 0,
        { 0, 0, -340 * 256 }, 0, { 240 * 256, 14 * 256, 260 * 256 },
        TUTORIAL_DRIVE_GATE, "DRIVE THE GATE", "ACCELERATE THROUGH THE ARCH"
    },
    {
        { -45 * 256, 24 * 256, -145 * 256 }, 0,
        { 0, 0, -250 * 256 }, 0, { 240 * 256, 14 * 256, 260 * 256 },
        TUTORIAL_STEER_GATES, "STEER THE SLALOM", "PASS THROUGH THREE GATES"
    },
    {
        { 0, 24 * 256, -55 * 256 }, 0,
        { 0, 0, -230 * 256 }, 0, { 240 * 256, 14 * 256, 260 * 256 },
        TUTORIAL_BOOST_GATE, "USE BOOST", "HOLD B THROUGH THE GATE"
    },
    {
        { 0, 12 * 256, -10 * 256 }, 0,
        { 0, 0, -10 * 256 }, 0, { 240 * 256, 14 * 256, 260 * 256 },
        TUTORIAL_JUMP_GATE, "FIRST JUMP", "STOP AND TAP A TO JUMP"
    },
    {
        { 0, 25 * 256, 110 * 256 }, 0,
        { 0, 0, 110 * 256 }, 0, { 240 * 256, 14 * 256, 260 * 256 },
        TUTORIAL_AERIAL_GATE, "DOUBLE JUMP", "RELEASE D-PAD. TAP A TWICE"
    },
    {
        { 0, 40 * 256, 459 * 256 }, 0,
        { 0, 0, 205 * 256 }, 0, { 0, 14 * 256, 295 * 256 },
        TUTORIAL_AIM_SHOT, "SCORE A GOAL", "HIT THE BALL INTO ORANGE GOAL"
    }
};

/* Per-stage torus colours: white, cyan, orange, green, yellow-ish, orange */
static const u8 tutorial_stage_colors[NUM_TUTORIAL_STAGES] = { 130, 129, 131, 133, 131, 131 };
static int current_tutorial_stage = 0;
static int current_tutorial_gate = 0;
static TutorialProgress tutorial_progress;
static int tutorial_complete_timer = 0;
static int tutorial_marker_pulse = 0;
static int tutorial_flash_timer = 0;   /* stage completion flash duration */

#define NUM_TRAINING_LEVELS 10
static const TrainingLevel training_levels[NUM_TRAINING_LEVELS] = {
    // Difficulty 1: Easy straight rolls
    { { 0, 12 * 256, -200 * 256 }, { 0, 0, -2 * 256 }, { 0, 0, -100 * 256 }, 128, 300, "DIFF 1: STRAIGHT ROLL" },
    { { -50 * 256, 12 * 256, -200 * 256 }, { 0, 0, -2 * 256 }, { 50 * 256, 0, -100 * 256 }, 128, 300, "DIFF 1: SLIGHT ANGLE" },
    // Difficulty 2: Faster angles
    { { 150 * 256, 12 * 256, -150 * 256 }, { -3 * 256, 0, -2 * 256 }, { 0, 0, -100 * 256 }, 128, 240, "DIFF 2: LEFT ANGLE" },
    { { -150 * 256, 12 * 256, -150 * 256 }, { 3 * 256, 0, -2 * 256 }, { 0, 0, -100 * 256 }, 128, 240, "DIFF 2: RIGHT ANGLE" },
    // Difficulty 3: Bounces
    { { 0, 150 * 256, -250 * 256 }, { 0, 0, 0 }, { 0, 0, -100 * 256 }, 128, 240, "DIFF 3: BOUNCE SHOT" },
    { { 250 * 256, 12 * 256, -300 * 256 }, { -6 * 256, 0, 0 }, { 0, 0, -150 * 256 }, 128, 240, "DIFF 3: FAST CROSS" },
    // Difficulty 4: Aerials
    { { 250 * 256, 50 * 256, -350 * 256 }, { -4 * 256, 3 * 256, 2 * 256 }, { -100 * 256, 0, -150 * 256 }, 128, 240, "DIFF 4: CORNER LOB" },
    { { 0, 100 * 256, -150 * 256 }, { 0, 3 * 256, -5 * 256 }, { 0, 0, -250 * 256 }, 128, 240, "DIFF 4: OVERHEAD" },
    // Difficulty 5: Expert
    { { 300 * 256, 12 * 256, -200 * 256 }, { -8 * 256, 0, 2 * 256 }, { 150 * 256, 0, -100 * 256 }, 128, 180, "DIFF 5: WALL PINCH" },
    { { 150 * 256, 100 * 256, -250 * 256 }, { -3 * 256, 2 * 256, -2 * 256 }, { 0, 0, -50 * 256 }, 128, 180, "DIFF 5: FAST AERIAL" }
};
static int current_training_level = 0;
static int training_timer = 0;
static int training_touches = 0; // track number of ball touches
static int training_post_touch_timer = 0;

/* Slower flips expose more intermediate poses while preserving total impulse. */
#define FLIP_DURATION_TICKS 50
#define FLIP_STEP_TICKS 3

/* --- Physics Structures --- */
typedef struct {
    Vector3 pos;
    Vector3 vel;
    int steer_velocity,steer_fraction; /* fractional, speed-sensitive steering */
    int yaw;            // 0-255 rotation angle
    fixed speed;        // Forward/backward driving speed
    fixed boost;        // Boost meter (0 to 100 * FP_SCALE)
    int is_on_ground;
    int can_double_jump;
    int surface_wall, surface_angle; /* signed axis and quarter-circle tilt */
    int settle_pitch_velocity, settle_roll_velocity;
    int jump_hold;
    int settle_delay; /* hold the impact pose before gently righting */
    int detach_timer; /* prevent a jump immediately reattaching */
    int team;           // 3 = Blue, 6 = Orange
    int boost_requested; /* consumed by physics after advancing the rotation */
    int flip_base_pitch, flip_base_roll;
    int flip_timer;
    int flip_pitch_dir;
    int flip_roll_dir;
    int visual_pitch;
    int visual_roll;
} Car;

static void build_car_rotation(const Car *car, int32_t rotation[9]) {
    if (car->flip_timer > 0) {
        int angle = ((FLIP_DURATION_TICKS - car->flip_timer) * 256 / FLIP_DURATION_TICKS) & 255;
        if(car->flip_base_pitch || car->flip_base_roll) {
            int32_t base[9],dodge[9];
            build_model_rotation(car->yaw,car->flip_base_pitch,car->flip_base_roll,base);
            build_dodge_rotation(0,car->flip_pitch_dir,car->flip_roll_dir,angle,dodge);
            for(int row=0;row<3;row++)for(int col=0;col<3;col++)
                rotation[row*3+col]=(base[row*3]*dodge[col]+base[row*3+1]*dodge[3+col]+base[row*3+2]*dodge[6+col])>>12;
        } else build_dodge_rotation(car->yaw, car->flip_pitch_dir, car->flip_roll_dir, angle, rotation);
    } else {
        build_model_rotation(car->yaw, car->visual_pitch, car->visual_roll, rotation);
    }
    /* Draw sub-step yaw instead of waiting for a whole 1/256 turn. */
    if(car->steer_fraction) {
        int sine=camera_sin_q8(car->steer_fraction);
        int cosine=camera_cos_q8(car->steer_fraction);
        for(int col=0;col<3;col++) {
            int x=rotation[col],z=rotation[6+col];
            rotation[col]=(x*cosine+z*sine)/4096;
            rotation[6+col]=(z*cosine-x*sine)/4096;
        }
    }
    if(!car->surface_wall || !car->surface_angle) return;
    for(int col=0;col<3;col++) {
        Vector3 v={rotation[col],rotation[3+col],rotation[6+col]};
        v=stadium_surface_vector(v,car->surface_wall,car->surface_angle);
        rotation[col]=v.x;rotation[3+col]=v.y;rotation[6+col]=v.z;
    }
}

/* Angle lookup only runs on touchdown, not every rendered frame. */
/* Rendering interpolates between fixed physics states, never feeding blended
   positions back into collision or linked-game simulation. */
static int scaled_simulation_steps(int elapsed,unsigned *phase) {
    *phase+=elapsed*2;
    int steps=*phase/4;
    *phase%=4;
    return steps;
}
static Vector3 blend_position(Vector3 a,Vector3 b,int fraction) {
    return (Vector3){a.x+(b.x-a.x)*fraction/256,
                     a.y+(b.y-a.y)*fraction/256,
                     a.z+(b.z-a.z)*fraction/256};
}
static int blend_heading(int a,int b,int fraction) {
    int delta=((b-a+32768)&65535)-32768;
    return (a+delta*fraction/256)&65535;
}
static void blend_car_rotation(const Car *before,const Car *after,int fraction,int32_t out[9]) {
    if(!fraction){build_car_rotation(before,out);return;}
    if(fraction==256){build_car_rotation(after,out);return;}
    int32_t old[9];
    build_car_rotation(before,old);build_car_rotation(after,out);
    for(int i=0;i<9;i++)out[i]=old[i]+(out[i]-old[i])*fraction/256;
    /* Restore a rigid orthonormal frame after blending. Otherwise the car
       subtly shrinks/shears between angular samples, especially mid-flip. */
    int length=int_sqrt(out[0]*out[0]+out[3]*out[3]+out[6]*out[6]);
    if(!length){build_car_rotation(after,out);return;}
    for(int row=0;row<3;row++)out[row*3]=out[row*3]*4096/length;
    int dot=(out[0]*out[1]+out[3]*out[4]+out[6]*out[7])/4096;
    for(int row=0;row<3;row++)out[row*3+1]-=out[row*3]*dot/4096;
    length=int_sqrt(out[1]*out[1]+out[4]*out[4]+out[7]*out[7]);
    if(!length){build_car_rotation(after,out);return;}
    for(int row=0;row<3;row++)out[row*3+1]=out[row*3+1]*4096/length;
    out[2]=(out[3]*out[7]-out[6]*out[4])/4096;
    out[5]=(out[6]*out[1]-out[0]*out[7])/4096;
    out[8]=(out[0]*out[4]-out[3]*out[1])/4096;
}

static int landing_angle(int y, int x) {
    if(!x && !y)return 0;
    int best=-2147483647,angle=0;
    for(int i=0;i<256;i++) {
        int dot=x*custom_cos_lut[i]+y*custom_sin_lut[i];
        if(dot>best){best=dot;angle=i;}
    }
    return angle;
}

static void retain_landing_rotation(Car *car,int wall,int angle) {
    int32_t world[9],local[9];
    build_car_rotation(car,world);
    /* Express the current world attitude in the new support frame. */
    for(int row=0;row<3;row++) {
        Vector3 axis={row==0?256:0,row==1?256:0,row==2?256:0};
        axis=stadium_surface_vector(axis,wall,angle);
        for(int col=0;col<3;col++)
            local[row*3+col]=(axis.x*world[col]+axis.y*world[3+col]+axis.z*world[6+col])/256;
    }
    int horizontal=fast_sqrt(local[3]*local[3]+local[4]*local[4]);
    car->visual_pitch=landing_angle(-local[5],horizontal);
    if(horizontal>64) {
        car->yaw=landing_angle(local[2],local[8]);
        car->visual_roll=landing_angle(local[3],local[4]);
    } else if(local[5]<0) {
        car->visual_roll=(car->yaw-landing_angle(local[1],local[0]))&255;
    } else {
        car->visual_roll=(landing_angle(-local[1],local[0])-car->yaw)&255;
    }
    car->flip_timer=0;
    car->settle_delay=4;
    car->settle_pitch_velocity=car->settle_roll_velocity=0;
}

static int settle_angle(int angle, int *velocity) {
    int error=angle&255;if(error>128)error-=256;
    if(error>=-1 && error<=1){*velocity=0;return 0;}
    /* Damped angular spring: ease into recovery, then slow near upright. */
    *velocity=(*velocity*3)/4-error*12;
    if(*velocity>768)*velocity=768;
    if(*velocity<-768)*velocity=-768;
    int step=*velocity/256;
    if(!step)step=error>0?-1:1;
    if((error>0 && error+step<0)||(error<0 && error+step>0)) {
        *velocity=0;return 0;
    }
    return (error+step)&255;
}

/* Camera headings use 8 fractional bits; gameplay headings stay 0..255. */
static int smooth_camera_heading(int current,int target) {
    int delta=((target*256-current+32768)&65535)-32768;
    int step=delta/10;
    if(!step && delta)step=delta>0?1:-1;
    if(step>3*256)step=3*256;
    if(step<-3*256)step=-3*256;
    return (current+step)&65535;
}

/* Fractional yaw with acceleration and braking toward the car heading. */
static int chase_turn_velocity;
static int follow_chase_heading(int current,const Car *car) {
    int target=(car->yaw*256+car->steer_fraction)&65535;
    int delta=((target-current+32768)&65535)-32768;
    if(!delta){chase_turn_velocity=0;return current;}
    int desired=delta;
    if(desired>2048)desired=2048;
    if(desired<-2048)desired=-2048;
    /* Follow 87.5% of the requested change immediately; retain a light ease. */
    int acceleration=(desired-chase_turn_velocity)*7/8;
    if(!acceleration && desired!=chase_turn_velocity)
        acceleration=desired>chase_turn_velocity?1:-1;
    chase_turn_velocity+=acceleration;
    int step=chase_turn_velocity;
    if(abs(delta)<=4 && abs(step)<=4){chase_turn_velocity=0;return target;}
    if((delta>0 && step>delta)||(delta<0 && step<delta)) {
        chase_turn_velocity=0;return target;
    }
    return (current+step)&65535;
}

static void reset_steering(Car *car) {car->steer_velocity=car->steer_fraction=0;}
static void steer_car(Car *car,int direction,int drifting,int blocked) {
    if(!direction || blocked){car->steer_velocity=0;return;}
    int speed=abs(car->speed)+(abs(car->vel.x)+abs(car->vel.z))/2;
    int rate=car->is_on_ground?768-speed/8:512;
    if(rate<384)rate=384;
    if(drifting)rate=1024;
    /* Half the previous steering speed, retaining fractional turn precision. */
    int target=direction*(rate*3/16);
    car->steer_velocity+=(target-car->steer_velocity)/4;
    car->steer_fraction+=car->steer_velocity;
    int step=car->steer_fraction/256;
    car->steer_fraction-=step*256;
    car->yaw=(car->yaw+step)&255;
}
static void grip_car(Car *car,int drifting,int braking) {
    if(!car->is_on_ground)return;
    if(!drifting) {
        Vector3 right=stadium_surface_vector((Vector3){custom_cos_fp[car->yaw&255],0,
            -custom_sin_fp[car->yaw&255]},car->surface_wall,car->surface_angle);
        fixed sideways=FP_MUL(car->vel.x,right.x)+FP_MUL(car->vel.y,right.y)+FP_MUL(car->vel.z,right.z);
        sideways=sideways*3/8;
        car->vel.x-=FP_MUL(right.x,sideways);
        car->vel.y-=FP_MUL(right.y,sideways);
        car->vel.z-=FP_MUL(right.z,sideways);
    }
    /* Braking also slows momentum from boost, rather than just drive speed. */
    if(braking) {
        car->vel.x=car->vel.x*216/256;car->vel.y=car->vel.y*216/256;
        car->vel.z=car->vel.z*216/256;
    }
}

static int car_needs_recovery(const Car *car) {
    return car->is_on_ground &&
        custom_cos_fp[car->visual_pitch&255]*custom_cos_fp[car->visual_roll&255]<128*256;
}
static void assist_ground_recovery(Car *car,int held) {
    if(!held || !car_needs_recovery(car))return;
    /* Deliberate help, not a snap to upright: reuse the damped landing spring. */
    car->settle_delay=0;
    car->visual_pitch=settle_angle(car->visual_pitch,&car->settle_pitch_velocity);
    car->visual_roll=settle_angle(car->visual_roll,&car->settle_roll_velocity);
}

static void limit_boost_velocity(Vector3 *velocity,int limit) {
    int x=velocity->x/16,y=velocity->y/16,z=velocity->z/16;
    int squared=x*x+y*y+z*z,cap=limit/16;
    if(squared<=cap*cap)return;
    int length=int_sqrt(squared)+1;
    velocity->x=velocity->x*cap/length;
    velocity->y=velocity->y*cap/length;
    velocity->z=velocity->z*cap/length;
}

static void apply_air_rotation(Car *car,int pitch,int roll) {
    car->visual_pitch=(car->visual_pitch+pitch*(roll?3:4))&255;
    car->visual_roll=(car->visual_roll+roll*(pitch?4:5))&255;
}

/* Rotate the standing pivot offset as well as the body. Wheel contact stays
   on the surface, while aerial rotation remains centered on the actual car. */
static Vector3 surface_car_position(int model, const Car *car, const int32_t rotation[9]) {
    if(!car->surface_wall || !car->surface_angle)
        return car_render_position(model,car->pos,car->yaw,rotation);
    static const int32_t identity[9]={4096,0,0,0,4096,0,0,0,4096};
    int32_t frame[9];
    for(int col=0;col<3;col++) {
        Vector3 v={identity[col],identity[3+col],identity[6+col]};
        v=stadium_surface_vector(v,car->surface_wall,car->surface_angle);
        frame[col]=v.x;frame[3+col]=v.y;frame[6+col]=v.z;
    }
    Vector3 baseline=car_render_position(model,(Vector3){0,0,0},car->yaw,identity);
    Vector3 tilted=car_render_position(model,(Vector3){0,0,0},car->yaw,frame);
    Vector3 shift=stadium_surface_vector(baseline,car->surface_wall,car->surface_angle);
    Vector3 pos=car->pos;
    pos.x+=shift.x-tilted.x;pos.y+=shift.y-tilted.y;pos.z+=shift.z-tilted.z;
    return car_render_position(model,pos,car->yaw,rotation);
}

static void jump_from_surface(Car *car, fixed impulse) {
    Vector3 normal=stadium_surface_vector((Vector3){0,256,0},car->surface_wall,car->surface_angle);
    car->vel.x+=FP_MUL(normal.x,impulse);
    car->vel.y+=FP_MUL(normal.y,impulse);
    car->vel.z+=FP_MUL(normal.z,impulse);
    car->detach_timer=6;
    car->jump_hold=6;
    car->is_on_ground=0;
}

/* A tap keeps the strong short hop; holding jump briefly extends the lift. */
static void extend_jump(Car *car,int held) {
    if(!held || car->is_on_ground){car->jump_hold=0;return;}
    if(car->jump_hold>0) {
        Vector3 lift=stadium_surface_vector((Vector3){0,FP_SCALE/5,0},car->surface_wall,car->surface_angle);
        car->vel.x+=lift.x;car->vel.y+=lift.y;car->vel.z+=lift.z;
        car->jump_hold--;
    }
}


typedef struct {
    Vector3 pos;
    Vector3 vel;
} Ball;

/* --- Global Game Objects & State --- */
static GameState game_state = STATE_START_SCREEN;
static Car player;
static Car opponent;
static Ball ball;
static int camera_yaw = 0;

static int measured_fps = 0;
static int animation_ticks = 1;
static int ball_spin_y,ball_spin_x; /* Q8 angles retain slow rolling motion. */
static Car previous_player __attribute__((section(".ewram")));
static Car previous_opponent __attribute__((section(".ewram")));
static Ball previous_ball;
static int previous_camera_yaw;
static int previous_ball_spin_y,previous_ball_spin_x;
static int render_fraction=256;

static int score_blue = 0;
static int score_orange = 0;
static int match_timer = 120 * 60; // 2 minutes in frames (60 fps)
static int state_timer = 0;        // Multi-use countdown timer for states
static int scoring_team = 0;       // 3 = Player scored, 6 = AI scored
static int ai_difficulty = 3;      // 1 to 5
static int enable_opponent = 1;    // 1 = ON, 0 = OFF
static int control_scheme = 0;     // 0 = Classic, 1 = Alternative
static int screen_shake = 0;       // Screen shake duration counter
static int show_boost_alert = 0;   // Frames to show "NO BOOST" warning
static int cam_mode = 0;           // 0=Chase, 1=Front, 2=Ball-cam
static int link_match,link_waiting,link_hockey,link_saved_opponent;
static u16 link_buttons[2],link_previous[2];
static int control_override;
static u16 control_down,control_hits;
static int drive_down(int key){return control_override?(control_down&key):key_is_down(key);}
static int drive_hit(int key){return control_override?(control_hits&key):key_hit(key);}

static int is_hockey_match = 0;    // 1 = playing hockey, 0 = soccer
/* Team identity stays blue/orange; cosmetic paint never changes scoring. */
static int garage_side = 0;
static int garage_row = 0;
static int garage_model[2] = { 0, 1 };
static int garage_paint[2] = { 0, 0 };
static int garage_goal[2] = { 0, 1 };
static const char *goal_effect_names[3]={"CONFETTI","NOVA","VORTEX"};
static Vector3 goal_effect_pos;
static int goal_effect_style;
static const u8 team_paints[2][3] = { { 3, 5, 7 }, { 6, 1, 4 } };
static const char *const team_paint_names[2][3] = {
    { "BLUE", "CYAN", "PURPLE" }, { "ORANGE", "RED", "GOLD" }
};
static int pause_selection = 0;    // 0=Resume, 1=Restart match, 2=Exit to Menu

/* --- Particles --- */
typedef struct {
    Vector3 pos;
    Vector3 vel;
    int life;
    u8 color;
    u8 flags;
} Particle;
#define MAX_PARTICLES 100
static Particle particles[MAX_PARTICLES] __attribute__((section(".ewram"), aligned(4)));

enum {
    PARTICLE_GRAVITY = 1,
    PARTICLE_CONFETTI = 2
};

/* A compact rolling replay buffer gives every goal a broadcast-style second
   look without storing a full frame buffer.  It records positions and the
   visible attitude only, which keeps the replay small enough for GBA EWRAM. */
typedef struct {
    Vector3 player_pos;
    Vector3 opponent_pos;
    Vector3 ball_pos;
    Vector3 ball_vel;
    int player_yaw;
    int opponent_yaw;
    int player_pitch;
    int player_roll;
    u8 player_flip_base_pitch,player_flip_base_roll;
    u8 player_flip_timer;
    signed char player_flip_pitch_dir, player_flip_roll_dir;
    int opponent_pitch;
    int opponent_roll;
    signed char player_wall, opponent_wall;
    u8 player_surface_angle, opponent_surface_angle;
} ReplayFrame;

#define REPLAY_MAX_FRAMES 180
#define REPLAY_MIN_FRAMES 45
static ReplayFrame replay_frames[REPLAY_MAX_FRAMES]
    __attribute__((section(".ewram"), aligned(4)));
static int replay_write = 0;
static int replay_count = 0;
static int replay_read = 0;
static int replay_remaining = 0;
static int replay_hold = 0;
static int replay_camera_phase = 0;
static int stadium_light_phase = 0;

/* Used by the replay handoff before the full setup section below. */
void reset_kickoff(void);

/* --- Boost Pads --- */
#define NUM_BOOST_PADS   6
#define PAD_RADIUS       (24 * FP_SCALE)   // pickup range XZ
#define PAD_RESPAWN      (600)             // 10 seconds refill timer (60 FPS)

typedef struct {
    Vector3 pos;
    int     cooldown;   // >0 = on cooldown (just picked up)
    int     amount;     // 100 or 12
} BoostPad;

static BoostPad boost_pads[NUM_BOOST_PADS];
static int pad_pulse = 0; // global frame counter for pulsing animation

static void init_boost_pads(void) {
    fixed cy = 10 * FP_SCALE;
    
    // 4 large pads in the corners (100 boost)
    fixed cx = 230 * FP_SCALE;
    fixed cz = 360 * FP_SCALE;
    boost_pads[0] = (BoostPad){ { -cx, cy, -cz }, 0, 100 };
    boost_pads[1] = (BoostPad){ {  cx, cy, -cz }, 0, 100 };
    boost_pads[2] = (BoostPad){ {  cx, cy,  cz }, 0, 100 };
    boost_pads[3] = (BoostPad){ { -cx, cy,  cz }, 0, 100 };
    
    // 2 small pads (12 boost) in the midfield
    boost_pads[4] = (BoostPad){ { -150 * FP_SCALE, cy, 0 }, 0, 12 };
    boost_pads[5] = (BoostPad){ {  150 * FP_SCALE, cy, 0 }, 0, 12 };
}

/* --- Physics Constants (8.8 Fixed Point) --- */
#define STADIUM_WIDTH    (306 * FP_SCALE)
#define STADIUM_LENGTH   (459 * FP_SCALE)
#define STADIUM_HEIGHT   (204 * FP_SCALE)
/* Goals are deliberately roomier than the original field markings: the
   scoring opening and the visible goal frame use the same 120% sizing. */
#define GOAL_HALF_WIDTH  ((91 * FP_SCALE * 120) / 100)
#define GOAL_HEIGHT      ((63 * FP_SCALE * 120) / 100)
#define GOAL_RENDER_SCALE 353

/* The field markings retain their original regulation footprint while the
   surrounding stadium cage gets 10% more breathing room. */
#define CAGE_WIDTH       ((STADIUM_WIDTH * 110) / 100)
#define CAGE_LENGTH      ((STADIUM_LENGTH * 110) / 100)
#define CAGE_HEIGHT      ((STADIUM_HEIGHT * 110) / 100)

#define GRAVITY          ((70 * 105 + 50) / 100) // +5%, rounded to nearest 8.8 unit
#define JUMP_FORCE       (7 * FP_SCALE) // 40% stronger takeoff impulse
#define MAX_DRIVE_SPEED  ((1232 * FP_SCALE) / 100) // 10% faster driving and boost cap
#define ACCEL_RATE       (88)  // 10% quicker acceleration for both cars
#define BOOST_ACCEL      (187) // Match the 10% increase in driving pace
#define DRAG_COEFF       (234) // Deceleration 2x (loses 22/256 per frame vs 11/256 previously)

#define CAR_RADIUS       (13 * FP_SCALE)
#define BALL_RADIUS      (14 * FP_SCALE)
#define PUCK_RADIUS      (12 * FP_SCALE)
#define PUCK_HALF_HEIGHT (4 * FP_SCALE)
#define MIN_DIST_COLL    (25 * FP_SCALE) // Radius sum (13 + 12)

/* --- Precomputed Center Circle Geometry (16 points, Radius 36 - Scaled 20%) --- */
static const Vector3 center_circle_pts[16] __attribute__((aligned(4))) = {
    {   0, 0,  36 * FP_SCALE },
    {  13 * FP_SCALE, 0,  32 * FP_SCALE },
    {  25 * FP_SCALE, 0,  25 * FP_SCALE },
    {  32 * FP_SCALE, 0,  13 * FP_SCALE },
    {  36 * FP_SCALE, 0,   0 },
    {  32 * FP_SCALE, 0, -13 * FP_SCALE },
    {  25 * FP_SCALE, 0, -25 * FP_SCALE },
    {  13 * FP_SCALE, 0, -32 * FP_SCALE },
    {   0, 0, -36 * FP_SCALE },
    { -13 * FP_SCALE, 0, -32 * FP_SCALE },
    { -25 * FP_SCALE, 0, -25 * FP_SCALE },
    { -32 * FP_SCALE, 0, -13 * FP_SCALE },
    { -36 * FP_SCALE, 0,   0 },
    { -32 * FP_SCALE, 0,  13 * FP_SCALE },
    { -25 * FP_SCALE, 0,  25 * FP_SCALE },
    { -13 * FP_SCALE, 0,  32 * FP_SCALE }
};

/* --- Soccer Pitch Renderer --- */
void draw_soccer_pitch(Vector3 cam_pos) {
    u8 line_color = 130; // White lines

    // The visible stadium cage sits 10% outside the pitch on every side.
    Vector3 cage_corners[4] = {
        { -CAGE_WIDTH, 0, -CAGE_LENGTH },
        {  CAGE_WIDTH, 0, -CAGE_LENGTH },
        {  CAGE_WIDTH, 0,  CAGE_LENGTH },
        { -CAGE_WIDTH, 0,  CAGE_LENGTH }
    };

    if (!is_hockey_match) {
        Vector3 corners[4]={{-STADIUM_WIDTH,0,-STADIUM_LENGTH},{STADIUM_WIDTH,0,-STADIUM_LENGTH},
                            {STADIUM_WIDTH,0,STADIUM_LENGTH},{-STADIUM_WIDTH,0,STADIUM_LENGTH}};
        for(int edge=0;edge<4;edge++) draw_world_line(corners[edge],corners[(edge+1)&3],line_color);
        draw_world_line((Vector3){-STADIUM_WIDTH,0,0},(Vector3){STADIUM_WIDTH,0,0},line_color);
        /* Shared projections keep the circle cheap; crossing segments use near clipping. */
        int x[16],y[16],visible[16];
        for(int i=0;i<16;i+=performance_mode==2?2:1) visible[i]=project_vertex_world(center_circle_pts[i],&x[i],&y[i]);
        for(int i=0;i<16;i+=performance_mode==2?2:1) {
            int next=(i+(performance_mode==2?2:1))&15;
            if(visible[i] && visible[next]) draw_line(x[i],y[i],x[next],y[next],line_color);
            else if(visible[i] || visible[next]) draw_world_line(center_circle_pts[i],center_circle_pts[next],line_color);
        }
        draw_world_line((Vector3){-2*FP_SCALE,0,0},(Vector3){2*FP_SCALE,0,0},line_color);
        for(int side=-1;side<=1;side+=2) {
            fixed end=side*STADIUM_LENGTH;
            for(int area=0;area<(performance_mode==2?0:2);area++) {
                fixed width=(area?110:150)*FP_SCALE;
                fixed front=end-side*(area?24:50)*FP_SCALE;
                Vector3 left={-width,0,end},left_front={-width,0,front};
                Vector3 right={width,0,end},right_front={width,0,front};
                draw_world_line(left,left_front,line_color);
                draw_world_line(left_front,right_front,line_color);
                draw_world_line(right_front,right,line_color);
            }
            /* Team-colored goal-mouth stripe distinguishes attack from defense. */
            draw_world_line((Vector3){-GOAL_HALF_WIDTH,FP_SCALE,end},
                            (Vector3){GOAL_HALF_WIDTH,FP_SCALE,end},side>0?131:146);
        }
    }

    if(is_hockey_match)draw_hockey_markings(STADIUM_WIDTH,STADIUM_LENGTH);
    int cam_x = cam_pos.x;
    int cam_z = cam_pos.z;
    if(performance_mode==2)return;
    draw_stadium_hex_walls(cam_pos, STADIUM_WIDTH, STADIUM_LENGTH, STADIUM_HEIGHT);

    // Vertical Pillars at corners
    Vector3 pillars[4] = { cage_corners[0], cage_corners[1], cage_corners[2], cage_corners[3] };
    for (int i = 0; i < 4; i++) {
        // Hide pillars if they are on a hidden wall. We can approximate by checking if camera is outside the corner's quadrant.
        int hide = 0;
        if (pillars[i].x < 0 && cam_x < -CAGE_WIDTH - 1000) hide = 1;
        if (pillars[i].x > 0 && cam_x > CAGE_WIDTH + 1000) hide = 1;
        if (pillars[i].z < 0 && cam_z < -CAGE_LENGTH - 1000) hide = 1;
        if (pillars[i].z > 0 && cam_z > CAGE_LENGTH + 1000) hide = 1;

        if (!hide) {
            Vector3 bottom = pillars[i];
            Vector3 top = pillars[i];
            top.y = CAGE_HEIGHT;
            int bx, by, tx, ty;
            if (project_vertex_world(bottom, &bx, &by) && project_vertex_world(top, &tx, &ty)) {
                u8 p_col = 130;
                if (pillars[i].z == -CAGE_LENGTH) p_col = 60; // Blue end
                else if (pillars[i].z == CAGE_LENGTH) p_col = 108; // Orange end
                else p_col = 129; // Touchlines
                draw_line(bx, by, tx, ty, p_col);
            }
        }
    }


}

/* Small floodlight clusters make the cage feel like an enclosed night arena.
   They are intentionally point-based: this stays crisp and cheap in Mode 4. */
static void draw_stadium_floodlights(void) {
    static const signed char anchors[12][2] = {
        { -80, -92 }, { -27, -100 }, { 27, -100 }, { 80, -92 },
        { -92, -42 }, {  92,  -42 }, { -92,  42 }, { 92,  42 },
        { -80,  92 }, { -27,  100 }, { 27,  100 }, { 80,  92 }
    };

    for (int i = 0; i < 12; i++) {
        Vector3 light;
        int sx, sy;
        light.x = (anchors[i][0] * CAGE_WIDTH) / 100;
        light.z = (anchors[i][1] * CAGE_LENGTH) / 100;
        light.y = CAGE_HEIGHT - 12 * FP_SCALE;

        if (project_vertex_world(light, &sx, &sy)) {
            u8 colour = (i < 4) ? 129 : (i < 8 ? 130 : 131);
            int sparkle = ((stadium_light_phase + i * 19) & 63) < 10;
            draw_point(sx, sy, colour);
            draw_point(sx + 1, sy, 130);
            if (sparkle) {
                draw_point(sx - 1, sy, colour);
                draw_point(sx, sy - 1, 130);
                draw_point(sx, sy + 1, colour);
            }
        }
    }
}

/* Two concentric, clipped ground polygons track the actual heading and
 * footprint. Height broadens the soft edge and offsets it away from the light. */
static void draw_car_shadow(Vector3 pos, int yaw, int model) {
    static const signed char ring[8][2] = {
        { -7, -10 }, { 7, -10 }, { 10, -7 }, { 10, 7 },
        { 7, 10 }, { -7, 10 }, { -10, 7 }, { -10, -7 }
    };
    Vector3 vertices[9];
    Face faces[8] = {0};
    Mesh shadow = { "SHADOW", 9, 8, vertices, faces, NULL };
    int height = FP_TO_INT(pos.y);
    if (height < 0) height = 0;
    if (height > 120) height = 120;
    pos.x -= height * 96;
    pos.z += height * 51;
    pos.y = FP_SCALE;
    if(!world_sphere_visible(pos,40*FP_SCALE))return;
    if(performance_mode) {
        int sx,sy,ex,ey;
        Vector3 edge=pos;edge.x+=15*FP_SCALE;
        if(!project_vertex_world(pos,&sx,&sy) || !project_vertex_world(edge,&ex,&ey))return;
        int radius=abs(ex-sx)+abs(ey-sy);
        if(radius<2)radius=2;
        if(radius>24)radius=24;
        int ry=radius/3+1;
        for(int y=-ry;y<=ry;y++) {
            int py=sy+y;
            if(py<0 || py>=RENDER_HEIGHT)continue;
            int span=radius-abs(y)*radius/(ry+1);
            int left=sx-span,right=sx+span;
            if(left<0)left=0;
            if(right>=RENDER_WIDTH)right=RENDER_WIDTH-1;
            if(right>=left)fast_span_fill(frame_buffer+py*SCREEN_WIDTH+left,
                (is_hockey_match?SHADOW_ICE_EDGE:SHADOW_GRASS_EDGE)*0x01010101u,right-left+1);
        }
        return;
    }
    for (int layer = 0; layer < 2; ++layer) {
        /* The dense contact core gradually shrinks as the car lifts off. */
        int width = layer ? 11 - height / 16 : 15 + height / 24;
        int length = (model == 1 ? 22 : 19) + (layer ? -height / 10 : height / 20);
        u8 color = is_hockey_match
            ? (layer && height < 40 ? SHADOW_ICE_CORE : SHADOW_ICE_EDGE)
            : (layer && height < 40 ? SHADOW_GRASS_CORE : SHADOW_GRASS_EDGE);
        if (layer && height > 80) continue;
        vertices[0] = (Vector3){0,0,0};
        for (int i = 0; i < 8; ++i) {
            vertices[i+1] = (Vector3){ ring[i][0] * width * FP_SCALE / 10, 0,
                                      ring[i][1] * length * FP_SCALE / 10 };
            faces[i].v1 = 0;
            faces[i].v2 = (i+1)%8+1;
            faces[i].v3 = i+1;
            faces[i].base_color = color;
        }
        draw_model_world(&shadow, pos, yaw, 0, 0, FP_ONE, -1, RENDER_FLAT);
    }
}

/* A ball needs a shadow even while airborne.  A two-layer flattened ellipse
   gives a readable contact point without an expensive projected decal. */
static void draw_ball_ground_shadow(Vector3 ball_pos) {
    Vector3 ground_pos = ball_pos;
    int sx, sy;
    int height = FP_TO_INT(ball_pos.y);
    int shrink = height / 46;
    int outer_rx;
    int outer_ry;

    if (shrink > 4) shrink = 4;
    ground_pos.y = 0;
    if (!project_vertex_world(ground_pos, &sx, &sy)) return;

    outer_rx = 9 - shrink;
    outer_ry = 3;
    for (int dy = -outer_ry; dy <= outer_ry; dy++) {
        int py = sy + dy;
        int span = outer_rx;
        int ry_sq = outer_ry * outer_ry;
        int rx_sq = outer_rx * outer_rx;
        while (span > 0 && span * span * ry_sq + dy * dy * rx_sq > rx_sq * ry_sq) span--;
        if (py >= 0 && py < RENDER_HEIGHT) {
            int left = sx - span;
            int right = sx + span;
            u32 edge = is_hockey_match ? SHADOW_ICE_EDGE : SHADOW_GRASS_EDGE;
            u32 dark = edge * 0x01010101u;
            if (left < 0) left = 0;
            if (right >= RENDER_WIDTH) right = RENDER_WIDTH - 1;
            if (left <= right) fast_span_fill(&frame_buffer[py * SCREEN_WIDTH + left], dark, right - left + 1);
        }
    }

    /* Colored contact core fades into the soft edge as the ball rises. */
    if (height > 100) return;
    for (int dy = -1; dy <= 1; dy++) {
        int py = sy + dy;
        int span = outer_rx - 3;
        u32 color = is_hockey_match
            ? (height < 50 ? SHADOW_ICE_CORE : SHADOW_ICE_EDGE)
            : (height < 50 ? SHADOW_GRASS_CORE : SHADOW_GRASS_EDGE);
        u32 core = color * 0x01010101u;
        if (span < 2) span = 2;
        if (py >= 0 && py < RENDER_HEIGHT) {
            int left = sx - span;
            int right = sx + span;
            if (left < 0) left = 0;
            if (right >= RENDER_WIDTH) right = RENDER_WIDTH - 1;
            if (left <= right) fast_span_fill(&frame_buffer[py * SCREEN_WIDTH + left], core, right - left + 1);
        }
    }
}

static void radar_point(Vector3 pos, u8 color, int player_marker) {
    int x=22+(pos.x/FP_SCALE)*16/306;
    int y=126-(pos.z/FP_SCALE)*24/459;
    if(x<6)x=6;
    if(x>38)x=38;
    if(y<102)y=102;
    if(y>150)y=150;
    draw_point(x,y,color);draw_point(x-1,y,color);draw_point(x+1,y,color);
    draw_point(x,y-1,color);draw_point(x,y+1,color);
    if(player_marker) {
        int dx=custom_sin_fp[player.yaw&255]*4/256;
        int dy=-custom_cos_fp[player.yaw&255]*4/256;
        draw_line(x,y,x+dx,y+dy,color);
    }
}
void draw_radar(void) {
    draw_line(6,102,38,102,13);draw_line(6,150,38,150,13);
    draw_line(6,102,6,150,13);draw_line(38,102,38,150,13);
    draw_line(6,126,38,126,13);
    draw_line(17,101,27,101,131); /* orange goal, forward up the map */
    draw_line(17,151,27,151,146);
    radar_point(player.pos,player.team==6?131:146,1);
    if(enable_opponent && game_state!=STATE_TRAINING && game_state!=STATE_TUTORIAL)
        radar_point(opponent.pos,opponent.team==6?131:146,0);
    radar_point(ball.pos,130,0);
}

/* --- Big Minimap Overlay (shown while SELECT is held) --- */
void draw_big_radar(void) {
    int cx = 120; // center x (screen pixel)
    int cy = 80;  // center y (screen pixel)
    int hw = 90;  // half-width  (stadium_x ±306 → ±90 px)
    int hh = 62;  // half-height (stadium_z ±459 → ±62 px)

    /* Dark background fill */
    u32 bg_col4 = 17 | (17 << 8) | (17 << 16) | (17 << 24);
    for (int y = cy - hh; y <= cy + hh; y++) {
        if (y < 0 || y >= RENDER_HEIGHT) continue;
        int left = cx - hw;
        int width = hw * 2 + 1;
        if (left < 0) left = 0;
        if (left + width > RENDER_WIDTH) width = RENDER_WIDTH - left;
        if (width > 0) fast_span_fill(&frame_buffer[y * 240 + left], bg_col4, width);
    }

    /* Outer boundary */
    draw_line(cx - hw, cy - hh, cx + hw, cy - hh, 130);
    draw_line(cx - hw, cy + hh, cx + hw, cy + hh, 130);
    draw_line(cx - hw, cy - hh, cx - hw, cy + hh, 130);
    draw_line(cx + hw, cy - hh, cx + hw, cy + hh, 130);

    /* Midfield line */
    draw_line(cx - hw, cy, cx + hw, cy, 130);

    /* Goals (slightly outside boundary box for clarity) */
    int ghw = (int)(GOAL_HALF_WIDTH / FP_SCALE) * hw / 306;
    draw_line(cx - ghw, cy - hh, cx + ghw, cy - hh, 129); /* Blue goal  (top) */
    draw_line(cx - ghw, cy + hh, cx + ghw, cy + hh, 131); /* Orange goal (bot) */

    /* Boost pads */
    for (int i = 0; i < NUM_BOOST_PADS; i++) {
        int px = cx + (boost_pads[i].pos.x / FP_SCALE) * hw / 306;
        int py = cy + (boost_pads[i].pos.z / FP_SCALE) * hh / 459;
        u8 pc = (boost_pads[i].cooldown > 0) ? 128 :
                (boost_pads[i].amount == 100 ? 131 : 129);
        draw_point(px, py, pc);
        draw_point(px + 1, py, pc);
        draw_point(px, py + 1, pc);
    }

    /* Player (cyan 5×5) */
    int ppx = cx + (player.pos.x / FP_SCALE) * hw / 306;
    int ppy = cy + (player.pos.z / FP_SCALE) * hh / 459;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            draw_point(ppx + dx, ppy + dy, 129);

    /* Opponent (orange 5×5) */
    int opx = cx + (opponent.pos.x / FP_SCALE) * hw / 306;
    int opy = cy + (opponent.pos.z / FP_SCALE) * hh / 459;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            draw_point(opx + dx, opy + dy, 131);

    /* Ball (white 5×5) */
    int bpx = cx + (ball.pos.x / FP_SCALE) * hw / 306;
    int bpy = cy + (ball.pos.z / FP_SCALE) * hh / 459;
    for (int dy = -2; dy <= 2; dy++)
        for (int dx = -2; dx <= 2; dx++)
            draw_point(bpx + dx, bpy + dy, 130);

    /* Labels */
    draw_string("MINIMAP", (cx - hw) * RENDER_SCALE, (cy - hh - 12) * RENDER_SCALE, 131);
    draw_string("BLU", (cx - hw - 2) * RENDER_SCALE, (cy - 4) * RENDER_SCALE, 129);
    draw_string("ORA", (cx - hw - 2) * RENDER_SCALE, (cy + 2) * RENDER_SCALE, 131);
    draw_string("HOLD SELECT", (cx - hw) * RENDER_SCALE, (cy + hh + 4) * RENDER_SCALE, 129);
}

void spawn_boost_particle(Vector3 pos, Vector3 forward) {
    fixed back_x = -forward.x;
    fixed back_y = -forward.y;
    fixed back_z = -forward.z;

    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life <= 0) {
            particles[i].pos = pos;
            particles[i].vel.x = (back_x * 2) + ((rand() % 256) - 128);
            particles[i].vel.y = (back_y * 2) + ((rand() % 256) - 128);
            particles[i].vel.z = (back_z * 2) + ((rand() % 256) - 128);
            particles[i].life = 15;
            particles[i].color = (rand() % 2 == 0) ? 108 : 130; // Lit orange and white exhaust
            particles[i].flags = 0;
            break;
        }
    }
}

void spawn_skid_particle(Vector3 pos) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life <= 0) {
            particles[i].pos = pos;
            particles[i].vel.x = 0;
            particles[i].vel.y = 0;
            particles[i].vel.z = 0;
            particles[i].life = 45; // Longer life for skid marks
            particles[i].color = 132; // Black color for skid
            particles[i].flags = 0;
            break;
        }
    }
}

void spawn_explosion(Vector3 pos, u8 color) {
    int spawned = 0;
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life <= 0) {
            particles[i].pos = pos;
            particles[i].vel.x = ((rand() % 512) - 256) * 3;
            particles[i].vel.y = ((rand() % 512) - 128) * 3;
            particles[i].vel.z = ((rand() % 512) - 256) * 3;
            particles[i].life = 30 + (rand() % 30);
            particles[i].color = color;
            particles[i].flags = PARTICLE_GRAVITY;
            if (++spawned == 14) break;
        }
    }
}

static void spawn_goal_celebration(Vector3 pos, u8 team_color) {
    static const u8 blue_colours[4] = { 129, 130, 133, 3 };
    static const u8 orange_colours[4] = { 131, 130, 6, 16 };
    const u8 *colours = team_color == 3 ? blue_colours : orange_colours;
    int spawned = 0;

    /* A wide, upward spray reads as confetti instead of a flat particle pop. */
    for (int i = 0; i < MAX_PARTICLES && spawned < 32; i++) {
        if (particles[i].life <= 0) {
            particles[i].pos = pos;
            particles[i].pos.x += ((rand() % 121) - 60) * FP_SCALE / 2;
            particles[i].pos.y += (rand() % 32) * FP_SCALE;
            particles[i].pos.z += ((rand() % 121) - 60) * FP_SCALE / 2;
            particles[i].vel.x = ((rand() % 512) - 256) * 2;
            particles[i].vel.y = (rand() % 480) + 360;
            particles[i].vel.z = ((rand() % 512) - 256) * 2;
            particles[i].life = 48 + (rand() % 52);
            particles[i].color = colours[rand() & 3];
            particles[i].flags = PARTICLE_GRAVITY | PARTICLE_CONFETTI;
            spawned++;
        }
    }
}

void update_particles(void) {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particles[i].life > 0) {
            particles[i].life--;
            if (particles[i].flags & PARTICLE_GRAVITY) {
                particles[i].vel.y -= (particles[i].flags & PARTICLE_CONFETTI) ? 18 : 9;
            }
            particles[i].pos.x += particles[i].vel.x;
            particles[i].pos.y += particles[i].vel.y;
            particles[i].pos.z += particles[i].vel.z;
            
        }
    }
}

void draw_particles(void) {
    for(int i=0;i<MAX_PARTICLES;i++) {
        if(particles[i].life>0) {
            int sx, sy;
            Vector3 position=particles[i].pos;
            position.x-=particles[i].vel.x*(256-render_fraction)/256;
            position.y-=particles[i].vel.y*(256-render_fraction)/256;
            position.z-=particles[i].vel.z*(256-render_fraction)/256;
            if (project_vertex_world(position, &sx, &sy)) {
                draw_point(sx, sy, particles[i].color);
                if ((particles[i].flags & PARTICLE_CONFETTI) &&
                    particles[i].life > 42 && (i & 1) == 0) {
                    draw_point(sx + 1, sy, particles[i].color);
                }
            }
        }
    }
}

static void reset_replay_buffer(void) {
    replay_write = 0;
    replay_count = 0;
    replay_read = 0;
    replay_remaining = 0;
    replay_hold = 0;
}

static void capture_replay_frame(void) {
    ReplayFrame *frame = &replay_frames[replay_write];
    frame->player_pos = player.pos;
    frame->opponent_pos = opponent.pos;
    frame->ball_pos = ball.pos;
    frame->ball_vel = ball.vel;
    frame->player_yaw = player.yaw;
    frame->opponent_yaw = opponent.yaw;
    frame->player_pitch = player.visual_pitch;
    frame->player_roll = player.visual_roll;
    frame->player_flip_timer = player.flip_timer;
    frame->player_flip_base_pitch=player.flip_base_pitch;frame->player_flip_base_roll=player.flip_base_roll;
    frame->player_flip_pitch_dir = player.flip_pitch_dir;
    frame->player_flip_roll_dir = player.flip_roll_dir;
    frame->opponent_pitch = opponent.visual_pitch;
    frame->opponent_roll = opponent.visual_roll;
    frame->player_wall=player.surface_wall;frame->player_surface_angle=player.surface_angle;
    frame->opponent_wall=opponent.surface_wall;frame->opponent_surface_angle=opponent.surface_angle;

    replay_write = (replay_write + 1) % REPLAY_MAX_FRAMES;
    if (replay_count < REPLAY_MAX_FRAMES) replay_count++;
}

static int start_goal_replay(void) {
    int frames = replay_count;
    if (frames < REPLAY_MIN_FRAMES) return 0;
    if (frames > 135) frames = 135;

    replay_read = replay_write - frames;
    if (replay_read < 0) replay_read += REPLAY_MAX_FRAMES;
    replay_remaining = frames;
    replay_hold = 12;
    replay_camera_phase = 32;
    game_state = STATE_REPLAY;
    return 1;
}

static void advance_goal_replay(void) {
    if (replay_hold > 0) {
        replay_hold--;
        return;
    }

    if (replay_remaining <= 0) {
        reset_kickoff();
        return;
    }

    const ReplayFrame *frame = &replay_frames[replay_read];
    player.pos = frame->player_pos;
    opponent.pos = frame->opponent_pos;
    ball.pos = frame->ball_pos;
    ball.vel = frame->ball_vel;
    player.yaw = frame->player_yaw;
    opponent.yaw = frame->opponent_yaw;
    player.visual_pitch = frame->player_pitch;
    player.visual_roll = frame->player_roll;
    player.flip_timer = frame->player_flip_timer;
    player.flip_base_pitch=frame->player_flip_base_pitch;player.flip_base_roll=frame->player_flip_base_roll;
    player.flip_pitch_dir = frame->player_flip_pitch_dir;
    player.flip_roll_dir = frame->player_flip_roll_dir;
    opponent.visual_pitch = frame->opponent_pitch;
    opponent.visual_roll = frame->opponent_roll;
    player.surface_wall=frame->player_wall;player.surface_angle=frame->player_surface_angle;
    opponent.surface_wall=frame->opponent_wall;opponent.surface_angle=frame->opponent_surface_angle;

    replay_read = (replay_read + 1) % REPLAY_MAX_FRAMES;
    replay_remaining--;
    replay_camera_phase = (replay_camera_phase + 2) & 255;
}

/* --- Gameplay Setup / Reset --- */
void reset_kickoff(void) {
    // Reset Player (Blue team) at south kickoff spot facing North (yaw = 0)
    player.pos.x = 0;
    player.pos.y = 0;
    player.pos.z = -250 * FP_SCALE;
    player.vel.x = player.vel.y = player.vel.z = 0;
    player.yaw = 0; reset_steering(&player);
    player.speed = 0;
    player.boost = 34 * FP_SCALE; // Kickoff boost
    player.is_on_ground = 1;
    player.surface_wall = player.surface_angle = player.detach_timer = player.settle_delay = 0;
    player.jump_hold=player.settle_pitch_velocity=player.settle_roll_velocity=0;
    player.can_double_jump = 1;
    player.team = 3; // Blue team identity, independent of garage paint
    player.visual_pitch = 0;
    player.visual_roll = 0;
    player.flip_timer = player.boost_requested = 0;
    camera_yaw = link_match && link_player_id()==1 ? 128*256 : 0;chase_turn_velocity=0;  // Local kickoff heading

    // Reset Opponent (Orange team) at north kickoff spot facing South (yaw = 128)
    opponent.pos.x = 0;
    opponent.pos.y = 0;
    opponent.pos.z = 250 * FP_SCALE;
    opponent.vel.x = opponent.vel.y = opponent.vel.z = 0;
    opponent.yaw = 128; reset_steering(&opponent);
    opponent.speed = 0;
    opponent.boost = 34 * FP_SCALE;
    opponent.is_on_ground = 1;
    opponent.surface_wall = opponent.surface_angle = opponent.detach_timer = opponent.settle_delay = 0;
    opponent.jump_hold=opponent.settle_pitch_velocity=opponent.settle_roll_velocity=0;
    opponent.can_double_jump = 1;
    opponent.team = 6; // Orange override index
    opponent.visual_pitch = 0;
    opponent.visual_roll = 0;
    opponent.flip_timer = opponent.boost_requested = 0;

    // Reset Ball - hockey on ground, soccer drops from air
    ball.pos.x = 0;
    ball.pos.z = 0;
    if (is_hockey_match) {
        ball.pos.y = PUCK_HALF_HEIGHT;   // puck rests on its flat base
        ball.vel.x = 3 * FP_SCALE;  // small sideways nudge so it's live
        ball.vel.y = 0;
        ball.vel.z = 0;
    } else {
        ball.pos.y = 120 * FP_SCALE; // soccer ball drops straight from high up
        ball.vel.x = 0;              // no horizontal direction
        ball.vel.y = 0;
        ball.vel.z = 0;              // pure gravity drop
    }

    game_state = STATE_PLAY;
}

void reset_match(void) {
    score_blue = 0;
    score_orange = 0;
    match_timer = 120 * 60; // 2 minutes
    reset_replay_buffer();
    init_boost_pads();
    reset_kickoff();
    /* Ensure the correct floor texture is active */
    if (is_hockey_match) {
        active_pitch_mode = 1;
        init_hockey_pitch_texture();
    } else {
        active_pitch_mode = 0;
        init_pitch_texture();
    }
}

/* --- Guided tutorial objectives ------------------------------------------ */
static void setup_tutorial_stage(void) {
    const TutorialStage *stage = &tutorial_stages[current_tutorial_stage];
    current_tutorial_gate = 0;
    tutorial_progress = (TutorialProgress){0};
    tutorial_flash_timer = 0;

    player.pos = stage->car_start_pos;
    player.vel.x = player.vel.y = player.vel.z = 0;
    player.yaw = stage->car_start_yaw; reset_steering(&player);
    player.speed = 0;
    player.boost = 100 * FP_SCALE;
    player.is_on_ground = 1;
    player.surface_wall = player.surface_angle = player.detach_timer = player.settle_delay = 0;
    player.jump_hold=player.settle_pitch_velocity=player.settle_roll_velocity=0;
    player.can_double_jump = 1;
    player.team = 3;
    player.flip_timer = 0;
    player.visual_pitch = 0;
    player.visual_roll = 0;

    ball.pos = stage->ball_start_pos;
    ball.vel.x = 0;
    ball.vel.y = 0;
    /* Stationary ball ahead of the car makes the first shot easy to line up. */
    ball.vel.z = 0;
    camera_yaw = player.yaw*256;chase_turn_velocity=0;

    player.boost_requested = 0;
    cam_mode = 0;
    memset(particles, 0, sizeof(particles));
    game_state = STATE_TUTORIAL;
}

static Vector3 tutorial_active_target(void) {
    const TutorialStage *stage = &tutorial_stages[current_tutorial_stage];
    if (stage->objective == TUTORIAL_STEER_GATES) {
        return tutorial_steering_gates[current_tutorial_gate];
    }
    return stage->target_pos;
}

static int tutorial_target_reached(Vector3 object_pos, Vector3 target_pos, int radius) {
    int dx = (object_pos.x - target_pos.x) >> FP_SHIFT;
    int dz = (object_pos.z - target_pos.z) >> FP_SHIFT;
    return dx * dx + dz * dz < radius * radius;
}

static int tutorial_ball_scored(void) {
    return tutorial_progress.goal_scored;
}

static const char *tutorial_control_hint(const TutorialStage *stage) {
    switch (stage->objective) {
        case TUTORIAL_DRIVE_GATE:
            return control_scheme == 0 ? "UP: ACCELERATE" : "R: ACCELERATE";
        case TUTORIAL_STEER_GATES:
            return "LEFT RIGHT: STEER";
        case TUTORIAL_BOOST_GATE:
            return "B: HOLD BOOST";
        case TUTORIAL_JUMP_GATE:
            return "STOP. TAP A TO JUMP";
        case TUTORIAL_AERIAL_GATE:
            return "RELEASE D-PAD. TAP A TWICE";
        case TUTORIAL_AIM_SHOT:
            return control_scheme == 0 ? "UP: DRIVE THE BALL" : "R: DRIVE THE BALL";
    }
    return "";
}

static const char *tutorial_briefing_objective(const TutorialStage *stage) {
    switch (stage->objective) {
        case TUTORIAL_DRIVE_GATE:  return "ACCELERATE THROUGH ARCH";
        case TUTORIAL_STEER_GATES: return "STEER THROUGH 3 GATES";
        case TUTORIAL_BOOST_GATE:  return "BOOST THROUGH ARCH";
        case TUTORIAL_JUMP_GATE:   return "JUMP ON THE SPOT";
        case TUTORIAL_AERIAL_GATE: return "JUMP AGAIN WHILE IN AIR";
        case TUTORIAL_AIM_SHOT:    return "SCORE IN ORANGE GOAL";
    }
    return "";
}

static void start_tutorial_mode(void) {
    is_hockey_match = 0;
    active_pitch_mode = 0;
    reset_match();
    current_tutorial_stage = 0;
    tutorial_marker_pulse = 0;
    setup_tutorial_stage();
    game_state = STATE_TUTORIAL_BRIEFING;
}

static void complete_tutorial_stage(void) {
    /* Let success remain visible before moving to the next lesson. */
    tutorial_flash_timer = 45;
    player.vel = (Vector3){0,0,0};
    player.speed = 0;
    player.boost_requested = 0;
}

static void advance_tutorial_stage(void) {
    if (++current_tutorial_stage >= NUM_TUTORIAL_STAGES) {
        current_tutorial_stage = NUM_TUTORIAL_STAGES - 1;
        tutorial_complete_timer = 600;
        game_state = STATE_TUTORIAL_COMPLETE;
    } else {
        setup_tutorial_stage();
    }
}

static void apply_player_boost(void) {
    if (player.boost_requested) {
        player.boost_requested = 0;
        if (player.boost > 0) {
            if (game_state == STATE_TUTORIAL) tutorial_progress.boosted = 1;
            int32_t rotation[9];
            build_car_rotation(&player, rotation);
            /* Local +Z is the nose: column 2 of the very same rendered matrix. */
            fixed Fx = rotation[2] >> 4;
            fixed Fy = rotation[5] >> 4;
            fixed Fz = rotation[8] >> 4;

            int fuel_spent=player.boost<310?player.boost:310;
            fixed boost_accel = ((FP_SCALE * 1848) / 1000)*fuel_spent/310; // 10% quicker aerial boost
            fixed push_x = FP_MUL(Fx, boost_accel);
            fixed push_y = FP_MUL(Fy, boost_accel);
            fixed push_z = FP_MUL(Fz, boost_accel);

            if ((!player.surface_wall && push_y > 30) || (!player.is_on_ground && Fy > 10)) {
                player.is_on_ground = 0;
            }

            if (!player.is_on_ground) {
                player.vel.x += push_x;
                player.vel.y += push_y;
                player.vel.z += push_z;

                limit_boost_velocity(&player.vel,(198*FP_SCALE)/10);
            } else {
                player.speed += ((BOOST_ACCEL * 11) / 10)*fuel_spent/310;
                if (player.speed > MAX_DRIVE_SPEED * 18 / 10)
                    player.speed = MAX_DRIVE_SPEED * 18 / 10;
            }
            player.boost -= fuel_spent;
            if (player.boost < 0) player.boost = 0;
            /* Transform a rear exhaust point with the centered body transform. */
            Vector3 exhaust = surface_car_position(garage_model[player.team==6], &player, rotation);
            exhaust.x += (rotation[1] * 6 - rotation[2] * 20) / 16;
            exhaust.y += (rotation[4] * 6 - rotation[5] * 20) / 16;
            exhaust.z += (rotation[7] * 6 - rotation[8] * 20) / 16;
            spawn_boost_particle(exhaust, (Vector3){Fx, Fy, Fz});
            if(!performance_mode) {
                spawn_boost_particle(exhaust, (Vector3){Fx, Fy, Fz});
                spawn_boost_particle(exhaust, (Vector3){Fx, Fy, Fz});
            }
        } else {
            show_boost_alert = 45;
        }
    }
}

/* --- Physics Core Logic --- */
void update_car_physics(Car *car, int is_player) {
    Vector3 forward=stadium_surface_vector((Vector3){custom_sin_fp[car->yaw&255],0,
        custom_cos_fp[car->yaw&255]},car->surface_wall,car->surface_angle);
    fixed dir_x=forward.x, dir_y=forward.y, dir_z=forward.z;
    if(car->detach_timer>0) car->detach_timer--;

    // Apply drag/friction to driving speed
    car->speed = (car->speed * DRAG_COEFF) >> 8;

    // When airborne, transfer ground speed into 3D velocity vector so speed doesn't pull car
    if (!car->is_on_ground && car->speed != 0) {
        car->vel.x += FP_MUL(dir_x, car->speed);
        car->vel.y += FP_MUL(dir_y, car->speed);
        car->vel.z += FP_MUL(dir_z, car->speed);
        car->speed = 0;
    }

    // Flip animation logic
    if (car->flip_timer > 0) {
        int ticks = car->flip_timer < FLIP_STEP_TICKS ? car->flip_timer : FLIP_STEP_TICKS;
        car->flip_timer -= ticks;
        int angle = ((FLIP_DURATION_TICKS - car->flip_timer) * 256 / FLIP_DURATION_TICKS) & 255;
        car->visual_pitch = (car->flip_pitch_dir * angle) & 255;
        car->visual_roll = (car->flip_roll_dir * angle) & 255;
        if(!car->flip_timer) {
            car->visual_pitch=car->flip_base_pitch;
            car->visual_roll=car->flip_base_roll;
        }
        
        // Gradual acceleration during flip
        fixed c_dir_x = custom_sin_fp[car->yaw & 255];
        fixed c_dir_z = custom_cos_fp[car->yaw & 255];
        fixed c_right_x = custom_cos_fp[car->yaw & 255];
        fixed c_right_z = -custom_sin_fp[car->yaw & 255];
        
        fixed ax = 0;
        fixed az = 0;
        
        if (car->flip_pitch_dir == 1) { ax += (c_dir_x * 3) / 4; az += (c_dir_z * 3) / 4; }
        else if (car->flip_pitch_dir == -1) { ax -= (c_dir_x * 3) / 4; az -= (c_dir_z * 3) / 4; }
        
        if (car->flip_roll_dir == 1) { ax -= (c_right_x * 3) / 4; az -= (c_right_z * 3) / 4; }
        else if (car->flip_roll_dir == -1) { ax += (c_right_x * 3) / 4; az += (c_right_z * 3) / 4; }
        
        /* Keep the original total push with a slower flip; diagonals have
           the same strength as a straight dodge, not sqrt(2) times more. */
        if (car->flip_pitch_dir && car->flip_roll_dir) {
            ax = (ax * 181) / 256;
            az = (az * 181) / 256;
        }
        Vector3 impulse={ax,0,az};
        if(car->flip_base_pitch || car->flip_base_roll) {
            /* Keep dodge thrust in the same initial attitude as the flip. */
            int sy=custom_sin_fp[car->yaw&255],cy=custom_cos_fp[car->yaw&255];
            int sp=custom_sin_fp[car->flip_base_pitch&255],cp=custom_cos_fp[car->flip_base_pitch&255];
            int sr=custom_sin_fp[car->flip_base_roll&255],cr=custom_cos_fp[car->flip_base_roll&255];
            fixed lx=(cy*ax-sy*az)/256,lz=(sy*ax+cy*az)/256;
            fixed x=lx*cr/256,y=lx*sr/256;
            fixed z=(sp*y+cp*lz)/256;
            impulse.y=(cp*y-sp*lz)/256;
            impulse.x=(cy*x+sy*z)/256;impulse.z=(-sy*x+cy*z)/256;
        }
        impulse=stadium_surface_vector(impulse,car->surface_wall,car->surface_angle);
        car->vel.x += (impulse.x * 16 * ticks) / FLIP_DURATION_TICKS;
        car->vel.y += (impulse.y * 16 * ticks) / FLIP_DURATION_TICKS;
        car->vel.z += (impulse.z * 16 * ticks) / FLIP_DURATION_TICKS;
    }

    // End flip animation
    if (is_player) apply_player_boost();

    // Translate position based on velocity vectors
    car->pos.x += FP_MUL(dir_x, car->speed) + car->vel.x;
    car->pos.z += FP_MUL(dir_z, car->speed) + car->vel.z;
    car->pos.y += FP_MUL(dir_y, car->speed) + car->vel.y;

    // Decay external impact velocities
    car->vel.x = (car->vel.x * 240) >> 8;
    car->vel.z = (car->vel.z * 240) >> 8;

    // Gravity
    if (!car->is_on_ground) {
        car->vel.y -= GRAVITY;
    }

    /* Resolve the same rounded cross-section that is drawn in the arena. */
    int wall, angle;
    Vector3 normal;
    int attached=car->is_on_ground;
    int contact=stadium_surface_contact(&car->pos,0,attached?2*FP_SCALE:0,
        STADIUM_WIDTH,STADIUM_LENGTH,GOAL_HALF_WIDTH,GOAL_HEIGHT,&wall,&angle,&normal);
    fixed inward=FP_MUL(car->vel.x,normal.x)+FP_MUL(car->vel.y,normal.y)+FP_MUL(car->vel.z,normal.z);
    if(contact && !car->detach_timer && inward<=FP_SCALE) {
        if(!attached || car->flip_timer>0) retain_landing_rotation(car,wall,angle);
        else if(car->settle_delay>0) car->settle_delay--;
        else {
            car->visual_pitch=settle_angle(car->visual_pitch,&car->settle_pitch_velocity);
            car->visual_roll=settle_angle(car->visual_roll,&car->settle_roll_velocity);
        }
        car->is_on_ground=1;
        car->can_double_jump=1;
        if(car->surface_wall && wall && car->surface_wall!=wall &&
           car->surface_angle>32 && angle>32) {
            int oldsign=car->surface_wall>0?1:-1, newsign=wall>0?1:-1;
            int oldx=abs(car->surface_wall)==1, newx=abs(wall)==1;
            fixed along=oldx?forward.z*oldsign:-forward.x*oldsign;
            Vector3 turned={newx?0:-newsign*along,forward.y,newx?newsign*along:0};
            int best=-2147483647;
            for(int yaw=0;yaw<256;yaw++) {
                Vector3 candidate=stadium_surface_vector((Vector3){custom_sin_fp[yaw],0,custom_cos_fp[yaw]},wall,angle);
                int dot=candidate.x*turned.x+candidate.y*turned.y+candidate.z*turned.z;
                if(dot>best){best=dot;car->yaw=yaw;}
            }
        }
        car->surface_wall=wall;car->surface_angle=angle;
        /* Grip cancels the normal force. A little tangential gravity lets an
           idle car slide down, while normal throttle can still climb. */
        if(wall) car->vel.y-=GRAVITY/4;
        inward=FP_MUL(car->vel.x,normal.x)+FP_MUL(car->vel.y,normal.y)+FP_MUL(car->vel.z,normal.z);
        car->vel.x-=FP_MUL(normal.x,inward);
        car->vel.y-=FP_MUL(normal.y,inward);
        car->vel.z-=FP_MUL(normal.z,inward);
        car->vel.y=car->vel.y*240/256;
    } else {
        car->is_on_ground=0;
        if(contact && inward<0) {
            car->vel.x-=FP_MUL(normal.x,inward);
            car->vel.y-=FP_MUL(normal.y,inward);
            car->vel.z-=FP_MUL(normal.z,inward);
        }
    }

    // Ceiling bounds check
    if (car->pos.y >= STADIUM_HEIGHT) {
        car->pos.y = STADIUM_HEIGHT;
        if (car->vel.y > 0) car->vel.y = 0; // Stop upward movement at ceiling
    }

    // Boost regeneration
    if (car->is_on_ground && car->boost < 100 * FP_SCALE) {
        car->boost += 25; // Slow charge on ground
    }

    if(abs(car->pos.z)>STADIUM_LENGTH+35*FP_SCALE) {
        car->pos.z=(car->pos.z<0?-1:1)*(STADIUM_LENGTH+35*FP_SCALE);
        car->vel.z=0;
    }

}

void update_ball_physics(void) {
    fixed radius=is_hockey_match?PUCK_RADIUS:BALL_RADIUS;
    fixed half_height=is_hockey_match?PUCK_HALF_HEIGHT:BALL_RADIUS;
    // Soccer falls more decisively; retain the low, sliding hockey motion.
    if (ball.pos.y > half_height) {
        ball.vel.y -= is_hockey_match ? GRAVITY/2 : GRAVITY*3/4;
    }

    // Position integration
    ball.pos.x += ball.vel.x;
    ball.pos.y += ball.vel.y;
    ball.pos.z += ball.vel.z;

    // Drag
    ball.vel.x = (ball.vel.x * (is_hockey_match?255:253)) / 256;
    ball.vel.y = (ball.vel.y * 254) / 256;
    ball.vel.z = (ball.vel.z * (is_hockey_match?255:253)) / 256;

    // 1. Floor collision
    if (ball.pos.y <= half_height) {
        if(ball.vel.y < -FP_SCALE)audio_impact(-ball.vel.y);
        ball.pos.y = half_height;
        if(ball.vel.y<0) {
            fixed impact=-ball.vel.y;
            /* Soft contacts settle; firm shots retain their familiar rebound. */
            if(impact<FP_SCALE)ball.vel.y=0;
            else {
                ball.vel.y=impact*(is_hockey_match?18:65)/100;
                if(ball.vel.y<(is_hockey_match?FP_SCALE:FP_SCALE/2))ball.vel.y=0;
            }
        }
        ball.vel.x = is_hockey_match?ball.vel.x:(ball.vel.x * 253) / 256; // Ice keeps its glide
        ball.vel.z = is_hockey_match?ball.vel.z:(ball.vel.z * 253) / 256;
    }

    // 2. Ceiling collision
    if (ball.pos.y >= STADIUM_HEIGHT - half_height) {
        ball.pos.y = STADIUM_HEIGHT - half_height;
        if (ball.vel.y > 0) ball.vel.y = -ball.vel.y * 65 / 100; // Bounce downward off ceiling
    }

    /* The ball rolls/bounces off the same curved boundary as the cars. */
    int wall,angle;
    Vector3 normal;
    Vector3 contact_pos=ball.pos;
    contact_pos.y+=radius-half_height;
    int touched=stadium_surface_contact(&contact_pos,radius,0,STADIUM_WIDTH,STADIUM_LENGTH,
        GOAL_HALF_WIDTH,GOAL_HEIGHT+radius-half_height,&wall,&angle,&normal);
    ball.pos=contact_pos;ball.pos.y-=radius-half_height;
    if(touched && angle) {
        fixed impact=FP_MUL(ball.vel.x,normal.x)+FP_MUL(ball.vel.y,normal.y)+FP_MUL(ball.vel.z,normal.z);
        if(impact<0) {
            if(impact < -FP_SCALE)audio_impact(-impact);
            /* Remove inward motion on gentle ramp/wall contacts without
               adding a tiny rebound every simulation tick. */
            impact=impact*(-impact<FP_SCALE?100:170)/100;
            ball.vel.x-=FP_MUL(normal.x,impact);
            ball.vel.y-=FP_MUL(normal.y,impact);
            ball.vel.z-=FP_MUL(normal.z,impact);
        }
    }

    // 4. Back walls / Goal line collision
    if (abs(ball.pos.z) >= STADIUM_LENGTH - radius) {
        // Goal zones check
        if (abs(ball.pos.x) < GOAL_HALF_WIDTH - radius && ball.pos.y < GOAL_HEIGHT - half_height) {
            if (game_state == STATE_TUTORIAL &&
                tutorial_stages[current_tutorial_stage].objective == TUTORIAL_AIM_SHOT &&
                ball.pos.z > STADIUM_LENGTH) {
                /* Record the exact goal-line crossing before a fast ball can
                   reach the tutorial back-wall bounce in the same frame. */
                tutorial_progress.goal_scored = 1;
            }
            // Goal cage boundaries
            if (abs(ball.pos.z) >= STADIUM_LENGTH + (32 * FP_SCALE)) {
                if (game_state == STATE_TUTORIAL) {
                    /* Tutorial drills own their target arches; don't let a
                       missed practice shot switch into match scoring. */
                    ball.pos.z = (ball.pos.z < 0)
                        ? -STADIUM_LENGTH + BALL_RADIUS
                        : STADIUM_LENGTH - radius;
                    ball.vel.z = -ball.vel.z * 70 / 100;
                } else if (game_state == STATE_TRAINING) {
                    game_state = STATE_TRAINING_GOAL;
                    state_timer = 120;
                    screen_shake = 40;
                    spawn_explosion(ball.pos, 129);
                    scoring_team=3;
                    goal_effect_pos=ball.pos;goal_effect_style=garage_goal[0];
                    if(goal_effect_style==0)spawn_goal_celebration(ball.pos,3);
                } else if (game_state != STATE_GOAL && game_state != STATE_TRAINING_GOAL) {
                    // Goal triggered!
                    scoring_team = (ball.pos.z > 0) ? 3 : 6; // Blue (Player) or Orange (AI)
                    game_state = STATE_GOAL;
                    state_timer = 120; // 2 seconds replay
                    screen_shake = 40; // Explode shake
                    spawn_explosion(ball.pos, (scoring_team == 3) ? 129 : 131);
                    goal_effect_pos=ball.pos;
                    goal_effect_style=garage_goal[scoring_team==3?0:1];
                    if(goal_effect_style==0) spawn_goal_celebration(ball.pos, scoring_team);
                }
            }
        } /* Other end-wall contacts were resolved by the curved boundary. */
    }
}

/* --- Sphere-to-Sphere Car-Ball Collision --- */
/* Exact integer root for contact normals and speed limits. The rendering
   square-root LUT deliberately loses precision and can amplify impulses. */
static int collision_sqrt(unsigned int value) {
    unsigned int root=0,bit=1u<<30;
    while(bit>value) bit>>=2;
    while(bit) {
        if(value>=root+bit) {value-=root+bit;root=(root>>1)+bit;}
        else root>>=1;
        bit>>=2;
    }
    return (int)root;
}

/* Called only after the small contact-volume check, so full precision fits.
   Round length upward: a normal must never be longer than one unit. */
static Vector3 contact_normal(fixed dx,fixed dy,fixed dz) {
    unsigned int squared=dx*dx+dy*dy+dz*dz;
    int length=collision_sqrt(squared);
    if(!length)return (Vector3){0,0,0};
    if((unsigned int)(length*length)<squared)length++;
    return (Vector3){dx*256/length,dy*256/length,dz*256/length};
}

int check_car_ball_collision(Car *car) {
    fixed dx = ball.pos.x - car->pos.x;
    int flat_hit=is_hockey_match && ball.pos.y<=PUCK_HALF_HEIGHT+FP_SCALE && car->pos.y<CAR_RADIUS;
    fixed dy = flat_hit?0:ball.pos.y-car->pos.y;
    fixed dz = ball.pos.z - car->pos.z;

    // Shift to avoid overflow on standard GBA 32-bit registers
    fixed dx_s = dx / 16;
    fixed dy_s = dy / 16;
    fixed dz_s = dz / 16;

    int32_t dist_sq_s = (dx_s * dx_s + dy_s * dy_s + dz_s * dz_s);
    int32_t min_coll = CAR_RADIUS + (is_hockey_match?PUCK_RADIUS:BALL_RADIUS);
    int32_t min_coll_sq_s = (min_coll >> 4) * (min_coll >> 4);

    if (dist_sq_s < min_coll_sq_s) {
        /* Symmetric division avoids stronger hits in negative directions. */
        Vector3 normal=contact_normal(dx,dy,dz);
        fixed nx=normal.x,ny=normal.y,nz=normal.z;

        /* Coincident centers need a defined contact normal as well. */
        if (!dx && !dy && !dz) {
            nx=custom_sin_fp[car->yaw&255];ny=0;nz=custom_cos_fp[car->yaw&255];
        }

        // Push ball out of car penetration volume
        ball.pos.x = car->pos.x + nx*min_coll/256;
        ball.pos.y = car->pos.y + ny*min_coll/256;
        ball.pos.z = car->pos.z + nz*min_coll/256;

        /* A car above the ball must not push its centre through the floor. */
        fixed half_height=is_hockey_match?PUCK_HALF_HEIGHT:BALL_RADIUS;
        if(ball.pos.y<half_height)ball.pos.y=half_height;
        if(ball.pos.y>STADIUM_HEIGHT-half_height)ball.pos.y=STADIUM_HEIGHT-half_height;

        // Convert car driving speed & velocity to world space vector
        Vector3 direction=stadium_surface_vector((Vector3){custom_sin_fp[car->yaw&255],0,
            custom_cos_fp[car->yaw&255]},car->surface_wall,car->surface_angle);
        fixed dir_x=direction.x,dir_z=direction.z;
        fixed car_wx = FP_MUL(dir_x, car->speed) + car->vel.x;
        fixed car_wy = FP_MUL(direction.y,car->speed)+car->vel.y;
        fixed car_wz = FP_MUL(dir_z, car->speed) + car->vel.z;

        // Relative velocity
        fixed rvx = ball.vel.x - car_wx;
        fixed rvy = ball.vel.y - car_wy;
        fixed rvz = ball.vel.z - car_wz;

        // Dot product along normal
        fixed vel_along_norm = (rvx * nx + rvy * ny + rvz * nz) >> 8;

        if (vel_along_norm < 0) {
            audio_impact(-vel_along_norm);
            /* One impulse, with car:ball mass ratio 8:1 and restitution
               0.35. Tangential velocity is untouched, so grazing contacts
               redirect the ball without inventing extra forward energy. */
            fixed impulse = (-vel_along_norm * 6) / 5;
            ball.vel.x += impulse*nx/256;
            ball.vel.y += impulse*ny/256;
            ball.vel.z += impulse*nz/256;

            /* Equal/opposite reaction scaled by the heavier car's mass.
               No fixed kickback or speed penalty on a feather-light touch. */
            fixed reaction=impulse/8;
            car->vel.x -= reaction*nx/256;
            car->vel.y -= reaction*ny/256;
            car->vel.z -= reaction*nz/256;

            /* Cap total speed, not individual axes; diagonal hits must not
               gain a sqrt(3) advantage. Apply after the complete impulse. */
            int vx=ball.vel.x/16,vy=ball.vel.y/16,vz=ball.vel.z/16;
            int speed=collision_sqrt(vx*vx+vy*vy+vz*vz)+2;
            const int limit=22*FP_SCALE/16;
            if(speed>limit) {
                ball.vel.x=ball.vel.x*limit/speed;
                ball.vel.y=ball.vel.y*limit/speed;
                ball.vel.z=ball.vel.z*limit/speed;
            }
        }
        if(flat_hit){ball.pos.y=PUCK_HALF_HEIGHT;ball.vel.y=0;}
        return 1; // Touched
    }
    return 0; // No touch
}

/* --- Car-to-Car Collision --- */
void check_car_car_collision(Car *c1, Car *c2) {
    fixed dx = c2->pos.x - c1->pos.x;
    fixed dy = c2->pos.y - c1->pos.y;
    fixed dz = c2->pos.z - c1->pos.z;

    fixed dx_s = dx / 16;
    fixed dy_s = dy / 16;
    fixed dz_s = dz / 16;

    int32_t dist_sq_s = (dx_s * dx_s + dy_s * dy_s + dz_s * dz_s);
    int32_t min_coll   = 26 * FP_SCALE;
    int32_t min_coll_sq_s = (min_coll >> 4) * (min_coll >> 4);

    if (dist_sq_s < min_coll_sq_s) {
        int32_t dist_s = collision_sqrt(dist_sq_s);
        if (dist_s == 0) dist_s = 1;

        Vector3 normal=contact_normal(dx,dy,dz);
        fixed nx=normal.x,ny=normal.y,nz=normal.z;

        /* Coincident centers otherwise produce a zero normal and stay stuck. */
        if(!dx && !dy && !dz){nx=256;ny=0;nz=0;}

        fixed overlap = min_coll - (dist_s << 4);

        /* Positional separation: capped per frame so deep overlaps
           don't cause an explosive pop — cars slide apart gently.   */
        fixed max_sep = 2 * FP_SCALE;
        fixed sep = (overlap < max_sep) ? overlap : max_sep;
        fixed half_sep = sep >> 1;
        c1->pos.x -= FP_MUL(nx, half_sep);
        c1->pos.y -= FP_MUL(ny, half_sep);
        c1->pos.z -= FP_MUL(nz, half_sep);
        c2->pos.x += FP_MUL(nx, half_sep);
        c2->pos.y += FP_MUL(ny, half_sep);
        c2->pos.z += FP_MUL(nz, half_sep);

        /* World velocity vectors */
        Vector3 c1_dir=stadium_surface_vector((Vector3){custom_sin_fp[c1->yaw&255],0,
            custom_cos_fp[c1->yaw&255]},c1->surface_wall,c1->surface_angle);
        fixed c1_dir_x=c1_dir.x,c1_dir_z=c1_dir.z;
        fixed c1_wy=FP_MUL(c1_dir.y,c1->speed)+c1->vel.y;
        fixed c1_wx = FP_MUL(c1_dir_x, c1->speed) + c1->vel.x;
        fixed c1_wz = FP_MUL(c1_dir_z, c1->speed) + c1->vel.z;

        Vector3 c2_dir=stadium_surface_vector((Vector3){custom_sin_fp[c2->yaw&255],0,
            custom_cos_fp[c2->yaw&255]},c2->surface_wall,c2->surface_angle);
        fixed c2_dir_x=c2_dir.x,c2_dir_z=c2_dir.z;
        fixed c2_wy=FP_MUL(c2_dir.y,c2->speed)+c2->vel.y;
        fixed c2_wx = FP_MUL(c2_dir_x, c2->speed) + c2->vel.x;
        fixed c2_wz = FP_MUL(c2_dir_z, c2->speed) + c2->vel.z;

        /* Relative velocity along collision normal */
        fixed rvx = c2_wx - c1_wx;
        fixed rvy = c2_wy - c1_wy;
        fixed rvz = c2_wz - c1_wz;
        fixed vel_along_norm = (rvx * nx + rvy * ny + rvz * nz) >> 8;

        if (vel_along_norm < 0) {
            /* Equal masses, restitution 0.35: one balanced normal impulse.
               Preserve tangential motion and avoid order-dependent ram bonuses. */
            fixed impulse=(-vel_along_norm*135)/200;
            fixed imp_x=impulse*nx/256;
            fixed imp_y=impulse*ny/256;
            fixed imp_z=impulse*nz/256;
            c1->vel.x-=imp_x;c1->vel.y-=imp_y;c1->vel.z-=imp_z;
            c2->vel.x+=imp_x;c2->vel.y+=imp_y;c2->vel.z+=imp_z;
            audio_impact(-vel_along_norm);

        }
    }
}


/* --- Fast Atan2 Approximation --- */
// Returns angle 0-255 based on vector y, x
int fast_atan2(int y, int x) {
    if (x == 0 && y == 0) return 0;
    int abs_y = y < 0 ? -y : y;
    int abs_x = x < 0 ? -x : x;
    int a;
    if (abs_x > abs_y) {
        a = (abs_y * 32) / abs_x;
    } else {
        a = 64 - (abs_x * 32) / abs_y;
    }
    if (x < 0) {
        a = 128 - a;
    }
    if (y < 0) {
        a = -a;
    }
    return a & 255;
}

/* --- AI Steering & Behaviour --- */
void update_ai_behavior(void) {
    // Determine AI capabilities based on ai_difficulty (1 to 5)
    fixed max_ai_speed = (MAX_DRIVE_SPEED * (ai_difficulty * 2 + 4)) / 10; // Level 1: 60%, Level 3: 100%, Level 5: 140%
    int turn_speed = ai_difficulty + 1; // Level 1: 2, Level 5: 6
    int can_boost = (ai_difficulty >= 3);
    int can_jump = (ai_difficulty >= 4);

    // 1. Vector to ball
    fixed dx = ball.pos.x - opponent.pos.x;
    fixed dz = ball.pos.z - opponent.pos.z;

    if(opponent.surface_wall) {
        /* Inverse of the surface rotation: transpose applied to the target. */
        Vector3 tx=stadium_surface_vector((Vector3){256,0,0},opponent.surface_wall,opponent.surface_angle);
        Vector3 tz=stadium_surface_vector((Vector3){0,0,256},opponent.surface_wall,opponent.surface_angle);
        fixed dy=ball.pos.y-opponent.pos.y;
        fixed local_x=FP_MUL(dx,tx.x)+FP_MUL(dy,tx.y)+FP_MUL(dz,tx.z);
        dz=FP_MUL(dx,tz.x)+FP_MUL(dy,tz.y)+FP_MUL(dz,tz.z);
        dx=local_x;
    }

    // AI car heading direction (LUT-based)
    fixed opp_dir_x = custom_sin_fp[opponent.yaw & 255];
    fixed opp_dir_z = custom_cos_fp[opponent.yaw & 255];

    // Compute cross-product to determine if target is left or right of heading
    // cross = opp_dir_x * dz - opp_dir_z * dx
    fixed cross = FP_MUL(opp_dir_x, dz) - FP_MUL(opp_dir_z, dx);
    fixed dot = FP_MUL(opp_dir_x, dx) + FP_MUL(opp_dir_z, dz);

    // Turn towards ball
    if (cross > (2 * FP_SCALE)) {
        opponent.yaw = (opponent.yaw + turn_speed) & 255;
    } else if (cross < -(2 * FP_SCALE)) {
        opponent.yaw = (opponent.yaw - turn_speed) & 255;
    }

    // Driving logic
    if (dot > 0) {
        // Ball is in front, accelerate
        opponent.speed += ACCEL_RATE;
        if (opponent.speed > max_ai_speed) {
            opponent.speed = max_ai_speed;
        }
        
        if (can_boost && dot > (80 * FP_SCALE) && abs(cross) < (10 * FP_SCALE) && opponent.boost > 0) {
            opponent.speed += BOOST_ACCEL;
            fixed boost_cap = (MAX_DRIVE_SPEED * 18) / 10;
            if (opponent.speed > boost_cap) opponent.speed = boost_cap;
            opponent.boost -= 60; // Consume faster
        }
    } else {
        // Ball is behind, reverse
        opponent.speed -= ACCEL_RATE;
        if (opponent.speed < -max_ai_speed / 2) {
            opponent.speed = -max_ai_speed / 2;
        }
    }

    // AI Jump logic
    if (can_jump && opponent.is_on_ground && dot > (20 * FP_SCALE) && abs(cross) < (15 * FP_SCALE)) {
        // Jump if ball is in the air directly in front
        if (ball.pos.y > (30 * FP_SCALE) && ball.pos.y < (100 * FP_SCALE)) {
            fixed dist_sq = (dx >> 4)*(dx >> 4) + (dz >> 4)*(dz >> 4);
            if (dist_sq < (60 * 60)) {
                jump_from_surface(&opponent,(JUMP_FORCE*3)/5);
            }
        }
    }
}

static void draw_hud_text(const char *text, int x, int y, u8 color) {
    if(performance_mode!=2)draw_string(text,x+1,y+1,132);
    draw_string(text,x,y,color);
}

/* --- Fast HUD String Formatters (Zero printf/division overhead) --- */
static void fast_draw_time(int total_sec, int x, int y, u8 color) {
    int m = total_sec / 60;
    int s = total_sec % 60;
    char buf[6];
    buf[0] = '0' + (m / 10);
    buf[1] = '0' + (m % 10);
    buf[2] = ':';
    buf[3] = '0' + (s / 10);
    buf[4] = '0' + (s % 10);
    buf[5] = 0;
    draw_hud_text(buf, x, y, color);
}

static void fast_draw_team_score(const char *prefix, int score, int x, int y, u8 color) {
    char buf[8];
    buf[0] = prefix[0];
    buf[1] = prefix[1];
    buf[2] = prefix[2];
    buf[3] = ' ';
    if(score<0)score=0;
    if(score>99)score=99;
    buf[4] = score>=10 ? '0'+score/10 : ' ';
    buf[5] = '0'+score%10;
    buf[6] = 0;
    draw_hud_text(buf, x, y, color);
}

static void fast_draw_speed(int val, int x, int y, u8 color) {
    char buf[10];
    buf[0] = 'S'; buf[1] = 'P'; buf[2] = 'D'; buf[3] = ' ';
    int p = 4;
    if (val >= 100) {
        buf[p++] = '0' + (val / 100);
        val %= 100;
        buf[p++] = '0' + (val / 10);
        buf[p++] = '0' + (val % 10);
    } else if (val >= 10) {
        buf[p++] = '0' + (val / 10);
        buf[p++] = '0' + (val % 10);
    } else {
        buf[p++] = '0' + val;
    }
    buf[p] = 0;
    draw_hud_text(buf, x, y, color);
}

/* Open circular turbo dial: separated charge ticks and a lightning emblem.
   One pass keeps the small HUD inexpensive on the GBA. */
static void fast_draw_boost(int pct, int x, int y) {
    char buf[4];
    int p=0;
    if(pct<0) pct=0;
    if(pct>100) pct=100;
    u8 fuel_color=pct<20?28:131;
    for(int i=0;i<25;i+=performance_mode==2?2:1) {
        int angle=(160+i*8)&255;
        u8 color=i*100<pct*25?fuel_color:149;
        int sx=custom_sin_fp[angle], sy=custom_cos_fp[angle];
        draw_line(x+sx*17/256,y-sy*17/256,
                  x+sx*21/256,y-sy*21/256,color);
    }
    /* Compact bolt above the number, with no opaque backing. */
    draw_line(x+2,y-13,x-3,y-8,fuel_color);
    draw_line(x-3,y-8,x+2,y-8,fuel_color);
    draw_line(x+2,y-8,x-2,y-4,fuel_color);
    if(pct==100) {buf[p++]='1';buf[p++]='0';buf[p++]='0';}
    else {if(pct>=10)buf[p++]='0'+pct/10;buf[p++]='0'+pct%10;}
    buf[p]=0;
    draw_hud_text(buf,x-p*4,y,pct<20?28:130);
    draw_hud_text("TURBO",x-20,y-31,131);
    /* Small feet finish the open bottom of the dial. */
    draw_line(x-7,y+17,x-3,y+17,fuel_color);
    draw_line(x+3,y+17,x+7,y+17,fuel_color);
}

static void draw_hud_box(int x, int y, int width, int height, u8 fill, u8 edge) {
    u32 colour4 = (u32)fill | ((u32)fill << 8) | ((u32)fill << 16) | ((u32)fill << 24);
    for (int row = y; row < y + height; row++) {
        fast_span_fill(&frame_buffer[row * SCREEN_WIDTH + x], colour4, width);
    }
    draw_line(x, y, x + width - 1, y, edge);
    draw_line(x, y + height - 1, x + width - 1, y + height - 1, edge);
}

static void draw_ball_indicator(void) {
    int x,y;
    u8 marker=is_hockey_match?145:131;
    int tracking_distance=is_hockey_match?120:200;
    if (!world_target_indicator(ball.pos,&x,&y)) {
        int dx=(ball.pos.x-player.pos.x)/256,dz=(ball.pos.z-player.pos.z)/256;
        if(dx*dx+dz*dz>tracking_distance*tracking_distance && project_vertex_world(ball.pos,&x,&y) &&
           x>12 && x<228 && y>28 && y<132) {
            /* Four small corner marks keep a distant ball easy to follow. */
            for(int side=-1;side<=1;side+=2)for(int up=-1;up<=1;up+=2) {
                draw_line(x+side*8,y+up*8,x+side*5,y+up*8,marker);
                draw_line(x+side*8,y+up*8,x+side*8,y+up*5,marker);
            }
        }
        return;
    }
    int dx=x-120,dy=y-72;
    int length=abs(dx)>abs(dy)?abs(dx):abs(dy);
    if(!length) return;
    dx=dx*5/length;dy=dy*5/length;
    draw_line(x,y,x-dx-dy,y-dy+dx,is_hockey_match?145:130);
    draw_line(x,y,x-dx+dy,y-dy-dx,is_hockey_match?145:130);
    int label_x=x-16;
    if(label_x<4)label_x=4;
    if(label_x>204)label_x=204;
    draw_hud_text(is_hockey_match?"PUCK":"BALL",label_x,y>90?y-12:y+8,is_hockey_match?145:130);
}

static void draw_match_hud(void) {
    int boost_pct = FP_TO_INT(player.boost);
    if (boost_pct < 0) boost_pct = 0;
    if (boost_pct > 100) boost_pct = 100;

    draw_ball_indicator();
    fast_draw_team_score("FPS",measured_fps,4,20,130);
    /* Transparent text with a one-pixel shadow keeps the arena visible. */
    fast_draw_team_score("BLU", score_blue, 40, 4, 130);
    fast_draw_time(match_timer / 60, 100, 4, match_timer<=10*60?28:match_timer<=30*60?131:130);
    fast_draw_team_score("ORA", score_orange, 152, 4, 130);
    draw_line(40,15,87,15,146);draw_line(152,15,199,15,131);

    if(performance_mode!=2)fast_draw_speed(FP_TO_INT(abs(player.speed) * 35), 62, 147, 130);
    fast_draw_boost(boost_pct, 212, 136);

    if(performance_mode==2) {
        if(game_state!=STATE_REPLAY) {
            if(car_needs_recovery(&player))
                draw_hud_text(control_scheme?"SEL:RECOVER":"R: RECOVER",4,138,131);
            else if(!player.is_on_ground)
                draw_hud_text(player.can_double_jump?"A: FLIP":control_scheme?"AIR CONTROL":"R: AIR",4,138,130);
        }
        draw_hud_text(game_state==STATE_REPLAY?"REPLAY":cam_mode?(is_hockey_match?"CAM:PUCK":"CAM:BALL"):"CAM:CHASE",4,150,130);
        return;
    }
    if (game_state == STATE_REPLAY) {
        draw_hud_text("REPLAY", 62, 134, 131);
    } else if (cam_mode == 0) {
        draw_hud_text("L: CHASE", 62, 134, 130);
    } else {
        draw_hud_text(is_hockey_match?"L: PUCK":"L: BALL", 62, 134, 131);
    }
}

static void draw_goal_celebration_panel(void) {
    u8 team_colour = scoring_team == 3 ? 129 : 131;
    draw_hud_text(scoring_team == 3 ? "BLUE SCORED!" : "ORANGE SCORED!",
                scoring_team == 3 ? 72 : 60, 26, team_colour);
    if ((state_timer / 12) & 1) draw_hud_text("INSTANT REPLAY", 64, 38, 130);
}

/* --- Main Application Frame logic ----------------------------------------- */
/* Opaque grey menu buttons over the supplied background art. */
static int menu_text_width(const char *text) {
    int width = 0;
    while (*text++) width += 8;
    return width;
}

static void draw_menu_text_box(const char *text, int y, u8 color, int box_width) {
    int width = menu_text_width(text);
    int x = (SCREEN_WIDTH - box_width) / 2;
    int text_x = x + (box_width - width) / 2;
    u8 contour = (color == 131) ? 131 : 10;
    u8 fill = (color == 131) ? 8 : 5;
    u32 solid = (u32)fill * 0x01010101u;
    /* Opaque grey buttons keep the cover artwork out of the letter shapes. */
    for (int row = y - 3; row <= y + 11; row++) {
        if (row < 0 || row >= SCREEN_HEIGHT) continue;
        int left = x < 0 ? 0 : x;
        int right = x + box_width;
        if (right > SCREEN_WIDTH) right = SCREEN_WIDTH;
        if (left < right) fast_span_fill(&frame_buffer[row * SCREEN_WIDTH + left], solid, right-left);
    }
    draw_line(x, y - 3, x + box_width - 1, y - 3, contour);
    draw_line(x, y + 11, x + box_width - 1, y + 11, contour);
    draw_line(x, y - 3, x, y + 11, contour);
    draw_line(x + box_width - 1, y - 3, x + box_width - 1, y + 11, contour);
    draw_string(text, text_x + 1, y + 1, 132);
    draw_string(text, text_x, y, color);
}

static void draw_centered_menu_text(const char *text, int y, u8 color) {
    draw_menu_text_box(text, y, color, menu_text_width(text) + 8);
}

static void draw_centered_text_line(const char *text, int y, u8 color) {
    draw_string(text, (SCREEN_WIDTH - menu_text_width(text)) / 2, y, color);
}

/* Briefing card — shown during STATE_TUTORIAL_BRIEFING.
   The world is frozen; controls are disabled until A is pressed.         */
static void draw_tutorial_briefing_card(const TutorialStage *stage) {
    draw_hud_text("LEARN TO PLAY", 68, 42, 131);
    draw_hud_text("6 SHORT HANDS-ON LESSONS", 28, 58, 130);
    draw_hud_text(tutorial_briefing_objective(stage), 28, 76, 130);
    draw_hud_text(tutorial_control_hint(stage), 28, 90, 129);
    draw_hud_text("A: BEGIN   SELECT: RETRY", 28, 108, 130);
    draw_hud_text("START: EXIT", 76, 122, 130);
}

/* ── HUD directional arrow pointing toward the active tutorial target ───── *
 * Draws a simple screen-space arrow at the edge of the HUD area.           *
 * When the target is already close, shows a pulsing "nearby" indicator.    */
static void draw_tutorial_arrow(void) {
    Vector3 target = tutorial_active_target();

    /* Vector from player to target (integer units) */
    int dx = (target.x - player.pos.x) >> FP_SHIFT;
    int dz = (target.z - player.pos.z) >> FP_SHIFT;

    /* Approximate distance without sqrt */
    int adx = dx < 0 ? -dx : dx;
    int adz = dz < 0 ? -dz : dz;
    int approx_dist = (adx > adz) ? adx + adz / 2 : adz + adx / 2;

    /* Distance label, centred at bottom of HUD */
    char dist_str[20];
    snprintf(dist_str, sizeof(dist_str), "TARGET %dm", approx_dist);
    draw_hud_text(dist_str, (SCREEN_WIDTH - menu_text_width(dist_str)) / 2, 143, 130);

    if (approx_dist < 55) {
        /* NEARBY pulse */
        if ((tutorial_marker_pulse >> 5) & 1) {
            u8 nc = tutorial_stage_colors[current_tutorial_stage];
            draw_hud_text("IN RANGE", 168, 132, nc);
        }
        return;
    }

    /* --- Arrow math (no trig needed: use camera-relative dx/dz) ---------- */
    /* The camera faces the same direction as the player yaw.
       Project world dx/dz into camera-space left/up components.            */
    fixed cam_cos = (camera_cos_q8(camera_yaw)>>4); /* camera forward Z component */
    fixed cam_sin = (camera_sin_q8(camera_yaw)>>4); /* camera forward X component */

    /* Camera-space right = (cos, -sin), forward = (-sin, -cos)
       We want screen-X = right dot (dx,dz), screen-Y = -forward dot (dx,dz) */
    int scr_x = (int)((((fixed)dx * cam_cos - (fixed)dz * cam_sin)) >> FP_SHIFT);
    int scr_y = (int)(((-(fixed)dx * cam_sin - (fixed)dz * cam_cos)) >> FP_SHIFT);

    /* Normalise to a fixed radius circle so arrow sits at fixed position */
    int len = (adx > adz) ? adx + adz / 2 : adz + adx / 2;
    if (len == 0) return;
    int radius = 40; /* arrow orbit radius in screen pixels */
    int ax = 120 + (scr_x * radius) / len;
    int ay =  80 + (scr_y * radius) / len;

    /* Clamp to a safe HUD ring */
    if (ax < 12) ax = 12;
    if (ax > 227) ax = 227;
    if (ay < 12) ay = 12;
    if (ay > 148) ay = 148;

    u8 col = tutorial_stage_colors[current_tutorial_stage];
    /* Arrow head: small chevron pointing toward (ax,ay) from screen centre */
    int hx = (scr_x * 6) / len;
    int hy = (scr_y * 6) / len;
    /* Two "wings" perpendicular to the arrow direction */
    int wx = (-hy * 4) / 6; /* perpendicular scaled */
    int wy = ( hx * 4) / 6;
    draw_line(ax, ay, ax - hx + wx, ay - hy + wy, col);
    draw_line(ax, ay, ax - hx - wx, ay - hy - wy, col);
    draw_line(ax - hx + wx, ay - hy + wy, ax - hx - wx, ay - hy - wy, col);
    /* Stem */
    draw_line(ax - hx, ay - hy, ax - hx * 3, ay - hy * 3, col);
}

/* Six-segment progress bar along the bottom strip */
static void draw_tutorial_progress_bar(void) {
    int bar_y = 153;
    int seg_w = 32;
    int gap   = 3;
    int total = NUM_TUTORIAL_STAGES * (seg_w + gap) - gap; /* total bar width */
    int x0    = (SCREEN_WIDTH - total) / 2;

    for (int i = 0; i < NUM_TUTORIAL_STAGES; i++) {
        int sx = x0 + i * (seg_w + gap);
        u8 col;
        if (i < current_tutorial_stage) {
            col = tutorial_stage_colors[i];          /* completed: stage colour */
        } else if (i == current_tutorial_stage) {
            /* Current segment pulses */
            col = ((tutorial_marker_pulse >> 4) & 1) ?
                  tutorial_stage_colors[i] : 130;
        } else {
            col = 128;                               /* future: dark grey */
        }
        draw_line(sx, bar_y, sx + seg_w - 1, bar_y, col);
        draw_line(sx, bar_y + 1, sx + seg_w - 1, bar_y + 1, col);
    }
}

/* 45-frame "STAGE COMPLETE!" flash overlay */
static void draw_tutorial_stage_flash(void) {
    draw_hud_text("LESSON COMPLETE!", 60, 68, 131);
    draw_hud_text("NEXT LESSON...", 68, 82, 130);
}

static void draw_fixed_width_menu_item(const char *text, int y, int selected) {
    draw_menu_text_box(text, y, selected ? 131 : 130, 136);
}

static void draw_centered_menu_item(const char *label, int y, int selected) {
    draw_menu_text_box(label, y, selected ? 131 : 130, 176);
}

/* Dedicated showroom uses the same camera, lighting and textured mesh pipeline
 * as matches. Configuration for both teams remains available until power-off. */
static void draw_garage(void) {
    static unsigned int spin = 96;
    u8 accent = team_paints[garage_side][garage_paint[garage_side]] * 16 + 12;
    clear_screen(2);
    for (int y = 36; y < 108; ++y)
        memset(frame_buffer + y * 240, y < 82 ? 3 : 5, 240);
    for (int x = 0; x <= 240; x += 30)
        draw_line(120 + (x - 120) / 3, 82, x, 107, 8);
    draw_line(0, 94, 239, 94, 8);
    draw_line(0, 107, 239, 107, accent);
    /* Elliptical display plinth and contact shadow. */
    for (int y = -7; y <= 7; ++y) {
        int half = 56 * (49 - y*y) / 49;
        draw_line(120-half, 94+y, 120+half, 94+y, y == 7 ? accent : 9);
    }
    for (int y = -3; y <= 3; ++y)
        draw_line(94+y*y, 94+y, 146-y*y, 94+y, 2);
    Vector3 eye = { 0, 29 * FP_SCALE, -68 * FP_SCALE };
    Vector3 focus = { 0, 9 * FP_SCALE, 0 };
    Vector3 origin = { 0, 0, 0 };
    set_camera_lookat(eye, focus, 0);
    draw_model_world(car_models[garage_model[garage_side]], origin,
                     ((spin+=animation_ticks) / 3) & 255, 0, 0, FP_ONE,
                     team_paints[garage_side][garage_paint[garage_side]], RENDER_TEXTURED);
    if(garage_row==2) draw_goal_effect(120,72,(spin%120)*96/120,
        garage_goal[garage_side],garage_side?131:146,23);
    draw_centered_text_line("GARAGE", 5, 130);
    draw_centered_text_line(garage_side ? "L/R: ORANGE SIDE" : "L/R: BLUE SIDE", 21, accent);
    char label[30];
    snprintf(label, sizeof(label), "%sBODY: %s", garage_row == 0 ? "- " : "  ",
             car_models[garage_model[garage_side]]->name);
    draw_hud_box(36, 109, 168, 12, garage_row == 0 ? 8 : 5, garage_row == 0 ? 131 : 10);
    draw_centered_text_line(label, 111, garage_row == 0 ? 131 : 130);
    snprintf(label, sizeof(label), "%sPAINT: %s", garage_row == 1 ? "- " : "  ",
             team_paint_names[garage_side][garage_paint[garage_side]]);
    draw_hud_box(36, 122, 168, 12, garage_row == 1 ? 8 : 5, garage_row == 1 ? 131 : 10);
    draw_centered_text_line(label, 124, garage_row == 1 ? 131 : 130);
    snprintf(label,sizeof(label),"%sGOAL: %s",garage_row==2?"- ":"  ",goal_effect_names[garage_goal[garage_side]]);
    draw_hud_box(36,135,168,12,garage_row==2?8:5,garage_row==2?131:10);
    draw_centered_text_line(label,137,garage_row==2?131:130);
    draw_centered_text_line("ARROWS:EDIT  A/B:DONE",151,130);
}

static void draw_achievements(int selection) {
    /* Small code-native badges: ball, cup, puck, wall, graduation cap, flag.
       Rank pips distinguish milestones within each family. */
    static const unsigned short icons[6][12]={
        {0x0f0,0x318,0x60c,0x466,0x8f1,0x8f1,0x8f1,0x466,0x60c,0x318,0x0f0,0},
        {0x3fc,0x7fe,0x642,0x642,0x3fc,0x1f8,0x0f0,0x060,0x060,0x1f8,0x3fc,0},
        {0,0,0x1f8,0x606,0x801,0xfff,0x801,0x801,0x606,0x1f8,0,0},
        {0xfff,0x108,0x108,0xfff,0x421,0x421,0xfff,0x108,0x108,0xfff,0,0},
        {0,0x060,0x1f8,0x7fe,0xfff,0x7fe,0x3fd,0x1f9,0x109,0x1f9,0x003,0},
        {0x7fe,0x492,0x6da,0x492,0x7fe,0x400,0x400,0x400,0x400,0x400,0x400,0}
    };
    static const unsigned char family[ACH_COUNT]={0,0,1,2,3,4,0,0,0,1,1,1,2,2,3,3,3,5,5,5,0,2,1,1};
    static const unsigned char rank[ACH_COUNT]={1,2,1,1,1,1,3,4,5,2,3,4,2,3,2,3,4,1,2,3,6,6,5,6};
    /* Display order only: saved achievement IDs remain stable. */
    static const unsigned char order[ACH_COUNT]={
        ACH_GOAL,ACH_TEN_GOALS,ACH_GOALS_25,ACH_GOALS_50,ACH_GOALS_100,ACH_SOCCER_WIN,
        ACH_WIN,ACH_WINS_5,ACH_WINS_10,ACH_WINS_25,ACH_LINK_WIN,ACH_SHUTOUT,
        ACH_HOCKEY,ACH_HOCKEY_10,ACH_HOCKEY_25,ACH_MATCH,ACH_MATCHES_10,ACH_MATCHES_50,
        ACH_HOCKEY_WIN,ACH_WALL,ACH_WALL_15,ACH_WALL_30,ACH_WALL_60,ACH_TUTORIAL
    };
    clear_screen(144);
    draw_centered_text_line("ACHIEVEMENTS",3,130);
    int unlocked=0;
    for(int i=0;i<ACH_COUNT;i++)unlocked+=achievement_progress[i]>=achievement_targets[i];
    char summary[24];snprintf(summary,sizeof(summary),"%d / 24 UNLOCKED",unlocked);
    draw_centered_text_line(summary,15,146);
    for(int cell=0;cell<ACH_COUNT;cell++) {
        int i=order[cell];
        int x=7+(cell%6)*38,y=27+(cell/6)*20;
        int done=achievement_progress[i]>=achievement_targets[i];
        draw_hud_box(x,y,35,19,cell==selection?145:5,cell==selection?131:done?146:150);
        u8 color=done?131:cell==selection?130:153;
        for(int row=0;row<12;row++)for(int col=0;col<12;col++)
            if(icons[family[i]][row]&(1u<<(11-col)))
                frame_buffer[(y+2+row)*SCREEN_WIDTH+x+11+col]=color;
        for(int r=0;r<rank[i];r++)draw_line(x+3+r*5,y+16,x+4+r*5,y+16,color);
        if(done) {draw_line(x+28,y+4,x+30,y+6,146);draw_line(x+30,y+6,x+33,y+2,146);}
    }
    selection=order[selection];
    draw_centered_text_line(achievement_names[selection],109,131);
    draw_centered_text_line(achievement_descriptions[selection],122,130);
    char progress[28];
    int wall=selection==ACH_WALL || (selection>=ACH_WALL_15 && selection<=ACH_WALL_60);
    if(achievement_progress[selection]>=achievement_targets[selection])snprintf(progress,sizeof(progress),"UNLOCKED");
    else snprintf(progress,sizeof(progress),wall?"%u / %u SECONDS":"%u / %u",
        achievement_progress[selection]/(wall?60:1),achievement_targets[selection]/(wall?60:1));
    draw_centered_text_line(progress,134,146);
    draw_centered_text_line("ARROWS:SELECT  B:BACK",149,130);
}

static void draw_link_lobby(int side,int hockey,int waiting) {
        clear_screen(144);
        draw_centered_text_line("2 PLAYER LINK",14,130);
        draw_centered_text_line(side?"PLAYER 2 - ORANGE":"PLAYER 1 - BLUE",38,131);
        draw_centered_text_line(hockey?"HOCKEY":"SOCCER",66,130);
        draw_centered_text_line(waiting?"CONNECT BOTH CONSOLES":"LINK READY",90,146);
        draw_centered_text_line("HOST: LEFT/RIGHT MODE",112,130);
        draw_centered_text_line("HOST A: START  B:BACK",130,130);
        draw_centered_text_line("START+SELECT: EXIT MATCH",147,150);
}

static void draw_menu_screen(GameState state, int selection) {
    if (state == STATE_MENU_GARAGE) { draw_garage(); return; }
    if (state == STATE_MENU_ACHIEVEMENTS) { draw_achievements(selection); return; }
    if(state==STATE_MENU_LINK) {draw_link_lobby(link_player_id(),link_hockey,link_waiting);return;}
    /* Artwork backdrop for the remaining menus. */
    memcpy32(frame_buffer, coverart_data, 38400 / 4);

    if (state == STATE_TITLE) {
        draw_fixed_width_menu_item("PLAY",     82, selection == 0);
        draw_fixed_width_menu_item("SETTINGS", 98, selection == 1);
        draw_fixed_width_menu_item("GARAGE",   114, selection == 2);
        draw_fixed_width_menu_item("TUTORIAL", 130, selection == 3);
        draw_fixed_width_menu_item("ACHIEVEMENTS",146,selection==4);
    } else if (state == STATE_MENU_PLAY) {
        draw_centered_menu_text("PLAY", 38, 131);
        draw_centered_menu_item("SOCCER MATCH",  68, selection == 0);
        draw_centered_menu_item("HOCKEY MATCH",  84, selection == 1);
        draw_centered_menu_item("TRAINING",     100, selection == 2);
        draw_centered_menu_item("2 PLAYER LINK",116,selection==3);
        draw_centered_menu_item("BACK",132,selection==4);
    } else if (state == STATE_MENU_TRAINING) {
        char level[20];
        draw_centered_menu_text("TRAINING", 38, 131);
        snprintf(level, sizeof(level), "LEVEL %02d", current_training_level + 1);
        draw_centered_menu_text(level, 64, 130);
        draw_centered_menu_text(training_levels[current_training_level].hint, 82, 131);
        draw_centered_menu_text("LEFT RIGHT:LEVEL", 108, 130);
    } else if (state == STATE_MENU_SETTINGS) {
        char value[24];
        draw_centered_menu_text("SETTINGS", 28, 131);
        snprintf(value, sizeof(value), "AI LEVEL: %d", ai_difficulty);
        draw_centered_menu_item(value, 52, selection == 0);
        snprintf(value, sizeof(value), "OPPONENT: %s", enable_opponent ? "ON" : "OFF");
        draw_centered_menu_item(value, 68, selection == 1);
        snprintf(value, sizeof(value), "CTRL: %s", control_scheme ? "ALT" : "CLASSIC");
        draw_centered_menu_item(value, 84, selection == 2);
        snprintf(value,sizeof(value),"GRAPHICS: %s",performance_mode==2?"SPEED":performance_mode?"FAST":"DETAILED");
        draw_centered_menu_item(value,100,selection==3);
        snprintf(value,sizeof(value),"SOUND: %s",audio_enabled()?"ON":"OFF");
        draw_centered_menu_item(value,116,selection==4);
        draw_centered_menu_item("BACK",132,selection==5);
        draw_centered_menu_text("LEFT RIGHT:CHANGE", 150, 129);

    }
}

static void handle_player_input(void) {
    int btn_accel, btn_reverse, btn_left, btn_right, btn_jump, btn_boost, btn_drift, btn_aerial_mod;
    
    if (control_scheme == 0) { // Classic
        btn_accel = drive_down(KEY_UP);
        btn_reverse = drive_down(KEY_DOWN);
        btn_left = drive_down(KEY_LEFT);
        btn_right = drive_down(KEY_RIGHT);
        btn_jump = drive_hit(KEY_A);
        btn_boost = drive_down(KEY_B);
        btn_drift = drive_down(KEY_R);
        btn_aerial_mod = drive_down(KEY_R);
    } else { // Alternative
        btn_accel = drive_down(KEY_R);
        btn_reverse = drive_down(KEY_DOWN); // Using down for reverse
        btn_left = drive_down(KEY_LEFT);
        btn_right = drive_down(KEY_RIGHT);
        btn_jump = drive_hit(KEY_A);
        btn_boost = drive_down(KEY_B);
        btn_drift = drive_down(KEY_SELECT); // Using Select for drift since L is camera
        btn_aerial_mod = 1; // Always enabled in air
    }

    if (game_state == STATE_TUTORIAL) {
        if (btn_accel) tutorial_progress.accelerated = 1;
        if (btn_left || btn_right) tutorial_progress.steered = 1;
    }

    // Steering + Drift
    int is_drifting = btn_drift && player.is_on_ground && (player.speed > FP_SCALE || player.speed < -FP_SCALE);
    int block_yaw = player.flip_timer > 0 ||
        (!player.is_on_ground && (btn_aerial_mod || btn_jump));
    steer_car(&player,btn_right-btn_left,is_drifting,block_yaw);
    grip_car(&player,is_drifting,btn_reverse);
    assist_ground_recovery(&player,btn_drift);
    if(btn_reverse)btn_accel=0;

    if (is_drifting && (btn_left || btn_right)) {
        Vector3 right=stadium_surface_vector((Vector3){custom_cos_fp[player.yaw&255],0,
            -custom_sin_fp[player.yaw&255]},player.surface_wall,player.surface_angle);
        fixed right_x=right.x, right_z=right.z;
        fixed slide = (player.speed * 12) / 100;
        player.vel.y += FP_MUL(right.y, slide);
        player.vel.x += FP_MUL(right_x, slide);
        player.vel.z += FP_MUL(right_z, slide);
        player.speed = (player.speed * 240) >> 8;
        
        if (state_timer % 2 == 0) {
            Vector3 skid_pos1 = player.pos;
            Vector3 skid_pos2 = player.pos;
            skid_pos1.x += FP_MUL(right_x, 13 * FP_SCALE);
            skid_pos1.z += FP_MUL(right_z, 13 * FP_SCALE);
            skid_pos2.x -= FP_MUL(right_x, 13 * FP_SCALE);
            skid_pos2.z -= FP_MUL(right_z, 13 * FP_SCALE);
            spawn_skid_particle(skid_pos1);
            spawn_skid_particle(skid_pos2);
        }
    }

    // Driving
    if (player.is_on_ground) {
        int grip=(custom_cos_fp[player.visual_pitch&255]*custom_cos_fp[player.visual_roll&255])/256;
        int acceleration=grip<128?ACCEL_RATE/4:ACCEL_RATE;
        if (btn_accel) {
            player.speed += acceleration;
            if (player.speed > MAX_DRIVE_SPEED) player.speed = MAX_DRIVE_SPEED;
        } else if (btn_reverse) {
            if (player.speed > 0) {
                player.speed -= ACCEL_RATE * 2;
                if(player.speed<0)player.speed=0;
            } else {
                player.speed -= ACCEL_RATE;
            }
            if (player.speed < -MAX_DRIVE_SPEED / 2) player.speed = -MAX_DRIVE_SPEED / 2;
        }
    }

    /* Accelerate into the turn and brake smoothly toward the car heading. */
    camera_yaw=follow_chase_heading(camera_yaw,&player);

    /* Camera cycle is handled globally in the game loop via KEY_L hit */

    // Balanced diagonal aerial input while the modifier is held.
    if (btn_aerial_mod && !player.is_on_ground && player.flip_timer == 0 && !btn_jump) {
        int pitch=(drive_down(KEY_UP)?1:0)-(drive_down(KEY_DOWN)?1:0);
        int roll=(drive_down(KEY_LEFT)?1:0)-(drive_down(KEY_RIGHT)?1:0);
        apply_air_rotation(&player,pitch,roll);
    }

    // Jump / Dodge
    extend_jump(&player,drive_down(KEY_A));
    if (btn_jump) {
        if (player.is_on_ground) {
            if (game_state == STATE_TUTORIAL) tutorial_progress.jumped = 1;
            audio_play(AUDIO_JUMP);
            jump_from_surface(&player,(JUMP_FORCE*3)/5);
        } else if (player.can_double_jump) {
            audio_play(AUDIO_JUMP);
            if (game_state == STATE_TUTORIAL) tutorial_progress.double_jumped = 1;
            player.can_double_jump = 0;
            player.jump_hold=0;
            int is_forward = drive_down(KEY_UP);
            int is_back = drive_down(KEY_DOWN);
            int is_left = drive_down(KEY_LEFT);
            int is_right = drive_down(KEY_RIGHT);

            if (!is_forward && !is_back && !is_left && !is_right) {
                player.vel.y = (JUMP_FORCE * 3) / 5;
            } else {
                player.flip_base_pitch=player.visual_pitch;
                player.flip_base_roll=player.visual_roll;
                player.flip_timer = FLIP_DURATION_TICKS;
                if (is_forward) player.flip_pitch_dir = 1;
                else if (is_back) player.flip_pitch_dir = -1;
                else player.flip_pitch_dir = 0;
                
                if (is_left) player.flip_roll_dir = 1;
                else if (is_right) player.flip_roll_dir = -1;
                else player.flip_roll_dir = 0;
                
                if (player.vel.y > 0) player.vel.y = 0;
            }
            spawn_explosion(player.pos, 130);
        }
    }

    player.boost_requested = btn_boost;

}

static void update_link_car(int side) {
    Car temp;
    if(side){temp=player;player=opponent;opponent=temp;}
    int saved_camera=camera_yaw,saved_turn=chase_turn_velocity,saved_scheme=control_scheme;
    control_override=1;control_scheme=0;
    control_down=link_buttons[side];control_hits=control_down&~link_previous[side];
    handle_player_input();
    update_car_physics(&player,1);
    control_override=0;control_scheme=saved_scheme;
    if(side!=link_player_id()){camera_yaw=saved_camera;chase_turn_velocity=saved_turn;}
    if(side){temp=player;player=opponent;opponent=temp;}
}

static u16 link_local_buttons(void) {
    u16 keys=key_curr_state();
    if(control_scheme) {
        const Car *local=link_player_id()?&opponent:&player;
        keys&=~(KEY_R|KEY_L|KEY_SELECT|KEY_UP);
        if((local->is_on_ground && key_is_down(KEY_R)) || key_is_down(KEY_UP))keys|=KEY_UP;
        if(local->is_on_ground && key_is_down(KEY_SELECT))keys|=KEY_R;
        if(!local->is_on_ground)keys|=KEY_R;
    }
    return keys;
}

static void game_vblank(void) { video_vblank(); audio_vblank(); }

int main(void) {
    /* Standard cartridge timing plus instruction prefetch for ROM helpers. */
    REG_WAITCNT=(REG_WAITCNT & ~0x1c) | WS_ROM0_N3 | WS_ROM0_S1 | WS_PREFETCH;
    // Setup hardware Mode 4 and custom palettes
    achievements_init((volatile unsigned char *)0x0e000000);
    init_3d_engine();          // Also calls init_pitch_texture() for soccer
    init_dynamic_models();     // Initialize sphere and torus meshes
    init_mesh_normals();       // Precalculate face normals in model space

    irq_init(NULL);
    audio_init();
    irq_set(II_VBLANK, game_vblank, ISR_DEF);
    irq_enable(II_VBLANK);
    REG_DISPSTAT |= DSTAT_VBL_IRQ;

    // Setup Timer 0 to count at 16.384 kHz (for real-time tracking)
    REG_TM0D = 0;
    REG_TM0CNT = 0x0083; // TM_ENABLE | TM_FREQ_1024

    int achievement_toast=-1,achievement_toast_timer=0;
    int ball_cam_yaw=0;
    int menu_selection = 0;
    int camera_shake_x = 0;
    int camera_shake_y = 0;

    while (1) {
        key_poll();

        // Measure delivered FPS against the independent hardware timer.
        static u16 last_tm0 = 0;
        u16 current_tm0 = REG_TM0D;
        u16 dt_ticks = current_tm0 - last_tm0;
        last_tm0 = current_tm0;
        static u32 fps_ticks=0, fps_frames=0;
        fps_ticks+=dt_ticks;fps_frames++;
        if(fps_ticks>=16384) {
            measured_fps=(fps_frames*16384+fps_ticks/2)/fps_ticks;
            if(measured_fps>99)measured_fps=99;
            fps_ticks=0;fps_frames=0;
        }

        /* One simulation tick per hardware refresh, independent of rendering.
           Normal 30 FPS frames contain two small physics updates. */
        static u32 previous_video_tick=0;
        u32 current_video_tick=video_ticks();
        int dt_time_frames=(int)(current_video_tick-previous_video_tick);
        previous_video_tick=current_video_tick;
        if(dt_time_frames<1)dt_time_frames=1;
        if(dt_time_frames>4)dt_time_frames=4; /* bounded recovery after a stall */

        int network_step=1;
        if(link_match) {
            if(key_is_down(KEY_START) && key_is_down(KEY_SELECT)) {
                link_close();link_match=link_waiting=0;enable_opponent=link_saved_opponent;
                game_state=STATE_MENU_PLAY;menu_selection=3;
            } else {
                network_step=link_step(link_local_buttons(),dt_time_frames,link_buttons,&dt_time_frames);
                link_waiting=!network_step;
            }
        }
        int real_ticks=dt_time_frames;
        int slowed_state=game_state==STATE_PLAY || game_state==STATE_TRAINING ||
            game_state==STATE_TUTORIAL || game_state==STATE_GOAL ||
            game_state==STATE_TRAINING_GOAL || game_state==STATE_REPLAY;
        static unsigned speed_phase=0;
        int simulation_steps=real_ticks;
        if(slowed_state && network_step) {
            /* Exactly one simulation tick per two refreshes: half original speed. */
            simulation_steps=scaled_simulation_steps(real_ticks,&speed_phase);
        } else if(!slowed_state) speed_phase=0;
        if(simulation_steps>4)simulation_steps=4;
        animation_ticks=real_ticks;
        u16 first_key_previous=__key_prev,polled_keys=__key_curr;
        static u16 pending_keys=0;
        pending_keys|=__key_curr & ~__key_prev;
        if(simulation_steps) {
            __key_curr|=pending_keys;
            __key_prev=__key_curr & ~pending_keys;
            pending_keys=0;
        }
        for(int simulation_step=0;simulation_step<simulation_steps;simulation_step++) {
        /* Held controls apply every tick; presses apply only on the first. */
        if(simulation_step){__key_curr=polled_keys;__key_prev=polled_keys;}
        dt_time_frames=(simulation_step+1)*real_ticks/simulation_steps-
                       simulation_step*real_ticks/simulation_steps;
        if(network_step) {
            previous_player=player;previous_opponent=opponent;previous_ball=ball;
            previous_camera_yaw=camera_yaw;
            previous_ball_spin_y=ball_spin_y;previous_ball_spin_x=ball_spin_x;
        }
        stadium_light_phase=(stadium_light_phase+1)&255;
        // 1. STATE MACHINE UPDATES
        if(game_state<=STATE_MENU_LINK) {
            if(key_hit(KEY_A|KEY_START|KEY_B))audio_play(AUDIO_SELECT);
            else if(key_hit(KEY_UP|KEY_DOWN|KEY_LEFT|KEY_RIGHT))audio_play(AUDIO_MOVE);
        }
        int achievement_previous_state=game_state;
        if(network_step) switch (game_state) {
            case STATE_START_SCREEN:
                if (key_hit(KEY_START) || key_hit(KEY_A)) {
                    game_state = STATE_TITLE;
                    menu_selection = 0;
                }
                break;

            case STATE_TITLE:
                if (key_hit(KEY_UP)) menu_selection = (menu_selection + 4) % 5;
                if (key_hit(KEY_DOWN)) menu_selection = (menu_selection + 1) % 5;

                /* A dedicated title-screen shortcut for the launcher click
                   adapter. It does not alter normal A/Start navigation. */
                if (key_hit(KEY_SELECT)) {
                    start_tutorial_mode();
                    break;
                }
                
                if (key_hit(KEY_A) || key_hit(KEY_START)) {
                    if (menu_selection == 0) {
                        game_state = STATE_MENU_PLAY;
                        menu_selection = 0;
                    } else if (menu_selection == 1) {
                        game_state = STATE_MENU_SETTINGS;
                        menu_selection = 0;
                    } else if (menu_selection == 2) {
                        game_state = STATE_MENU_GARAGE;
                    } else if (menu_selection == 3) {
                        start_tutorial_mode();
                    } else if(menu_selection==4) {
                        game_state=STATE_MENU_ACHIEVEMENTS;menu_selection=0;
                    }
                }
                break;

            case STATE_MENU_ACHIEVEMENTS:
                if(key_hit(KEY_LEFT))menu_selection=(menu_selection/6)*6+(menu_selection%6+5)%6;
                if(key_hit(KEY_RIGHT))menu_selection=(menu_selection/6)*6+(menu_selection%6+1)%6;
                if(key_hit(KEY_UP))menu_selection=(menu_selection+18)%24;
                if(key_hit(KEY_DOWN))menu_selection=(menu_selection+6)%24;
                if(key_hit(KEY_B)||key_hit(KEY_START)){game_state=STATE_TITLE;menu_selection=4;}
                break;

            case STATE_MENU_PLAY:
                if (key_hit(KEY_UP)) menu_selection = (menu_selection + 4) % 5;
                if (key_hit(KEY_DOWN)) menu_selection = (menu_selection + 1) % 5;
                
                if (key_hit(KEY_B)) {
                    game_state = STATE_TITLE; menu_selection = 0;
                } else if (key_hit(KEY_A) || key_hit(KEY_START)) {
                    if (menu_selection == 0) {
                        is_hockey_match = 0; active_pitch_mode = 0; reset_match();
                    } else if (menu_selection == 1) {
                        is_hockey_match = 1; active_pitch_mode = 1; reset_match();
                    } else if (menu_selection == 2) {
                        game_state = STATE_MENU_TRAINING;
                    } else if (menu_selection == 3) {
                        link_open();link_waiting=1;link_hockey=0;game_state=STATE_MENU_LINK;
                    } else if(menu_selection==4) {
                        game_state = STATE_TITLE; menu_selection = 0;
                    }
                }
                break;

            case STATE_MENU_LINK: {
                if(key_hit(KEY_B)) {link_close();game_state=STATE_MENU_PLAY;menu_selection=3;break;}
                if(!link_player_id() && (key_hit(KEY_LEFT)||key_hit(KEY_RIGHT)))link_hockey^=1;
                u16 values[2];
                u16 request=link_hockey | ((!link_player_id() && key_is_down(KEY_A))?512:0);
                link_waiting=!link_exchange(request,values);
                if(!link_waiting) {
                    link_hockey=values[0]&1;
                    if(values[0]&512) {
                        is_hockey_match=link_hockey;active_pitch_mode=link_hockey;
                        link_saved_opponent=enable_opponent;
                        enable_opponent=1;link_match=1;
                        link_buttons[0]=link_buttons[1]=link_previous[0]=link_previous[1]=0;
                        srand(1);reset_match();
                        camera_yaw=link_player_id()?128*256:0;chase_turn_velocity=0;
                    }
                }
                break;
            }

            case STATE_MENU_TRAINING:
                if (key_hit(KEY_LEFT)) {
                    current_training_level--;
                    if (current_training_level < 0) current_training_level = NUM_TRAINING_LEVELS - 1;
                }
                if (key_hit(KEY_RIGHT)) {
                    current_training_level++;
                    if (current_training_level >= NUM_TRAINING_LEVELS) current_training_level = 0;
                }
                if (key_hit(KEY_B)) {
                    game_state = STATE_MENU_PLAY; menu_selection = 2;
                } else if (key_hit(KEY_A) || key_hit(KEY_START)) {
                    game_state = STATE_TRAINING_INIT;
                }
                break;

            case STATE_MENU_SETTINGS:
                if (key_hit(KEY_UP)) menu_selection = (menu_selection + 5) % 6;
                if (key_hit(KEY_DOWN)) menu_selection = (menu_selection + 1) % 6;
                
                if (key_hit(KEY_B)) {
                    game_state = STATE_TITLE; menu_selection = 1;
                }
                
                if (menu_selection == 0) {
                    if (key_hit(KEY_LEFT) && ai_difficulty > 1) ai_difficulty--;
                    if (key_hit(KEY_RIGHT) && ai_difficulty < 5) ai_difficulty++;
                } else if (menu_selection == 1) {
                    if (key_hit(KEY_LEFT) || key_hit(KEY_RIGHT) || key_hit(KEY_A)) {
                        enable_opponent = !enable_opponent;
                    }
                } else if (menu_selection == 2) {
                    if (key_hit(KEY_LEFT) || key_hit(KEY_RIGHT) || key_hit(KEY_A)) {
                        control_scheme = !control_scheme;
                    }
                } else if (menu_selection == 3) {
                    if(key_hit(KEY_LEFT)||key_hit(KEY_RIGHT)||key_hit(KEY_A)) performance_mode=(performance_mode+1)%3;
                } else if (menu_selection == 4) {
                    if(key_hit(KEY_LEFT)||key_hit(KEY_RIGHT)||key_hit(KEY_A))audio_set_enabled(!audio_enabled());
                } else if (menu_selection == 5) {
                    if (key_hit(KEY_A) || key_hit(KEY_START)) {
                        game_state = STATE_TITLE; menu_selection = 1;
                    }
                }
                break;

            case STATE_MENU_GARAGE:
                if (key_hit(KEY_L) || key_hit(KEY_R)) garage_side ^= 1;
                if (key_hit(KEY_UP)) garage_row=(garage_row+2)%3;
                if (key_hit(KEY_DOWN)) garage_row=(garage_row+1)%3;
                if (key_hit(KEY_LEFT) || key_hit(KEY_RIGHT)) {
                    int step = key_hit(KEY_RIGHT) ? 1 : 2;
                    if (garage_row == 0)
                        garage_model[garage_side] = (garage_model[garage_side] + step) % CAR_MODEL_COUNT;
                    else if(garage_row==1)
                        garage_paint[garage_side] = (garage_paint[garage_side] + step) % 3;
                    else garage_goal[garage_side]=(garage_goal[garage_side]+step)%3;
                }
                if (key_hit(KEY_B) || key_hit(KEY_A) || key_hit(KEY_START)) {
                    game_state = STATE_TITLE; menu_selection = 2;
                }
                break;
                
            case STATE_PLAY:
                // Pause on START (linked matches keep both simulations running).
                if (!link_match && key_hit(KEY_START)) {
                    game_state = STATE_PAUSED;
                    pause_selection = 0;
                    break;
                }
                // Match countdown
                if (match_timer > 0) {
                    match_timer -= dt_time_frames;
                    if (match_timer <= 0) {
                        match_timer = 0;
                        game_state = STATE_GAMEOVER;
                        state_timer = 240;
                    }
                }

                if(!link_match)handle_player_input();
                /* L button always cycles camera (both control schemes) */
                if (key_hit(KEY_L)) cam_mode = (cam_mode + 1) % 2;

                // Apply Physics Updates
                if(link_match){update_link_car(0);update_link_car(1);}
                else {
                    update_car_physics(&player, 1);
                    if (enable_opponent) update_car_physics(&opponent, 0);
                }
                update_ball_physics();
                if(!link_match)capture_replay_frame();

                // Collision interactions
                check_car_ball_collision(&player);
                if (enable_opponent) {
                    check_car_ball_collision(&opponent);
                    check_car_car_collision(&player, &opponent);
                }

                // AI Opponent steering action
                if (enable_opponent && !link_match) update_ai_behavior();

                // ── Boost Pad logic ──────────────────────────────────────────
                pad_pulse = (pad_pulse + 3) & 255;
                for (int i = 0; i < NUM_BOOST_PADS; i++) {
                    if (boost_pads[i].cooldown > 0) {
                        boost_pads[i].cooldown -= dt_time_frames;
                        if (boost_pads[i].cooldown < 0) boost_pads[i].cooldown = 0;
                        if (boost_pads[i].cooldown > 0) continue;
                    }
                    /* Check player pickup */
                    fixed pdx = (player.pos.x - boost_pads[i].pos.x) >> 4;
                    fixed pdz = (player.pos.z - boost_pads[i].pos.z) >> 4;
                    fixed prad = PAD_RADIUS >> 4;
                    if (pdx*pdx + pdz*pdz < prad*prad) {
                        player.boost += boost_pads[i].amount * FP_SCALE;
                        if (player.boost > 100 * FP_SCALE) player.boost = 100 * FP_SCALE;
                        boost_pads[i].cooldown = 600; // 10 seconds refill
                        spawn_explosion(boost_pads[i].pos, (boost_pads[i].amount == 100) ? 131 : 129); // Orange or Cyan burst
                        if (boost_pads[i].amount == 100) screen_shake = 8;
                    }
                    /* Check opponent pickup */
                    if (enable_opponent) {
                        fixed odx = (opponent.pos.x - boost_pads[i].pos.x) >> 4;
                        fixed odz = (opponent.pos.z - boost_pads[i].pos.z) >> 4;
                        if (odx*odx + odz*odz < prad*prad) {
                            opponent.boost += boost_pads[i].amount * FP_SCALE;
                            if (opponent.boost > 100 * FP_SCALE) opponent.boost = 100 * FP_SCALE;
                            boost_pads[i].cooldown = 600; // 10 seconds refill
                            spawn_explosion(boost_pads[i].pos, (boost_pads[i].amount == 100) ? 131 : 129);
                        }
                    }
                }
                break;


            case STATE_TUTORIAL_BRIEFING:
                /* World is frozen — only accept A (start) or START (exit) */
                if (key_hit(KEY_START)) {
                    game_state = STATE_TITLE;
                    break;
                }
                if (key_hit(KEY_A)) {
                    game_state = STATE_TUTORIAL;
                }
                /* Keep marker pulse ticking for the briefing card animation */
                tutorial_marker_pulse = (tutorial_marker_pulse + 3) & 255;
                break;

            case STATE_TUTORIAL:
            case STATE_TRAINING:
                if (key_hit(KEY_START)) {
                    game_state = (game_state == STATE_TRAINING)
                        ? STATE_MENU_TRAINING : STATE_TITLE;
                    break;
                }
                if(game_state==STATE_TRAINING && key_is_down(KEY_SELECT)) {
                    if(key_hit(KEY_A) || key_hit(KEY_B)) {
                        if(key_hit(KEY_B))current_training_level=(current_training_level+1)%NUM_TRAINING_LEVELS;
                        game_state=STATE_TRAINING_INIT;
                        break;
                    }
                }
                if (game_state == STATE_TUTORIAL) {
                    if (key_hit(KEY_SELECT)) { setup_tutorial_stage(); break; }
                    if (tutorial_flash_timer > 0) {
                        tutorial_flash_timer -= dt_time_frames;
                        if (tutorial_flash_timer <= 0) advance_tutorial_stage();
                        break;
                    }
                    player.boost = 100 * FP_SCALE;
                }
                /* L button always cycles camera */
                if (key_hit(KEY_L)) cam_mode = (cam_mode + 1) % 2;
                handle_player_input();

                if (game_state == STATE_TUTORIAL) {
                    tutorial_marker_pulse = (tutorial_marker_pulse + 3) & 255;
                }

                // Shared physics updates for solo modes (Tutorial & Training)
                update_car_physics(&player, 1);
                update_ball_physics();

                static int touch_cooldown = 0;
                if (check_car_ball_collision(&player)) {
                    if (game_state == STATE_TRAINING && touch_cooldown == 0) {
                        training_touches++;
                    }
                    if (game_state == STATE_TUTORIAL) tutorial_progress.ball_touched = 1;
                    touch_cooldown = 15;
                }
                if (touch_cooldown > 0) touch_cooldown--;

                if (game_state == STATE_TUTORIAL) {
                    const TutorialStage *stage = &tutorial_stages[current_tutorial_stage];
                    int completed = 0;

                    if (stage->objective == TUTORIAL_DRIVE_GATE) {
                        completed = tutorial_progress.accelerated &&
                            abs(player.speed) > 2 * FP_SCALE &&
                            tutorial_target_reached(player.pos, stage->target_pos, 48);
                    } else if (stage->objective == TUTORIAL_STEER_GATES) {
                        Vector3 gate = tutorial_active_target();
                        if (tutorial_progress.steered && tutorial_target_reached(player.pos, gate, 48)) {
                            if (current_tutorial_gate == NUM_STEERING_GATES - 1) {
                                completed = 1;
                            } else {
                                spawn_explosion(gate, tutorial_stage_colors[current_tutorial_stage]);
                                current_tutorial_gate++;
                            }
                        }
                    } else if (stage->objective == TUTORIAL_BOOST_GATE) {
                        completed = tutorial_progress.boosted && key_is_down(KEY_B) &&
                            tutorial_target_reached(player.pos, stage->target_pos, 48);
                    } else if (stage->objective == TUTORIAL_JUMP_GATE) {
                        completed = tutorial_progress.jumped && !player.is_on_ground &&
                            player.pos.y > 8 * FP_SCALE;
                    } else if (stage->objective == TUTORIAL_AERIAL_GATE) {
                        completed = tutorial_progress.double_jumped && !player.is_on_ground &&
                            player.pos.y > 18 * FP_SCALE;
                    } else if (stage->objective == TUTORIAL_AIM_SHOT) {
                        completed = tutorial_progress.ball_touched && tutorial_ball_scored();
                    }

                    if (completed) complete_tutorial_stage();
                } else if (game_state == STATE_TRAINING) {
                    if (training_touches == 0) {
                        training_timer += dt_time_frames;
                        // Fail conditions: Out of time to kick the ball
                        if (training_timer > training_levels[current_training_level].time_limit) {
                            game_state = STATE_TRAINING_INIT;
                        }
                    } else {
                        training_post_touch_timer += dt_time_frames;
                        // Fail conditions: Took more than 8 seconds after kick, or touched again
                        if (training_post_touch_timer > 480 || training_touches > 1) {
                            game_state = STATE_TRAINING_INIT;
                        }
                    }
                }
                break;

            case STATE_TUTORIAL_COMPLETE:
                if (tutorial_complete_timer > 0) tutorial_complete_timer -= dt_time_frames;
                /* Player can exit early with START, or auto-exit after timer */
                if (key_hit(KEY_START) || tutorial_complete_timer <= 0) game_state = STATE_TITLE;
                /* START → play a match: offer quick match launch */
                if (key_hit(KEY_A)) {
                    is_hockey_match = 0;
                    reset_match();
                    game_state = STATE_PLAY;
                }
                break;

            case STATE_TRAINING_INIT:
                /* A retry must also clear an unfinished dodge and old effects. */
                player.flip_timer=player.flip_pitch_dir=player.flip_roll_dir=0;
                player.flip_base_pitch=player.flip_base_roll=0;
                player.boost_requested=0;
                screen_shake=camera_shake_x=camera_shake_y=0;
                touch_cooldown=0;
                player.pos = training_levels[current_training_level].car_start_pos;
                player.yaw = training_levels[current_training_level].car_start_yaw; reset_steering(&player);
                player.vel.x = 0; player.vel.y = 0; player.vel.z = 0;
                player.speed = 0;
                player.boost = 34 * FP_SCALE;
                player.is_on_ground = 1;
                player.surface_wall = player.surface_angle = player.detach_timer = player.settle_delay = 0;
                player.jump_hold=player.settle_pitch_velocity=player.settle_roll_velocity=0;
                player.can_double_jump = 1;
                player.team = 3;
                player.visual_pitch = 0;
                player.visual_roll = 0;
                
                ball.pos = training_levels[current_training_level].ball_start_pos;
                ball.vel = training_levels[current_training_level].ball_start_vel;
                
                training_timer = 0;
                training_touches = 0;
                training_post_touch_timer = 0;
                camera_yaw = player.yaw*256;chase_turn_velocity=0;
                game_state = STATE_TRAINING;
                break;

            case STATE_TRAINING_GOAL:
                state_timer -= dt_time_frames;
                update_car_physics(&player, 1);
                update_ball_physics();
                if (state_timer <= 0) {
                    current_training_level++;
                    if (current_training_level >= NUM_TRAINING_LEVELS) {
                        current_training_level = 0;
                        game_state = STATE_MENU_TRAINING;
                    } else {
                        game_state = STATE_TRAINING_INIT;
                    }
                }
                break;

            case STATE_HOCKEY:
                /* Hockey uses STATE_PLAY; this silences -Wswitch */
                break;

            case STATE_GOAL:
                state_timer -= dt_time_frames;
                if (screen_shake > 0) {
                    screen_shake--;
                    camera_shake_x = (rand() % 6) - 3;
                    camera_shake_y = (rand() % 6) - 3;
                } else {
                    camera_shake_x = 0;
                    camera_shake_y = 0;
                }

                // Keep simple physics updates active during goal slide
                update_car_physics(&player, 1);
                update_car_physics(&opponent, 0);
                update_ball_physics();

                if (state_timer <= 0) {
                    if (scoring_team == 3) score_blue++;
                    else score_orange++;

                    if (match_timer <= 0) {
                        game_state = STATE_GAMEOVER;
                        state_timer = 240;
                    } else if (link_match || !start_goal_replay()) {
                        reset_kickoff();
                    }
                }
                break;

            case STATE_REPLAY:
                /* Replay never re-runs physics or changes the match result. */
                if (key_hit(KEY_START) || key_hit(KEY_A)) {
                    reset_kickoff();
                } else {
                    advance_goal_replay();
                }
                break;

            case STATE_PAUSED:
                if (key_hit(KEY_UP) && pause_selection > 0) pause_selection--;
                if (key_hit(KEY_DOWN) && pause_selection < 2) pause_selection++;
                /* Resume: START, or A on "RESUME" */
                if (key_hit(KEY_START) || (key_hit(KEY_B))) {
                    game_state = STATE_PLAY;
                }
                if (key_hit(KEY_A)) {
                    if (pause_selection == 0) {
                        game_state = STATE_PLAY;
                    } else if(pause_selection==1) {
                        reset_match();
                    } else {
                        game_state = STATE_TITLE;
                        menu_selection = 0;
                    }
                }
                break;

            case STATE_GAMEOVER:
                state_timer -= dt_time_frames;
                if (state_timer <= 0 && (link_match?(link_buttons[0]&KEY_START):key_hit(KEY_START))) {
                    reset_match();
                }
                break;
        }

        if(network_step) {
            int local_team=link_match && link_player_id()==1?6:3;
            if(game_state==STATE_GOAL && achievement_previous_state==STATE_PLAY && scoring_team==local_team) {
                achievement_add(ACH_GOAL,1);achievement_add(ACH_TEN_GOALS,1);
                if(is_hockey_match)achievement_add(ACH_HOCKEY,1);
            }
            if(game_state==STATE_GAMEOVER && achievement_previous_state!=STATE_GAMEOVER) {
                achievement_add(ACH_MATCH,1);
                if(local_team==3?score_blue>score_orange:score_orange>score_blue) {
                    achievement_add(ACH_WIN,1);
                    achievement_add(is_hockey_match?ACH_HOCKEY_WIN:ACH_SOCCER_WIN,1);
                    if(link_match)achievement_add(ACH_LINK_WIN,1);
                    if((local_team==3?score_orange:score_blue)==0)achievement_add(ACH_SHUTOUT,1);
                }
            }
            if(game_state==STATE_TUTORIAL_COMPLETE && achievement_previous_state!=STATE_TUTORIAL_COMPLETE)
                achievement_add(ACH_TUTORIAL,1);
            const Car *local=local_team==3?&player:&opponent;
            if(achievement_previous_state==STATE_PLAY && local->is_on_ground && local->surface_wall &&
               local->surface_angle>=48 && abs(local->speed)>FP_SCALE)achievement_add(ACH_WALL,dt_time_frames);
        }
        if(game_state!=achievement_previous_state) {
            if(game_state==STATE_GOAL || game_state==STATE_TRAINING_GOAL)audio_play(AUDIO_GOAL);
        }
        const Car *audio_car=link_match && link_player_id()==1?&opponent:&player;
        if(game_state==STATE_TITLE)audio_loop(AUDIO_MUSIC);
        else if(network_step && (game_state==STATE_PLAY || game_state==STATE_TRAINING || game_state==STATE_TUTORIAL))
            audio_loop(key_is_down(KEY_B) && audio_car->boost>0?AUDIO_BOOST:
                -1);
        else audio_loop(-1);
        if(achievement_toast_timer>0)achievement_toast_timer-=dt_time_frames;
        if(achievement_toast_timer<=0) {
            achievement_toast=achievement_next_unlock();
            if(achievement_toast>=0)achievement_toast_timer=150;
        }
        if(link_match && network_step){link_previous[0]=link_buttons[0];link_previous[1]=link_buttons[1];}
        if(network_step && game_state>STATE_MENU_LINK && game_state!=STATE_PAUSED) {
            update_particles();
            ball_spin_y=(ball_spin_y+ball.vel.x*2)&65535;
            ball_spin_x=(ball_spin_x+ball.vel.z*2)&65535;
        }
        if(game_state!=achievement_previous_state) {
            previous_player=player;previous_opponent=opponent;previous_ball=ball;
            previous_camera_yaw=camera_yaw;
            previous_ball_spin_y=ball_spin_y;previous_ball_spin_x=ball_spin_x;
        }
        } /* fixed simulation ticks */
        __key_curr=polled_keys;__key_prev=first_key_previous;
        /* Physics and presentation share the 30 Hz cadence. Show this tick,
           not the previous pose: delaying it adds a full frame of input lag. */
        Car live_player=player,live_opponent=opponent;
        Ball live_ball=ball;
        int live_camera_yaw=camera_yaw;
        int live_spin_y=ball_spin_y,live_spin_x=ball_spin_x;
        render_fraction=256;
        if(abs(player.pos.x-previous_player.pos.x)>128*256 ||
           abs(player.pos.z-previous_player.pos.z)>128*256)render_fraction=256;
        int32_t player_render_rotation[9],opponent_render_rotation[9];
        blend_car_rotation(&previous_player,&player,render_fraction,player_render_rotation);
        blend_car_rotation(&previous_opponent,&opponent,render_fraction,opponent_render_rotation);
        player.pos=blend_position(previous_player.pos,player.pos,render_fraction);
        opponent.pos=blend_position(previous_opponent.pos,opponent.pos,render_fraction);
        ball.pos=blend_position(previous_ball.pos,ball.pos,render_fraction);
        camera_yaw=blend_heading(previous_camera_yaw,camera_yaw,render_fraction);
        ball_spin_y=blend_heading(previous_ball_spin_y,ball_spin_y,render_fraction);
        ball_spin_x=blend_heading(previous_ball_spin_x,ball_spin_x,render_fraction);
        /* Simulation always runs blue then orange. Only the presentation swaps
           on console 2, so collisions and pad pickups have identical ordering. */
        int swapped_view=link_match && link_player_id()==1;
        if(swapped_view){Car tmp=player;player=opponent;opponent=tmp;}

        // 2. CAMERA CALCULATION
        Vector3 cam_pos;
        int cam_yaw = 0;
        int cam_pitch = 0;

        if (game_state == STATE_TITLE || (game_state >= STATE_MENU_PLAY && game_state <= STATE_MENU_LINK)) {
            // Static beautiful camera for the menu
            cam_pos.x = -130 * FP_SCALE;
            cam_pos.z = -15 * FP_SCALE;
            cam_pos.y = 25 * FP_SCALE;
            cam_yaw = 68; // face +X, slightly right
            cam_pitch = -4;
            set_camera(cam_pos, cam_yaw, cam_pitch);
        } else {
            if (game_state == STATE_REPLAY) {
                /* Broadcast replay camera: slow orbit keeps the ball and both
                   cars in frame while showing the play from a fresh angle. */
                fixed orbit_x = custom_sin_fp[replay_camera_phase & 255];
                fixed orbit_z = custom_cos_fp[replay_camera_phase & 255];
                Vector3 focus = ball.pos;
                focus.y += 12 * FP_SCALE;
                cam_pos.x = ball.pos.x - orbit_x * 112;
                cam_pos.z = ball.pos.z - orbit_z * 112;
                cam_pos.y = ball.pos.y + 54 * FP_SCALE;
                set_camera_lookat(cam_pos, focus, 0);
            } else if (cam_mode == 0) {
                ball_cam_yaw=camera_yaw;
                /* Pull back 10% so the entire car stays visible on approach. */
                fixed c_dir_x = camera_sin_q8(camera_yaw);
                fixed c_dir_z = camera_cos_q8(camera_yaw);
                cam_pos.x = player.pos.x - ((c_dir_x * 132)>>4) + camera_shake_x * FP_SCALE;
                cam_pos.z = player.pos.z - ((c_dir_z * 132)>>4) + camera_shake_y * FP_SCALE;
                cam_pos.y = player.pos.y + (31 * FP_SCALE);
                cam_pitch = -11;
                cam_yaw   = camera_yaw;
                if(player.surface_wall && player.surface_angle>24) {
                    Vector3 normal=stadium_surface_vector((Vector3){0,256,0},player.surface_wall,player.surface_angle);
                    cam_pos.x+=normal.x*65;cam_pos.z+=normal.z*65;
                    if(cam_pos.x>STADIUM_WIDTH-12*256)cam_pos.x=STADIUM_WIDTH-12*256;
                    if(cam_pos.x<-STADIUM_WIDTH+12*256)cam_pos.x=-STADIUM_WIDTH+12*256;
                    if(cam_pos.z>STADIUM_LENGTH-12*256)cam_pos.z=STADIUM_LENGTH-12*256;
                    if(cam_pos.z<-STADIUM_LENGTH+12*256)cam_pos.z=-STADIUM_LENGTH+12*256;
                    Vector3 target=player.pos;target.y+=8*256;
                    set_camera_lookat(cam_pos,target,0);
                } else set_camera_q8(cam_pos, cam_yaw, cam_pitch*256);
            } else {
                /* --- Improved Ball-cam ---
                 * Smoothly orbits behind the player while always looking at the ball.
                 * Uses an elastically-smoothed yaw so the camera never snaps.
                 * Height and distance dynamically adapt to ball altitude / distance.
                 */


                /* Desired yaw = direction from player toward ball */
                fixed dx_b = ball.pos.x - player.pos.x;
                fixed dz_b = ball.pos.z - player.pos.z;
                int target_yaw = fast_atan2(dx_b >> 8, dz_b >> 8);

                /* Avoid abrupt half-turns when the ball crosses the car. */
                if(abs(dx_b)+abs(dz_b)>8*FP_SCALE)
                    for(int step=0;step<simulation_steps;step++)
                        ball_cam_yaw=smooth_camera_heading(ball_cam_yaw,target_yaw);

                fixed bdir_x = camera_sin_q8(ball_cam_yaw);
                fixed bdir_z = camera_cos_q8(ball_cam_yaw);

                /* Keep enough distance to frame the whole car, even when the
                   ball is directly in front of its bumper. */
                fixed horiz_sq = (dx_b >> 8) * (dx_b >> 8) + (dz_b >> 8) * (dz_b >> 8);
                int horiz_dist = collision_sqrt(horiz_sq);   /* in FP>>8 units */
                int cam_dist = 70 + (horiz_dist >> 3);
                if (cam_dist > 120) cam_dist = 120;

                cam_pos.x = player.pos.x - ((bdir_x * cam_dist)>>4) + camera_shake_x * FP_SCALE;
                cam_pos.z = player.pos.z - ((bdir_z * cam_dist)>>4) + camera_shake_y * FP_SCALE;

                /* Dynamic height: lower when ball is high to look UP at it */
                int ball_h = FP_TO_INT(ball.pos.y);
                int cam_h = 28 - (ball_h / 4);
                if (cam_h < 15) cam_h = 15;
                cam_pos.y = player.pos.y + cam_h * FP_SCALE;

                if(player.surface_wall && player.surface_angle>24) {
                    if(cam_pos.x>STADIUM_WIDTH-12*256)cam_pos.x=STADIUM_WIDTH-12*256;
                    if(cam_pos.x<-STADIUM_WIDTH+12*256)cam_pos.x=-STADIUM_WIDTH+12*256;
                    if(cam_pos.z>STADIUM_LENGTH-12*256)cam_pos.z=STADIUM_LENGTH-12*256;
                    if(cam_pos.z<-STADIUM_LENGTH+12*256)cam_pos.z=-STADIUM_LENGTH+12*256;
                }
                /* set_camera_lookat auto-computes pitch toward ball */
                set_camera_lookat(cam_pos, ball.pos, 0);
            }
        }


        // 3. RENDERING CODE
        if (game_state == STATE_START_SCREEN) {
            // Draw cover art full screen
            memcpy32(frame_buffer, coverart_data, 38400 / 4);
            
            // Flashing "PRESS START TO PLAY"
            if ((current_video_tick / 30) & 1) {
                draw_string("PRESS START TO PLAY", 50, 140, 130);
            }
        } else if (game_state == STATE_TITLE || (game_state >= STATE_MENU_PLAY && game_state <= STATE_MENU_LINK)) {
            draw_menu_screen(game_state, menu_selection);
        } else {
            // Gameplay: sky + raycast ground seamlessly merged
            u8 sky_col = is_hockey_match ? 14 : 128; /* hockey sky/ice palette selector */
            draw_environment_background(sky_col);

            if(performance_mode!=2)draw_stadium_crowd(cam_pos, CAGE_WIDTH, CAGE_LENGTH);

            // Draw 3D Soccer Pitch Lines
            draw_soccer_pitch(cam_pos);
            draw_stadium_curves(cam_pos,STADIUM_WIDTH,STADIUM_LENGTH,GOAL_HALF_WIDTH);
            if(performance_mode!=2)draw_stadium_floodlights();

            // Draw Shadows
            if(performance_mode!=2)draw_car_shadow(player.pos, player.yaw, garage_model[player.team==6]);
            if (performance_mode!=2 && enable_opponent && game_state != STATE_TRAINING && game_state != STATE_TUTORIAL) {
                draw_car_shadow(opponent.pos, opponent.yaw, garage_model[opponent.team==6]);
            }
            draw_ball_ground_shadow(ball.pos);

            draw_stadium_goal(-STADIUM_LENGTH, GOAL_HALF_WIDTH, GOAL_HEIGHT,
                              is_hockey_match ? 5 : 146);
            draw_stadium_goal(STADIUM_LENGTH, GOAL_HALF_WIDTH, GOAL_HEIGHT,
                              is_hockey_match ? 28 : 131);

            if (game_state == STATE_TUTORIAL || game_state == STATE_TUTORIAL_BRIEFING) {
                const TutorialStage *stage = &tutorial_stages[current_tutorial_stage];
                Vector3 target = tutorial_active_target();
                Vector3 beacon_base = target;
                Vector3 beacon_top = target;
                int bx, by, tx, ty;
                u8 torus_col = tutorial_stage_colors[current_tutorial_stage];

                beacon_base.y = 0;
                beacon_top.y += 48 * FP_SCALE;
                Vector3 gate = tutorial_active_target();
                static const signed char gate_ring[8][2] = {
                    {-30,-18},{30,-18},{40,-8},{40,18},
                    {30,28},{-30,28},{-40,18},{-40,-8}
                };
                for (int edge=0;edge<8;edge++) {
                    int next=(edge+1)&7;
                    Vector3 a={gate.x+gate_ring[edge][0]*FP_SCALE,
                               gate.y+gate_ring[edge][1]*FP_SCALE,gate.z};
                    Vector3 b={gate.x+gate_ring[next][0]*FP_SCALE,
                               gate.y+gate_ring[next][1]*FP_SCALE,gate.z};
                    draw_world_line(a,b,torus_col);
                }
                if (project_vertex_world(beacon_base, &bx, &by) &&
                    project_vertex_world(beacon_top, &tx, &ty)) {
                    draw_line(bx, by, tx, ty, torus_col);
                    draw_line(bx + 1, by, tx + 1, ty, torus_col);
                }

                /* Stage 2 slalom: draw small ground rings under ALL gate positions
                   so the player can see upcoming waypoints even before reaching them */
                if (stage->objective == TUTORIAL_STEER_GATES) {
                    static const int ring_pts[8][2] = {
                        {16, 0}, {11, 11}, {0, 16}, {-11, 11},
                        {-16, 0}, {-11, -11}, {0, -16}, {11, -11}
                    };
                    for (int gi = 0; gi < NUM_STEERING_GATES; gi++) {
                        Vector3 gpos = tutorial_steering_gates[gi];
                        u8 gcol = (gi == current_tutorial_gate) ? 129 :
                                  (gi < current_tutorial_gate) ? 128 : 17;
                        int prx[8], pry[8], prvis[8];
                        for (int ri = 0; ri < 8; ri++) {
                            Vector3 rp = { gpos.x + ring_pts[ri][0] * FP_SCALE,
                                           0,
                                           gpos.z + ring_pts[ri][1] * FP_SCALE };
                            prvis[ri] = project_vertex_world(rp, &prx[ri], &pry[ri]);
                        }
                        for (int ri = 0; ri < 8; ri++) {
                            int ni = (ri + 1) & 7;
                            if (prvis[ri] && prvis[ni])
                                draw_line(prx[ri], pry[ri], prx[ni], pry[ni], gcol);
                        }
                    }
                }
            }


            // ── Draw Boost Pads (Ground Wireframe Rings & 3D Crystals) ──────
            for (int i = 0; i < NUM_BOOST_PADS; i++) {
                fixed dx = (boost_pads[i].pos.x - player.pos.x) >> FP_SHIFT;
                fixed dz = (boost_pads[i].pos.z - player.pos.z) >> FP_SHIFT;
                int dist_sq = dx * dx + dz * dz;

                u8 pad_col = (boost_pads[i].amount == 100) ? 131 : 129; // Orange for 100, Cyan for 12

                if(performance_mode==2) {
                    int sx,sy;
                    if(project_vertex_world(boost_pads[i].pos,&sx,&sy)) {
                        u8 color=boost_pads[i].cooldown?149:pad_col;
                        draw_line(sx-3,sy,sx+3,sy,color);
                        draw_line(sx,sy-1,sx,sy+1,color);
                    }
                    continue;
                }
                if (dist_sq <= 140 * 140) {
                    /* NEAR PAD: Draw wireframe circle on the playground floor */
                    u8 ring_col = (boost_pads[i].cooldown > 0) ? 128 : pad_col; // Grey if empty, colored if full
                    int rx[8], ry[8], rvis[8];
                    
                    Vector3 rpts[8] = {
                        { 16 * FP_SCALE, 0, 0 },
                        { 11 * FP_SCALE, 0, 11 * FP_SCALE },
                        { 0, 0, 16 * FP_SCALE },
                        { -11 * FP_SCALE, 0, 11 * FP_SCALE },
                        { -16 * FP_SCALE, 0, 0 },
                        { -11 * FP_SCALE, 0, -11 * FP_SCALE },
                        { 0, 0, -16 * FP_SCALE },
                        { 11 * FP_SCALE, 0, -11 * FP_SCALE }
                    };

                    for (int j = 0; j < 8; j++) {
                        Vector3 pt = boost_pads[i].pos;
                        pt.y = 0;
                        pt.x += rpts[j].x;
                        pt.z += rpts[j].z;
                        rvis[j] = project_vertex_world(pt, &rx[j], &ry[j]);
                    }
                    for (int j = 0; j < 8; j++) {
                        int next = (j + 1) & 7;
                        if (rvis[j] && rvis[next]) {
                            draw_line(rx[j], ry[j], rx[next], ry[next], ring_col);
                        }
                    }
                }

                if (boost_pads[i].cooldown == 0) {
                    /* FULL PAD: Hovering 3D crystal / Distant emblem */
                    Vector3 hover_pos = boost_pads[i].pos;
                    hover_pos.y = (10 * FP_SCALE) + (custom_sin_fp[(pad_pulse * 4) & 255] >> 3);

                    if (dist_sq > 140 * 140) {
                        /* Distant Full Pad: Hovering light gem emblem */
                        int sx, sy;
                        if (project_vertex_world(hover_pos, &sx, &sy)) {
                            draw_line(sx - 2, sy, sx + 2, sy, pad_col);
                            draw_line(sx, sy - 2, sx, sy + 2, pad_col);
                            draw_line(sx - 1, sy - 1, sx + 1, sy + 1, 130);
                        }
                    } else {
                        /* Near Full Pad: Clean 3D Floating Diamond Crystal */
                        fixed pulse_s = (FP_SCALE / 3) + (custom_sin_fp[pad_pulse & 255] >> 4);
                        if (boost_pads[i].amount == 12) pulse_s /= 2;

                        draw_model_world(&pyramid_mesh, hover_pos,
                                         pad_pulse & 255, pad_pulse & 255, 0,
                                         pulse_s / 2, pad_col, RENDER_FLAT);

                        /* Add tiny particle animation (halo) floating upwards */
                        for (int p = 0; p < 4; p++) {
                            int part_phase = (pad_pulse + p * 64) & 255; // 0 to 255
                            // Float up to ~25 units high
                            fixed py = (part_phase * 25 * FP_SCALE) >> 8; 
                            // Orbiting radius
                            fixed px = custom_sin_fp[(pad_pulse * 3 + p * 64) & 255] >> 4;
                            fixed pz = custom_cos_fp[(pad_pulse * 3 + p * 64) & 255] >> 4;
                            
                            Vector3 part_pos = boost_pads[i].pos;
                            part_pos.x += px;
                            part_pos.y += py;
                            part_pos.z += pz;
                            
                            int psx, psy;
                            if (project_vertex_world(part_pos, &psx, &psy)) {
                                draw_point(psx, psy, 130); // White particle
                            }
                        }
                    }
                }
            }

            // Z-Sorting (Painter's Algorithm) for Dynamic Objects
            typedef struct {
                int id;       // 0 = Player, 1 = Opponent, 2 = Ball
                int32_t dist; // Squared distance to camera
            } RenderItem;
            
            RenderItem items[3];
            
            // Player distance
            int32_t pdx = (player.pos.x - cam_pos.x) >> 8;
            int32_t pdz = (player.pos.z - cam_pos.z) >> 8;
            items[0].id = 0;
            int32_t pdy = (player.pos.y - cam_pos.y) >> 8;
            items[0].dist = pdx * pdx + pdy * pdy + pdz * pdz;
            
            // Opponent distance
            int32_t odx = (opponent.pos.x - cam_pos.x) >> 8;
            int32_t odz = (opponent.pos.z - cam_pos.z) >> 8;
            items[1].id = 1;
            int32_t ody = (opponent.pos.y - cam_pos.y) >> 8;
            items[1].dist = odx * odx + ody * ody + odz * odz;
            
            // Ball distance
            int32_t bdx = (ball.pos.x - cam_pos.x) >> 8;
            int32_t bdz = (ball.pos.z - cam_pos.z) >> 8;
            items[2].id = 2;
            int32_t bdy = (ball.pos.y - cam_pos.y) >> 8;
            items[2].dist = bdx * bdx + bdy * bdy + bdz * bdz;
            
            // Sort items descending (furthest first)
            for (int i = 0; i < 2; i++) {
                for (int j = i + 1; j < 3; j++) {
                    if (items[i].dist < items[j].dist) {
                        RenderItem temp = items[i];
                        items[i] = items[j];
                        items[j] = temp;
                    }
                }
            }
            
            // Render Soccer Ball Spin Prep

            // Draw objects in sorted order
            for (int i = 0; i < 3; i++) {
                Vector3 object_pos = items[i].id==0 ? player.pos : items[i].id==1 ? opponent.pos : ball.pos;
                if (!world_sphere_visible(object_pos, (items[i].id==2 ? 16 : 48)*FP_SCALE)) continue;
                if (items[i].id == 0) {
                    int32_t mod_m[9];
                    memcpy(mod_m,swapped_view?opponent_render_rotation:player_render_rotation,sizeof(mod_m));
                    draw_model_world_mat(car_gameplay_mesh(garage_model[player.team==6], items[i].dist), surface_car_position(garage_model[player.team==6], &player, mod_m), mod_m, FP_ONE, team_paints[player.team==6][garage_paint[player.team==6]], performance_mode?RENDER_ACCENTS:RENDER_TEXTURED);
                } else if (items[i].id == 1 && enable_opponent &&
                           game_state != STATE_TRAINING && game_state != STATE_TUTORIAL) {
                    int32_t mod_m[9];
                    memcpy(mod_m,swapped_view?player_render_rotation:opponent_render_rotation,sizeof(mod_m));
                    draw_model_world_mat(car_gameplay_mesh(garage_model[opponent.team==6], items[i].dist), surface_car_position(garage_model[opponent.team==6], &opponent, mod_m), mod_m, FP_ONE, team_paints[opponent.team==6][garage_paint[opponent.team==6]], performance_mode==2?RENDER_FLAT:performance_mode?RENDER_ACCENTS:RENDER_TEXTURED);
                } else if (items[i].id == 2) {
                    if (is_hockey_match) {
                        /* Dark rubber puck contrasts with the ice. */
                        draw_model_world(&puck_mesh, ball.pos, ball_spin_y>>8, 0, 0, FP_SCALE, 149, RENDER_FLAT);
                    } else {
                        if (!draw_soccer_ball(ball.pos, ball_spin_y>>8, ball_spin_x>>8))
                            draw_model_world(ball_gameplay_mesh(items[i].dist), ball.pos, ball_spin_y>>8, ball_spin_x>>8, 0,
                                             FP_SCALE / 3 * 2, 130, RENDER_FLAT);
                    }
                }
            }
            
            // Draw Boost Particles
            draw_particles();
            
            // Draw Minimap Radar (big when SELECT held)
            if (key_is_down(KEY_SELECT) && !(control_scheme && abs(player.speed)>FP_SCALE) && game_state != STATE_PAUSED && game_state != STATE_TUTORIAL && game_state != STATE_TRAINING) {
                draw_big_radar();
            } else if (game_state!=STATE_TUTORIAL && game_state!=STATE_TUTORIAL_BRIEFING && game_state!=STATE_TUTORIAL_COMPLETE) {
                if(performance_mode!=2)draw_radar();
            }

            // ==== HUD LAYOUT =================================================
            if (game_state == STATE_PLAY || game_state == STATE_HOCKEY ||
                game_state == STATE_GOAL || game_state == STATE_REPLAY ||
                game_state == STATE_PAUSED) {
                draw_match_hud();
            } else if (game_state != STATE_TUTORIAL_BRIEFING && game_state != STATE_TUTORIAL) {
                /* Solo modes: minimal driving readout */
                int speed_val = FP_TO_INT(abs(player.speed) * 35);
                fast_draw_speed(speed_val, 62, 147, 130);
                fast_draw_boost(FP_TO_INT(player.boost), 212, 136);
                fast_draw_team_score("FPS",measured_fps,4,20,130);
            }

            // Mid-screen alerts (Y=72, centred) – only one shown at a time
            if (show_boost_alert > 0) {
                show_boost_alert--;
                draw_hud_text("BOOST EMPTY", 72, 119, 28);      // Red warning
            }

            if (game_state == STATE_TUTORIAL || game_state == STATE_TUTORIAL_BRIEFING) {
                const TutorialStage *stage = &tutorial_stages[current_tutorial_stage];

                if (game_state == STATE_TUTORIAL) {
                    /* ── Compact corner HUD ── */
                    char tut_str[24];
                    snprintf(tut_str, sizeof(tut_str), "TUT %d/%d",
                             current_tutorial_stage + 1, NUM_TUTORIAL_STAGES);
                    draw_string(tut_str, 2, 2, tutorial_stage_colors[current_tutorial_stage]);
                    draw_hud_text("START:EXIT", 158, 2, 130);
                    draw_string(stage->title, 2, 12, 130);
                    draw_hud_text(tutorial_control_hint(stage), 2, 22, 129);
                    draw_hud_text("SELECT: RETRY", 4, 132, 130);

                    /* Gate counter for slalom */
                    if (stage->objective == TUTORIAL_STEER_GATES) {
                        char gate_str[32];
                        snprintf(gate_str, sizeof(gate_str), "GATE %d/%d",
                                 current_tutorial_gate + 1, NUM_STEERING_GATES);
                        draw_string(gate_str, 2, 34, 129);
                    }

                    /* Directional arrow + distance */
                    draw_tutorial_arrow();

                    /* 6-segment progress bar */
                    draw_tutorial_progress_bar();

                    /* Stage completion flash */
                    if (tutorial_flash_timer > 0) {
                        draw_tutorial_stage_flash();
                    }
                } else {
                    /* BRIEFING state: full card overlay */
                    draw_tutorial_briefing_card(stage);
                }
            } else if (game_state == STATE_TUTORIAL_COMPLETE) {
                draw_hud_text("TUTORIAL COMPLETE!", 52, 50, 131);
                draw_hud_text("YOU ARE READY TO PLAY", 40, 72, 130);
                draw_hud_text("A: PLAY A MATCH", 60, 94, 129);
                draw_hud_text("START: MAIN MENU", 56, 110, 130);
            } else if (game_state == STATE_TRAINING) {
                char tr_str[32];
                snprintf(tr_str, sizeof(tr_str), "TRAINING %d/%d", current_training_level+1, NUM_TRAINING_LEVELS);
                draw_string(tr_str, 2, 2, 131);
                
                // Show hint text
                draw_string(training_levels[current_training_level].hint, 2, 14, 130);
                
                int time_left = 0;
                if (training_touches == 0) {
                    time_left = (training_levels[current_training_level].time_limit - training_timer) / 60;
                } else {
                    time_left = (480 - training_post_touch_timer) / 60;
                }
                if (time_left < 0) time_left = 0;
                
                char tm_str[16];
                snprintf(tm_str, sizeof(tm_str), "%02d SEC", time_left);
                draw_string(tm_str, 200, 2, 131);
                draw_hud_text("SEL+A:RETRY  SEL+B:NEXT",4,28,130);
            }
            if (game_state == STATE_TRAINING_GOAL) {
                draw_string("GREAT SHOT!", 76, 60, 131);
            }

            if(game_state==STATE_GOAL || game_state==STATE_TRAINING_GOAL) {
                int gx,gy,tx,ty;
                Vector3 tip=goal_effect_pos;tip.y+=30*FP_SCALE;
                if(project_vertex_world(goal_effect_pos,&gx,&gy) && project_vertex_world(tip,&tx,&ty)) {
                    int size=abs(ty-gy);if(size<12)size=12;if(size>60)size=60;
                    draw_goal_effect(gx,gy,120-state_timer,goal_effect_style,scoring_team==3?146:131,size);
                }
            }
            if (game_state == STATE_GOAL) {
                draw_goal_celebration_panel();
            } else if (game_state == STATE_REPLAY) {
                draw_hud_text("A/START: SKIP", 76, 128, 130);
            }

            if (game_state == STATE_GAMEOVER) {
                // Winner Y=60, press start Y=76
                if (score_blue > score_orange) {
                    draw_hud_text("BLUE WINS!", 80, 60, 129);
                } else if (score_orange > score_blue) {
                    draw_hud_text("ORANGE WINS!", 72, 60, 131);
                } else {
                    draw_hud_text("DRAW!", 108, 60, 130);
                }
                draw_hud_text("START:REPLAY", 72, 76, 130);
            }

            /* ==== PAUSE OVERLAY ==== */
            if (game_state == STATE_PAUSED) {
                draw_hud_box(44, 39, 152, 88, 5, 10);
                /* Title */
                draw_string("- PAUSED -", 80, 46, 131);
                /* Options */
                draw_menu_text_box("RESUME", 64, pause_selection == 0 ? 131 : 130, 136);
                draw_menu_text_box("RESTART MATCH", 80, pause_selection == 1 ? 131 : 130, 136);
                draw_menu_text_box("EXIT MENU", 96, pause_selection == 2 ? 131 : 130, 136);
                /* Controls hint */
                draw_string("A:SEL  B:RESUME", 64, 116, 130);
            }

        }

        if(achievement_toast_timer>0 && achievement_toast>=0) {
            draw_centered_text_line("ACHIEVEMENT UNLOCKED",112,131);
            draw_centered_text_line(achievement_names[achievement_toast],124,130);
        }

        if(link_match) {
            draw_hud_text(link_player_id()?"P2 LINK":"P1 LINK",4,32,146);
            if(link_waiting) {
                draw_hud_text("WAITING FOR LINK",56,76,131);
                draw_hud_text("START+SELECT: EXIT",44,92,130);
            }
        }
        if(swapped_view){Car tmp=player;player=opponent;opponent=tmp;}
        player=live_player;opponent=live_opponent;ball=live_ball;
        camera_yaw=live_camera_yaw;ball_spin_y=live_spin_y;ball_spin_x=live_spin_x;
        // 4. SWAP AND SYNC
        swap_buffers();
    }

    return 0;
}
