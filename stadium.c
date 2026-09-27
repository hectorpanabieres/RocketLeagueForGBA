#include "stadium.h"

/* Shared wall coordinates: u runs along the wall; depth points out of the arena. */
static Vector3 wall_point(int wall, fixed u, fixed y, fixed depth, fixed width, fixed length) {
    switch (wall) {
    case 0: return (Vector3){u, y, -length-depth};
    case 1: return (Vector3){width+depth, y, u};
    case 2: return (Vector3){-u, y, length+depth};
    default: return (Vector3){-width-depth, y, -u};
    }
}

static int wall_visible(int wall, Vector3 camera, fixed width, fixed length) {
    switch (wall) {
    case 0: return camera.z >= -length;
    case 1: return camera.x <= width;
    case 2: return camera.z <= length;
    default: return camera.x >= -width;
    }
}

/* Opposite corners define these axis-aligned wall strips. */
static int strip_visible(Vector3 a, Vector3 b) {
    Vector3 lo={a.x<b.x?a.x:b.x,a.y<b.y?a.y:b.y,a.z<b.z?a.z:b.z};
    Vector3 hi={a.x>b.x?a.x:b.x,a.y>b.y?a.y:b.y,a.z>b.z?a.z:b.z};
    return world_bounds_visible(lo,hi);
}

/* Cache shared net vertices/edges once: no cell clipping or topology work in play. */
typedef struct { short u,y; } NetPoint;
typedef struct { unsigned short a,b; } NetEdge;
typedef struct {
    NetPoint points[256]; NetEdge edges[384];
    int point_count,edge_count,half,top,density;
} Net;
/* Geometry is read once per vertex, so keep this cache out of fast-code/stack RAM. */
static Net nets[2] __attribute__((section(".ewram"), aligned(4)));

static int add_point(Net *net,int u,int y) {
    for(int i=0;i<net->point_count;i++)
        if(net->points[i].u==u && net->points[i].y==y) return i;
    if(net->point_count>=256) return -1;
    int i=net->point_count++;
    net->points[i]=(NetPoint){u,y}; return i;
}
static void prepare_net(Net *net,int half,int top) {
    int density=performance_mode?2:1;
    if(net->half==half && net->top==top && net->density==density) return;
    net->density=density;
    net->half=half;net->top=top;net->point_count=net->edge_count=0;
    static const short hu[6]={64,32,-32,-64,-32,32};
    static const short hv[6]={0,56,56,0,-56,-56};
    for(int col=0,u=-half-64*density;u<=half+64*density;u+=96*density,++col)
        for(int y=-112*density+(col&1)*56*density;y<=top+112*density;y+=112*density)
            for(int edge=0;edge<3;edge++) {
                int u0=u+hu[edge]*density,y0=y+hv[edge]*density,u1=u+hu[edge+1]*density,y1=y+hv[edge+1]*density;
                for(int boundary=0;boundary<4;boundary++) {
                    int d0=boundary==0?u0+half:boundary==1?half-u0:boundary==2?y0:top-y0;
                    int d1=boundary==0?u1+half:boundary==1?half-u1:boundary==2?y1:top-y1;
                    if(d0<0 && d1<0) {y0=y1=-1;break;}
                    if((d0<0)!=(d1<0)) {
                        int nu=u0+(u1-u0)*d0/(d0-d1),ny=y0+(y1-y0)*d0/(d0-d1);
                        if(d0<0) {u0=nu;y0=ny;} else {u1=nu;y1=ny;}
                    }
                }
                if(y0<0 || y1<0 || net->edge_count>=384) continue;
                int a=add_point(net,u0,y0),b=add_point(net,u1,y1);
                if(a>=0 && b>=0) net->edges[net->edge_count++]=(NetEdge){a,b};
            }
}

void draw_stadium_crowd(Vector3 camera, fixed width, fixed length) {
    /* One terrace surface per wall; spectators interpolate along projected rows
       instead of individually transforming hundreds of world-space points. */
    for(int wall=0;wall<4;wall++) {
        if(!wall_visible(wall,camera,width,length)) continue;
        fixed half=(wall&1)?length:width;
        Vector3 v[4]={wall_point(wall,-half,12*256,24*256,width,length),
                      wall_point(wall,half,12*256,24*256,width,length),
                      wall_point(wall,half,78*256,99*256,width,length),
                      wall_point(wall,-half,78*256,99*256,width,length)};
        if(!strip_visible(v[0],v[2])) continue;
        Face f[4]={0};
        const int indices[4][3]={{0,1,2},{0,2,3},{2,1,0},{3,2,0}};
        for(int i=0;i<4;i++) {f[i].v1=indices[i][0];f[i].v2=indices[i][1];f[i].v3=indices[i][2];f[i].base_color=STAND_DARK;}
        Mesh strip={"CROWD",4,4,v,f,0};
        draw_model_world(&strip,(Vector3){0,0,0},0,0,0,FP_ONE,-1,RENDER_FLAT);
        for(int row=0;row<(performance_mode?2:3);row++) {
            int x0,y0,x1,y1;
            Vector3 a=wall_point(wall,-half,(24+row*22)*256,(36+row*25)*256,width,length);
            Vector3 b=wall_point(wall,half,(24+row*22)*256,(36+row*25)*256,width,length);
            if(!project_vertex_world(a,&x0,&y0) || !project_vertex_world(b,&x1,&y1)) continue;
            if(x0>x1) {int t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;}
            if(x1<0 || x0>=240 || x0==x1) continue;
            draw_line(x0,y0+2,x1,y1+2,STAND_LIGHT);
            int start=x0<0?0:x0,end=x1>239?239:x1;
            for(int x=start+((row+wall)&3);x<=end;x+=performance_mode?9:6) {
                int y=y0+(y1-y0)*(x-x0)/(x1-x0);
                if(y<1 || y>=159) continue;
                int seed=(x/6+row*7+wall*3)&7;
                u8 c=seed<4?CROWD_BLUE:seed<6?CROWD_ORANGE:CROWD_NEUTRAL;
                draw_point(x,y,c);draw_point(x,y-1,c);
            }
        }
    }
}

void draw_stadium_hex_walls(Vector3 camera, fixed width, fixed length, fixed height) {
    prepare_net(&nets[0],width/256,height/256);
    prepare_net(&nets[1],length/256,height/256);
    for(int wall=0;wall<4;wall++) {
        if(!wall_visible(wall,camera,width,length)) continue;
        Net *net=&nets[wall&1];
        if(!strip_visible(wall_point(wall,-net->half*256,0,0,width,length),
                          wall_point(wall,net->half*256,height,0,width,length))) continue;
        int sx[256],sy[256]; unsigned char visible[256];
        for(int i=0;i<net->point_count;i++) {
            Vector3 p=wall_point(wall,net->points[i].u*256,net->points[i].y*256,0,width,length);
            visible[i]=project_vertex_world(p,&sx[i],&sy[i]);
        }
        for(int i=0;i<net->edge_count;i++) {
            int a=net->edges[i].a,b=net->edges[i].b;
            if(visible[a] && visible[b]) {
                if((sx[a]<0 && sx[b]<0)||(sx[a]>=240 && sx[b]>=240)||
                   (sy[a]<0 && sy[b]<0)||(sy[a]>=160 && sy[b]>=160)) continue;
                draw_line(sx[a],sy[a],sx[b],sy[b],WALL_HEX);
            } else if(visible[a] || visible[b]) {
                draw_world_line(wall_point(wall,net->points[a].u*256,net->points[a].y*256,0,width,length),
                                wall_point(wall,net->points[b].u*256,net->points[b].y*256,0,width,length),WALL_HEX);
            }
        }
        draw_world_line(wall_point(wall,-net->half*256,0,0,width,length),
                        wall_point(wall,net->half*256,0,0,width,length),STAND_RAIL);
    }
}

/* Open goal mouth, inset net and reinforced posts. Line geometry keeps the
   structure transparent and avoids large filled panels and their overdraw. */
void draw_stadium_goal(fixed z, fixed w, fixed h, u8 color) {
    fixed depth = z < 0 ? -INT_TO_FP(35) : INT_TO_FP(35);
    fixed back = z + depth;
    if(!world_bounds_visible((Vector3){-w-3*256,0,z<back?z:back},
        (Vector3){w+3*256,h+3*256,z>back?z:back})) return;
    Vector3 front[4]={{-w,0,z},{w,0,z},{w,h,z},{-w,h,z}};
    Vector3 rear[4]={{-w,0,back},{w,0,back},{w,h,back},{-w,h,back}};
    if(performance_mode==2) {
        for(int i=1;i<4;i++)draw_world_line(front[i],front[(i+1)&3],i==2?color:130);
        draw_world_line(front[0],front[1],color);
        draw_world_line(rear[2],rear[3],color);
        draw_world_line(front[2],rear[2],color);
        draw_world_line(front[3],rear[3],color);
        return;
    }
    /* Sparse net: eight columns, three rows, plus roof and side tension lines. */
    for(int i=1;i<8;i+=performance_mode?2:1) {
        fixed x=-w+(2*w*i)/8;
        draw_world_line((Vector3){x,0,back},(Vector3){x,h,back},149);
        draw_world_line((Vector3){x,h,z},(Vector3){x,h,back},149);
    }
    for(int i=1;i<3;i+=performance_mode?2:1) {
        fixed y=h*i/3;
        draw_world_line((Vector3){-w,y,back},(Vector3){w,y,back},149);
        draw_world_line((Vector3){-w,y,z},(Vector3){-w,y,back},149);
        draw_world_line((Vector3){w,y,z},(Vector3){w,y,back},149);
    }
    for(int i=0;i<4;i++) {
        draw_world_line(rear[i],rear[(i+1)&3],color);
        draw_world_line(front[i],rear[i],color);
    }
    /* White inner lip and colored outer rail make the opening easy to read. */
    for(int i=1;i<4;i++) draw_world_line(front[i],front[(i+1)&3],130);
    fixed rail=INT_TO_FP(3);
    draw_world_line((Vector3){-w-rail,0,z},(Vector3){-w-rail,h+rail,z},color);
    draw_world_line((Vector3){w+rail,0,z},(Vector3){w+rail,h+rail,z},color);
    draw_world_line((Vector3){-w-rail,h+rail,z},(Vector3){w+rail,h+rail,z},color);
    draw_world_line(front[0],front[1],color);
}

/* Rotate an unfolded floor vector onto a wall. Yaw remains local to this
   frame, so the same steering, dodge, exhaust and boost axes work everywhere. */
Vector3 stadium_surface_vector(Vector3 v, int wall, int angle) {
    if(!wall || !angle) return v;
    int sn=custom_sin_fp[angle], cs=custom_cos_fp[angle];
    int sign=wall>0?1:-1;
    Vector3 r=v;
    if(wall==1 || wall==-1) {
        r.x=(cs*v.x-sign*sn*v.y)/256;
        r.y=(sign*sn*v.x+cs*v.y)/256;
    } else {
        r.z=(cs*v.z-sign*sn*v.y)/256;
        r.y=(sign*sn*v.z+cs*v.y)/256;
    }
    return r;
}

/* Exact integer square root for the small ramp cross-section; no floating
   point, large lookup table or per-frame geometry allocation. */
static int surface_sqrt(unsigned value) {
    unsigned root=0,bit=1u<<30;
    while(bit>value) bit>>=2;
    while(bit) {
        if(value>=root+bit) {value-=root+bit;root=(root>>1)+bit;}
        else root>>=1;
        bit>>=2;
    }
    return root;
}

int stadium_surface_contact(Vector3 *pos, fixed radius, fixed tolerance,
    fixed width, fixed length, fixed goal_width, fixed goal_height,
    int *wall, int *angle, Vector3 *normal) {
    int hit=0;
    fixed deepest=-2147483647;
    *wall=0;*angle=0;*normal=(Vector3){0,256,0};
    if(pos->y<=radius+tolerance) {
        deepest=radius-pos->y;pos->y=radius;hit=1;
    }
    for(int axis=0;axis<2;axis++) {
        if(axis && pos->x>-goal_width+radius && pos->x<goal_width-radius &&
           pos->y<goal_height-radius) continue;
        fixed *coord=axis?&pos->z:&pos->x;
        fixed extent=axis?length:width;
        int sign=*coord<0?-1:1;
        fixed u=sign*(*coord), center=extent-WALL_CURVE_RADIUS;
        if(u<center) continue;
        int local_angle=64;
        fixed penetration=u-(extent-radius);
        if(pos->y<WALL_CURVE_RADIUS) {
            /* Free space lies inside the quarter-circle; radius offsets
               the same surface for the ball instead of burying it in the ramp. */
            fixed dx=u-center, dy=WALL_CURVE_RADIUS-pos->y;
            /* Bound an out-of-arena impact before squaring fixed coordinates. */
            if(dx>WALL_CURVE_RADIUS+32*256) dx=WALL_CURVE_RADIUS+32*256;
            fixed dist=surface_sqrt((unsigned)(dx*dx+dy*dy));
            fixed limit=WALL_CURVE_RADIUS-radius;
            if(dist<limit-tolerance || !dist) continue;
            penetration=dist-limit;
            *coord=sign*(center+dx*limit/dist);
            pos->y=WALL_CURVE_RADIUS-dy*limit/dist;
            int best=-1;
            for(int a=0;a<=64;a++) {
                int dot=dx*custom_sin_fp[a]+dy*custom_cos_fp[a];
                if(dot>best){best=dot;local_angle=a;}
            }
        } else {
            if(u<extent-radius-tolerance) continue;
            *coord=sign*(extent-radius);
        }
        if(!hit || !*wall || penetration>deepest) {
            deepest=penetration;
            *wall=sign*(axis+1);*angle=local_angle;
            *normal=stadium_surface_vector((Vector3){0,256,0},*wall,*angle);
        }
        hit=1;
    }
    return hit;
}

/* Static geometry is built once. Distant ramps use four facets instead of
   eight, halving their triangle and vertex work without changing collisions. */
static Vector3 ramp_near[6][18] __attribute__((section(".ewram"),aligned(4)));
static Vector3 ramp_far[6][10] __attribute__((section(".ewram"),aligned(4)));
static fixed ramp_width,ramp_length,ramp_goal;
static IWRAM_CODE __attribute__((noinline)) void draw_speed_curves(fixed width, fixed length, fixed goal_width) {
    for(int wall=0;wall<4;wall++) {
        int endwall=!(wall&1);
        fixed half=endwall?width:length;
        for(int strip=0;strip<(endwall?2:1);strip++) {
            fixed lo=endwall?(strip?goal_width:-half):-half;
            fixed hi=endwall?(strip?half:-goal_width):half;
            Vector3 a=wall_point(wall,lo,0,-WALL_CURVE_RADIUS,width,length);
            Vector3 b=wall_point(wall,hi,0,-WALL_CURVE_RADIUS,width,length);
            Vector3 c=wall_point(wall,hi,WALL_CURVE_RADIUS,0,width,length);
            Vector3 d=wall_point(wall,lo,WALL_CURVE_RADIUS,0,width,length);
            int ax,ay,bx,by,cx,cy,dx,dy;
            if(project_vertex_world(a,&ax,&ay) && project_vertex_world(b,&bx,&by) &&
               project_vertex_world(c,&cx,&cy) && project_vertex_world(d,&dx,&dy)) {
                draw_triangle_flat_clipped(ax,ay,bx,by,cx,cy,153);
                draw_triangle_flat_clipped(ax,ay,cx,cy,dx,dy,153);
            } else draw_world_line(a,b,153);
        }
    }
}
void draw_stadium_curves(Vector3 camera, fixed width, fixed length, fixed goal_width) {
    static const int32_t identity[9]={4096,0,0,0,4096,0,0,0,4096};
    static const Face near_faces[16]={
        {0,1,3,155},{0,3,2,155},{2,3,5,154},{2,5,4,154},
        {4,5,7,154},{4,7,6,154},{6,7,9,153},{6,9,8,153},
        {8,9,11,153},{8,11,10,153},{10,11,13,152},{10,13,12,152},
        {12,13,15,152},{12,15,14,152},{14,15,17,151},{14,17,16,151}
    };
    static const Face far_faces[8]={
        {0,1,3,154},{0,3,2,154},{2,3,5,153},{2,5,4,153},
        {4,5,7,152},{4,7,6,152},{6,7,9,151},{6,9,8,151}
    };
    if(performance_mode==2) {draw_speed_curves(width,length,goal_width);return;}
    int rebuild=ramp_width!=width || ramp_length!=length || ramp_goal!=goal_width;
    int index=0;
    for(int wall=0;wall<4;wall++) {
        int endwall=!(wall&1);
        fixed distance=wall==0?camera.z+length:wall==1?width-camera.x:
                       wall==2?length-camera.z:camera.x+width;
        for(int strip=0;strip<(endwall?2:1);strip++,index++) {
            if(rebuild) {
                fixed lo=endwall?(strip?goal_width:-width):-length;
                fixed hi=endwall?(strip?width:-goal_width):length;
                for(int i=0;i<=8;i++) {
                    int a=i*8;
                    fixed depth=-WALL_CURVE_RADIUS+WALL_CURVE_RADIUS*custom_sin_fp[a]/256;
                    fixed y=WALL_CURVE_RADIUS-WALL_CURVE_RADIUS*custom_cos_fp[a]/256;
                    ramp_near[index][2*i]=wall_point(wall,lo,y,depth,width,length);
                    ramp_near[index][2*i+1]=wall_point(wall,hi,y,depth,width,length);
                    if(!(i&1)) {
                        ramp_far[index][i]=ramp_near[index][2*i];
                        ramp_far[index][i+1]=ramp_near[index][2*i+1];
                    }
                }
            }
            if(distance<0 || !strip_visible(ramp_near[index][0],ramp_near[index][17])) continue;
            int near=distance<(performance_mode?95:190)*256;
            Mesh mesh={"CURVED WALL",near?18:10,near?16:8,
                near?ramp_near[index]:ramp_far[index],near?near_faces:far_faces,0};
            draw_model_world_mat(&mesh,(Vector3){0,0,0},identity,256,-1,RENDER_FLAT);
            Vector3 *v=ramp_near[index];
            u8 accent=wall==2?131:146;
            draw_world_line(v[0],v[1],130);
            draw_world_line(v[16],v[17],accent);
            if(near) {
                draw_world_line(v[14],v[15],accent);
                /* One seam and luminous cap per panel, without extra faces. */
                for(int i=0;i<8;i+=2) {
                    Vector3 a={(v[2*i].x+v[2*i+1].x)/2,v[2*i].y,(v[2*i].z+v[2*i+1].z)/2};
                    Vector3 b={(v[2*i+4].x+v[2*i+5].x)/2,v[2*i+4].y,(v[2*i+4].z+v[2*i+5].z)/2};
                    draw_world_line(a,b,150);
                }
            }
        }
    }
    ramp_width=width;ramp_length=length;ramp_goal=goal_width;
}

/* The same bounded, allocation-free animation is used in the garage and
   after scoring. It costs no work during normal play. */
void draw_goal_effect(int x, int y, int age, int style, u8 color, int scale) {
    if(age<0 || age>=96) return;
    if(scale<1)scale=1;
    if(scale>80)scale=80;
    int radius=scale*(12+age)/80;
    if(style==0) {
        /* Confetti fountain: deterministic paths make previews repeatable. */
        for(int i=0;i<24;i++) {
            int life=(age+i*3)%96;
            int dx=((i*37)%49-24)*life*scale/2400;
            int dy=(-life*3+life*life/28)*scale/80;
            draw_line(x+dx,y+dy,x+dx+((i&1)?2:-2),y+dy+2,(i%3)?color:130);
        }
    } else if(style==1) {
        /* Nova: expanding shock ring and sixteen outward sparks. */
        for(int i=0;i<32;i++) {
            int a=i*8,b=((i+1)*8)&255;
            draw_line(x+custom_sin_fp[a]*radius/256,y+custom_cos_fp[a]*radius/256,
                      x+custom_sin_fp[b]*radius/256,y+custom_cos_fp[b]*radius/256,color);
            if(!(i&1)) draw_line(x+custom_sin_fp[a]*radius/320,y+custom_cos_fp[a]*radius/320,
                x+custom_sin_fp[a]*radius/200,y+custom_cos_fp[a]*radius/200,130);
        }
    } else {
        /* Vortex: three rotating spiral arms with bright tips. */
        for(int arm=0;arm<3;arm++)for(int j=1;j<=12;j++) {
            int a=(age*5+arm*85+j*7)&255,b=(a+7)&255;
            int r=radius*j/12,r2=radius*(j+1)/12;
            draw_line(x+custom_sin_fp[a]*r/256,y+custom_cos_fp[a]*r/256,
                      x+custom_sin_fp[b]*r2/256,y+custom_cos_fp[b]*r2/256,j>9?130:color);
        }
    }
}

void draw_hockey_markings(fixed width,fixed length) {
    for(int zone=-1;zone<=1;zone++) {
        fixed z=zone*length/3;
        draw_world_line((Vector3){-width+WALL_CURVE_RADIUS,256,z},
                        (Vector3){width-WALL_CURVE_RADIUS,256,z},zone?146:28);
    }
    /* Team-coloured creases identify the defended ends at a glance. */
    for(int side=-1;side<=1;side+=2) {
        fixed z=side*(length-32*256);
        u8 color=side<0?146:131;
        draw_world_line((Vector3){-48*256,256,z},(Vector3){48*256,256,z},color);
        draw_world_line((Vector3){-48*256,256,z},(Vector3){-48*256,256,side*length},color);
        draw_world_line((Vector3){48*256,256,z},(Vector3){48*256,256,side*length},color);
    }
    for(int spot=0;spot<5;spot++) {
        fixed cx=spot?((spot&1)?-130:130)*256:0;
        fixed cz=spot?((spot<3)?-230:230)*256:0;
        int radius=spot?20:36;
        for(int i=0;i<12;i++) {
            int a=i*256/12,b=(i+1)*256/12;
            draw_world_line((Vector3){cx+custom_sin_fp[a]*radius,256,cz+custom_cos_fp[a]*radius},
                (Vector3){cx+custom_sin_fp[b&255]*radius,256,cz+custom_cos_fp[b&255]*radius},28);
        }
        draw_world_line((Vector3){cx-2*256,256,cz},(Vector3){cx+2*256,256,cz},28);
    }
}
