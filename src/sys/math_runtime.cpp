#include "math_runtime.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <random>

/* ── Helpers ──────────────────────────────────────────────────────────── */
static inline float v3len(float x, float y, float z) { return sqrtf(x*x + y*y + z*z); }

/* ── vec2 ─────────────────────────────────────────────────────────────── */
void  aiml_math_vec2_add      (const AimlVec2* a, const AimlVec2* b, AimlVec2* o) { o->x=a->x+b->x; o->y=a->y+b->y; }
void  aiml_math_vec2_sub      (const AimlVec2* a, const AimlVec2* b, AimlVec2* o) { o->x=a->x-b->x; o->y=a->y-b->y; }
void  aiml_math_vec2_mul      (const AimlVec2* a, const AimlVec2* b, AimlVec2* o) { o->x=a->x*b->x; o->y=a->y*b->y; }
void  aiml_math_vec2_scale    (const AimlVec2* a, float s, AimlVec2* o)            { o->x=a->x*s;    o->y=a->y*s;   }
void  aiml_math_vec2_div      (const AimlVec2* a, float s, AimlVec2* o)            { o->x=a->x/s;    o->y=a->y/s;   }
void  aiml_math_vec2_neg      (const AimlVec2* a,           AimlVec2* o)            { o->x=-a->x;     o->y=-a->y;    }
float aiml_math_vec2_dot      (const AimlVec2* a, const AimlVec2* b)               { return a->x*b->x + a->y*b->y; }
float aiml_math_vec2_length   (const AimlVec2* a)                                   { return sqrtf(a->x*a->x + a->y*a->y); }
float aiml_math_vec2_length_sq(const AimlVec2* a)                                   { return a->x*a->x + a->y*a->y; }
void  aiml_math_vec2_normalize(const AimlVec2* a, AimlVec2* o) {
    float len = aiml_math_vec2_length(a);
    if (len < 1e-8f) { o->x=0; o->y=0; return; }
    o->x=a->x/len; o->y=a->y/len;
}
void  aiml_math_vec2_lerp    (const AimlVec2* a, const AimlVec2* b, float t, AimlVec2* o) {
    o->x=a->x+(b->x-a->x)*t; o->y=a->y+(b->y-a->y)*t;
}
float aiml_math_vec2_distance(const AimlVec2* a, const AimlVec2* b) {
    float dx=a->x-b->x, dy=a->y-b->y; return sqrtf(dx*dx+dy*dy);
}
float aiml_math_vec2_angle   (const AimlVec2* a) { return atan2f(a->y, a->x); }
void  aiml_math_vec2_perp    (const AimlVec2* a, AimlVec2* o) { o->x=-a->y; o->y=a->x; }
int   aiml_math_vec2_eq      (const AimlVec2* a, const AimlVec2* b) { return a->x==b->x && a->y==b->y; }

/* ── vec3 ─────────────────────────────────────────────────────────────── */
void  aiml_math_vec3_add      (const AimlVec3* a, const AimlVec3* b, AimlVec3* o) {
    o->x=a->x+b->x; o->y=a->y+b->y; o->z=a->z+b->z; o->_w=0;
}
void  aiml_math_vec3_sub      (const AimlVec3* a, const AimlVec3* b, AimlVec3* o) {
    o->x=a->x-b->x; o->y=a->y-b->y; o->z=a->z-b->z; o->_w=0;
}
void  aiml_math_vec3_mul      (const AimlVec3* a, const AimlVec3* b, AimlVec3* o) {
    o->x=a->x*b->x; o->y=a->y*b->y; o->z=a->z*b->z; o->_w=0;
}
void  aiml_math_vec3_scale    (const AimlVec3* a, float s, AimlVec3* o) {
    o->x=a->x*s; o->y=a->y*s; o->z=a->z*s; o->_w=0;
}
void  aiml_math_vec3_div      (const AimlVec3* a, float s, AimlVec3* o) {
    o->x=a->x/s; o->y=a->y/s; o->z=a->z/s; o->_w=0;
}
void  aiml_math_vec3_neg      (const AimlVec3* a, AimlVec3* o) {
    o->x=-a->x; o->y=-a->y; o->z=-a->z; o->_w=0;
}
float aiml_math_vec3_dot      (const AimlVec3* a, const AimlVec3* b) {
    return a->x*b->x + a->y*b->y + a->z*b->z;
}
void  aiml_math_vec3_cross    (const AimlVec3* a, const AimlVec3* b, AimlVec3* o) {
    o->x = a->y*b->z - a->z*b->y;
    o->y = a->z*b->x - a->x*b->z;
    o->z = a->x*b->y - a->y*b->x;
    o->_w = 0;
}
float aiml_math_vec3_length   (const AimlVec3* a)                    { return v3len(a->x,a->y,a->z); }
float aiml_math_vec3_length_sq(const AimlVec3* a)                    { return a->x*a->x+a->y*a->y+a->z*a->z; }
void  aiml_math_vec3_normalize(const AimlVec3* a, AimlVec3* o) {
    float len = aiml_math_vec3_length(a);
    if (len < 1e-8f) { o->x=0; o->y=0; o->z=0; o->_w=0; return; }
    o->x=a->x/len; o->y=a->y/len; o->z=a->z/len; o->_w=0;
}
void  aiml_math_vec3_lerp     (const AimlVec3* a, const AimlVec3* b, float t, AimlVec3* o) {
    o->x=a->x+(b->x-a->x)*t; o->y=a->y+(b->y-a->y)*t; o->z=a->z+(b->z-a->z)*t; o->_w=0;
}
float aiml_math_vec3_distance (const AimlVec3* a, const AimlVec3* b) {
    float dx=a->x-b->x, dy=a->y-b->y, dz=a->z-b->z; return sqrtf(dx*dx+dy*dy+dz*dz);
}
void  aiml_math_vec3_reflect  (const AimlVec3* v, const AimlVec3* n, AimlVec3* o) {
    float d = 2.0f * aiml_math_vec3_dot(v, n);
    o->x=v->x-d*n->x; o->y=v->y-d*n->y; o->z=v->z-d*n->z; o->_w=0;
}
void  aiml_math_vec3_refract  (const AimlVec3* v, const AimlVec3* n, float eta, AimlVec3* o) {
    float nd = aiml_math_vec3_dot(n, v);
    float k  = 1.0f - eta*eta*(1.0f - nd*nd);
    if (k < 0.0f) { o->x=0; o->y=0; o->z=0; o->_w=0; }
    else {
        float s = eta*nd + sqrtf(k);
        o->x=eta*v->x-s*n->x; o->y=eta*v->y-s*n->y; o->z=eta*v->z-s*n->z; o->_w=0;
    }
}
int   aiml_math_vec3_eq       (const AimlVec3* a, const AimlVec3* b) {
    return a->x==b->x && a->y==b->y && a->z==b->z;
}

/* ── vec4 ─────────────────────────────────────────────────────────────── */
void  aiml_math_vec4_add  (const AimlVec4* a, const AimlVec4* b, AimlVec4* o) {
    o->x=a->x+b->x; o->y=a->y+b->y; o->z=a->z+b->z; o->w=a->w+b->w;
}
void  aiml_math_vec4_sub  (const AimlVec4* a, const AimlVec4* b, AimlVec4* o) {
    o->x=a->x-b->x; o->y=a->y-b->y; o->z=a->z-b->z; o->w=a->w-b->w;
}
void  aiml_math_vec4_mul  (const AimlVec4* a, const AimlVec4* b, AimlVec4* o) {
    o->x=a->x*b->x; o->y=a->y*b->y; o->z=a->z*b->z; o->w=a->w*b->w;
}
void  aiml_math_vec4_scale(const AimlVec4* a, float s, AimlVec4* o) {
    o->x=a->x*s; o->y=a->y*s; o->z=a->z*s; o->w=a->w*s;
}
void  aiml_math_vec4_div  (const AimlVec4* a, float s, AimlVec4* o) {
    o->x=a->x/s; o->y=a->y/s; o->z=a->z/s; o->w=a->w/s;
}
void  aiml_math_vec4_neg  (const AimlVec4* a, AimlVec4* o) {
    o->x=-a->x; o->y=-a->y; o->z=-a->z; o->w=-a->w;
}
void  aiml_math_vec4_lerp (const AimlVec4* a, const AimlVec4* b, float t, AimlVec4* o) {
    o->x=a->x+(b->x-a->x)*t; o->y=a->y+(b->y-a->y)*t;
    o->z=a->z+(b->z-a->z)*t; o->w=a->w+(b->w-a->w)*t;
}
int   aiml_math_vec4_eq   (const AimlVec4* a, const AimlVec4* b) {
    return a->x==b->x && a->y==b->y && a->z==b->z && a->w==b->w;
}

/* ── quat ─────────────────────────────────────────────────────────────── */
void aiml_math_quat_identity(AimlQuat* o) { o->x=0; o->y=0; o->z=0; o->w=1; }

void aiml_math_quat_axis_angle(const AimlVec3* axis, float angle, AimlQuat* o) {
    float s = sinf(angle * 0.5f);
    o->x = axis->x * s;
    o->y = axis->y * s;
    o->z = axis->z * s;
    o->w = cosf(angle * 0.5f);
}

void aiml_math_quat_euler(float ex, float ey, float ez, AimlQuat* o) {
    /* XYZ order */
    float cx=cosf(ex*0.5f), sx=sinf(ex*0.5f);
    float cy=cosf(ey*0.5f), sy=sinf(ey*0.5f);
    float cz=cosf(ez*0.5f), sz=sinf(ez*0.5f);
    o->x = sx*cy*cz + cx*sy*sz;
    o->y = cx*sy*cz - sx*cy*sz;
    o->z = cx*cy*sz + sx*sy*cz;
    o->w = cx*cy*cz - sx*sy*sz;
}

void aiml_math_quat_mul(const AimlQuat* a, const AimlQuat* b, AimlQuat* o) {
    o->x = a->w*b->x + a->x*b->w + a->y*b->z - a->z*b->y;
    o->y = a->w*b->y - a->x*b->z + a->y*b->w + a->z*b->x;
    o->z = a->w*b->z + a->x*b->y - a->y*b->x + a->z*b->w;
    o->w = a->w*b->w - a->x*b->x - a->y*b->y - a->z*b->z;
}

void aiml_math_quat_normalize(const AimlQuat* q, AimlQuat* o) {
    float len = sqrtf(q->x*q->x + q->y*q->y + q->z*q->z + q->w*q->w);
    if (len < 1e-8f) { o->x=0; o->y=0; o->z=0; o->w=1; return; }
    o->x=q->x/len; o->y=q->y/len; o->z=q->z/len; o->w=q->w/len;
}

void aiml_math_quat_inverse(const AimlQuat* q, AimlQuat* o) {
    /* conjugate / length^2  */
    float ls = q->x*q->x + q->y*q->y + q->z*q->z + q->w*q->w;
    if (ls < 1e-16f) { o->x=0; o->y=0; o->z=0; o->w=1; return; }
    o->x=-q->x/ls; o->y=-q->y/ls; o->z=-q->z/ls; o->w=q->w/ls;
}

void aiml_math_quat_rotate_vec3(const AimlQuat* q, const AimlVec3* v, AimlVec3* o) {
    /* Rodrigues' rotation formula: q v q* */
    float qx=q->x, qy=q->y, qz=q->z, qw=q->w;
    float vx=v->x, vy=v->y, vz=v->z;
    float tx = 2*(qy*vz - qz*vy);
    float ty = 2*(qz*vx - qx*vz);
    float tz = 2*(qx*vy - qy*vx);
    o->x = vx + qw*tx + qy*tz - qz*ty;
    o->y = vy + qw*ty + qz*tx - qx*tz;
    o->z = vz + qw*tz + qx*ty - qy*tx;
    o->_w = 0;
}

void aiml_math_quat_lerp(const AimlQuat* a, const AimlQuat* b, float t, AimlQuat* o) {
    o->x=a->x+(b->x-a->x)*t; o->y=a->y+(b->y-a->y)*t;
    o->z=a->z+(b->z-a->z)*t; o->w=a->w+(b->w-a->w)*t;
    aiml_math_quat_normalize(o, o);
}

void aiml_math_quat_slerp(const AimlQuat* a, const AimlQuat* b, float t, AimlQuat* o) {
    float dot = a->x*b->x + a->y*b->y + a->z*b->z + a->w*b->w;
    AimlQuat bb = *b;
    if (dot < 0.0f) { bb.x=-bb.x; bb.y=-bb.y; bb.z=-bb.z; bb.w=-bb.w; dot=-dot; }
    if (dot > 0.9995f) { aiml_math_quat_lerp(a, &bb, t, o); return; }
    float theta0 = acosf(dot);
    float theta  = theta0 * t;
    float s0 = cosf(theta) - dot * sinf(theta) / sinf(theta0);
    float s1 = sinf(theta) / sinf(theta0);
    o->x = s0*a->x + s1*bb.x;
    o->y = s0*a->y + s1*bb.y;
    o->z = s0*a->z + s1*bb.z;
    o->w = s0*a->w + s1*bb.w;
}

void aiml_math_quat_to_mat4(const AimlQuat* q, AimlMat4* o) {
    float x=q->x, y=q->y, z=q->z, w=q->w;
    float x2=x*2,y2=y*2,z2=z*2;
    float xx=x*x2, xy=x*y2, xz=x*z2;
    float yy=y*y2, yz=y*z2, zz=z*z2;
    float wx=w*x2, wy=w*y2, wz=w*z2;
    /* column-major */
    o->m[0]=1-(yy+zz); o->m[1]=xy+wz;     o->m[2]=xz-wy;     o->m[3]=0;
    o->m[4]=xy-wz;     o->m[5]=1-(xx+zz); o->m[6]=yz+wx;     o->m[7]=0;
    o->m[8]=xz+wy;     o->m[9]=yz-wx;     o->m[10]=1-(xx+yy);o->m[11]=0;
    o->m[12]=0;        o->m[13]=0;        o->m[14]=0;        o->m[15]=1;
}

void aiml_math_mat4_to_quat(const AimlMat4* m, AimlQuat* o) {
    /* Shepperd's method */
    float trace = m->m[0]+m->m[5]+m->m[10];
    if (trace > 0) {
        float s = 0.5f / sqrtf(trace+1.0f);
        o->w = 0.25f/s;
        o->x = (m->m[6]-m->m[9])*s;
        o->y = (m->m[8]-m->m[2])*s;
        o->z = (m->m[1]-m->m[4])*s;
    } else if (m->m[0]>m->m[5] && m->m[0]>m->m[10]) {
        float s = 2.0f * sqrtf(1.0f+m->m[0]-m->m[5]-m->m[10]);
        o->w = (m->m[6]-m->m[9])/s;
        o->x = 0.25f*s;
        o->y = (m->m[4]+m->m[1])/s;
        o->z = (m->m[8]+m->m[2])/s;
    } else if (m->m[5]>m->m[10]) {
        float s = 2.0f * sqrtf(1.0f+m->m[5]-m->m[0]-m->m[10]);
        o->w = (m->m[8]-m->m[2])/s;
        o->x = (m->m[4]+m->m[1])/s;
        o->y = 0.25f*s;
        o->z = (m->m[9]+m->m[6])/s;
    } else {
        float s = 2.0f * sqrtf(1.0f+m->m[10]-m->m[0]-m->m[5]);
        o->w = (m->m[1]-m->m[4])/s;
        o->x = (m->m[8]+m->m[2])/s;
        o->y = (m->m[9]+m->m[6])/s;
        o->z = 0.25f*s;
    }
}

/* ── mat4 ─────────────────────────────────────────────────────────────── */
/* Column-major: m[col*4 + row]  i.e. m[c][r] */
#define M(mp,r,c) (mp)->m[(c)*4+(r)]

void aiml_math_mat4_identity(AimlMat4* o) {
    memset(o, 0, sizeof(AimlMat4));
    M(o,0,0)=M(o,1,1)=M(o,2,2)=M(o,3,3)=1.0f;
}

void aiml_math_mat4_mul(const AimlMat4* a, const AimlMat4* b, AimlMat4* o) {
    for (int r=0; r<4; r++)
        for (int c=0; c<4; c++) {
            float s=0;
            for (int k=0; k<4; k++) s += M(a,r,k) * M(b,k,c);
            M(o,r,c) = s;
        }
}

void aiml_math_mat4_mul_vec4(const AimlMat4* m, const AimlVec4* v, AimlVec4* o) {
    o->x = M(m,0,0)*v->x + M(m,0,1)*v->y + M(m,0,2)*v->z + M(m,0,3)*v->w;
    o->y = M(m,1,0)*v->x + M(m,1,1)*v->y + M(m,1,2)*v->z + M(m,1,3)*v->w;
    o->z = M(m,2,0)*v->x + M(m,2,1)*v->y + M(m,2,2)*v->z + M(m,2,3)*v->w;
    o->w = M(m,3,0)*v->x + M(m,3,1)*v->y + M(m,3,2)*v->z + M(m,3,3)*v->w;
}

void aiml_math_mat4_translate(const AimlVec3* v, AimlMat4* o) {
    aiml_math_mat4_identity(o);
    M(o,0,3) = v->x; M(o,1,3) = v->y; M(o,2,3) = v->z;
}

void aiml_math_mat4_rotate_x(float a, AimlMat4* o) {
    aiml_math_mat4_identity(o);
    float c=cosf(a), s=sinf(a);
    M(o,1,1)=c; M(o,1,2)=-s;
    M(o,2,1)=s; M(o,2,2)= c;
}
void aiml_math_mat4_rotate_y(float a, AimlMat4* o) {
    aiml_math_mat4_identity(o);
    float c=cosf(a), s=sinf(a);
    M(o,0,0)= c; M(o,0,2)=s;
    M(o,2,0)=-s; M(o,2,2)=c;
}
void aiml_math_mat4_rotate_z(float a, AimlMat4* o) {
    aiml_math_mat4_identity(o);
    float c=cosf(a), s=sinf(a);
    M(o,0,0)=c; M(o,0,1)=-s;
    M(o,1,0)=s; M(o,1,1)= c;
}
void aiml_math_mat4_rotate(const AimlVec3* axis, float angle, AimlMat4* o) {
    AimlQuat q;
    aiml_math_quat_axis_angle(axis, angle, &q);
    aiml_math_quat_to_mat4(&q, o);
}

void aiml_math_mat4_scale(const AimlVec3* v, AimlMat4* o) {
    aiml_math_mat4_identity(o);
    M(o,0,0)=v->x; M(o,1,1)=v->y; M(o,2,2)=v->z;
}

void aiml_math_mat4_perspective(float fov, float aspect, float nearZ, float farZ, AimlMat4* o) {
    memset(o, 0, sizeof(AimlMat4));
    float f = 1.0f / tanf(fov * 0.5f);
    M(o,0,0) = f / aspect;
    M(o,1,1) = f;
    M(o,2,2) = (farZ + nearZ) / (nearZ - farZ);
    M(o,2,3) = (2.0f * farZ * nearZ) / (nearZ - farZ);
    M(o,3,2) = -1.0f;
}

void aiml_math_mat4_ortho(float l, float r, float b, float t, float nearZ, float farZ, AimlMat4* o) {
    memset(o, 0, sizeof(AimlMat4));
    M(o,0,0) =  2.0f/(r-l);
    M(o,1,1) =  2.0f/(t-b);
    M(o,2,2) = -2.0f/(farZ-nearZ);
    M(o,0,3) = -(r+l)/(r-l);
    M(o,1,3) = -(t+b)/(t-b);
    M(o,2,3) = -(farZ+nearZ)/(farZ-nearZ);
    M(o,3,3) = 1.0f;
}

void aiml_math_mat4_lookat(const AimlVec3* eye, const AimlVec3* center, const AimlVec3* up, AimlMat4* o) {
    /* f = normalize(center - eye) */
    AimlVec3 f, s, u;
    f.x=center->x-eye->x; f.y=center->y-eye->y; f.z=center->z-eye->z; f._w=0;
    aiml_math_vec3_normalize(&f, &f);
    aiml_math_vec3_cross(&f, up, &s);
    aiml_math_vec3_normalize(&s, &s);
    aiml_math_vec3_cross(&s, &f, &u);

    memset(o, 0, sizeof(AimlMat4));
    M(o,0,0)= s.x; M(o,0,1)= s.y; M(o,0,2)= s.z;
    M(o,1,0)= u.x; M(o,1,1)= u.y; M(o,1,2)= u.z;
    M(o,2,0)=-f.x; M(o,2,1)=-f.y; M(o,2,2)=-f.z;
    M(o,0,3)=-(s.x*eye->x+s.y*eye->y+s.z*eye->z);
    M(o,1,3)=-(u.x*eye->x+u.y*eye->y+u.z*eye->z);
    M(o,2,3)= (f.x*eye->x+f.y*eye->y+f.z*eye->z);
    M(o,3,3)= 1.0f;
}

void aiml_math_mat4_transpose(const AimlMat4* m, AimlMat4* o) {
    for (int r=0; r<4; r++)
        for (int c=0; c<4; c++)
            M(o,r,c) = M(m,c,r);
}

void aiml_math_mat4_inverse(const AimlMat4* m, AimlMat4* o) {
    /* Cofactor expansion (correct for 4x4) */
    const float* s = m->m;
    float inv[16];
    inv[0]  =  s[5]*s[10]*s[15]-s[5]*s[11]*s[14]-s[9]*s[6]*s[15]+s[9]*s[7]*s[14]+s[13]*s[6]*s[11]-s[13]*s[7]*s[10];
    inv[4]  = -s[4]*s[10]*s[15]+s[4]*s[11]*s[14]+s[8]*s[6]*s[15]-s[8]*s[7]*s[14]-s[12]*s[6]*s[11]+s[12]*s[7]*s[10];
    inv[8]  =  s[4]*s[9]*s[15] -s[4]*s[11]*s[13]-s[8]*s[5]*s[15]+s[8]*s[7]*s[13]+s[12]*s[5]*s[11]-s[12]*s[7]*s[9];
    inv[12] = -s[4]*s[9]*s[14] +s[4]*s[10]*s[13]+s[8]*s[5]*s[14]-s[8]*s[6]*s[13]-s[12]*s[5]*s[10]+s[12]*s[6]*s[9];
    inv[1]  = -s[1]*s[10]*s[15]+s[1]*s[11]*s[14]+s[9]*s[2]*s[15]-s[9]*s[3]*s[14]-s[13]*s[2]*s[11]+s[13]*s[3]*s[10];
    inv[5]  =  s[0]*s[10]*s[15]-s[0]*s[11]*s[14]-s[8]*s[2]*s[15]+s[8]*s[3]*s[14]+s[12]*s[2]*s[11]-s[12]*s[3]*s[10];
    inv[9]  = -s[0]*s[9]*s[15] +s[0]*s[11]*s[13]+s[8]*s[1]*s[15]-s[8]*s[3]*s[13]-s[12]*s[1]*s[11]+s[12]*s[3]*s[9];
    inv[13] =  s[0]*s[9]*s[14] -s[0]*s[10]*s[13]-s[8]*s[1]*s[14]+s[8]*s[2]*s[13]+s[12]*s[1]*s[10]-s[12]*s[2]*s[9];
    inv[2]  =  s[1]*s[6]*s[15] -s[1]*s[7]*s[14] -s[5]*s[2]*s[15]+s[5]*s[3]*s[14]+s[13]*s[2]*s[7] -s[13]*s[3]*s[6];
    inv[6]  = -s[0]*s[6]*s[15] +s[0]*s[7]*s[14] +s[4]*s[2]*s[15]-s[4]*s[3]*s[14]-s[12]*s[2]*s[7] +s[12]*s[3]*s[6];
    inv[10] =  s[0]*s[5]*s[15] -s[0]*s[7]*s[13] -s[4]*s[1]*s[15]+s[4]*s[3]*s[13]+s[12]*s[1]*s[7] -s[12]*s[3]*s[5];
    inv[14] = -s[0]*s[5]*s[14] +s[0]*s[6]*s[13] +s[4]*s[1]*s[14]-s[4]*s[2]*s[13]-s[12]*s[1]*s[6] +s[12]*s[2]*s[5];
    inv[3]  = -s[1]*s[6]*s[11] +s[1]*s[7]*s[10] +s[5]*s[2]*s[11]-s[5]*s[3]*s[10]-s[9]*s[2]*s[7]  +s[9]*s[3]*s[6];
    inv[7]  =  s[0]*s[6]*s[11] -s[0]*s[7]*s[10] -s[4]*s[2]*s[11]+s[4]*s[3]*s[10]+s[8]*s[2]*s[7]  -s[8]*s[3]*s[6];
    inv[11] = -s[0]*s[5]*s[11] +s[0]*s[7]*s[9]  +s[4]*s[1]*s[11]-s[4]*s[3]*s[9] -s[8]*s[1]*s[7]  +s[8]*s[3]*s[5];
    inv[15] =  s[0]*s[5]*s[10] -s[0]*s[6]*s[9]  -s[4]*s[1]*s[10]+s[4]*s[2]*s[9] +s[8]*s[1]*s[6]  -s[8]*s[2]*s[5];
    float det = s[0]*inv[0]+s[1]*inv[4]+s[2]*inv[8]+s[3]*inv[12];
    if (det == 0.0f) { memcpy(o, m, sizeof(AimlMat4)); return; }
    det = 1.0f/det;
    for (int i=0; i<16; i++) o->m[i] = inv[i]*det;
}

void* aiml_math_mat4_to_buffer(const AimlMat4* m) {
    AimlMathBuffer* buf = (AimlMathBuffer*)malloc(sizeof(AimlMathBuffer));
    buf->size      = 16 * (int)sizeof(float);
    buf->capacity  = 16 * (int)sizeof(float);
    buf->read_idx  = 0;
    buf->write_idx = 16 * (int)sizeof(float);
    buf->data      = (float*)malloc(16 * sizeof(float));
    memcpy(buf->data, m->m, 16 * sizeof(float));
    return (void*)buf;
}

float aiml_math_mat4_get(const AimlMat4* m, int row, int col) { return M(m, row, col); }
void  aiml_math_mat4_set(AimlMat4* m, int row, int col, float v) { M(m, row, col) = v; }

/* ── ivec ─────────────────────────────────────────────────────────────── */
void aiml_math_ivec2_add    (const AimlIVec2* a, const AimlIVec2* b, AimlIVec2* o) { o->x=a->x+b->x; o->y=a->y+b->y; }
void aiml_math_ivec2_div    (const AimlIVec2* a, int32_t s, AimlIVec2* o)           { o->x=a->x/s;    o->y=a->y/s; }
void aiml_math_ivec2_to_vec2(const AimlIVec2* a, AimlVec2* o)                       { o->x=(float)a->x; o->y=(float)a->y; }
void aiml_math_vec2_to_ivec2(const AimlVec2*  a, AimlIVec2* o)                      { o->x=(int32_t)a->x; o->y=(int32_t)a->y; }
void aiml_math_ivec3_add    (const AimlIVec3* a, const AimlIVec3* b, AimlIVec3* o) { o->x=a->x+b->x; o->y=a->y+b->y; o->z=a->z+b->z; o->_w=0; }

/* ── list / raw-array reductions ──────────────────────────────────────── */
float aiml_math_fmin_list(const float* d, int32_t n) {
    if (n <= 0) return 0.0f;
    float r = d[0];
    for (int32_t i = 1; i < n; i++) if (d[i] < r) r = d[i];
    return r;
}
float aiml_math_fmax_list(const float* d, int32_t n) {
    if (n <= 0) return 0.0f;
    float r = d[0];
    for (int32_t i = 1; i < n; i++) if (d[i] > r) r = d[i];
    return r;
}

/* ── random ───────────────────────────────────────────────────────────── */
static std::mt19937& aiml_rng() {
    static thread_local std::mt19937 rng(std::random_device{}());
    return rng;
}

extern "C" float aiml_math_random_f32(float lo, float hi) {
    if (hi <= lo) return lo;
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(aiml_rng());
}

extern "C" int32_t aiml_math_random_int(int32_t lo, int32_t hi) {
    if (hi <= lo) return lo;
    std::uniform_int_distribution<int32_t> dist(lo, hi - 1);
    return dist(aiml_rng());
}

/* NOTE: aiml_math_random_hex (math.random_hex, CSPRNG via OpenSSL RAND_bytes)
 * lives in hash_runtime.cpp — the WASM runtime compiles this file but has no
 * OpenSSL, so the crypto-random impl must stay out of math_runtime.cpp. */
