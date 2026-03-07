; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%buffer = type { i32, i32, i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [25 x i8] c"AIML \E2\80\94 wei\C3\9Fer W\C3\BCrfel\00", align 1
@1 = private unnamed_addr constant [46 x i8] c"Phase A+B OK: Fenster und GL-Kontext erstellt\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [42 x i8] c"Phase C OK: W\C3\BCrfel-Geometrie hochgeladen\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [122 x i8] c"#version 330 core\0Alayout(location=0) in vec3 pos;\0Auniform mat4 mvp;\0Avoid main(){\0A  gl_Position = mvp * vec4(pos, 1.0);\0A}\0A\00", align 1
@6 = private unnamed_addr constant [86 x i8] c"#version 330 core\0Aout vec4 color;\0Avoid main(){\0A  color = vec4(1.0, 1.0, 1.0, 1.0);\0A}\0A\00", align 1
@7 = private unnamed_addr constant [30 x i8] c"Phase D OK: Shader kompiliert\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@9 = private unnamed_addr constant [33 x i8] c"Phase E OK: 3 Sekunden gerendert\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [27 x i8] c"All gl_window tests passed\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@12 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  %ok = alloca i32, align 4
  store i32 0, ptr %ok, align 4
  %0 = call i32 @glfwInit()
  store i32 %0, ptr %ok, align 4
  call void @glfwWindowHint(i32 139266, i32 3)
  call void @glfwWindowHint(i32 139267, i32 3)
  call void @glfwWindowHint(i32 139272, i32 204801)
  %null_mon = alloca ptr, align 8
  store ptr null, ptr %null_mon, align 8
  store ptr null, ptr %null_mon, align 8
  %null_sh = alloca ptr, align 8
  store ptr null, ptr %null_sh, align 8
  store ptr null, ptr %null_sh, align 8
  %win = alloca ptr, align 8
  store ptr null, ptr %win, align 8
  %null_mon_val = load ptr, ptr %null_mon, align 8
  %null_sh_val = load ptr, ptr %null_sh, align 8
  %1 = call ptr @glfwCreateWindow(i32 800, i32 600, ptr @0, ptr %null_mon_val, ptr %null_sh_val)
  store ptr %1, ptr %win, align 8
  %win_val = load ptr, ptr %win, align 8
  call void @glfwMakeContextCurrent(ptr %win_val)
  call void @aiml_gl_init()
  call void @aiml_gl_enable_depth_test()
  call void @aiml_gl_viewport(i32 0, i32 0, i32 800, i32 600)
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr @1)
  %verts = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %verts, align 8
  %2 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %3 = load i32, ptr %2, align 4
  %4 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %5 = load ptr, ptr %4, align 8
  %6 = getelementptr inbounds i8, ptr %5, i32 %3
  store float -5.000000e-01, ptr %6, align 4
  %7 = add i32 %3, 4
  store i32 %7, ptr %2, align 4
  %8 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %9 = load i32, ptr %8, align 4
  %10 = icmp sgt i32 %7, %9
  %11 = select i1 %10, i32 %7, i32 %9
  store i32 %11, ptr %8, align 4
  %12 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %13 = load i32, ptr %12, align 4
  %14 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %15 = load ptr, ptr %14, align 8
  %16 = getelementptr inbounds i8, ptr %15, i32 %13
  store float -5.000000e-01, ptr %16, align 4
  %17 = add i32 %13, 4
  store i32 %17, ptr %12, align 4
  %18 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %19 = load i32, ptr %18, align 4
  %20 = icmp sgt i32 %17, %19
  %21 = select i1 %20, i32 %17, i32 %19
  store i32 %21, ptr %18, align 4
  %22 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %23 = load i32, ptr %22, align 4
  %24 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %25 = load ptr, ptr %24, align 8
  %26 = getelementptr inbounds i8, ptr %25, i32 %23
  store float 5.000000e-01, ptr %26, align 4
  %27 = add i32 %23, 4
  store i32 %27, ptr %22, align 4
  %28 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %29 = load i32, ptr %28, align 4
  %30 = icmp sgt i32 %27, %29
  %31 = select i1 %30, i32 %27, i32 %29
  store i32 %31, ptr %28, align 4
  %32 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %33 = load i32, ptr %32, align 4
  %34 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %35 = load ptr, ptr %34, align 8
  %36 = getelementptr inbounds i8, ptr %35, i32 %33
  store float 5.000000e-01, ptr %36, align 4
  %37 = add i32 %33, 4
  store i32 %37, ptr %32, align 4
  %38 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %39 = load i32, ptr %38, align 4
  %40 = icmp sgt i32 %37, %39
  %41 = select i1 %40, i32 %37, i32 %39
  store i32 %41, ptr %38, align 4
  %42 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %43 = load i32, ptr %42, align 4
  %44 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %45 = load ptr, ptr %44, align 8
  %46 = getelementptr inbounds i8, ptr %45, i32 %43
  store float -5.000000e-01, ptr %46, align 4
  %47 = add i32 %43, 4
  store i32 %47, ptr %42, align 4
  %48 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %49 = load i32, ptr %48, align 4
  %50 = icmp sgt i32 %47, %49
  %51 = select i1 %50, i32 %47, i32 %49
  store i32 %51, ptr %48, align 4
  %52 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %53 = load i32, ptr %52, align 4
  %54 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %55 = load ptr, ptr %54, align 8
  %56 = getelementptr inbounds i8, ptr %55, i32 %53
  store float 5.000000e-01, ptr %56, align 4
  %57 = add i32 %53, 4
  store i32 %57, ptr %52, align 4
  %58 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %59 = load i32, ptr %58, align 4
  %60 = icmp sgt i32 %57, %59
  %61 = select i1 %60, i32 %57, i32 %59
  store i32 %61, ptr %58, align 4
  %62 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %63 = load i32, ptr %62, align 4
  %64 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %65 = load ptr, ptr %64, align 8
  %66 = getelementptr inbounds i8, ptr %65, i32 %63
  store float 5.000000e-01, ptr %66, align 4
  %67 = add i32 %63, 4
  store i32 %67, ptr %62, align 4
  %68 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %69 = load i32, ptr %68, align 4
  %70 = icmp sgt i32 %67, %69
  %71 = select i1 %70, i32 %67, i32 %69
  store i32 %71, ptr %68, align 4
  %72 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %73 = load i32, ptr %72, align 4
  %74 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %75 = load ptr, ptr %74, align 8
  %76 = getelementptr inbounds i8, ptr %75, i32 %73
  store float 5.000000e-01, ptr %76, align 4
  %77 = add i32 %73, 4
  store i32 %77, ptr %72, align 4
  %78 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %79 = load i32, ptr %78, align 4
  %80 = icmp sgt i32 %77, %79
  %81 = select i1 %80, i32 %77, i32 %79
  store i32 %81, ptr %78, align 4
  %82 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %83 = load i32, ptr %82, align 4
  %84 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %85 = load ptr, ptr %84, align 8
  %86 = getelementptr inbounds i8, ptr %85, i32 %83
  store float 5.000000e-01, ptr %86, align 4
  %87 = add i32 %83, 4
  store i32 %87, ptr %82, align 4
  %88 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %89 = load i32, ptr %88, align 4
  %90 = icmp sgt i32 %87, %89
  %91 = select i1 %90, i32 %87, i32 %89
  store i32 %91, ptr %88, align 4
  %92 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %93 = load i32, ptr %92, align 4
  %94 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %95 = load ptr, ptr %94, align 8
  %96 = getelementptr inbounds i8, ptr %95, i32 %93
  store float -5.000000e-01, ptr %96, align 4
  %97 = add i32 %93, 4
  store i32 %97, ptr %92, align 4
  %98 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %99 = load i32, ptr %98, align 4
  %100 = icmp sgt i32 %97, %99
  %101 = select i1 %100, i32 %97, i32 %99
  store i32 %101, ptr %98, align 4
  %102 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %103 = load i32, ptr %102, align 4
  %104 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %105 = load ptr, ptr %104, align 8
  %106 = getelementptr inbounds i8, ptr %105, i32 %103
  store float 5.000000e-01, ptr %106, align 4
  %107 = add i32 %103, 4
  store i32 %107, ptr %102, align 4
  %108 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %109 = load i32, ptr %108, align 4
  %110 = icmp sgt i32 %107, %109
  %111 = select i1 %110, i32 %107, i32 %109
  store i32 %111, ptr %108, align 4
  %112 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %113 = load i32, ptr %112, align 4
  %114 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %115 = load ptr, ptr %114, align 8
  %116 = getelementptr inbounds i8, ptr %115, i32 %113
  store float 5.000000e-01, ptr %116, align 4
  %117 = add i32 %113, 4
  store i32 %117, ptr %112, align 4
  %118 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %119 = load i32, ptr %118, align 4
  %120 = icmp sgt i32 %117, %119
  %121 = select i1 %120, i32 %117, i32 %119
  store i32 %121, ptr %118, align 4
  %122 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %123 = load i32, ptr %122, align 4
  %124 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %125 = load ptr, ptr %124, align 8
  %126 = getelementptr inbounds i8, ptr %125, i32 %123
  store float -5.000000e-01, ptr %126, align 4
  %127 = add i32 %123, 4
  store i32 %127, ptr %122, align 4
  %128 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %129 = load i32, ptr %128, align 4
  %130 = icmp sgt i32 %127, %129
  %131 = select i1 %130, i32 %127, i32 %129
  store i32 %131, ptr %128, align 4
  %132 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %133 = load i32, ptr %132, align 4
  %134 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %135 = load ptr, ptr %134, align 8
  %136 = getelementptr inbounds i8, ptr %135, i32 %133
  store float -5.000000e-01, ptr %136, align 4
  %137 = add i32 %133, 4
  store i32 %137, ptr %132, align 4
  %138 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %139 = load i32, ptr %138, align 4
  %140 = icmp sgt i32 %137, %139
  %141 = select i1 %140, i32 %137, i32 %139
  store i32 %141, ptr %138, align 4
  %142 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %143 = load i32, ptr %142, align 4
  %144 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %145 = load ptr, ptr %144, align 8
  %146 = getelementptr inbounds i8, ptr %145, i32 %143
  store float -5.000000e-01, ptr %146, align 4
  %147 = add i32 %143, 4
  store i32 %147, ptr %142, align 4
  %148 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %149 = load i32, ptr %148, align 4
  %150 = icmp sgt i32 %147, %149
  %151 = select i1 %150, i32 %147, i32 %149
  store i32 %151, ptr %148, align 4
  %152 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %153 = load i32, ptr %152, align 4
  %154 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %155 = load ptr, ptr %154, align 8
  %156 = getelementptr inbounds i8, ptr %155, i32 %153
  store float 5.000000e-01, ptr %156, align 4
  %157 = add i32 %153, 4
  store i32 %157, ptr %152, align 4
  %158 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %159 = load i32, ptr %158, align 4
  %160 = icmp sgt i32 %157, %159
  %161 = select i1 %160, i32 %157, i32 %159
  store i32 %161, ptr %158, align 4
  %162 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %163 = load i32, ptr %162, align 4
  %164 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %165 = load ptr, ptr %164, align 8
  %166 = getelementptr inbounds i8, ptr %165, i32 %163
  store float -5.000000e-01, ptr %166, align 4
  %167 = add i32 %163, 4
  store i32 %167, ptr %162, align 4
  %168 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %169 = load i32, ptr %168, align 4
  %170 = icmp sgt i32 %167, %169
  %171 = select i1 %170, i32 %167, i32 %169
  store i32 %171, ptr %168, align 4
  %172 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %173 = load i32, ptr %172, align 4
  %174 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %175 = load ptr, ptr %174, align 8
  %176 = getelementptr inbounds i8, ptr %175, i32 %173
  store float -5.000000e-01, ptr %176, align 4
  %177 = add i32 %173, 4
  store i32 %177, ptr %172, align 4
  %178 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %179 = load i32, ptr %178, align 4
  %180 = icmp sgt i32 %177, %179
  %181 = select i1 %180, i32 %177, i32 %179
  store i32 %181, ptr %178, align 4
  %182 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %183 = load i32, ptr %182, align 4
  %184 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %185 = load ptr, ptr %184, align 8
  %186 = getelementptr inbounds i8, ptr %185, i32 %183
  store float 5.000000e-01, ptr %186, align 4
  %187 = add i32 %183, 4
  store i32 %187, ptr %182, align 4
  %188 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %189 = load i32, ptr %188, align 4
  %190 = icmp sgt i32 %187, %189
  %191 = select i1 %190, i32 %187, i32 %189
  store i32 %191, ptr %188, align 4
  %192 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %193 = load i32, ptr %192, align 4
  %194 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %195 = load ptr, ptr %194, align 8
  %196 = getelementptr inbounds i8, ptr %195, i32 %193
  store float 5.000000e-01, ptr %196, align 4
  %197 = add i32 %193, 4
  store i32 %197, ptr %192, align 4
  %198 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %199 = load i32, ptr %198, align 4
  %200 = icmp sgt i32 %197, %199
  %201 = select i1 %200, i32 %197, i32 %199
  store i32 %201, ptr %198, align 4
  %202 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %203 = load i32, ptr %202, align 4
  %204 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %205 = load ptr, ptr %204, align 8
  %206 = getelementptr inbounds i8, ptr %205, i32 %203
  store float -5.000000e-01, ptr %206, align 4
  %207 = add i32 %203, 4
  store i32 %207, ptr %202, align 4
  %208 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %209 = load i32, ptr %208, align 4
  %210 = icmp sgt i32 %207, %209
  %211 = select i1 %210, i32 %207, i32 %209
  store i32 %211, ptr %208, align 4
  %212 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %213 = load i32, ptr %212, align 4
  %214 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %215 = load ptr, ptr %214, align 8
  %216 = getelementptr inbounds i8, ptr %215, i32 %213
  store float -5.000000e-01, ptr %216, align 4
  %217 = add i32 %213, 4
  store i32 %217, ptr %212, align 4
  %218 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %219 = load i32, ptr %218, align 4
  %220 = icmp sgt i32 %217, %219
  %221 = select i1 %220, i32 %217, i32 %219
  store i32 %221, ptr %218, align 4
  %222 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %223 = load i32, ptr %222, align 4
  %224 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %225 = load ptr, ptr %224, align 8
  %226 = getelementptr inbounds i8, ptr %225, i32 %223
  store float 5.000000e-01, ptr %226, align 4
  %227 = add i32 %223, 4
  store i32 %227, ptr %222, align 4
  %228 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %229 = load i32, ptr %228, align 4
  %230 = icmp sgt i32 %227, %229
  %231 = select i1 %230, i32 %227, i32 %229
  store i32 %231, ptr %228, align 4
  %232 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 3
  %233 = load i32, ptr %232, align 4
  %234 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 4
  %235 = load ptr, ptr %234, align 8
  %236 = getelementptr inbounds i8, ptr %235, i32 %233
  store float -5.000000e-01, ptr %236, align 4
  %237 = add i32 %233, 4
  store i32 %237, ptr %232, align 4
  %238 = getelementptr inbounds %buffer, ptr %verts, i32 0, i32 0
  %239 = load i32, ptr %238, align 4
  %240 = icmp sgt i32 %237, %239
  %241 = select i1 %240, i32 %237, i32 %239
  store i32 %241, ptr %238, align 4
  %idxs = alloca %buffer, align 8
  store %buffer zeroinitializer, ptr %idxs, align 8
  %242 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %243 = load i32, ptr %242, align 4
  %244 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %245 = load ptr, ptr %244, align 8
  %246 = getelementptr inbounds i8, ptr %245, i32 %243
  store i32 0, ptr %246, align 4
  %247 = add i32 %243, 4
  store i32 %247, ptr %242, align 4
  %248 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %249 = load i32, ptr %248, align 4
  %250 = icmp sgt i32 %247, %249
  %251 = select i1 %250, i32 %247, i32 %249
  store i32 %251, ptr %248, align 4
  %252 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %253 = load i32, ptr %252, align 4
  %254 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %255 = load ptr, ptr %254, align 8
  %256 = getelementptr inbounds i8, ptr %255, i32 %253
  store i32 1, ptr %256, align 4
  %257 = add i32 %253, 4
  store i32 %257, ptr %252, align 4
  %258 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %259 = load i32, ptr %258, align 4
  %260 = icmp sgt i32 %257, %259
  %261 = select i1 %260, i32 %257, i32 %259
  store i32 %261, ptr %258, align 4
  %262 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %263 = load i32, ptr %262, align 4
  %264 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %265 = load ptr, ptr %264, align 8
  %266 = getelementptr inbounds i8, ptr %265, i32 %263
  store i32 2, ptr %266, align 4
  %267 = add i32 %263, 4
  store i32 %267, ptr %262, align 4
  %268 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %269 = load i32, ptr %268, align 4
  %270 = icmp sgt i32 %267, %269
  %271 = select i1 %270, i32 %267, i32 %269
  store i32 %271, ptr %268, align 4
  %272 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %273 = load i32, ptr %272, align 4
  %274 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %275 = load ptr, ptr %274, align 8
  %276 = getelementptr inbounds i8, ptr %275, i32 %273
  store i32 0, ptr %276, align 4
  %277 = add i32 %273, 4
  store i32 %277, ptr %272, align 4
  %278 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %279 = load i32, ptr %278, align 4
  %280 = icmp sgt i32 %277, %279
  %281 = select i1 %280, i32 %277, i32 %279
  store i32 %281, ptr %278, align 4
  %282 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %283 = load i32, ptr %282, align 4
  %284 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %285 = load ptr, ptr %284, align 8
  %286 = getelementptr inbounds i8, ptr %285, i32 %283
  store i32 2, ptr %286, align 4
  %287 = add i32 %283, 4
  store i32 %287, ptr %282, align 4
  %288 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %289 = load i32, ptr %288, align 4
  %290 = icmp sgt i32 %287, %289
  %291 = select i1 %290, i32 %287, i32 %289
  store i32 %291, ptr %288, align 4
  %292 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %293 = load i32, ptr %292, align 4
  %294 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %295 = load ptr, ptr %294, align 8
  %296 = getelementptr inbounds i8, ptr %295, i32 %293
  store i32 3, ptr %296, align 4
  %297 = add i32 %293, 4
  store i32 %297, ptr %292, align 4
  %298 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %299 = load i32, ptr %298, align 4
  %300 = icmp sgt i32 %297, %299
  %301 = select i1 %300, i32 %297, i32 %299
  store i32 %301, ptr %298, align 4
  %302 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %303 = load i32, ptr %302, align 4
  %304 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %305 = load ptr, ptr %304, align 8
  %306 = getelementptr inbounds i8, ptr %305, i32 %303
  store i32 5, ptr %306, align 4
  %307 = add i32 %303, 4
  store i32 %307, ptr %302, align 4
  %308 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %309 = load i32, ptr %308, align 4
  %310 = icmp sgt i32 %307, %309
  %311 = select i1 %310, i32 %307, i32 %309
  store i32 %311, ptr %308, align 4
  %312 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %313 = load i32, ptr %312, align 4
  %314 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %315 = load ptr, ptr %314, align 8
  %316 = getelementptr inbounds i8, ptr %315, i32 %313
  store i32 4, ptr %316, align 4
  %317 = add i32 %313, 4
  store i32 %317, ptr %312, align 4
  %318 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %319 = load i32, ptr %318, align 4
  %320 = icmp sgt i32 %317, %319
  %321 = select i1 %320, i32 %317, i32 %319
  store i32 %321, ptr %318, align 4
  %322 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %323 = load i32, ptr %322, align 4
  %324 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %325 = load ptr, ptr %324, align 8
  %326 = getelementptr inbounds i8, ptr %325, i32 %323
  store i32 7, ptr %326, align 4
  %327 = add i32 %323, 4
  store i32 %327, ptr %322, align 4
  %328 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %329 = load i32, ptr %328, align 4
  %330 = icmp sgt i32 %327, %329
  %331 = select i1 %330, i32 %327, i32 %329
  store i32 %331, ptr %328, align 4
  %332 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %333 = load i32, ptr %332, align 4
  %334 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %335 = load ptr, ptr %334, align 8
  %336 = getelementptr inbounds i8, ptr %335, i32 %333
  store i32 5, ptr %336, align 4
  %337 = add i32 %333, 4
  store i32 %337, ptr %332, align 4
  %338 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %339 = load i32, ptr %338, align 4
  %340 = icmp sgt i32 %337, %339
  %341 = select i1 %340, i32 %337, i32 %339
  store i32 %341, ptr %338, align 4
  %342 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %343 = load i32, ptr %342, align 4
  %344 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %345 = load ptr, ptr %344, align 8
  %346 = getelementptr inbounds i8, ptr %345, i32 %343
  store i32 7, ptr %346, align 4
  %347 = add i32 %343, 4
  store i32 %347, ptr %342, align 4
  %348 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %349 = load i32, ptr %348, align 4
  %350 = icmp sgt i32 %347, %349
  %351 = select i1 %350, i32 %347, i32 %349
  store i32 %351, ptr %348, align 4
  %352 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %353 = load i32, ptr %352, align 4
  %354 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %355 = load ptr, ptr %354, align 8
  %356 = getelementptr inbounds i8, ptr %355, i32 %353
  store i32 6, ptr %356, align 4
  %357 = add i32 %353, 4
  store i32 %357, ptr %352, align 4
  %358 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %359 = load i32, ptr %358, align 4
  %360 = icmp sgt i32 %357, %359
  %361 = select i1 %360, i32 %357, i32 %359
  store i32 %361, ptr %358, align 4
  %362 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %363 = load i32, ptr %362, align 4
  %364 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %365 = load ptr, ptr %364, align 8
  %366 = getelementptr inbounds i8, ptr %365, i32 %363
  store i32 4, ptr %366, align 4
  %367 = add i32 %363, 4
  store i32 %367, ptr %362, align 4
  %368 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %369 = load i32, ptr %368, align 4
  %370 = icmp sgt i32 %367, %369
  %371 = select i1 %370, i32 %367, i32 %369
  store i32 %371, ptr %368, align 4
  %372 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %373 = load i32, ptr %372, align 4
  %374 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %375 = load ptr, ptr %374, align 8
  %376 = getelementptr inbounds i8, ptr %375, i32 %373
  store i32 0, ptr %376, align 4
  %377 = add i32 %373, 4
  store i32 %377, ptr %372, align 4
  %378 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %379 = load i32, ptr %378, align 4
  %380 = icmp sgt i32 %377, %379
  %381 = select i1 %380, i32 %377, i32 %379
  store i32 %381, ptr %378, align 4
  %382 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %383 = load i32, ptr %382, align 4
  %384 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %385 = load ptr, ptr %384, align 8
  %386 = getelementptr inbounds i8, ptr %385, i32 %383
  store i32 3, ptr %386, align 4
  %387 = add i32 %383, 4
  store i32 %387, ptr %382, align 4
  %388 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %389 = load i32, ptr %388, align 4
  %390 = icmp sgt i32 %387, %389
  %391 = select i1 %390, i32 %387, i32 %389
  store i32 %391, ptr %388, align 4
  %392 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %393 = load i32, ptr %392, align 4
  %394 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %395 = load ptr, ptr %394, align 8
  %396 = getelementptr inbounds i8, ptr %395, i32 %393
  store i32 4, ptr %396, align 4
  %397 = add i32 %393, 4
  store i32 %397, ptr %392, align 4
  %398 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %399 = load i32, ptr %398, align 4
  %400 = icmp sgt i32 %397, %399
  %401 = select i1 %400, i32 %397, i32 %399
  store i32 %401, ptr %398, align 4
  %402 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %403 = load i32, ptr %402, align 4
  %404 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %405 = load ptr, ptr %404, align 8
  %406 = getelementptr inbounds i8, ptr %405, i32 %403
  store i32 3, ptr %406, align 4
  %407 = add i32 %403, 4
  store i32 %407, ptr %402, align 4
  %408 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %409 = load i32, ptr %408, align 4
  %410 = icmp sgt i32 %407, %409
  %411 = select i1 %410, i32 %407, i32 %409
  store i32 %411, ptr %408, align 4
  %412 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %413 = load i32, ptr %412, align 4
  %414 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %415 = load ptr, ptr %414, align 8
  %416 = getelementptr inbounds i8, ptr %415, i32 %413
  store i32 7, ptr %416, align 4
  %417 = add i32 %413, 4
  store i32 %417, ptr %412, align 4
  %418 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %419 = load i32, ptr %418, align 4
  %420 = icmp sgt i32 %417, %419
  %421 = select i1 %420, i32 %417, i32 %419
  store i32 %421, ptr %418, align 4
  %422 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %423 = load i32, ptr %422, align 4
  %424 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %425 = load ptr, ptr %424, align 8
  %426 = getelementptr inbounds i8, ptr %425, i32 %423
  store i32 1, ptr %426, align 4
  %427 = add i32 %423, 4
  store i32 %427, ptr %422, align 4
  %428 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %429 = load i32, ptr %428, align 4
  %430 = icmp sgt i32 %427, %429
  %431 = select i1 %430, i32 %427, i32 %429
  store i32 %431, ptr %428, align 4
  %432 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %433 = load i32, ptr %432, align 4
  %434 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %435 = load ptr, ptr %434, align 8
  %436 = getelementptr inbounds i8, ptr %435, i32 %433
  store i32 5, ptr %436, align 4
  %437 = add i32 %433, 4
  store i32 %437, ptr %432, align 4
  %438 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %439 = load i32, ptr %438, align 4
  %440 = icmp sgt i32 %437, %439
  %441 = select i1 %440, i32 %437, i32 %439
  store i32 %441, ptr %438, align 4
  %442 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %443 = load i32, ptr %442, align 4
  %444 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %445 = load ptr, ptr %444, align 8
  %446 = getelementptr inbounds i8, ptr %445, i32 %443
  store i32 6, ptr %446, align 4
  %447 = add i32 %443, 4
  store i32 %447, ptr %442, align 4
  %448 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %449 = load i32, ptr %448, align 4
  %450 = icmp sgt i32 %447, %449
  %451 = select i1 %450, i32 %447, i32 %449
  store i32 %451, ptr %448, align 4
  %452 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %453 = load i32, ptr %452, align 4
  %454 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %455 = load ptr, ptr %454, align 8
  %456 = getelementptr inbounds i8, ptr %455, i32 %453
  store i32 1, ptr %456, align 4
  %457 = add i32 %453, 4
  store i32 %457, ptr %452, align 4
  %458 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %459 = load i32, ptr %458, align 4
  %460 = icmp sgt i32 %457, %459
  %461 = select i1 %460, i32 %457, i32 %459
  store i32 %461, ptr %458, align 4
  %462 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %463 = load i32, ptr %462, align 4
  %464 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %465 = load ptr, ptr %464, align 8
  %466 = getelementptr inbounds i8, ptr %465, i32 %463
  store i32 6, ptr %466, align 4
  %467 = add i32 %463, 4
  store i32 %467, ptr %462, align 4
  %468 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %469 = load i32, ptr %468, align 4
  %470 = icmp sgt i32 %467, %469
  %471 = select i1 %470, i32 %467, i32 %469
  store i32 %471, ptr %468, align 4
  %472 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %473 = load i32, ptr %472, align 4
  %474 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %475 = load ptr, ptr %474, align 8
  %476 = getelementptr inbounds i8, ptr %475, i32 %473
  store i32 2, ptr %476, align 4
  %477 = add i32 %473, 4
  store i32 %477, ptr %472, align 4
  %478 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %479 = load i32, ptr %478, align 4
  %480 = icmp sgt i32 %477, %479
  %481 = select i1 %480, i32 %477, i32 %479
  store i32 %481, ptr %478, align 4
  %482 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %483 = load i32, ptr %482, align 4
  %484 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %485 = load ptr, ptr %484, align 8
  %486 = getelementptr inbounds i8, ptr %485, i32 %483
  store i32 3, ptr %486, align 4
  %487 = add i32 %483, 4
  store i32 %487, ptr %482, align 4
  %488 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %489 = load i32, ptr %488, align 4
  %490 = icmp sgt i32 %487, %489
  %491 = select i1 %490, i32 %487, i32 %489
  store i32 %491, ptr %488, align 4
  %492 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %493 = load i32, ptr %492, align 4
  %494 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %495 = load ptr, ptr %494, align 8
  %496 = getelementptr inbounds i8, ptr %495, i32 %493
  store i32 2, ptr %496, align 4
  %497 = add i32 %493, 4
  store i32 %497, ptr %492, align 4
  %498 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %499 = load i32, ptr %498, align 4
  %500 = icmp sgt i32 %497, %499
  %501 = select i1 %500, i32 %497, i32 %499
  store i32 %501, ptr %498, align 4
  %502 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %503 = load i32, ptr %502, align 4
  %504 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %505 = load ptr, ptr %504, align 8
  %506 = getelementptr inbounds i8, ptr %505, i32 %503
  store i32 6, ptr %506, align 4
  %507 = add i32 %503, 4
  store i32 %507, ptr %502, align 4
  %508 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %509 = load i32, ptr %508, align 4
  %510 = icmp sgt i32 %507, %509
  %511 = select i1 %510, i32 %507, i32 %509
  store i32 %511, ptr %508, align 4
  %512 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %513 = load i32, ptr %512, align 4
  %514 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %515 = load ptr, ptr %514, align 8
  %516 = getelementptr inbounds i8, ptr %515, i32 %513
  store i32 3, ptr %516, align 4
  %517 = add i32 %513, 4
  store i32 %517, ptr %512, align 4
  %518 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %519 = load i32, ptr %518, align 4
  %520 = icmp sgt i32 %517, %519
  %521 = select i1 %520, i32 %517, i32 %519
  store i32 %521, ptr %518, align 4
  %522 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %523 = load i32, ptr %522, align 4
  %524 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %525 = load ptr, ptr %524, align 8
  %526 = getelementptr inbounds i8, ptr %525, i32 %523
  store i32 6, ptr %526, align 4
  %527 = add i32 %523, 4
  store i32 %527, ptr %522, align 4
  %528 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %529 = load i32, ptr %528, align 4
  %530 = icmp sgt i32 %527, %529
  %531 = select i1 %530, i32 %527, i32 %529
  store i32 %531, ptr %528, align 4
  %532 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %533 = load i32, ptr %532, align 4
  %534 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %535 = load ptr, ptr %534, align 8
  %536 = getelementptr inbounds i8, ptr %535, i32 %533
  store i32 7, ptr %536, align 4
  %537 = add i32 %533, 4
  store i32 %537, ptr %532, align 4
  %538 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %539 = load i32, ptr %538, align 4
  %540 = icmp sgt i32 %537, %539
  %541 = select i1 %540, i32 %537, i32 %539
  store i32 %541, ptr %538, align 4
  %542 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %543 = load i32, ptr %542, align 4
  %544 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %545 = load ptr, ptr %544, align 8
  %546 = getelementptr inbounds i8, ptr %545, i32 %543
  store i32 4, ptr %546, align 4
  %547 = add i32 %543, 4
  store i32 %547, ptr %542, align 4
  %548 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %549 = load i32, ptr %548, align 4
  %550 = icmp sgt i32 %547, %549
  %551 = select i1 %550, i32 %547, i32 %549
  store i32 %551, ptr %548, align 4
  %552 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %553 = load i32, ptr %552, align 4
  %554 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %555 = load ptr, ptr %554, align 8
  %556 = getelementptr inbounds i8, ptr %555, i32 %553
  store i32 5, ptr %556, align 4
  %557 = add i32 %553, 4
  store i32 %557, ptr %552, align 4
  %558 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %559 = load i32, ptr %558, align 4
  %560 = icmp sgt i32 %557, %559
  %561 = select i1 %560, i32 %557, i32 %559
  store i32 %561, ptr %558, align 4
  %562 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %563 = load i32, ptr %562, align 4
  %564 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %565 = load ptr, ptr %564, align 8
  %566 = getelementptr inbounds i8, ptr %565, i32 %563
  store i32 1, ptr %566, align 4
  %567 = add i32 %563, 4
  store i32 %567, ptr %562, align 4
  %568 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %569 = load i32, ptr %568, align 4
  %570 = icmp sgt i32 %567, %569
  %571 = select i1 %570, i32 %567, i32 %569
  store i32 %571, ptr %568, align 4
  %572 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %573 = load i32, ptr %572, align 4
  %574 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %575 = load ptr, ptr %574, align 8
  %576 = getelementptr inbounds i8, ptr %575, i32 %573
  store i32 4, ptr %576, align 4
  %577 = add i32 %573, 4
  store i32 %577, ptr %572, align 4
  %578 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %579 = load i32, ptr %578, align 4
  %580 = icmp sgt i32 %577, %579
  %581 = select i1 %580, i32 %577, i32 %579
  store i32 %581, ptr %578, align 4
  %582 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %583 = load i32, ptr %582, align 4
  %584 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %585 = load ptr, ptr %584, align 8
  %586 = getelementptr inbounds i8, ptr %585, i32 %583
  store i32 1, ptr %586, align 4
  %587 = add i32 %583, 4
  store i32 %587, ptr %582, align 4
  %588 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %589 = load i32, ptr %588, align 4
  %590 = icmp sgt i32 %587, %589
  %591 = select i1 %590, i32 %587, i32 %589
  store i32 %591, ptr %588, align 4
  %592 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 3
  %593 = load i32, ptr %592, align 4
  %594 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 4
  %595 = load ptr, ptr %594, align 8
  %596 = getelementptr inbounds i8, ptr %595, i32 %593
  store i32 0, ptr %596, align 4
  %597 = add i32 %593, 4
  store i32 %597, ptr %592, align 4
  %598 = getelementptr inbounds %buffer, ptr %idxs, i32 0, i32 0
  %599 = load i32, ptr %598, align 4
  %600 = icmp sgt i32 %597, %599
  %601 = select i1 %600, i32 %597, i32 %599
  store i32 %601, ptr %598, align 4
  %vbo = alloca ptr, align 8
  store ptr null, ptr %vbo, align 8
  %verts_val = load %buffer, ptr %verts, align 8
  %gl_buf_in = alloca %buffer, align 8
  store %buffer %verts_val, ptr %gl_buf_in, align 8
  %602 = call ptr @aiml_gl_create_buffer(ptr %gl_buf_in)
  store ptr %602, ptr %vbo, align 8
  %ibo = alloca ptr, align 8
  store ptr null, ptr %ibo, align 8
  %idxs_val = load %buffer, ptr %idxs, align 8
  %gl_buf_in1 = alloca %buffer, align 8
  store %buffer %idxs_val, ptr %gl_buf_in1, align 8
  %603 = call ptr @aiml_gl_create_index_buffer(ptr %gl_buf_in1)
  store ptr %603, ptr %ibo, align 8
  %vao = alloca ptr, align 8
  store ptr null, ptr %vao, align 8
  %604 = call ptr @aiml_gl_create_vao()
  store ptr %604, ptr %vao, align 8
  %h_base = load ptr, ptr %vao, align 8
  %vbo_val = load ptr, ptr %vbo, align 8
  call void @aiml_gl_vao_bind_vbo(ptr %h_base, ptr %vbo_val)
  %h_base2 = load ptr, ptr %vao, align 8
  %ibo_val = load ptr, ptr %ibo, align 8
  call void @aiml_gl_vao_bind_ibo(ptr %h_base2, ptr %ibo_val)
  %h_base3 = load ptr, ptr %vao, align 8
  call void @aiml_gl_vao_attrib(ptr %h_base3, i32 0, i32 3, i32 12, i32 0)
  %print_call4 = call i32 (ptr, ...) @printf(ptr @4, ptr @3)
  %vert_src = alloca ptr, align 8
  store ptr null, ptr %vert_src, align 8
  store ptr @5, ptr %vert_src, align 8
  %frag_src = alloca ptr, align 8
  store ptr null, ptr %frag_src, align 8
  store ptr @6, ptr %frag_src, align 8
  %sh = alloca ptr, align 8
  store ptr null, ptr %sh, align 8
  %vert_src_val = load ptr, ptr %vert_src, align 8
  %frag_src_val = load ptr, ptr %frag_src, align 8
  %605 = call ptr @aiml_gl_compile_shader(ptr %vert_src_val, ptr %frag_src_val)
  store ptr %605, ptr %sh, align 8
  %print_call5 = call i32 (ptr, ...) @printf(ptr @8, ptr @7)
  %start_ms = alloca i64, align 8
  store i64 0, ptr %start_ms, align 8
  %606 = call i64 @time_now_ms()
  store i64 %606, ptr %start_ms, align 8
  %running = alloca i32, align 4
  store i32 0, ptr %running, align 4
  store i32 1, ptr %running, align 4
  %print_call6 = call i32 (ptr, ...) @printf(ptr @10, ptr @9)
  %h_base7 = load ptr, ptr %vao, align 8
  call void @aiml_gl_vao_delete(ptr %h_base7)
  %h_base8 = load ptr, ptr %vbo, align 8
  call void @aiml_gl_buffer_delete(ptr %h_base8)
  %h_base9 = load ptr, ptr %ibo, align 8
  call void @aiml_gl_buffer_delete(ptr %h_base9)
  %h_base10 = load ptr, ptr %sh, align 8
  call void @aiml_gl_shader_delete(ptr %h_base10)
  call void @glfwTerminate()
  %print_call11 = call i32 (ptr, ...) @printf(ptr @12, ptr @11)
  ret void
}

declare i32 @glfwInit()

declare void @glfwWindowHint(i32, i32)

declare ptr @glfwCreateWindow(i32, i32, ptr, ptr, ptr)

declare void @glfwMakeContextCurrent(ptr)

declare void @glfwSwapBuffers(ptr)

declare void @glfwPollEvents()

declare i32 @glfwWindowShouldClose(ptr)

declare void @glfwTerminate()

declare void @aiml_gl_init()

declare void @aiml_gl_enable_depth_test()

declare void @aiml_gl_viewport(i32, i32, i32, i32)

declare i32 @printf(ptr, ...)

declare ptr @aiml_gl_create_buffer(ptr)

declare ptr @aiml_gl_create_index_buffer(ptr)

declare ptr @aiml_gl_create_vao()

declare void @aiml_gl_vao_bind_vbo(ptr, ptr)

declare void @aiml_gl_vao_bind_ibo(ptr, ptr)

declare void @aiml_gl_vao_attrib(ptr, i32, i32, i32, i32)

declare ptr @aiml_gl_compile_shader(ptr, ptr)

declare i64 @time_now_ms()

declare void @aiml_gl_vao_delete(ptr)

declare void @aiml_gl_buffer_delete(ptr)

declare void @aiml_gl_shader_delete(ptr)
