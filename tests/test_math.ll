; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%vec2 = type { float, float }
%vec3 = type { float, float, float, float }
%vec4 = type { float, float, float, float }
%mat4 = type { [16 x float] }
%buffer = type { i32, i32, i32, i32, ptr }
%quat = type { float, float, float, float }
%ivec2 = type { i32, i32 }
%ivec3 = type { i32, i32, i32, i32 }
%list_f32 = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [4 x i8] c"PI=\00", align 1
@fmt_f32 = private unnamed_addr constant [5 x i8] c"%.6g\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@1 = private unnamed_addr constant [7 x i8] c"  TAU=\00", align 1
@2 = private unnamed_addr constant [5 x i8] c"  E=\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [17 x i8] c"to_radians(180)=\00", align 1
@5 = private unnamed_addr constant [18 x i8] c"  to_degrees(PI)=\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [8 x i8] c"sin(0)=\00", align 1
@8 = private unnamed_addr constant [10 x i8] c"  cos(0)=\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [9 x i8] c"sqrt(4)=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [11 x i8] c"pow(2,10)=\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [12 x i8] c"floor(2.7)=\00", align 1
@15 = private unnamed_addr constant [13 x i8] c"  ceil(2.3)=\00", align 1
@16 = private unnamed_addr constant [14 x i8] c"  round(2.5)=\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [12 x i8] c"fract(2.7)=\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [10 x i8] c"min(1,2)=\00", align 1
@21 = private unnamed_addr constant [12 x i8] c"  max(1,2)=\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@23 = private unnamed_addr constant [16 x i8] c"clamp(1.5,0,1)=\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@25 = private unnamed_addr constant [11 x i8] c"abs(-3.5)=\00", align 1
@26 = private unnamed_addr constant [14 x i8] c"  sign(-3.5)=\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@28 = private unnamed_addr constant [16 x i8] c"lerp(0,10,0.3)=\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@30 = private unnamed_addr constant [15 x i8] c"fmod(7.5,3.0)=\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@31 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@32 = private unnamed_addr constant [21 x i8] c"smoothstep(0,1,0.5)=\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@33 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@34 = private unnamed_addr constant [29 x i8] c"Phase B: scalar functions OK\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@36 = private unnamed_addr constant [9 x i8] c"vec2: x=\00", align 1
@37 = private unnamed_addr constant [5 x i8] c"  y=\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@38 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@39 = private unnamed_addr constant [19 x i8] c"vec2.x after set: \00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@40 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@41 = private unnamed_addr constant [13 x i8] c"vec2 add x: \00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@42 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@43 = private unnamed_addr constant [15 x i8] c"vec2 scale x: \00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@44 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@45 = private unnamed_addr constant [13 x i8] c"vec2 neg x: \00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@46 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@47 = private unnamed_addr constant [13 x i8] c"vec2 angle: \00", align 1
@print_null_str.19 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@48 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@49 = private unnamed_addr constant [14 x i8] c"vec2 perp x: \00", align 1
@print_null_str.20 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@50 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@51 = private unnamed_addr constant [18 x i8] c"vec2 length(3,4)=\00", align 1
@print_null_str.21 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@52 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@53 = private unnamed_addr constant [17 x i8] c"Phase C: vec2 OK\00", align 1
@print_null_str.22 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@54 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@55 = private unnamed_addr constant [9 x i8] c"vec3 a: \00", align 1
@56 = private unnamed_addr constant [2 x i8] c" \00", align 1
@57 = private unnamed_addr constant [2 x i8] c" \00", align 1
@print_null_str.23 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@58 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@59 = private unnamed_addr constant [21 x i8] c"vec3 a.x after set: \00", align 1
@print_null_str.24 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@60 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@61 = private unnamed_addr constant [11 x i8] c"vec3 add: \00", align 1
@62 = private unnamed_addr constant [2 x i8] c" \00", align 1
@print_null_str.25 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@63 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@64 = private unnamed_addr constant [13 x i8] c"vec3 sub x: \00", align 1
@print_null_str.26 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@65 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@66 = private unnamed_addr constant [15 x i8] c"vec3 scale x: \00", align 1
@print_null_str.27 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@67 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@68 = private unnamed_addr constant [13 x i8] c"vec3 neg x: \00", align 1
@print_null_str.28 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@69 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@70 = private unnamed_addr constant [13 x i8] c"vec3 div x: \00", align 1
@print_null_str.29 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@71 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@72 = private unnamed_addr constant [18 x i8] c"vec3 hadamard x: \00", align 1
@print_null_str.30 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@73 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@74 = private unnamed_addr constant [10 x i8] c"dot(a,b)=\00", align 1
@print_null_str.31 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@75 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@76 = private unnamed_addr constant [14 x i8] c"cross(x,y).z=\00", align 1
@print_null_str.32 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@77 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@78 = private unnamed_addr constant [15 x i8] c"length(1,0,0)=\00", align 1
@print_null_str.33 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@79 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@80 = private unnamed_addr constant [20 x i8] c"normalize(2,3,4).x=\00", align 1
@print_null_str.34 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@81 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@82 = private unnamed_addr constant [12 x i8] c"lerp vec3: \00", align 1
@83 = private unnamed_addr constant [2 x i8] c" \00", align 1
@print_null_str.35 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@84 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@85 = private unnamed_addr constant [15 x i8] c"distance(a,b)=\00", align 1
@print_null_str.36 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@86 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@87 = private unnamed_addr constant [11 x i8] c"reflect x=\00", align 1
@88 = private unnamed_addr constant [4 x i8] c" y=\00", align 1
@print_null_str.37 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@89 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@90 = private unnamed_addr constant [17 x i8] c"Phase D: vec3 OK\00", align 1
@print_null_str.38 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@91 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@92 = private unnamed_addr constant [9 x i8] c"vec4: r=\00", align 1
@93 = private unnamed_addr constant [4 x i8] c" g=\00", align 1
@94 = private unnamed_addr constant [4 x i8] c" b=\00", align 1
@95 = private unnamed_addr constant [4 x i8] c" a=\00", align 1
@print_null_str.39 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@96 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@97 = private unnamed_addr constant [14 x i8] c"vec4 lerp r: \00", align 1
@print_null_str.40 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@98 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@99 = private unnamed_addr constant [15 x i8] c"vec4 eq self: \00", align 1
@100 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@101 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@print_null_str.41 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@102 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@103 = private unnamed_addr constant [17 x i8] c"Phase E: vec4 OK\00", align 1
@print_null_str.42 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@104 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@105 = private unnamed_addr constant [19 x i8] c"mat4 identity m00=\00", align 1
@106 = private unnamed_addr constant [6 x i8] c" m11=\00", align 1
@print_null_str.43 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@107 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@108 = private unnamed_addr constant [14 x i8] c"mat4 set m03=\00", align 1
@print_null_str.44 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@109 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@110 = private unnamed_addr constant [20 x i8] c"mat4_translate m03=\00", align 1
@print_null_str.45 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@111 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@112 = private unnamed_addr constant [16 x i8] c"mat4_scale m00=\00", align 1
@print_null_str.46 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@113 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@114 = private unnamed_addr constant [19 x i8] c"mat4_rotate_y m00=\00", align 1
@print_null_str.47 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@115 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@116 = private unnamed_addr constant [15 x i8] c"mat4*mat4 m00=\00", align 1
@print_null_str.48 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@117 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@118 = private unnamed_addr constant [13 x i8] c"mat4*vec4 x=\00", align 1
@print_null_str.49 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@119 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@120 = private unnamed_addr constant [17 x i8] c"perspective m11=\00", align 1
@print_null_str.50 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@121 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@122 = private unnamed_addr constant [12 x i8] c"lookat m22=\00", align 1
@print_null_str.51 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@123 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@124 = private unnamed_addr constant [18 x i8] c"mat4_inverse m00=\00", align 1
@print_null_str.52 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@125 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@126 = private unnamed_addr constant [20 x i8] c"mat4_transpose m01=\00", align 1
@print_null_str.53 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@127 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@128 = private unnamed_addr constant [18 x i8] c"mat4_to_buffer ok\00", align 1
@print_null_str.54 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@129 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@130 = private unnamed_addr constant [17 x i8] c"Phase F: mat4 OK\00", align 1
@print_null_str.55 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@131 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@132 = private unnamed_addr constant [17 x i8] c"quat identity w=\00", align 1
@print_null_str.56 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@133 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@134 = private unnamed_addr constant [19 x i8] c"quat axis_angle y=\00", align 1
@print_null_str.57 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@135 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@136 = private unnamed_addr constant [14 x i8] c"quat euler y=\00", align 1
@print_null_str.58 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@137 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@138 = private unnamed_addr constant [12 x i8] c"quat_mul w=\00", align 1
@print_null_str.59 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@139 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@140 = private unnamed_addr constant [18 x i8] c"quat_normalize w=\00", align 1
@print_null_str.60 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@141 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@142 = private unnamed_addr constant [16 x i8] c"quat_inverse y=\00", align 1
@print_null_str.61 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@143 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@144 = private unnamed_addr constant [20 x i8] c"quat_rotate_vec3 z=\00", align 1
@print_null_str.62 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@145 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@146 = private unnamed_addr constant [14 x i8] c"quat_slerp w=\00", align 1
@print_null_str.63 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@147 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@148 = private unnamed_addr constant [18 x i8] c"quat_to_mat4 m00=\00", align 1
@print_null_str.64 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@149 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@150 = private unnamed_addr constant [16 x i8] c"mat4_to_quat w=\00", align 1
@print_null_str.65 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@151 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@152 = private unnamed_addr constant [13 x i8] c"quat lerp w=\00", align 1
@print_null_str.66 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@153 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@154 = private unnamed_addr constant [17 x i8] c"Phase G: quat OK\00", align 1
@print_null_str.67 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@155 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@156 = private unnamed_addr constant [15 x i8] c"ivec2 screen: \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@157 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@print_null_str.68 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@158 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@159 = private unnamed_addr constant [15 x i8] c"ivec3 tile: x=\00", align 1
@160 = private unnamed_addr constant [4 x i8] c" z=\00", align 1
@print_null_str.69 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@161 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@162 = private unnamed_addr constant [14 x i8] c"idiv(1920,2)=\00", align 1
@print_null_str.70 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@163 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@164 = private unnamed_addr constant [8 x i8] c"iadd x=\00", align 1
@print_null_str.71 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@165 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@166 = private unnamed_addr constant [17 x i8] c"ivec2_to_vec2 x=\00", align 1
@print_null_str.72 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@167 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@168 = private unnamed_addr constant [17 x i8] c"vec2_to_ivec2 x=\00", align 1
@print_null_str.73 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@169 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@170 = private unnamed_addr constant [17 x i8] c"Phase H: ivec OK\00", align 1
@print_null_str.74 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@171 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@172 = private unnamed_addr constant [10 x i8] c"list min=\00", align 1
@173 = private unnamed_addr constant [7 x i8] c"  max=\00", align 1
@print_null_str.75 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@174 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  %pi = alloca float, align 4
  store float 0.000000e+00, ptr %pi, align 4
  store float 0x400921FB60000000, ptr %pi, align 4
  %tau = alloca float, align 4
  store float 0.000000e+00, ptr %tau, align 4
  store float 0x401921FB60000000, ptr %tau, align 4
  %e = alloca float, align 4
  store float 0.000000e+00, ptr %e, align 4
  store float 0x4005BF0A80000000, ptr %e, align 4
  %pi_val = load float, ptr %pi, align 4
  %0 = call ptr @__aiml_f32_to_string(float %pi_val)
  %1 = call ptr @__aiml_str_concat(ptr @0, ptr %0)
  %2 = call ptr @__aiml_str_concat(ptr %1, ptr @1)
  %tau_val = load float, ptr %tau, align 4
  %3 = call ptr @__aiml_f32_to_string(float %tau_val)
  %4 = call ptr @__aiml_str_concat(ptr %2, ptr %3)
  %5 = call ptr @__aiml_str_concat(ptr %4, ptr @2)
  %e_val = load float, ptr %e, align 4
  %6 = call ptr @__aiml_f32_to_string(float %e_val)
  %7 = call ptr @__aiml_str_concat(ptr %5, ptr %6)
  %print_null_check = icmp eq ptr %7, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %7
  %print_call = call i32 (ptr, ...) @printf(ptr @3, ptr %print_safe_str)
  %rad = alloca float, align 4
  store float 0.000000e+00, ptr %rad, align 4
  store float 0x400921FB60000000, ptr %rad, align 4
  %deg = alloca float, align 4
  store float 0.000000e+00, ptr %deg, align 4
  store float 0x40667FFEC0000000, ptr %deg, align 4
  %rad_val = load float, ptr %rad, align 4
  %8 = call ptr @__aiml_f32_to_string(float %rad_val)
  %9 = call ptr @__aiml_str_concat(ptr @4, ptr %8)
  %10 = call ptr @__aiml_str_concat(ptr %9, ptr @5)
  %deg_val = load float, ptr %deg, align 4
  %11 = call ptr @__aiml_f32_to_string(float %deg_val)
  %12 = call ptr @__aiml_str_concat(ptr %10, ptr %11)
  %print_null_check1 = icmp eq ptr %12, null
  %print_safe_str2 = select i1 %print_null_check1, ptr @print_null_str.1, ptr %12
  %print_call3 = call i32 (ptr, ...) @printf(ptr @6, ptr %print_safe_str2)
  %s = alloca float, align 4
  store float 0.000000e+00, ptr %s, align 4
  %13 = call float @sinf(float 0.000000e+00)
  store float %13, ptr %s, align 4
  %c = alloca float, align 4
  store float 0.000000e+00, ptr %c, align 4
  %14 = call float @cosf(float 0.000000e+00)
  store float %14, ptr %c, align 4
  %s_val = load float, ptr %s, align 4
  %15 = call ptr @__aiml_f32_to_string(float %s_val)
  %16 = call ptr @__aiml_str_concat(ptr @7, ptr %15)
  %17 = call ptr @__aiml_str_concat(ptr %16, ptr @8)
  %c_val = load float, ptr %c, align 4
  %18 = call ptr @__aiml_f32_to_string(float %c_val)
  %19 = call ptr @__aiml_str_concat(ptr %17, ptr %18)
  %print_null_check4 = icmp eq ptr %19, null
  %print_safe_str5 = select i1 %print_null_check4, ptr @print_null_str.2, ptr %19
  %print_call6 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str5)
  %sq = alloca float, align 4
  store float 0.000000e+00, ptr %sq, align 4
  %20 = call float @sqrtf(float 4.000000e+00)
  store float %20, ptr %sq, align 4
  %sq_val = load float, ptr %sq, align 4
  %21 = call ptr @__aiml_f32_to_string(float %sq_val)
  %22 = call ptr @__aiml_str_concat(ptr @10, ptr %21)
  %print_null_check7 = icmp eq ptr %22, null
  %print_safe_str8 = select i1 %print_null_check7, ptr @print_null_str.3, ptr %22
  %print_call9 = call i32 (ptr, ...) @printf(ptr @11, ptr %print_safe_str8)
  %p = alloca float, align 4
  store float 0.000000e+00, ptr %p, align 4
  %23 = call float @powf(float 2.000000e+00, float 1.000000e+01)
  store float %23, ptr %p, align 4
  %p_val = load float, ptr %p, align 4
  %24 = call ptr @__aiml_f32_to_string(float %p_val)
  %25 = call ptr @__aiml_str_concat(ptr @12, ptr %24)
  %print_null_check10 = icmp eq ptr %25, null
  %print_safe_str11 = select i1 %print_null_check10, ptr @print_null_str.4, ptr %25
  %print_call12 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str11)
  %fl = alloca float, align 4
  store float 0.000000e+00, ptr %fl, align 4
  %26 = call float @floorf(float 0x40059999A0000000)
  store float %26, ptr %fl, align 4
  %cl = alloca float, align 4
  store float 0.000000e+00, ptr %cl, align 4
  %27 = call float @ceilf(float 0x4002666660000000)
  store float %27, ptr %cl, align 4
  %ro = alloca float, align 4
  store float 0.000000e+00, ptr %ro, align 4
  %28 = call float @roundf(float 2.500000e+00)
  store float %28, ptr %ro, align 4
  %fl_val = load float, ptr %fl, align 4
  %29 = call ptr @__aiml_f32_to_string(float %fl_val)
  %30 = call ptr @__aiml_str_concat(ptr @14, ptr %29)
  %31 = call ptr @__aiml_str_concat(ptr %30, ptr @15)
  %cl_val = load float, ptr %cl, align 4
  %32 = call ptr @__aiml_f32_to_string(float %cl_val)
  %33 = call ptr @__aiml_str_concat(ptr %31, ptr %32)
  %34 = call ptr @__aiml_str_concat(ptr %33, ptr @16)
  %ro_val = load float, ptr %ro, align 4
  %35 = call ptr @__aiml_f32_to_string(float %ro_val)
  %36 = call ptr @__aiml_str_concat(ptr %34, ptr %35)
  %print_null_check13 = icmp eq ptr %36, null
  %print_safe_str14 = select i1 %print_null_check13, ptr @print_null_str.5, ptr %36
  %print_call15 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str14)
  %fr = alloca float, align 4
  store float 0.000000e+00, ptr %fr, align 4
  %37 = call float @floorf(float 0x40059999A0000000)
  %38 = fsub float 0x40059999A0000000, %37
  store float %38, ptr %fr, align 4
  %fr_val = load float, ptr %fr, align 4
  %39 = call ptr @__aiml_f32_to_string(float %fr_val)
  %40 = call ptr @__aiml_str_concat(ptr @18, ptr %39)
  %print_null_check16 = icmp eq ptr %40, null
  %print_safe_str17 = select i1 %print_null_check16, ptr @print_null_str.6, ptr %40
  %print_call18 = call i32 (ptr, ...) @printf(ptr @19, ptr %print_safe_str17)
  %mn = alloca float, align 4
  store float 0.000000e+00, ptr %mn, align 4
  store float 1.000000e+00, ptr %mn, align 4
  %mx = alloca float, align 4
  store float 0.000000e+00, ptr %mx, align 4
  store float 2.000000e+00, ptr %mx, align 4
  %mn_val = load float, ptr %mn, align 4
  %41 = call ptr @__aiml_f32_to_string(float %mn_val)
  %42 = call ptr @__aiml_str_concat(ptr @20, ptr %41)
  %43 = call ptr @__aiml_str_concat(ptr %42, ptr @21)
  %mx_val = load float, ptr %mx, align 4
  %44 = call ptr @__aiml_f32_to_string(float %mx_val)
  %45 = call ptr @__aiml_str_concat(ptr %43, ptr %44)
  %print_null_check19 = icmp eq ptr %45, null
  %print_safe_str20 = select i1 %print_null_check19, ptr @print_null_str.7, ptr %45
  %print_call21 = call i32 (ptr, ...) @printf(ptr @22, ptr %print_safe_str20)
  %cl2 = alloca float, align 4
  store float 0.000000e+00, ptr %cl2, align 4
  store float 1.000000e+00, ptr %cl2, align 4
  %cl2_val = load float, ptr %cl2, align 4
  %46 = call ptr @__aiml_f32_to_string(float %cl2_val)
  %47 = call ptr @__aiml_str_concat(ptr @23, ptr %46)
  %print_null_check22 = icmp eq ptr %47, null
  %print_safe_str23 = select i1 %print_null_check22, ptr @print_null_str.8, ptr %47
  %print_call24 = call i32 (ptr, ...) @printf(ptr @24, ptr %print_safe_str23)
  %ab = alloca float, align 4
  store float 0.000000e+00, ptr %ab, align 4
  %48 = call float @fabsf(float -3.500000e+00)
  store float %48, ptr %ab, align 4
  %sg = alloca float, align 4
  store float 0.000000e+00, ptr %sg, align 4
  store float -1.000000e+00, ptr %sg, align 4
  %ab_val = load float, ptr %ab, align 4
  %49 = call ptr @__aiml_f32_to_string(float %ab_val)
  %50 = call ptr @__aiml_str_concat(ptr @25, ptr %49)
  %51 = call ptr @__aiml_str_concat(ptr %50, ptr @26)
  %sg_val = load float, ptr %sg, align 4
  %52 = call ptr @__aiml_f32_to_string(float %sg_val)
  %53 = call ptr @__aiml_str_concat(ptr %51, ptr %52)
  %print_null_check25 = icmp eq ptr %53, null
  %print_safe_str26 = select i1 %print_null_check25, ptr @print_null_str.9, ptr %53
  %print_call27 = call i32 (ptr, ...) @printf(ptr @27, ptr %print_safe_str26)
  %lp = alloca float, align 4
  store float 0.000000e+00, ptr %lp, align 4
  store float 3.000000e+00, ptr %lp, align 4
  %lp_val = load float, ptr %lp, align 4
  %54 = call ptr @__aiml_f32_to_string(float %lp_val)
  %55 = call ptr @__aiml_str_concat(ptr @28, ptr %54)
  %print_null_check28 = icmp eq ptr %55, null
  %print_safe_str29 = select i1 %print_null_check28, ptr @print_null_str.10, ptr %55
  %print_call30 = call i32 (ptr, ...) @printf(ptr @29, ptr %print_safe_str29)
  %md = alloca float, align 4
  store float 0.000000e+00, ptr %md, align 4
  %56 = call float @fmodf(float 7.500000e+00, float 3.000000e+00)
  store float %56, ptr %md, align 4
  %md_val = load float, ptr %md, align 4
  %57 = call ptr @__aiml_f32_to_string(float %md_val)
  %58 = call ptr @__aiml_str_concat(ptr @30, ptr %57)
  %print_null_check31 = icmp eq ptr %58, null
  %print_safe_str32 = select i1 %print_null_check31, ptr @print_null_str.11, ptr %58
  %print_call33 = call i32 (ptr, ...) @printf(ptr @31, ptr %print_safe_str32)
  %sm = alloca float, align 4
  store float 0.000000e+00, ptr %sm, align 4
  store float 5.000000e-01, ptr %sm, align 4
  %sm_val = load float, ptr %sm, align 4
  %59 = call ptr @__aiml_f32_to_string(float %sm_val)
  %60 = call ptr @__aiml_str_concat(ptr @32, ptr %59)
  %print_null_check34 = icmp eq ptr %60, null
  %print_safe_str35 = select i1 %print_null_check34, ptr @print_null_str.12, ptr %60
  %print_call36 = call i32 (ptr, ...) @printf(ptr @33, ptr %print_safe_str35)
  %print_call37 = call i32 (ptr, ...) @printf(ptr @35, ptr @34)
  %v2 = alloca %vec2, align 8
  store %vec2 zeroinitializer, ptr %v2, align 4
  store %vec2 { float 3.000000e+00, float 4.000000e+00 }, ptr %v2, align 4
  %vx = alloca float, align 4
  store float 0.000000e+00, ptr %vx, align 4
  %prop_ptr = getelementptr inbounds %vec2, ptr %v2, i32 0, i32 0
  %x_val = load float, ptr %prop_ptr, align 4
  store float %x_val, ptr %vx, align 4
  %vy = alloca float, align 4
  store float 0.000000e+00, ptr %vy, align 4
  %prop_ptr38 = getelementptr inbounds %vec2, ptr %v2, i32 0, i32 1
  %y_val = load float, ptr %prop_ptr38, align 4
  store float %y_val, ptr %vy, align 4
  %vx_val = load float, ptr %vx, align 4
  %61 = call ptr @__aiml_f32_to_string(float %vx_val)
  %62 = call ptr @__aiml_str_concat(ptr @36, ptr %61)
  %63 = call ptr @__aiml_str_concat(ptr %62, ptr @37)
  %vy_val = load float, ptr %vy, align 4
  %64 = call ptr @__aiml_f32_to_string(float %vy_val)
  %65 = call ptr @__aiml_str_concat(ptr %63, ptr %64)
  %print_null_check39 = icmp eq ptr %65, null
  %print_safe_str40 = select i1 %print_null_check39, ptr @print_null_str.14, ptr %65
  %print_call41 = call i32 (ptr, ...) @printf(ptr @38, ptr %print_safe_str40)
  %set_prop_ptr = getelementptr inbounds %vec2, ptr %v2, i32 0, i32 0
  store float 1.000000e+00, ptr %set_prop_ptr, align 4
  %prop_ptr42 = getelementptr inbounds %vec2, ptr %v2, i32 0, i32 0
  %x_val43 = load float, ptr %prop_ptr42, align 4
  store float %x_val43, ptr %vx, align 4
  %vx_val44 = load float, ptr %vx, align 4
  %66 = call ptr @__aiml_f32_to_string(float %vx_val44)
  %67 = call ptr @__aiml_str_concat(ptr @39, ptr %66)
  %print_null_check45 = icmp eq ptr %67, null
  %print_safe_str46 = select i1 %print_null_check45, ptr @print_null_str.15, ptr %67
  %print_call47 = call i32 (ptr, ...) @printf(ptr @40, ptr %print_safe_str46)
  %va = alloca %vec2, align 8
  store %vec2 zeroinitializer, ptr %va, align 4
  store %vec2 { float 1.000000e+00, float 2.000000e+00 }, ptr %va, align 4
  %vb = alloca %vec2, align 8
  store %vec2 zeroinitializer, ptr %vb, align 4
  store %vec2 { float 3.000000e+00, float 4.000000e+00 }, ptr %vb, align 4
  %vsum = alloca %vec2, align 8
  store %vec2 zeroinitializer, ptr %vsum, align 4
  %va_val = load %vec2, ptr %va, align 4
  %vb_val = load %vec2, ptr %vb, align 4
  %68 = alloca %vec2, align 8
  store %vec2 %va_val, ptr %68, align 4
  %69 = alloca %vec2, align 8
  store %vec2 %vb_val, ptr %69, align 4
  %70 = alloca %vec2, align 8
  call void @aiml_math_vec2_add(ptr %68, ptr %69, ptr %70)
  %71 = load %vec2, ptr %70, align 4
  store %vec2 %71, ptr %vsum, align 4
  %vsumx = alloca float, align 4
  store float 0.000000e+00, ptr %vsumx, align 4
  %prop_ptr48 = getelementptr inbounds %vec2, ptr %vsum, i32 0, i32 0
  %x_val49 = load float, ptr %prop_ptr48, align 4
  store float %x_val49, ptr %vsumx, align 4
  %vsumx_val = load float, ptr %vsumx, align 4
  %72 = call ptr @__aiml_f32_to_string(float %vsumx_val)
  %73 = call ptr @__aiml_str_concat(ptr @41, ptr %72)
  %print_null_check50 = icmp eq ptr %73, null
  %print_safe_str51 = select i1 %print_null_check50, ptr @print_null_str.16, ptr %73
  %print_call52 = call i32 (ptr, ...) @printf(ptr @42, ptr %print_safe_str51)
  %vscaled = alloca %vec2, align 8
  store %vec2 zeroinitializer, ptr %vscaled, align 4
  %va_val53 = load %vec2, ptr %va, align 4
  %74 = alloca %vec2, align 8
  store %vec2 %va_val53, ptr %74, align 4
  %75 = alloca %vec2, align 8
  call void @aiml_math_vec2_scale(ptr %74, float 2.000000e+00, ptr %75)
  %76 = load %vec2, ptr %75, align 4
  store %vec2 %76, ptr %vscaled, align 4
  %vscx = alloca float, align 4
  store float 0.000000e+00, ptr %vscx, align 4
  %prop_ptr54 = getelementptr inbounds %vec2, ptr %vscaled, i32 0, i32 0
  %x_val55 = load float, ptr %prop_ptr54, align 4
  store float %x_val55, ptr %vscx, align 4
  %vscx_val = load float, ptr %vscx, align 4
  %77 = call ptr @__aiml_f32_to_string(float %vscx_val)
  %78 = call ptr @__aiml_str_concat(ptr @43, ptr %77)
  %print_null_check56 = icmp eq ptr %78, null
  %print_safe_str57 = select i1 %print_null_check56, ptr @print_null_str.17, ptr %78
  %print_call58 = call i32 (ptr, ...) @printf(ptr @44, ptr %print_safe_str57)
  %vneg = alloca %vec2, align 8
  store %vec2 zeroinitializer, ptr %vneg, align 4
  %va_val59 = load %vec2, ptr %va, align 4
  %79 = alloca %vec2, align 8
  store %vec2 %va_val59, ptr %79, align 4
  %80 = alloca %vec2, align 8
  call void @aiml_math_vec2_neg(ptr %79, ptr %80)
  %81 = load %vec2, ptr %80, align 4
  store %vec2 %81, ptr %vneg, align 4
  %vnx = alloca float, align 4
  store float 0.000000e+00, ptr %vnx, align 4
  %prop_ptr60 = getelementptr inbounds %vec2, ptr %vneg, i32 0, i32 0
  %x_val61 = load float, ptr %prop_ptr60, align 4
  store float %x_val61, ptr %vnx, align 4
  %vnx_val = load float, ptr %vnx, align 4
  %82 = call ptr @__aiml_f32_to_string(float %vnx_val)
  %83 = call ptr @__aiml_str_concat(ptr @45, ptr %82)
  %print_null_check62 = icmp eq ptr %83, null
  %print_safe_str63 = select i1 %print_null_check62, ptr @print_null_str.18, ptr %83
  %print_call64 = call i32 (ptr, ...) @printf(ptr @46, ptr %print_safe_str63)
  %ang = alloca float, align 4
  store float 0.000000e+00, ptr %ang, align 4
  %va_val65 = load %vec2, ptr %va, align 4
  %mtmp = alloca %vec2, align 8
  store %vec2 %va_val65, ptr %mtmp, align 4
  %84 = call float @aiml_math_vec2_angle(ptr %mtmp)
  store float %84, ptr %ang, align 4
  %ang_val = load float, ptr %ang, align 4
  %85 = call ptr @__aiml_f32_to_string(float %ang_val)
  %86 = call ptr @__aiml_str_concat(ptr @47, ptr %85)
  %print_null_check66 = icmp eq ptr %86, null
  %print_safe_str67 = select i1 %print_null_check66, ptr @print_null_str.19, ptr %86
  %print_call68 = call i32 (ptr, ...) @printf(ptr @48, ptr %print_safe_str67)
  %perp = alloca %vec2, align 8
  store %vec2 zeroinitializer, ptr %perp, align 4
  %va_val69 = load %vec2, ptr %va, align 4
  %87 = alloca %vec2, align 8
  %mtmp70 = alloca %vec2, align 8
  store %vec2 %va_val69, ptr %mtmp70, align 4
  call void @aiml_math_vec2_perp(ptr %mtmp70, ptr %87)
  %88 = load %vec2, ptr %87, align 4
  store %vec2 %88, ptr %perp, align 4
  %perpx = alloca float, align 4
  store float 0.000000e+00, ptr %perpx, align 4
  %prop_ptr71 = getelementptr inbounds %vec2, ptr %perp, i32 0, i32 0
  %x_val72 = load float, ptr %prop_ptr71, align 4
  store float %x_val72, ptr %perpx, align 4
  %perpx_val = load float, ptr %perpx, align 4
  %89 = call ptr @__aiml_f32_to_string(float %perpx_val)
  %90 = call ptr @__aiml_str_concat(ptr @49, ptr %89)
  %print_null_check73 = icmp eq ptr %90, null
  %print_safe_str74 = select i1 %print_null_check73, ptr @print_null_str.20, ptr %90
  %print_call75 = call i32 (ptr, ...) @printf(ptr @50, ptr %print_safe_str74)
  %v34 = alloca %vec2, align 8
  store %vec2 zeroinitializer, ptr %v34, align 4
  store %vec2 { float 3.000000e+00, float 4.000000e+00 }, ptr %v34, align 4
  %vlen = alloca float, align 4
  store float 0.000000e+00, ptr %vlen, align 4
  %v34_val = load %vec2, ptr %v34, align 4
  %mtmp76 = alloca %vec2, align 8
  store %vec2 %v34_val, ptr %mtmp76, align 4
  %91 = call float @aiml_math_vec2_length(ptr %mtmp76)
  store float %91, ptr %vlen, align 4
  %vlen_val = load float, ptr %vlen, align 4
  %92 = call ptr @__aiml_f32_to_string(float %vlen_val)
  %93 = call ptr @__aiml_str_concat(ptr @51, ptr %92)
  %print_null_check77 = icmp eq ptr %93, null
  %print_safe_str78 = select i1 %print_null_check77, ptr @print_null_str.21, ptr %93
  %print_call79 = call i32 (ptr, ...) @printf(ptr @52, ptr %print_safe_str78)
  %print_call80 = call i32 (ptr, ...) @printf(ptr @54, ptr @53)
  %a = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %a, align 4
  store %vec3 { float 1.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00 }, ptr %a, align 4
  %b = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %b, align 4
  store %vec3 { float 0.000000e+00, float 1.000000e+00, float 0.000000e+00, float 0.000000e+00 }, ptr %b, align 4
  %ax = alloca float, align 4
  store float 0.000000e+00, ptr %ax, align 4
  %prop_ptr81 = getelementptr inbounds %vec3, ptr %a, i32 0, i32 0
  %x_val82 = load float, ptr %prop_ptr81, align 4
  store float %x_val82, ptr %ax, align 4
  %ay = alloca float, align 4
  store float 0.000000e+00, ptr %ay, align 4
  %prop_ptr83 = getelementptr inbounds %vec3, ptr %a, i32 0, i32 1
  %y_val84 = load float, ptr %prop_ptr83, align 4
  store float %y_val84, ptr %ay, align 4
  %az = alloca float, align 4
  store float 0.000000e+00, ptr %az, align 4
  %prop_ptr85 = getelementptr inbounds %vec3, ptr %a, i32 0, i32 2
  %z_val = load float, ptr %prop_ptr85, align 4
  store float %z_val, ptr %az, align 4
  %ax_val = load float, ptr %ax, align 4
  %94 = call ptr @__aiml_f32_to_string(float %ax_val)
  %95 = call ptr @__aiml_str_concat(ptr @55, ptr %94)
  %96 = call ptr @__aiml_str_concat(ptr %95, ptr @56)
  %ay_val = load float, ptr %ay, align 4
  %97 = call ptr @__aiml_f32_to_string(float %ay_val)
  %98 = call ptr @__aiml_str_concat(ptr %96, ptr %97)
  %99 = call ptr @__aiml_str_concat(ptr %98, ptr @57)
  %az_val = load float, ptr %az, align 4
  %100 = call ptr @__aiml_f32_to_string(float %az_val)
  %101 = call ptr @__aiml_str_concat(ptr %99, ptr %100)
  %print_null_check86 = icmp eq ptr %101, null
  %print_safe_str87 = select i1 %print_null_check86, ptr @print_null_str.23, ptr %101
  %print_call88 = call i32 (ptr, ...) @printf(ptr @58, ptr %print_safe_str87)
  %set_prop_ptr89 = getelementptr inbounds %vec3, ptr %a, i32 0, i32 0
  store float 2.000000e+00, ptr %set_prop_ptr89, align 4
  %prop_ptr90 = getelementptr inbounds %vec3, ptr %a, i32 0, i32 0
  %x_val91 = load float, ptr %prop_ptr90, align 4
  store float %x_val91, ptr %ax, align 4
  %ax_val92 = load float, ptr %ax, align 4
  %102 = call ptr @__aiml_f32_to_string(float %ax_val92)
  %103 = call ptr @__aiml_str_concat(ptr @59, ptr %102)
  %print_null_check93 = icmp eq ptr %103, null
  %print_safe_str94 = select i1 %print_null_check93, ptr @print_null_str.24, ptr %103
  %print_call95 = call i32 (ptr, ...) @printf(ptr @60, ptr %print_safe_str94)
  %set_prop_ptr96 = getelementptr inbounds %vec3, ptr %a, i32 0, i32 0
  store float 1.000000e+00, ptr %set_prop_ptr96, align 4
  %sum = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %sum, align 4
  %a_val = load %vec3, ptr %a, align 4
  %b_val = load %vec3, ptr %b, align 4
  %104 = alloca %vec3, align 8
  store %vec3 %a_val, ptr %104, align 4
  %105 = alloca %vec3, align 8
  store %vec3 %b_val, ptr %105, align 4
  %106 = alloca %vec3, align 8
  call void @aiml_math_vec3_add(ptr %104, ptr %105, ptr %106)
  %107 = load %vec3, ptr %106, align 4
  store %vec3 %107, ptr %sum, align 4
  %sumx = alloca float, align 4
  store float 0.000000e+00, ptr %sumx, align 4
  %prop_ptr97 = getelementptr inbounds %vec3, ptr %sum, i32 0, i32 0
  %x_val98 = load float, ptr %prop_ptr97, align 4
  store float %x_val98, ptr %sumx, align 4
  %sumy = alloca float, align 4
  store float 0.000000e+00, ptr %sumy, align 4
  %prop_ptr99 = getelementptr inbounds %vec3, ptr %sum, i32 0, i32 1
  %y_val100 = load float, ptr %prop_ptr99, align 4
  store float %y_val100, ptr %sumy, align 4
  %sumx_val = load float, ptr %sumx, align 4
  %108 = call ptr @__aiml_f32_to_string(float %sumx_val)
  %109 = call ptr @__aiml_str_concat(ptr @61, ptr %108)
  %110 = call ptr @__aiml_str_concat(ptr %109, ptr @62)
  %sumy_val = load float, ptr %sumy, align 4
  %111 = call ptr @__aiml_f32_to_string(float %sumy_val)
  %112 = call ptr @__aiml_str_concat(ptr %110, ptr %111)
  %print_null_check101 = icmp eq ptr %112, null
  %print_safe_str102 = select i1 %print_null_check101, ptr @print_null_str.25, ptr %112
  %print_call103 = call i32 (ptr, ...) @printf(ptr @63, ptr %print_safe_str102)
  %diff = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %diff, align 4
  %a_val104 = load %vec3, ptr %a, align 4
  %b_val105 = load %vec3, ptr %b, align 4
  %113 = alloca %vec3, align 8
  store %vec3 %a_val104, ptr %113, align 4
  %114 = alloca %vec3, align 8
  store %vec3 %b_val105, ptr %114, align 4
  %115 = alloca %vec3, align 8
  call void @aiml_math_vec3_sub(ptr %113, ptr %114, ptr %115)
  %116 = load %vec3, ptr %115, align 4
  store %vec3 %116, ptr %diff, align 4
  %difx = alloca float, align 4
  store float 0.000000e+00, ptr %difx, align 4
  %prop_ptr106 = getelementptr inbounds %vec3, ptr %diff, i32 0, i32 0
  %x_val107 = load float, ptr %prop_ptr106, align 4
  store float %x_val107, ptr %difx, align 4
  %difx_val = load float, ptr %difx, align 4
  %117 = call ptr @__aiml_f32_to_string(float %difx_val)
  %118 = call ptr @__aiml_str_concat(ptr @64, ptr %117)
  %print_null_check108 = icmp eq ptr %118, null
  %print_safe_str109 = select i1 %print_null_check108, ptr @print_null_str.26, ptr %118
  %print_call110 = call i32 (ptr, ...) @printf(ptr @65, ptr %print_safe_str109)
  %scaled = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %scaled, align 4
  %a_val111 = load %vec3, ptr %a, align 4
  %119 = alloca %vec3, align 8
  store %vec3 %a_val111, ptr %119, align 4
  %120 = alloca %vec3, align 8
  call void @aiml_math_vec3_scale(ptr %119, float 2.500000e+00, ptr %120)
  %121 = load %vec3, ptr %120, align 4
  store %vec3 %121, ptr %scaled, align 4
  %scalx = alloca float, align 4
  store float 0.000000e+00, ptr %scalx, align 4
  %prop_ptr112 = getelementptr inbounds %vec3, ptr %scaled, i32 0, i32 0
  %x_val113 = load float, ptr %prop_ptr112, align 4
  store float %x_val113, ptr %scalx, align 4
  %scalx_val = load float, ptr %scalx, align 4
  %122 = call ptr @__aiml_f32_to_string(float %scalx_val)
  %123 = call ptr @__aiml_str_concat(ptr @66, ptr %122)
  %print_null_check114 = icmp eq ptr %123, null
  %print_safe_str115 = select i1 %print_null_check114, ptr @print_null_str.27, ptr %123
  %print_call116 = call i32 (ptr, ...) @printf(ptr @67, ptr %print_safe_str115)
  %neg = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %neg, align 4
  %a_val117 = load %vec3, ptr %a, align 4
  %124 = alloca %vec3, align 8
  store %vec3 %a_val117, ptr %124, align 4
  %125 = alloca %vec3, align 8
  call void @aiml_math_vec3_neg(ptr %124, ptr %125)
  %126 = load %vec3, ptr %125, align 4
  store %vec3 %126, ptr %neg, align 4
  %negx = alloca float, align 4
  store float 0.000000e+00, ptr %negx, align 4
  %prop_ptr118 = getelementptr inbounds %vec3, ptr %neg, i32 0, i32 0
  %x_val119 = load float, ptr %prop_ptr118, align 4
  store float %x_val119, ptr %negx, align 4
  %negx_val = load float, ptr %negx, align 4
  %127 = call ptr @__aiml_f32_to_string(float %negx_val)
  %128 = call ptr @__aiml_str_concat(ptr @68, ptr %127)
  %print_null_check120 = icmp eq ptr %128, null
  %print_safe_str121 = select i1 %print_null_check120, ptr @print_null_str.28, ptr %128
  %print_call122 = call i32 (ptr, ...) @printf(ptr @69, ptr %print_safe_str121)
  %divv = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %divv, align 4
  %a_val123 = load %vec3, ptr %a, align 4
  %129 = alloca %vec3, align 8
  store %vec3 %a_val123, ptr %129, align 4
  %130 = alloca %vec3, align 8
  call void @aiml_math_vec3_div(ptr %129, float 2.000000e+00, ptr %130)
  %131 = load %vec3, ptr %130, align 4
  store %vec3 %131, ptr %divv, align 4
  %divx = alloca float, align 4
  store float 0.000000e+00, ptr %divx, align 4
  %prop_ptr124 = getelementptr inbounds %vec3, ptr %divv, i32 0, i32 0
  %x_val125 = load float, ptr %prop_ptr124, align 4
  store float %x_val125, ptr %divx, align 4
  %divx_val = load float, ptr %divx, align 4
  %132 = call ptr @__aiml_f32_to_string(float %divx_val)
  %133 = call ptr @__aiml_str_concat(ptr @70, ptr %132)
  %print_null_check126 = icmp eq ptr %133, null
  %print_safe_str127 = select i1 %print_null_check126, ptr @print_null_str.29, ptr %133
  %print_call128 = call i32 (ptr, ...) @printf(ptr @71, ptr %print_safe_str127)
  %aa = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %aa, align 4
  store %vec3 { float 2.000000e+00, float 3.000000e+00, float 4.000000e+00, float 0.000000e+00 }, ptr %aa, align 4
  %bb = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %bb, align 4
  store %vec3 { float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 0.000000e+00 }, ptr %bb, align 4
  %prod = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %prod, align 4
  %aa_val = load %vec3, ptr %aa, align 4
  %bb_val = load %vec3, ptr %bb, align 4
  %134 = alloca %vec3, align 8
  store %vec3 %aa_val, ptr %134, align 4
  %135 = alloca %vec3, align 8
  store %vec3 %bb_val, ptr %135, align 4
  %136 = alloca %vec3, align 8
  call void @aiml_math_vec3_mul(ptr %134, ptr %135, ptr %136)
  %137 = load %vec3, ptr %136, align 4
  store %vec3 %137, ptr %prod, align 4
  %prodx = alloca float, align 4
  store float 0.000000e+00, ptr %prodx, align 4
  %prop_ptr129 = getelementptr inbounds %vec3, ptr %prod, i32 0, i32 0
  %x_val130 = load float, ptr %prop_ptr129, align 4
  store float %x_val130, ptr %prodx, align 4
  %prodx_val = load float, ptr %prodx, align 4
  %138 = call ptr @__aiml_f32_to_string(float %prodx_val)
  %139 = call ptr @__aiml_str_concat(ptr @72, ptr %138)
  %print_null_check131 = icmp eq ptr %139, null
  %print_safe_str132 = select i1 %print_null_check131, ptr @print_null_str.30, ptr %139
  %print_call133 = call i32 (ptr, ...) @printf(ptr @73, ptr %print_safe_str132)
  %d = alloca float, align 4
  store float 0.000000e+00, ptr %d, align 4
  %a_val134 = load %vec3, ptr %a, align 4
  %b_val135 = load %vec3, ptr %b, align 4
  %mtmp136 = alloca %vec3, align 8
  store %vec3 %a_val134, ptr %mtmp136, align 4
  %mtmp137 = alloca %vec3, align 8
  store %vec3 %b_val135, ptr %mtmp137, align 4
  %140 = call float @aiml_math_vec3_dot(ptr %mtmp136, ptr %mtmp137)
  store float %140, ptr %d, align 4
  %d_val = load float, ptr %d, align 4
  %141 = call ptr @__aiml_f32_to_string(float %d_val)
  %142 = call ptr @__aiml_str_concat(ptr @74, ptr %141)
  %print_null_check138 = icmp eq ptr %142, null
  %print_safe_str139 = select i1 %print_null_check138, ptr @print_null_str.31, ptr %142
  %print_call140 = call i32 (ptr, ...) @printf(ptr @75, ptr %print_safe_str139)
  %cr = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %cr, align 4
  %a_val141 = load %vec3, ptr %a, align 4
  %b_val142 = load %vec3, ptr %b, align 4
  %143 = alloca %vec3, align 8
  %mtmp143 = alloca %vec3, align 8
  store %vec3 %a_val141, ptr %mtmp143, align 4
  %mtmp144 = alloca %vec3, align 8
  store %vec3 %b_val142, ptr %mtmp144, align 4
  call void @aiml_math_vec3_cross(ptr %mtmp143, ptr %mtmp144, ptr %143)
  %144 = load %vec3, ptr %143, align 4
  store %vec3 %144, ptr %cr, align 4
  %crz = alloca float, align 4
  store float 0.000000e+00, ptr %crz, align 4
  %prop_ptr145 = getelementptr inbounds %vec3, ptr %cr, i32 0, i32 2
  %z_val146 = load float, ptr %prop_ptr145, align 4
  store float %z_val146, ptr %crz, align 4
  %crz_val = load float, ptr %crz, align 4
  %145 = call ptr @__aiml_f32_to_string(float %crz_val)
  %146 = call ptr @__aiml_str_concat(ptr @76, ptr %145)
  %print_null_check147 = icmp eq ptr %146, null
  %print_safe_str148 = select i1 %print_null_check147, ptr @print_null_str.32, ptr %146
  %print_call149 = call i32 (ptr, ...) @printf(ptr @77, ptr %print_safe_str148)
  %len = alloca float, align 4
  store float 0.000000e+00, ptr %len, align 4
  %a_val150 = load %vec3, ptr %a, align 4
  %mtmp151 = alloca %vec3, align 8
  store %vec3 %a_val150, ptr %mtmp151, align 4
  %147 = call float @aiml_math_vec3_length(ptr %mtmp151)
  store float %147, ptr %len, align 4
  %len_val = load float, ptr %len, align 4
  %148 = call ptr @__aiml_f32_to_string(float %len_val)
  %149 = call ptr @__aiml_str_concat(ptr @78, ptr %148)
  %print_null_check152 = icmp eq ptr %149, null
  %print_safe_str153 = select i1 %print_null_check152, ptr @print_null_str.33, ptr %149
  %print_call154 = call i32 (ptr, ...) @printf(ptr @79, ptr %print_safe_str153)
  %n = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %n, align 4
  %aa_val155 = load %vec3, ptr %aa, align 4
  %150 = alloca %vec3, align 8
  %mtmp156 = alloca %vec3, align 8
  store %vec3 %aa_val155, ptr %mtmp156, align 4
  call void @aiml_math_vec3_normalize(ptr %mtmp156, ptr %150)
  %151 = load %vec3, ptr %150, align 4
  store %vec3 %151, ptr %n, align 4
  %nx = alloca float, align 4
  store float 0.000000e+00, ptr %nx, align 4
  %prop_ptr157 = getelementptr inbounds %vec3, ptr %n, i32 0, i32 0
  %x_val158 = load float, ptr %prop_ptr157, align 4
  store float %x_val158, ptr %nx, align 4
  %nx_val = load float, ptr %nx, align 4
  %152 = call ptr @__aiml_f32_to_string(float %nx_val)
  %153 = call ptr @__aiml_str_concat(ptr @80, ptr %152)
  %print_null_check159 = icmp eq ptr %153, null
  %print_safe_str160 = select i1 %print_null_check159, ptr @print_null_str.34, ptr %153
  %print_call161 = call i32 (ptr, ...) @printf(ptr @81, ptr %print_safe_str160)
  %lerped = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %lerped, align 4
  %a_val162 = load %vec3, ptr %a, align 4
  %b_val163 = load %vec3, ptr %b, align 4
  %mtmp164 = alloca %vec3, align 8
  store %vec3 %a_val162, ptr %mtmp164, align 4
  %mtmp165 = alloca %vec3, align 8
  store %vec3 %b_val163, ptr %mtmp165, align 4
  %154 = alloca %vec3, align 8
  call void @aiml_math_vec3_lerp(ptr %mtmp164, ptr %mtmp165, float 5.000000e-01, ptr %154)
  %155 = load %vec3, ptr %154, align 4
  store %vec3 %155, ptr %lerped, align 4
  %lerpx = alloca float, align 4
  store float 0.000000e+00, ptr %lerpx, align 4
  %prop_ptr166 = getelementptr inbounds %vec3, ptr %lerped, i32 0, i32 0
  %x_val167 = load float, ptr %prop_ptr166, align 4
  store float %x_val167, ptr %lerpx, align 4
  %lerpy = alloca float, align 4
  store float 0.000000e+00, ptr %lerpy, align 4
  %prop_ptr168 = getelementptr inbounds %vec3, ptr %lerped, i32 0, i32 1
  %y_val169 = load float, ptr %prop_ptr168, align 4
  store float %y_val169, ptr %lerpy, align 4
  %lerpx_val = load float, ptr %lerpx, align 4
  %156 = call ptr @__aiml_f32_to_string(float %lerpx_val)
  %157 = call ptr @__aiml_str_concat(ptr @82, ptr %156)
  %158 = call ptr @__aiml_str_concat(ptr %157, ptr @83)
  %lerpy_val = load float, ptr %lerpy, align 4
  %159 = call ptr @__aiml_f32_to_string(float %lerpy_val)
  %160 = call ptr @__aiml_str_concat(ptr %158, ptr %159)
  %print_null_check170 = icmp eq ptr %160, null
  %print_safe_str171 = select i1 %print_null_check170, ptr @print_null_str.35, ptr %160
  %print_call172 = call i32 (ptr, ...) @printf(ptr @84, ptr %print_safe_str171)
  %dist = alloca float, align 4
  store float 0.000000e+00, ptr %dist, align 4
  %a_val173 = load %vec3, ptr %a, align 4
  %b_val174 = load %vec3, ptr %b, align 4
  %mtmp175 = alloca %vec3, align 8
  store %vec3 %b_val174, ptr %mtmp175, align 4
  %mtmp176 = alloca %vec3, align 8
  store %vec3 %a_val173, ptr %mtmp176, align 4
  %161 = call float @aiml_math_vec3_distance(ptr %mtmp176, ptr %mtmp175)
  store float %161, ptr %dist, align 4
  %dist_val = load float, ptr %dist, align 4
  %162 = call ptr @__aiml_f32_to_string(float %dist_val)
  %163 = call ptr @__aiml_str_concat(ptr @85, ptr %162)
  %print_null_check177 = icmp eq ptr %163, null
  %print_safe_str178 = select i1 %print_null_check177, ptr @print_null_str.36, ptr %163
  %print_call179 = call i32 (ptr, ...) @printf(ptr @86, ptr %print_safe_str178)
  %normal = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %normal, align 4
  store %vec3 { float 0.000000e+00, float 1.000000e+00, float 0.000000e+00, float 0.000000e+00 }, ptr %normal, align 4
  %incident = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %incident, align 4
  store %vec3 { float 1.000000e+00, float -1.000000e+00, float 0.000000e+00, float 0.000000e+00 }, ptr %incident, align 4
  %refl = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %refl, align 4
  %incident_val = load %vec3, ptr %incident, align 4
  %normal_val = load %vec3, ptr %normal, align 4
  %164 = alloca %vec3, align 8
  %mtmp180 = alloca %vec3, align 8
  store %vec3 %incident_val, ptr %mtmp180, align 4
  %mtmp181 = alloca %vec3, align 8
  store %vec3 %normal_val, ptr %mtmp181, align 4
  call void @aiml_math_vec3_reflect(ptr %mtmp180, ptr %mtmp181, ptr %164)
  %165 = load %vec3, ptr %164, align 4
  store %vec3 %165, ptr %refl, align 4
  %reflx = alloca float, align 4
  store float 0.000000e+00, ptr %reflx, align 4
  %prop_ptr182 = getelementptr inbounds %vec3, ptr %refl, i32 0, i32 0
  %x_val183 = load float, ptr %prop_ptr182, align 4
  store float %x_val183, ptr %reflx, align 4
  %refly = alloca float, align 4
  store float 0.000000e+00, ptr %refly, align 4
  %prop_ptr184 = getelementptr inbounds %vec3, ptr %refl, i32 0, i32 1
  %y_val185 = load float, ptr %prop_ptr184, align 4
  store float %y_val185, ptr %refly, align 4
  %reflx_val = load float, ptr %reflx, align 4
  %166 = call ptr @__aiml_f32_to_string(float %reflx_val)
  %167 = call ptr @__aiml_str_concat(ptr @87, ptr %166)
  %168 = call ptr @__aiml_str_concat(ptr %167, ptr @88)
  %refly_val = load float, ptr %refly, align 4
  %169 = call ptr @__aiml_f32_to_string(float %refly_val)
  %170 = call ptr @__aiml_str_concat(ptr %168, ptr %169)
  %print_null_check186 = icmp eq ptr %170, null
  %print_safe_str187 = select i1 %print_null_check186, ptr @print_null_str.37, ptr %170
  %print_call188 = call i32 (ptr, ...) @printf(ptr @89, ptr %print_safe_str187)
  %print_call189 = call i32 (ptr, ...) @printf(ptr @91, ptr @90)
  %col = alloca %vec4, align 8
  store %vec4 zeroinitializer, ptr %col, align 4
  store %vec4 { float 1.000000e+00, float 5.000000e-01, float 0x3FC99999A0000000, float 1.000000e+00 }, ptr %col, align 4
  %colr = alloca float, align 4
  store float 0.000000e+00, ptr %colr, align 4
  %prop_ptr190 = getelementptr inbounds %vec4, ptr %col, i32 0, i32 0
  %x_val191 = load float, ptr %prop_ptr190, align 4
  store float %x_val191, ptr %colr, align 4
  %colg = alloca float, align 4
  store float 0.000000e+00, ptr %colg, align 4
  %prop_ptr192 = getelementptr inbounds %vec4, ptr %col, i32 0, i32 1
  %y_val193 = load float, ptr %prop_ptr192, align 4
  store float %y_val193, ptr %colg, align 4
  %colb = alloca float, align 4
  store float 0.000000e+00, ptr %colb, align 4
  %prop_ptr194 = getelementptr inbounds %vec4, ptr %col, i32 0, i32 2
  %z_val195 = load float, ptr %prop_ptr194, align 4
  store float %z_val195, ptr %colb, align 4
  %cola = alloca float, align 4
  store float 0.000000e+00, ptr %cola, align 4
  %prop_ptr196 = getelementptr inbounds %vec4, ptr %col, i32 0, i32 3
  %w_val = load float, ptr %prop_ptr196, align 4
  store float %w_val, ptr %cola, align 4
  %colr_val = load float, ptr %colr, align 4
  %171 = call ptr @__aiml_f32_to_string(float %colr_val)
  %172 = call ptr @__aiml_str_concat(ptr @92, ptr %171)
  %173 = call ptr @__aiml_str_concat(ptr %172, ptr @93)
  %colg_val = load float, ptr %colg, align 4
  %174 = call ptr @__aiml_f32_to_string(float %colg_val)
  %175 = call ptr @__aiml_str_concat(ptr %173, ptr %174)
  %176 = call ptr @__aiml_str_concat(ptr %175, ptr @94)
  %colb_val = load float, ptr %colb, align 4
  %177 = call ptr @__aiml_f32_to_string(float %colb_val)
  %178 = call ptr @__aiml_str_concat(ptr %176, ptr %177)
  %179 = call ptr @__aiml_str_concat(ptr %178, ptr @95)
  %cola_val = load float, ptr %cola, align 4
  %180 = call ptr @__aiml_f32_to_string(float %cola_val)
  %181 = call ptr @__aiml_str_concat(ptr %179, ptr %180)
  %print_null_check197 = icmp eq ptr %181, null
  %print_safe_str198 = select i1 %print_null_check197, ptr @print_null_str.39, ptr %181
  %print_call199 = call i32 (ptr, ...) @printf(ptr @96, ptr %print_safe_str198)
  %black = alloca %vec4, align 8
  store %vec4 zeroinitializer, ptr %black, align 4
  store %vec4 { float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 1.000000e+00 }, ptr %black, align 4
  %mid = alloca %vec4, align 8
  store %vec4 zeroinitializer, ptr %mid, align 4
  %col_val = load %vec4, ptr %col, align 4
  %black_val = load %vec4, ptr %black, align 4
  %mtmp200 = alloca %vec4, align 8
  store %vec4 %col_val, ptr %mtmp200, align 4
  %mtmp201 = alloca %vec4, align 8
  store %vec4 %black_val, ptr %mtmp201, align 4
  %182 = alloca %vec4, align 8
  call void @aiml_math_vec4_lerp(ptr %mtmp200, ptr %mtmp201, float 5.000000e-01, ptr %182)
  %183 = load %vec4, ptr %182, align 4
  store %vec4 %183, ptr %mid, align 4
  %midr = alloca float, align 4
  store float 0.000000e+00, ptr %midr, align 4
  %prop_ptr202 = getelementptr inbounds %vec4, ptr %mid, i32 0, i32 0
  %x_val203 = load float, ptr %prop_ptr202, align 4
  store float %x_val203, ptr %midr, align 4
  %midr_val = load float, ptr %midr, align 4
  %184 = call ptr @__aiml_f32_to_string(float %midr_val)
  %185 = call ptr @__aiml_str_concat(ptr @97, ptr %184)
  %print_null_check204 = icmp eq ptr %185, null
  %print_safe_str205 = select i1 %print_null_check204, ptr @print_null_str.40, ptr %185
  %print_call206 = call i32 (ptr, ...) @printf(ptr @98, ptr %print_safe_str205)
  %eq = alloca i8, align 1
  store i8 0, ptr %eq, align 1
  %col_val207 = load %vec4, ptr %col, align 4
  %col_val208 = load %vec4, ptr %col, align 4
  %186 = alloca %vec4, align 8
  store %vec4 %col_val207, ptr %186, align 4
  %187 = alloca %vec4, align 8
  store %vec4 %col_val208, ptr %187, align 4
  %188 = call i32 @aiml_math_vec4_eq(ptr %186, ptr %187)
  %189 = icmp ne i32 %188, 0
  store i1 %189, ptr %eq, align 1
  %eq_val = load i8, ptr %eq, align 1
  %190 = icmp ne i8 %eq_val, 0
  %191 = select i1 %190, ptr @100, ptr @101
  %192 = call ptr @__aiml_str_concat(ptr @99, ptr %191)
  %print_null_check209 = icmp eq ptr %192, null
  %print_safe_str210 = select i1 %print_null_check209, ptr @print_null_str.41, ptr %192
  %print_call211 = call i32 (ptr, ...) @printf(ptr @102, ptr %print_safe_str210)
  %print_call212 = call i32 (ptr, ...) @printf(ptr @104, ptr @103)
  %m = alloca %mat4, align 8
  store %mat4 zeroinitializer, ptr %m, align 4
  %mi = alloca %mat4, align 8
  call void @aiml_math_mat4_identity(ptr %mi)
  %mi_val = load %mat4, ptr %mi, align 4
  store %mat4 %mi_val, ptr %m, align 4
  %m00 = alloca float, align 4
  store float 0.000000e+00, ptr %m00, align 4
  %mat4_get = call float @aiml_math_mat4_get(ptr %m, i32 0, i32 0)
  store float %mat4_get, ptr %m00, align 4
  %m11 = alloca float, align 4
  store float 0.000000e+00, ptr %m11, align 4
  %mat4_get213 = call float @aiml_math_mat4_get(ptr %m, i32 1, i32 1)
  store float %mat4_get213, ptr %m11, align 4
  %m00_val = load float, ptr %m00, align 4
  %193 = call ptr @__aiml_f32_to_string(float %m00_val)
  %194 = call ptr @__aiml_str_concat(ptr @105, ptr %193)
  %195 = call ptr @__aiml_str_concat(ptr %194, ptr @106)
  %m11_val = load float, ptr %m11, align 4
  %196 = call ptr @__aiml_f32_to_string(float %m11_val)
  %197 = call ptr @__aiml_str_concat(ptr %195, ptr %196)
  %print_null_check214 = icmp eq ptr %197, null
  %print_safe_str215 = select i1 %print_null_check214, ptr @print_null_str.43, ptr %197
  %print_call216 = call i32 (ptr, ...) @printf(ptr @107, ptr %print_safe_str215)
  call void @aiml_math_mat4_set(ptr %m, i32 0, i32 3, float 5.000000e+00)
  %m03 = alloca float, align 4
  store float 0.000000e+00, ptr %m03, align 4
  %mat4_get217 = call float @aiml_math_mat4_get(ptr %m, i32 0, i32 3)
  store float %mat4_get217, ptr %m03, align 4
  %m03_val = load float, ptr %m03, align 4
  %198 = call ptr @__aiml_f32_to_string(float %m03_val)
  %199 = call ptr @__aiml_str_concat(ptr @108, ptr %198)
  %print_null_check218 = icmp eq ptr %199, null
  %print_safe_str219 = select i1 %print_null_check218, ptr @print_null_str.44, ptr %199
  %print_call220 = call i32 (ptr, ...) @printf(ptr @109, ptr %print_safe_str219)
  %tvec = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %tvec, align 4
  store %vec3 { float 1.000000e+00, float 2.000000e+00, float 0.000000e+00, float 0.000000e+00 }, ptr %tvec, align 4
  %t = alloca %mat4, align 8
  store %mat4 zeroinitializer, ptr %t, align 4
  %tvec_val = load %vec3, ptr %tvec, align 4
  %200 = alloca %mat4, align 8
  %mtmp221 = alloca %vec3, align 8
  store %vec3 %tvec_val, ptr %mtmp221, align 4
  call void @aiml_math_mat4_translate(ptr %mtmp221, ptr %200)
  %201 = load %mat4, ptr %200, align 4
  store %mat4 %201, ptr %t, align 4
  %t03 = alloca float, align 4
  store float 0.000000e+00, ptr %t03, align 4
  %mat4_get222 = call float @aiml_math_mat4_get(ptr %t, i32 0, i32 3)
  store float %mat4_get222, ptr %t03, align 4
  %t03_val = load float, ptr %t03, align 4
  %202 = call ptr @__aiml_f32_to_string(float %t03_val)
  %203 = call ptr @__aiml_str_concat(ptr @110, ptr %202)
  %print_null_check223 = icmp eq ptr %203, null
  %print_safe_str224 = select i1 %print_null_check223, ptr @print_null_str.45, ptr %203
  %print_call225 = call i32 (ptr, ...) @printf(ptr @111, ptr %print_safe_str224)
  %svec = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %svec, align 4
  store %vec3 { float 2.000000e+00, float 2.000000e+00, float 2.000000e+00, float 0.000000e+00 }, ptr %svec, align 4
  %s226 = alloca %mat4, align 8
  store %mat4 zeroinitializer, ptr %s226, align 4
  %svec_val = load %vec3, ptr %svec, align 4
  %204 = alloca %mat4, align 8
  %mtmp227 = alloca %vec3, align 8
  store %vec3 %svec_val, ptr %mtmp227, align 4
  call void @aiml_math_mat4_scale(ptr %mtmp227, ptr %204)
  %205 = load %mat4, ptr %204, align 4
  store %mat4 %205, ptr %s226, align 4
  %s00 = alloca float, align 4
  store float 0.000000e+00, ptr %s00, align 4
  %mat4_get228 = call float @aiml_math_mat4_get(ptr %s226, i32 0, i32 0)
  store float %mat4_get228, ptr %s00, align 4
  %s00_val = load float, ptr %s00, align 4
  %206 = call ptr @__aiml_f32_to_string(float %s00_val)
  %207 = call ptr @__aiml_str_concat(ptr @112, ptr %206)
  %print_null_check229 = icmp eq ptr %207, null
  %print_safe_str230 = select i1 %print_null_check229, ptr @print_null_str.46, ptr %207
  %print_call231 = call i32 (ptr, ...) @printf(ptr @113, ptr %print_safe_str230)
  %ry = alloca %mat4, align 8
  store %mat4 zeroinitializer, ptr %ry, align 4
  %208 = alloca %mat4, align 8
  call void @aiml_math_mat4_rotate_y(float 0x3FF921FB60000000, ptr %208)
  %209 = load %mat4, ptr %208, align 4
  store %mat4 %209, ptr %ry, align 4
  %ry00 = alloca float, align 4
  store float 0.000000e+00, ptr %ry00, align 4
  %mat4_get232 = call float @aiml_math_mat4_get(ptr %ry, i32 0, i32 0)
  store float %mat4_get232, ptr %ry00, align 4
  %ry00_val = load float, ptr %ry00, align 4
  %210 = call ptr @__aiml_f32_to_string(float %ry00_val)
  %211 = call ptr @__aiml_str_concat(ptr @114, ptr %210)
  %print_null_check233 = icmp eq ptr %211, null
  %print_safe_str234 = select i1 %print_null_check233, ptr @print_null_str.47, ptr %211
  %print_call235 = call i32 (ptr, ...) @printf(ptr @115, ptr %print_safe_str234)
  %ts = alloca %mat4, align 8
  store %mat4 zeroinitializer, ptr %ts, align 4
  %t_val = load %mat4, ptr %t, align 4
  %s_val236 = load %mat4, ptr %s226, align 4
  %212 = alloca %mat4, align 8
  store %mat4 %t_val, ptr %212, align 4
  %213 = alloca %mat4, align 8
  store %mat4 %s_val236, ptr %213, align 4
  %214 = alloca %mat4, align 8
  call void @aiml_math_mat4_mul(ptr %212, ptr %213, ptr %214)
  %215 = load %mat4, ptr %214, align 4
  store %mat4 %215, ptr %ts, align 4
  %ts00 = alloca float, align 4
  store float 0.000000e+00, ptr %ts00, align 4
  %mat4_get237 = call float @aiml_math_mat4_get(ptr %ts, i32 0, i32 0)
  store float %mat4_get237, ptr %ts00, align 4
  %ts00_val = load float, ptr %ts00, align 4
  %216 = call ptr @__aiml_f32_to_string(float %ts00_val)
  %217 = call ptr @__aiml_str_concat(ptr @116, ptr %216)
  %print_null_check238 = icmp eq ptr %217, null
  %print_safe_str239 = select i1 %print_null_check238, ptr @print_null_str.48, ptr %217
  %print_call240 = call i32 (ptr, ...) @printf(ptr @117, ptr %print_safe_str239)
  %pos = alloca %vec4, align 8
  store %vec4 zeroinitializer, ptr %pos, align 4
  store %vec4 { float 0.000000e+00, float 0.000000e+00, float 0.000000e+00, float 1.000000e+00 }, ptr %pos, align 4
  %world = alloca %vec4, align 8
  store %vec4 zeroinitializer, ptr %world, align 4
  %t_val241 = load %mat4, ptr %t, align 4
  %pos_val = load %vec4, ptr %pos, align 4
  %218 = alloca %mat4, align 8
  store %mat4 %t_val241, ptr %218, align 4
  %219 = alloca %vec4, align 8
  store %vec4 %pos_val, ptr %219, align 4
  %220 = alloca %vec4, align 8
  call void @aiml_math_mat4_mul_vec4(ptr %218, ptr %219, ptr %220)
  %221 = load %vec4, ptr %220, align 4
  store %vec4 %221, ptr %world, align 4
  %wx = alloca float, align 4
  store float 0.000000e+00, ptr %wx, align 4
  %prop_ptr242 = getelementptr inbounds %vec4, ptr %world, i32 0, i32 0
  %x_val243 = load float, ptr %prop_ptr242, align 4
  store float %x_val243, ptr %wx, align 4
  %wx_val = load float, ptr %wx, align 4
  %222 = call ptr @__aiml_f32_to_string(float %wx_val)
  %223 = call ptr @__aiml_str_concat(ptr @118, ptr %222)
  %print_null_check244 = icmp eq ptr %223, null
  %print_safe_str245 = select i1 %print_null_check244, ptr @print_null_str.49, ptr %223
  %print_call246 = call i32 (ptr, ...) @printf(ptr @119, ptr %print_safe_str245)
  %proj = alloca %mat4, align 8
  store %mat4 zeroinitializer, ptr %proj, align 4
  %224 = alloca %mat4, align 8
  call void @aiml_math_mat4_perspective(float 0x3FF0C15240000000, float 0x3FFC6E9780000000, float 0x3FB99999A0000000, float 1.000000e+03, ptr %224)
  %225 = load %mat4, ptr %224, align 4
  store %mat4 %225, ptr %proj, align 4
  %p11 = alloca float, align 4
  store float 0.000000e+00, ptr %p11, align 4
  %mat4_get247 = call float @aiml_math_mat4_get(ptr %proj, i32 1, i32 1)
  store float %mat4_get247, ptr %p11, align 4
  %p11_val = load float, ptr %p11, align 4
  %226 = call ptr @__aiml_f32_to_string(float %p11_val)
  %227 = call ptr @__aiml_str_concat(ptr @120, ptr %226)
  %print_null_check248 = icmp eq ptr %227, null
  %print_safe_str249 = select i1 %print_null_check248, ptr @print_null_str.50, ptr %227
  %print_call250 = call i32 (ptr, ...) @printf(ptr @121, ptr %print_safe_str249)
  %eye = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %eye, align 4
  store %vec3 { float 0.000000e+00, float 0.000000e+00, float 5.000000e+00, float 0.000000e+00 }, ptr %eye, align 4
  %center = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %center, align 4
  store %vec3 zeroinitializer, ptr %center, align 4
  %up = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %up, align 4
  store %vec3 { float 0.000000e+00, float 1.000000e+00, float 0.000000e+00, float 0.000000e+00 }, ptr %up, align 4
  %view = alloca %mat4, align 8
  store %mat4 zeroinitializer, ptr %view, align 4
  %eye_val = load %vec3, ptr %eye, align 4
  %center_val = load %vec3, ptr %center, align 4
  %up_val = load %vec3, ptr %up, align 4
  %228 = alloca %mat4, align 8
  %mtmp251 = alloca %vec3, align 8
  store %vec3 %eye_val, ptr %mtmp251, align 4
  %mtmp252 = alloca %vec3, align 8
  store %vec3 %center_val, ptr %mtmp252, align 4
  %mtmp253 = alloca %vec3, align 8
  store %vec3 %up_val, ptr %mtmp253, align 4
  call void @aiml_math_mat4_lookat(ptr %mtmp251, ptr %mtmp252, ptr %mtmp253, ptr %228)
  %229 = load %mat4, ptr %228, align 4
  store %mat4 %229, ptr %view, align 4
  %v22 = alloca float, align 4
  store float 0.000000e+00, ptr %v22, align 4
  %mat4_get254 = call float @aiml_math_mat4_get(ptr %view, i32 2, i32 2)
  store float %mat4_get254, ptr %v22, align 4
  %v22_val = load float, ptr %v22, align 4
  %230 = call ptr @__aiml_f32_to_string(float %v22_val)
  %231 = call ptr @__aiml_str_concat(ptr @122, ptr %230)
  %print_null_check255 = icmp eq ptr %231, null
  %print_safe_str256 = select i1 %print_null_check255, ptr @print_null_str.51, ptr %231
  %print_call257 = call i32 (ptr, ...) @printf(ptr @123, ptr %print_safe_str256)
  %inv = alloca %mat4, align 8
  store %mat4 zeroinitializer, ptr %inv, align 4
  %view_val = load %mat4, ptr %view, align 4
  %232 = alloca %mat4, align 8
  %mtmp258 = alloca %mat4, align 8
  store %mat4 %view_val, ptr %mtmp258, align 4
  call void @aiml_math_mat4_inverse(ptr %mtmp258, ptr %232)
  %233 = load %mat4, ptr %232, align 4
  store %mat4 %233, ptr %inv, align 4
  %inv00 = alloca float, align 4
  store float 0.000000e+00, ptr %inv00, align 4
  %mat4_get259 = call float @aiml_math_mat4_get(ptr %inv, i32 0, i32 0)
  store float %mat4_get259, ptr %inv00, align 4
  %inv00_val = load float, ptr %inv00, align 4
  %234 = call ptr @__aiml_f32_to_string(float %inv00_val)
  %235 = call ptr @__aiml_str_concat(ptr @124, ptr %234)
  %print_null_check260 = icmp eq ptr %235, null
  %print_safe_str261 = select i1 %print_null_check260, ptr @print_null_str.52, ptr %235
  %print_call262 = call i32 (ptr, ...) @printf(ptr @125, ptr %print_safe_str261)
  %tran = alloca %mat4, align 8
  store %mat4 zeroinitializer, ptr %tran, align 4
  %proj_val = load %mat4, ptr %proj, align 4
  %236 = alloca %mat4, align 8
  %mtmp263 = alloca %mat4, align 8
  store %mat4 %proj_val, ptr %mtmp263, align 4
  call void @aiml_math_mat4_transpose(ptr %mtmp263, ptr %236)
  %237 = load %mat4, ptr %236, align 4
  store %mat4 %237, ptr %tran, align 4
  %tran01 = alloca float, align 4
  store float 0.000000e+00, ptr %tran01, align 4
  %mat4_get264 = call float @aiml_math_mat4_get(ptr %tran, i32 0, i32 1)
  store float %mat4_get264, ptr %tran01, align 4
  %tran01_val = load float, ptr %tran01, align 4
  %238 = call ptr @__aiml_f32_to_string(float %tran01_val)
  %239 = call ptr @__aiml_str_concat(ptr @126, ptr %238)
  %print_null_check265 = icmp eq ptr %239, null
  %print_safe_str266 = select i1 %print_null_check265, ptr @print_null_str.53, ptr %239
  %print_call267 = call i32 (ptr, ...) @printf(ptr @127, ptr %print_safe_str266)
  %buf = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %buf, align 8
  %proj_val268 = load %mat4, ptr %proj, align 4
  %mtmp269 = alloca %mat4, align 8
  store %mat4 %proj_val268, ptr %mtmp269, align 4
  %240 = call ptr @aiml_math_mat4_to_buffer(ptr %mtmp269)
  %241 = call ptr @memcpy(ptr %buf, ptr %240, i64 24)
  %print_call270 = call i32 (ptr, ...) @printf(ptr @129, ptr @128)
  %print_call271 = call i32 (ptr, ...) @printf(ptr @131, ptr @130)
  %q_id = alloca %quat, align 8
  store %quat zeroinitializer, ptr %q_id, align 4
  %qi = alloca %quat, align 8
  call void @aiml_math_quat_identity(ptr %qi)
  %qi_val = load %quat, ptr %qi, align 4
  store %quat %qi_val, ptr %q_id, align 4
  %qw = alloca float, align 4
  store float 0.000000e+00, ptr %qw, align 4
  %prop_ptr272 = getelementptr inbounds %quat, ptr %q_id, i32 0, i32 3
  %w_val273 = load float, ptr %prop_ptr272, align 4
  store float %w_val273, ptr %qw, align 4
  %qw_val = load float, ptr %qw, align 4
  %242 = call ptr @__aiml_f32_to_string(float %qw_val)
  %243 = call ptr @__aiml_str_concat(ptr @132, ptr %242)
  %print_null_check274 = icmp eq ptr %243, null
  %print_safe_str275 = select i1 %print_null_check274, ptr @print_null_str.56, ptr %243
  %print_call276 = call i32 (ptr, ...) @printf(ptr @133, ptr %print_safe_str275)
  %yaxis = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %yaxis, align 4
  store %vec3 { float 0.000000e+00, float 1.000000e+00, float 0.000000e+00, float 0.000000e+00 }, ptr %yaxis, align 4
  %q = alloca %quat, align 8
  store %quat zeroinitializer, ptr %q, align 4
  %yaxis_val = load %vec3, ptr %yaxis, align 4
  %244 = alloca %quat, align 8
  %mtmp277 = alloca %vec3, align 8
  store %vec3 %yaxis_val, ptr %mtmp277, align 4
  call void @aiml_math_quat_axis_angle(ptr %mtmp277, float 0x3FF921FB60000000, ptr %244)
  %245 = load %quat, ptr %244, align 4
  store %quat %245, ptr %q, align 4
  %qy = alloca float, align 4
  store float 0.000000e+00, ptr %qy, align 4
  %prop_ptr278 = getelementptr inbounds %quat, ptr %q, i32 0, i32 1
  %y_val279 = load float, ptr %prop_ptr278, align 4
  store float %y_val279, ptr %qy, align 4
  %qy_val = load float, ptr %qy, align 4
  %246 = call ptr @__aiml_f32_to_string(float %qy_val)
  %247 = call ptr @__aiml_str_concat(ptr @134, ptr %246)
  %print_null_check280 = icmp eq ptr %247, null
  %print_safe_str281 = select i1 %print_null_check280, ptr @print_null_str.57, ptr %247
  %print_call282 = call i32 (ptr, ...) @printf(ptr @135, ptr %print_safe_str281)
  %q2 = alloca %quat, align 8
  store %quat zeroinitializer, ptr %q2, align 4
  %248 = alloca %quat, align 8
  call void @aiml_math_quat_euler(float 0.000000e+00, float 0x3FE921FB60000000, float 0.000000e+00, ptr %248)
  %249 = load %quat, ptr %248, align 4
  store %quat %249, ptr %q2, align 4
  %q2y = alloca float, align 4
  store float 0.000000e+00, ptr %q2y, align 4
  %prop_ptr283 = getelementptr inbounds %quat, ptr %q2, i32 0, i32 1
  %y_val284 = load float, ptr %prop_ptr283, align 4
  store float %y_val284, ptr %q2y, align 4
  %q2y_val = load float, ptr %q2y, align 4
  %250 = call ptr @__aiml_f32_to_string(float %q2y_val)
  %251 = call ptr @__aiml_str_concat(ptr @136, ptr %250)
  %print_null_check285 = icmp eq ptr %251, null
  %print_safe_str286 = select i1 %print_null_check285, ptr @print_null_str.58, ptr %251
  %print_call287 = call i32 (ptr, ...) @printf(ptr @137, ptr %print_safe_str286)
  %combined = alloca %quat, align 8
  store %quat zeroinitializer, ptr %combined, align 4
  %q_val = load %quat, ptr %q, align 4
  %q2_val = load %quat, ptr %q2, align 4
  %252 = alloca %quat, align 8
  %mtmp288 = alloca %quat, align 8
  store %quat %q_val, ptr %mtmp288, align 4
  %mtmp289 = alloca %quat, align 8
  store %quat %q2_val, ptr %mtmp289, align 4
  call void @aiml_math_quat_mul(ptr %mtmp288, ptr %mtmp289, ptr %252)
  %253 = load %quat, ptr %252, align 4
  store %quat %253, ptr %combined, align 4
  %cw = alloca float, align 4
  store float 0.000000e+00, ptr %cw, align 4
  %prop_ptr290 = getelementptr inbounds %quat, ptr %combined, i32 0, i32 3
  %w_val291 = load float, ptr %prop_ptr290, align 4
  store float %w_val291, ptr %cw, align 4
  %cw_val = load float, ptr %cw, align 4
  %254 = call ptr @__aiml_f32_to_string(float %cw_val)
  %255 = call ptr @__aiml_str_concat(ptr @138, ptr %254)
  %print_null_check292 = icmp eq ptr %255, null
  %print_safe_str293 = select i1 %print_null_check292, ptr @print_null_str.59, ptr %255
  %print_call294 = call i32 (ptr, ...) @printf(ptr @139, ptr %print_safe_str293)
  %qn = alloca %quat, align 8
  store %quat zeroinitializer, ptr %qn, align 4
  %combined_val = load %quat, ptr %combined, align 4
  %256 = alloca %quat, align 8
  %mtmp295 = alloca %quat, align 8
  store %quat %combined_val, ptr %mtmp295, align 4
  call void @aiml_math_quat_normalize(ptr %mtmp295, ptr %256)
  %257 = load %quat, ptr %256, align 4
  store %quat %257, ptr %qn, align 4
  %qnw = alloca float, align 4
  store float 0.000000e+00, ptr %qnw, align 4
  %prop_ptr296 = getelementptr inbounds %quat, ptr %qn, i32 0, i32 3
  %w_val297 = load float, ptr %prop_ptr296, align 4
  store float %w_val297, ptr %qnw, align 4
  %qnw_val = load float, ptr %qnw, align 4
  %258 = call ptr @__aiml_f32_to_string(float %qnw_val)
  %259 = call ptr @__aiml_str_concat(ptr @140, ptr %258)
  %print_null_check298 = icmp eq ptr %259, null
  %print_safe_str299 = select i1 %print_null_check298, ptr @print_null_str.60, ptr %259
  %print_call300 = call i32 (ptr, ...) @printf(ptr @141, ptr %print_safe_str299)
  %qi301 = alloca %quat, align 8
  store %quat zeroinitializer, ptr %qi301, align 4
  %q_val302 = load %quat, ptr %q, align 4
  %260 = alloca %quat, align 8
  %mtmp303 = alloca %quat, align 8
  store %quat %q_val302, ptr %mtmp303, align 4
  call void @aiml_math_quat_inverse(ptr %mtmp303, ptr %260)
  %261 = load %quat, ptr %260, align 4
  store %quat %261, ptr %qi301, align 4
  %qiy = alloca float, align 4
  store float 0.000000e+00, ptr %qiy, align 4
  %prop_ptr304 = getelementptr inbounds %quat, ptr %qi301, i32 0, i32 1
  %y_val305 = load float, ptr %prop_ptr304, align 4
  store float %y_val305, ptr %qiy, align 4
  %qiy_val = load float, ptr %qiy, align 4
  %262 = call ptr @__aiml_f32_to_string(float %qiy_val)
  %263 = call ptr @__aiml_str_concat(ptr @142, ptr %262)
  %print_null_check306 = icmp eq ptr %263, null
  %print_safe_str307 = select i1 %print_null_check306, ptr @print_null_str.61, ptr %263
  %print_call308 = call i32 (ptr, ...) @printf(ptr @143, ptr %print_safe_str307)
  %xvec = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %xvec, align 4
  store %vec3 { float 1.000000e+00, float 0.000000e+00, float 0.000000e+00, float 0.000000e+00 }, ptr %xvec, align 4
  %rotated = alloca %vec3, align 8
  store %vec3 zeroinitializer, ptr %rotated, align 4
  %q_val309 = load %quat, ptr %q, align 4
  %xvec_val = load %vec3, ptr %xvec, align 4
  %264 = alloca %vec3, align 8
  %mtmp310 = alloca %quat, align 8
  store %quat %q_val309, ptr %mtmp310, align 4
  %mtmp311 = alloca %vec3, align 8
  store %vec3 %xvec_val, ptr %mtmp311, align 4
  call void @aiml_math_quat_rotate_vec3(ptr %mtmp310, ptr %mtmp311, ptr %264)
  %265 = load %vec3, ptr %264, align 4
  store %vec3 %265, ptr %rotated, align 4
  %rz = alloca float, align 4
  store float 0.000000e+00, ptr %rz, align 4
  %prop_ptr312 = getelementptr inbounds %vec3, ptr %rotated, i32 0, i32 2
  %z_val313 = load float, ptr %prop_ptr312, align 4
  store float %z_val313, ptr %rz, align 4
  %rz_val = load float, ptr %rz, align 4
  %266 = call ptr @__aiml_f32_to_string(float %rz_val)
  %267 = call ptr @__aiml_str_concat(ptr @144, ptr %266)
  %print_null_check314 = icmp eq ptr %267, null
  %print_safe_str315 = select i1 %print_null_check314, ptr @print_null_str.62, ptr %267
  %print_call316 = call i32 (ptr, ...) @printf(ptr @145, ptr %print_safe_str315)
  %slerped = alloca %quat, align 8
  store %quat zeroinitializer, ptr %slerped, align 4
  %q_id_val = load %quat, ptr %q_id, align 4
  %q_val317 = load %quat, ptr %q, align 4
  %268 = alloca %quat, align 8
  %mtmp318 = alloca %quat, align 8
  store %quat %q_id_val, ptr %mtmp318, align 4
  %mtmp319 = alloca %quat, align 8
  store %quat %q_val317, ptr %mtmp319, align 4
  call void @aiml_math_quat_slerp(ptr %mtmp318, ptr %mtmp319, float 5.000000e-01, ptr %268)
  %269 = load %quat, ptr %268, align 4
  store %quat %269, ptr %slerped, align 4
  %slw = alloca float, align 4
  store float 0.000000e+00, ptr %slw, align 4
  %prop_ptr320 = getelementptr inbounds %quat, ptr %slerped, i32 0, i32 3
  %w_val321 = load float, ptr %prop_ptr320, align 4
  store float %w_val321, ptr %slw, align 4
  %slw_val = load float, ptr %slw, align 4
  %270 = call ptr @__aiml_f32_to_string(float %slw_val)
  %271 = call ptr @__aiml_str_concat(ptr @146, ptr %270)
  %print_null_check322 = icmp eq ptr %271, null
  %print_safe_str323 = select i1 %print_null_check322, ptr @print_null_str.63, ptr %271
  %print_call324 = call i32 (ptr, ...) @printf(ptr @147, ptr %print_safe_str323)
  %rot_mat = alloca %mat4, align 8
  store %mat4 zeroinitializer, ptr %rot_mat, align 4
  %q_val325 = load %quat, ptr %q, align 4
  %272 = alloca %mat4, align 8
  %mtmp326 = alloca %quat, align 8
  store %quat %q_val325, ptr %mtmp326, align 4
  call void @aiml_math_quat_to_mat4(ptr %mtmp326, ptr %272)
  %273 = load %mat4, ptr %272, align 4
  store %mat4 %273, ptr %rot_mat, align 4
  %rm00 = alloca float, align 4
  store float 0.000000e+00, ptr %rm00, align 4
  %mat4_get327 = call float @aiml_math_mat4_get(ptr %rot_mat, i32 0, i32 0)
  store float %mat4_get327, ptr %rm00, align 4
  %rm00_val = load float, ptr %rm00, align 4
  %274 = call ptr @__aiml_f32_to_string(float %rm00_val)
  %275 = call ptr @__aiml_str_concat(ptr @148, ptr %274)
  %print_null_check328 = icmp eq ptr %275, null
  %print_safe_str329 = select i1 %print_null_check328, ptr @print_null_str.64, ptr %275
  %print_call330 = call i32 (ptr, ...) @printf(ptr @149, ptr %print_safe_str329)
  %back = alloca %quat, align 8
  store %quat zeroinitializer, ptr %back, align 4
  %rot_mat_val = load %mat4, ptr %rot_mat, align 4
  %276 = alloca %quat, align 8
  %mtmp331 = alloca %mat4, align 8
  store %mat4 %rot_mat_val, ptr %mtmp331, align 4
  call void @aiml_math_mat4_to_quat(ptr %mtmp331, ptr %276)
  %277 = load %quat, ptr %276, align 4
  store %quat %277, ptr %back, align 4
  %bw = alloca float, align 4
  store float 0.000000e+00, ptr %bw, align 4
  %prop_ptr332 = getelementptr inbounds %quat, ptr %back, i32 0, i32 3
  %w_val333 = load float, ptr %prop_ptr332, align 4
  store float %w_val333, ptr %bw, align 4
  %bw_val = load float, ptr %bw, align 4
  %278 = call ptr @__aiml_f32_to_string(float %bw_val)
  %279 = call ptr @__aiml_str_concat(ptr @150, ptr %278)
  %print_null_check334 = icmp eq ptr %279, null
  %print_safe_str335 = select i1 %print_null_check334, ptr @print_null_str.65, ptr %279
  %print_call336 = call i32 (ptr, ...) @printf(ptr @151, ptr %print_safe_str335)
  %ql = alloca %quat, align 8
  store %quat zeroinitializer, ptr %ql, align 4
  %q_id_val337 = load %quat, ptr %q_id, align 4
  %q_val338 = load %quat, ptr %q, align 4
  %mtmp339 = alloca %quat, align 8
  store %quat %q_id_val337, ptr %mtmp339, align 4
  %mtmp340 = alloca %quat, align 8
  store %quat %q_val338, ptr %mtmp340, align 4
  %280 = alloca %quat, align 8
  call void @aiml_math_quat_lerp(ptr %mtmp339, ptr %mtmp340, float 5.000000e-01, ptr %280)
  %281 = load %quat, ptr %280, align 4
  store %quat %281, ptr %ql, align 4
  %qlw = alloca float, align 4
  store float 0.000000e+00, ptr %qlw, align 4
  %prop_ptr341 = getelementptr inbounds %quat, ptr %ql, i32 0, i32 3
  %w_val342 = load float, ptr %prop_ptr341, align 4
  store float %w_val342, ptr %qlw, align 4
  %qlw_val = load float, ptr %qlw, align 4
  %282 = call ptr @__aiml_f32_to_string(float %qlw_val)
  %283 = call ptr @__aiml_str_concat(ptr @152, ptr %282)
  %print_null_check343 = icmp eq ptr %283, null
  %print_safe_str344 = select i1 %print_null_check343, ptr @print_null_str.66, ptr %283
  %print_call345 = call i32 (ptr, ...) @printf(ptr @153, ptr %print_safe_str344)
  %print_call346 = call i32 (ptr, ...) @printf(ptr @155, ptr @154)
  %screen = alloca %ivec2, align 8
  store %ivec2 zeroinitializer, ptr %screen, align 4
  store %ivec2 { i32 1920, i32 1080 }, ptr %screen, align 4
  %sw = alloca i32, align 4
  store i32 0, ptr %sw, align 4
  %prop_ptr347 = getelementptr inbounds %ivec2, ptr %screen, i32 0, i32 0
  %x_val348 = load i32, ptr %prop_ptr347, align 4
  store i32 %x_val348, ptr %sw, align 4
  %sh = alloca i32, align 4
  store i32 0, ptr %sh, align 4
  %prop_ptr349 = getelementptr inbounds %ivec2, ptr %screen, i32 0, i32 1
  %y_val350 = load i32, ptr %prop_ptr349, align 4
  store i32 %y_val350, ptr %sh, align 4
  %sw_val = load i32, ptr %sw, align 4
  %284 = call ptr @__aiml_int_to_string(i32 %sw_val)
  %285 = call ptr @__aiml_str_concat(ptr @156, ptr %284)
  %286 = call ptr @__aiml_str_concat(ptr %285, ptr @157)
  %sh_val = load i32, ptr %sh, align 4
  %287 = call ptr @__aiml_int_to_string(i32 %sh_val)
  %288 = call ptr @__aiml_str_concat(ptr %286, ptr %287)
  %print_null_check351 = icmp eq ptr %288, null
  %print_safe_str352 = select i1 %print_null_check351, ptr @print_null_str.68, ptr %288
  %print_call353 = call i32 (ptr, ...) @printf(ptr @158, ptr %print_safe_str352)
  %tile = alloca %ivec3, align 8
  store %ivec3 zeroinitializer, ptr %tile, align 4
  store %ivec3 { i32 5, i32 0, i32 3, i32 0 }, ptr %tile, align 4
  %tx = alloca i32, align 4
  store i32 0, ptr %tx, align 4
  %prop_ptr354 = getelementptr inbounds %ivec3, ptr %tile, i32 0, i32 0
  %x_val355 = load i32, ptr %prop_ptr354, align 4
  store i32 %x_val355, ptr %tx, align 4
  %tz = alloca i32, align 4
  store i32 0, ptr %tz, align 4
  %prop_ptr356 = getelementptr inbounds %ivec3, ptr %tile, i32 0, i32 2
  %z_val357 = load i32, ptr %prop_ptr356, align 4
  store i32 %z_val357, ptr %tz, align 4
  %tx_val = load i32, ptr %tx, align 4
  %289 = call ptr @__aiml_int_to_string(i32 %tx_val)
  %290 = call ptr @__aiml_str_concat(ptr @159, ptr %289)
  %291 = call ptr @__aiml_str_concat(ptr %290, ptr @160)
  %tz_val = load i32, ptr %tz, align 4
  %292 = call ptr @__aiml_int_to_string(i32 %tz_val)
  %293 = call ptr @__aiml_str_concat(ptr %291, ptr %292)
  %print_null_check358 = icmp eq ptr %293, null
  %print_safe_str359 = select i1 %print_null_check358, ptr @print_null_str.69, ptr %293
  %print_call360 = call i32 (ptr, ...) @printf(ptr @161, ptr %print_safe_str359)
  %half = alloca %ivec2, align 8
  store %ivec2 zeroinitializer, ptr %half, align 4
  %screen_val = load %ivec2, ptr %screen, align 4
  %294 = alloca %ivec2, align 8
  %mtmp361 = alloca %ivec2, align 8
  store %ivec2 %screen_val, ptr %mtmp361, align 4
  call void @aiml_math_ivec2_div(ptr %mtmp361, i32 2, ptr %294)
  %295 = load %ivec2, ptr %294, align 4
  store %ivec2 %295, ptr %half, align 4
  %hx = alloca i32, align 4
  store i32 0, ptr %hx, align 4
  %prop_ptr362 = getelementptr inbounds %ivec2, ptr %half, i32 0, i32 0
  %x_val363 = load i32, ptr %prop_ptr362, align 4
  store i32 %x_val363, ptr %hx, align 4
  %hx_val = load i32, ptr %hx, align 4
  %296 = call ptr @__aiml_int_to_string(i32 %hx_val)
  %297 = call ptr @__aiml_str_concat(ptr @162, ptr %296)
  %print_null_check364 = icmp eq ptr %297, null
  %print_safe_str365 = select i1 %print_null_check364, ptr @print_null_str.70, ptr %297
  %print_call366 = call i32 (ptr, ...) @printf(ptr @163, ptr %print_safe_str365)
  %sum2 = alloca %ivec2, align 8
  store %ivec2 zeroinitializer, ptr %sum2, align 4
  %half_val = load %ivec2, ptr %half, align 4
  %298 = alloca %ivec2, align 8
  %mtmp367 = alloca %ivec2, align 8
  store %ivec2 %half_val, ptr %mtmp367, align 4
  %mtmp368 = alloca %ivec2, align 8
  store %ivec2 { i32 10, i32 10 }, ptr %mtmp368, align 4
  call void @aiml_math_ivec2_add(ptr %mtmp367, ptr %mtmp368, ptr %298)
  %299 = load %ivec2, ptr %298, align 4
  store %ivec2 %299, ptr %sum2, align 4
  %sumx369 = alloca i32, align 4
  store i32 0, ptr %sumx369, align 4
  %prop_ptr370 = getelementptr inbounds %ivec2, ptr %sum2, i32 0, i32 0
  %x_val371 = load i32, ptr %prop_ptr370, align 4
  store i32 %x_val371, ptr %sumx369, align 4
  %sumx_val372 = load i32, ptr %sumx369, align 4
  %300 = call ptr @__aiml_int_to_string(i32 %sumx_val372)
  %301 = call ptr @__aiml_str_concat(ptr @164, ptr %300)
  %print_null_check373 = icmp eq ptr %301, null
  %print_safe_str374 = select i1 %print_null_check373, ptr @print_null_str.71, ptr %301
  %print_call375 = call i32 (ptr, ...) @printf(ptr @165, ptr %print_safe_str374)
  %fv = alloca %vec2, align 8
  store %vec2 zeroinitializer, ptr %fv, align 4
  %screen_val376 = load %ivec2, ptr %screen, align 4
  %302 = alloca %vec2, align 8
  %mtmp377 = alloca %ivec2, align 8
  store %ivec2 %screen_val376, ptr %mtmp377, align 4
  call void @aiml_math_ivec2_to_vec2(ptr %mtmp377, ptr %302)
  %303 = load %vec2, ptr %302, align 4
  store %vec2 %303, ptr %fv, align 4
  %fvx = alloca float, align 4
  store float 0.000000e+00, ptr %fvx, align 4
  %prop_ptr378 = getelementptr inbounds %vec2, ptr %fv, i32 0, i32 0
  %x_val379 = load float, ptr %prop_ptr378, align 4
  store float %x_val379, ptr %fvx, align 4
  %fvx_val = load float, ptr %fvx, align 4
  %304 = call ptr @__aiml_f32_to_string(float %fvx_val)
  %305 = call ptr @__aiml_str_concat(ptr @166, ptr %304)
  %print_null_check380 = icmp eq ptr %305, null
  %print_safe_str381 = select i1 %print_null_check380, ptr @print_null_str.72, ptr %305
  %print_call382 = call i32 (ptr, ...) @printf(ptr @167, ptr %print_safe_str381)
  %iv = alloca %ivec2, align 8
  store %ivec2 zeroinitializer, ptr %iv, align 4
  %fv_val = load %vec2, ptr %fv, align 4
  %306 = alloca %ivec2, align 8
  %mtmp383 = alloca %vec2, align 8
  store %vec2 %fv_val, ptr %mtmp383, align 4
  call void @aiml_math_vec2_to_ivec2(ptr %mtmp383, ptr %306)
  %307 = load %ivec2, ptr %306, align 4
  store %ivec2 %307, ptr %iv, align 4
  %ivx = alloca i32, align 4
  store i32 0, ptr %ivx, align 4
  %prop_ptr384 = getelementptr inbounds %ivec2, ptr %iv, i32 0, i32 0
  %x_val385 = load i32, ptr %prop_ptr384, align 4
  store i32 %x_val385, ptr %ivx, align 4
  %ivx_val = load i32, ptr %ivx, align 4
  %308 = call ptr @__aiml_int_to_string(i32 %ivx_val)
  %309 = call ptr @__aiml_str_concat(ptr @168, ptr %308)
  %print_null_check386 = icmp eq ptr %309, null
  %print_safe_str387 = select i1 %print_null_check386, ptr @print_null_str.73, ptr %309
  %print_call388 = call i32 (ptr, ...) @printf(ptr @169, ptr %print_safe_str387)
  %print_call389 = call i32 (ptr, ...) @printf(ptr @171, ptr @170)
  %vals = alloca %list_f32, align 8
  store %list_f32 zeroinitializer, ptr %vals, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %310 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  store ptr %310, ptr %dataPtr, align 8
  %sizePtr390 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 0
  %capPtr391 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 1
  %size = load i32, ptr %sizePtr390, align 4
  %capacity = load i32, ptr %capPtr391, align 4
  %311 = icmp eq i32 %size, %capacity
  br i1 %311, label %grow, label %cont

grow:                                             ; preds = %entry
  %312 = mul i32 %capacity, 2
  store i32 %312, ptr %capPtr391, align 4
  %dataPtr392 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  %313 = load ptr, ptr %dataPtr392, align 8
  %314 = zext i32 %312 to i64
  %315 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %314
  %316 = call ptr @realloc(ptr %313, i64 %315)
  store ptr %316, ptr %dataPtr392, align 8
  br label %cont

cont:                                             ; preds = %grow, %entry
  %dataPtr393 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr393, align 8
  %317 = getelementptr inbounds float, ptr %dataBase, i32 %size
  store float 3.500000e+00, ptr %317, align 4
  %318 = add i32 %size, 1
  store i32 %318, ptr %sizePtr390, align 4
  %sizePtr394 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 0
  %capPtr395 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 1
  %size396 = load i32, ptr %sizePtr394, align 4
  %capacity397 = load i32, ptr %capPtr395, align 4
  %319 = icmp eq i32 %size396, %capacity397
  br i1 %319, label %grow398, label %cont399

grow398:                                          ; preds = %cont
  %320 = mul i32 %capacity397, 2
  store i32 %320, ptr %capPtr395, align 4
  %dataPtr400 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  %321 = load ptr, ptr %dataPtr400, align 8
  %322 = zext i32 %320 to i64
  %323 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %322
  %324 = call ptr @realloc(ptr %321, i64 %323)
  store ptr %324, ptr %dataPtr400, align 8
  br label %cont399

cont399:                                          ; preds = %grow398, %cont
  %dataPtr401 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  %dataBase402 = load ptr, ptr %dataPtr401, align 8
  %325 = getelementptr inbounds float, ptr %dataBase402, i32 %size396
  store float 0x3FF3333340000000, ptr %325, align 4
  %326 = add i32 %size396, 1
  store i32 %326, ptr %sizePtr394, align 4
  %sizePtr403 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 0
  %capPtr404 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 1
  %size405 = load i32, ptr %sizePtr403, align 4
  %capacity406 = load i32, ptr %capPtr404, align 4
  %327 = icmp eq i32 %size405, %capacity406
  br i1 %327, label %grow407, label %cont408

grow407:                                          ; preds = %cont399
  %328 = mul i32 %capacity406, 2
  store i32 %328, ptr %capPtr404, align 4
  %dataPtr409 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  %329 = load ptr, ptr %dataPtr409, align 8
  %330 = zext i32 %328 to i64
  %331 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %330
  %332 = call ptr @realloc(ptr %329, i64 %331)
  store ptr %332, ptr %dataPtr409, align 8
  br label %cont408

cont408:                                          ; preds = %grow407, %cont399
  %dataPtr410 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  %dataBase411 = load ptr, ptr %dataPtr410, align 8
  %333 = getelementptr inbounds float, ptr %dataBase411, i32 %size405
  store float 0x401F333340000000, ptr %333, align 4
  %334 = add i32 %size405, 1
  store i32 %334, ptr %sizePtr403, align 4
  %sizePtr412 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 0
  %capPtr413 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 1
  %size414 = load i32, ptr %sizePtr412, align 4
  %capacity415 = load i32, ptr %capPtr413, align 4
  %335 = icmp eq i32 %size414, %capacity415
  br i1 %335, label %grow416, label %cont417

grow416:                                          ; preds = %cont408
  %336 = mul i32 %capacity415, 2
  store i32 %336, ptr %capPtr413, align 4
  %dataPtr418 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  %337 = load ptr, ptr %dataPtr418, align 8
  %338 = zext i32 %336 to i64
  %339 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %338
  %340 = call ptr @realloc(ptr %337, i64 %339)
  store ptr %340, ptr %dataPtr418, align 8
  br label %cont417

cont417:                                          ; preds = %grow416, %cont408
  %dataPtr419 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  %dataBase420 = load ptr, ptr %dataPtr419, align 8
  %341 = getelementptr inbounds float, ptr %dataBase420, i32 %size414
  store float 0x3FD99999A0000000, ptr %341, align 4
  %342 = add i32 %size414, 1
  store i32 %342, ptr %sizePtr412, align 4
  %sizePtr421 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 0
  %capPtr422 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 1
  %size423 = load i32, ptr %sizePtr421, align 4
  %capacity424 = load i32, ptr %capPtr422, align 4
  %343 = icmp eq i32 %size423, %capacity424
  br i1 %343, label %grow425, label %cont426

grow425:                                          ; preds = %cont417
  %344 = mul i32 %capacity424, 2
  store i32 %344, ptr %capPtr422, align 4
  %dataPtr427 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  %345 = load ptr, ptr %dataPtr427, align 8
  %346 = zext i32 %344 to i64
  %347 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %346
  %348 = call ptr @realloc(ptr %345, i64 %347)
  store ptr %348, ptr %dataPtr427, align 8
  br label %cont426

cont426:                                          ; preds = %grow425, %cont417
  %dataPtr428 = getelementptr inbounds %GenericList, ptr %vals, i32 0, i32 2
  %dataBase429 = load ptr, ptr %dataPtr428, align 8
  %349 = getelementptr inbounds float, ptr %dataBase429, i32 %size423
  store float 0x4014666660000000, ptr %349, align 4
  %350 = add i32 %size423, 1
  store i32 %350, ptr %sizePtr421, align 4
  %lo = alloca float, align 4
  store float 0.000000e+00, ptr %lo, align 4
  %vals_val = load %list_f32, ptr %vals, align 8
  %351 = extractvalue %list_f32 %vals_val, 0
  %352 = extractvalue %list_f32 %vals_val, 2
  %353 = call float @aiml_math_fmin_list(ptr %352, i32 %351)
  store float %353, ptr %lo, align 4
  %hi = alloca float, align 4
  store float 0.000000e+00, ptr %hi, align 4
  %vals_val430 = load %list_f32, ptr %vals, align 8
  %354 = extractvalue %list_f32 %vals_val430, 0
  %355 = extractvalue %list_f32 %vals_val430, 2
  %356 = call float @aiml_math_fmax_list(ptr %355, i32 %354)
  store float %356, ptr %hi, align 4
  %lo_val = load float, ptr %lo, align 4
  %357 = call ptr @__aiml_f32_to_string(float %lo_val)
  %358 = call ptr @__aiml_str_concat(ptr @172, ptr %357)
  %359 = call ptr @__aiml_str_concat(ptr %358, ptr @173)
  %hi_val = load float, ptr %hi, align 4
  %360 = call ptr @__aiml_f32_to_string(float %hi_val)
  %361 = call ptr @__aiml_str_concat(ptr %359, ptr %360)
  %print_null_check431 = icmp eq ptr %361, null
  %print_safe_str432 = select i1 %print_null_check431, ptr @print_null_str.75, ptr %361
  %print_call433 = call i32 (ptr, ...) @printf(ptr @174, ptr %print_safe_str432)
  ret void
}

declare ptr @malloc(i64)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_f32_to_string(float %0) #0 {
entry:
  %1 = call ptr @malloc(i64 32)
  %2 = fpext float %0 to double
  %3 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_f32, double %2)
  ret ptr %1
}

declare i64 @strlen(ptr)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

; Function Attrs: noinline
define internal ptr @__aiml_str_concat(ptr %0, ptr %1) #0 {
entry:
  %2 = icmp eq ptr %0, null
  %3 = select i1 %2, ptr @empty, ptr %0
  %4 = icmp eq ptr %1, null
  %5 = select i1 %4, ptr @empty, ptr %1
  %6 = call i64 @strlen(ptr %3)
  %7 = call i64 @strlen(ptr %5)
  %8 = add i64 %6, %7
  %9 = add i64 %8, 1
  %10 = call ptr @malloc(i64 %9)
  %11 = call ptr @strcpy(ptr %10, ptr %3)
  %12 = call ptr @strcat(ptr %10, ptr %5)
  ret ptr %10
}

declare i32 @printf(ptr, ...)

declare float @sinf(float)

declare float @cosf(float)

declare float @sqrtf(float)

declare float @powf(float, float)

declare float @floorf(float)

declare float @ceilf(float)

declare float @roundf(float)

declare float @fabsf(float)

declare float @fmodf(float, float)

declare void @aiml_math_vec2_add(ptr, ptr, ptr)

declare void @aiml_math_vec2_scale(ptr, float, ptr)

declare void @aiml_math_vec2_neg(ptr, ptr)

declare float @aiml_math_vec2_angle(ptr)

declare void @aiml_math_vec2_perp(ptr, ptr)

declare float @aiml_math_vec2_length(ptr)

declare void @aiml_math_vec3_add(ptr, ptr, ptr)

declare void @aiml_math_vec3_sub(ptr, ptr, ptr)

declare void @aiml_math_vec3_scale(ptr, float, ptr)

declare void @aiml_math_vec3_neg(ptr, ptr)

declare void @aiml_math_vec3_div(ptr, float, ptr)

declare void @aiml_math_vec3_mul(ptr, ptr, ptr)

declare float @aiml_math_vec3_dot(ptr, ptr)

declare void @aiml_math_vec3_cross(ptr, ptr, ptr)

declare float @aiml_math_vec3_length(ptr)

declare void @aiml_math_vec3_normalize(ptr, ptr)

declare void @aiml_math_vec3_lerp(ptr, ptr, float, ptr)

declare float @aiml_math_vec3_distance(ptr, ptr)

declare void @aiml_math_vec3_reflect(ptr, ptr, ptr)

declare void @aiml_math_vec4_lerp(ptr, ptr, float, ptr)

declare i32 @aiml_math_vec4_eq(ptr, ptr)

declare void @aiml_math_mat4_identity(ptr)

declare float @aiml_math_mat4_get(ptr, i32, i32)

declare void @aiml_math_mat4_set(ptr, i32, i32, float)

declare void @aiml_math_mat4_translate(ptr, ptr)

declare void @aiml_math_mat4_scale(ptr, ptr)

declare void @aiml_math_mat4_rotate_y(float, ptr)

declare void @aiml_math_mat4_mul(ptr, ptr, ptr)

declare void @aiml_math_mat4_mul_vec4(ptr, ptr, ptr)

declare void @aiml_math_mat4_perspective(float, float, float, float, ptr)

declare void @aiml_math_mat4_lookat(ptr, ptr, ptr, ptr)

declare void @aiml_math_mat4_inverse(ptr, ptr)

declare void @aiml_math_mat4_transpose(ptr, ptr)

declare ptr @aiml_math_mat4_to_buffer(ptr)

declare ptr @memcpy(ptr, ptr, i64)

declare void @aiml_math_quat_identity(ptr)

declare void @aiml_math_quat_axis_angle(ptr, float, ptr)

declare void @aiml_math_quat_euler(float, float, float, ptr)

declare void @aiml_math_quat_mul(ptr, ptr, ptr)

declare void @aiml_math_quat_normalize(ptr, ptr)

declare void @aiml_math_quat_inverse(ptr, ptr)

declare void @aiml_math_quat_rotate_vec3(ptr, ptr, ptr)

declare void @aiml_math_quat_slerp(ptr, ptr, float, ptr)

declare void @aiml_math_quat_to_mat4(ptr, ptr)

declare void @aiml_math_mat4_to_quat(ptr, ptr)

declare void @aiml_math_quat_lerp(ptr, ptr, float, ptr)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare void @aiml_math_ivec2_div(ptr, i32, ptr)

declare void @aiml_math_ivec2_add(ptr, ptr, ptr)

declare void @aiml_math_ivec2_to_vec2(ptr, ptr)

declare void @aiml_math_vec2_to_ivec2(ptr, ptr)

declare ptr @realloc(ptr, i64)

declare float @aiml_math_fmin_list(ptr, i32)

declare float @aiml_math_fmax_list(ptr, i32)

attributes #0 = { noinline }
