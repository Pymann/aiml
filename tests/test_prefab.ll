; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@prefab_name = private unnamed_addr constant [9 x i8] c"TestCard\00", align 1
@prefab_tex = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@prefab_children = private unnamed_addr constant [499 x i8] c"[{\22name\22:\22card_name\22,\22type\22:\22label\22,\22x\22:70.000000,\22y\22:175.000000,\22width\22:0.000000,\22height\22:0.000000,\22font_size\22:11.000000,\22color\22:16777215,\22texture\22:\22\22,\22anchor\22:\22center\22},{\22name\22:\22art_area\22,\22type\22:\22sprite\22,\22x\22:15.000000,\22y\22:100.000000,\22width\22:110.000000,\22height\22:80.000000,\22font_size\22:12.000000,\22color\22:16777215,\22texture\22:\22\22,\22anchor\22:\22\22},{\22name\22:\22stat_bg\22,\22type\22:\22rect\22,\22x\22:0.000000,\22y\22:0.000000,\22width\22:140.000000,\22height\22:30.000000,\22font_size\22:12.000000,\22color\22:2236996,\22texture\22:\22\22,\22anchor\22:\22\22}]\00", align 1
@prefab_name.1 = private unnamed_addr constant [10 x i8] c"BoardZone\00", align 1
@prefab_tex.2 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@prefab_children.3 = private unnamed_addr constant [327 x i8] c"[{\22name\22:\22bg\22,\22type\22:\22rect\22,\22x\22:0.000000,\22y\22:0.000000,\22width\22:180.000000,\22height\22:140.000000,\22font_size\22:12.000000,\22color\22:1710638,\22texture\22:\22\22,\22anchor\22:\22\22},{\22name\22:\22zone_label\22,\22type\22:\22label\22,\22x\22:90.000000,\22y\22:130.000000,\22width\22:0.000000,\22height\22:0.000000,\22font_size\22:9.000000,\22color\22:4473958,\22texture\22:\22\22,\22anchor\22:\22center\22}]\00", align 1
@0 = private unnamed_addr constant [26 x i8] c"PASS: prefab registration\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [9 x i8] c"TestCard\00", align 1
@3 = private unnamed_addr constant [32 x i8] c"FAIL: instantiate returned null\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@6 = private unnamed_addr constant [25 x i8] c"PASS: prefab instantiate\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [19 x i8] c"PASS: get_x == 100\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [19 x i8] c"FAIL: get_x != 100\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [19 x i8] c"PASS: get_y == 200\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [19 x i8] c"FAIL: get_y != 200\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [23 x i8] c"PASS: get_scale == 1.5\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [23 x i8] c"FAIL: get_scale != 1.5\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [18 x i8] c"PASS: get_z == 10\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@22 = private unnamed_addr constant [18 x i8] c"FAIL: get_z != 10\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@24 = private unnamed_addr constant [34 x i8] c"PASS: get_visible == 0 after hide\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@26 = private unnamed_addr constant [23 x i8] c"FAIL: get_visible != 0\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@28 = private unnamed_addr constant [23 x i8] c"PASS: get_alpha == 0.5\00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@30 = private unnamed_addr constant [23 x i8] c"FAIL: get_alpha != 0.5\00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@31 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@32 = private unnamed_addr constant [7 x i8] c"width=\00", align 1
@fmt_f32 = private unnamed_addr constant [5 x i8] c"%.6g\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@33 = private unnamed_addr constant [9 x i8] c" height=\00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@34 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@35 = private unnamed_addr constant [29 x i8] c"PASS: width == 210 (140*1.5)\00", align 1
@print_null_str.19 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@36 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@37 = private unnamed_addr constant [19 x i8] c"FAIL: width != 210\00", align 1
@print_null_str.20 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@38 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@39 = private unnamed_addr constant [10 x i8] c"card_name\00", align 1
@40 = private unnamed_addr constant [12 x i8] c"Fire Dragon\00", align 1
@41 = private unnamed_addr constant [26 x i8] c"PASS: set_text (no crash)\00", align 1
@print_null_str.21 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@42 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@43 = private unnamed_addr constant [10 x i8] c"card_name\00", align 1
@44 = private unnamed_addr constant [27 x i8] c"PASS: set_color (no crash)\00", align 1
@print_null_str.22 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@45 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@46 = private unnamed_addr constant [9 x i8] c"art_area\00", align 1
@47 = private unnamed_addr constant [35 x i8] c"PASS: set_child_visible (no crash)\00", align 1
@print_null_str.23 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@48 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@49 = private unnamed_addr constant [10 x i8] c"BoardZone\00", align 1
@50 = private unnamed_addr constant [28 x i8] c"PASS: BoardZone instantiate\00", align 1
@print_null_str.24 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@51 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@52 = private unnamed_addr constant [42 x i8] c"FAIL: BoardZone instantiate returned null\00", align 1
@print_null_str.25 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@53 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@54 = private unnamed_addr constant [11 x i8] c"zone_label\00", align 1
@55 = private unnamed_addr constant [14 x i8] c"Combat Zone 1\00", align 1
@56 = private unnamed_addr constant [30 x i8] c"PASS: hit_test found instance\00", align 1
@print_null_str.26 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@57 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@58 = private unnamed_addr constant [29 x i8] c"FAIL: hit_test returned null\00", align 1
@print_null_str.27 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@59 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@60 = private unnamed_addr constant [33 x i8] c"PASS: hit_test miss returns null\00", align 1
@print_null_str.28 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@61 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@62 = private unnamed_addr constant [35 x i8] c"FAIL: hit_test miss should be null\00", align 1
@print_null_str.29 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@63 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@64 = private unnamed_addr constant [28 x i8] c"PASS: draw_scene (no crash)\00", align 1
@print_null_str.30 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@65 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@66 = private unnamed_addr constant [25 x i8] c"PASS: destroy (no crash)\00", align 1
@print_null_str.31 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@67 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@68 = private unnamed_addr constant [26 x i8] c"PASS: get_dragged == null\00", align 1
@print_null_str.32 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@69 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@70 = private unnamed_addr constant [33 x i8] c"FAIL: get_dragged should be null\00", align 1
@print_null_str.33 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@71 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@72 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@73 = private unnamed_addr constant [28 x i8] c"=== Prefab System Tests ===\00", align 1
@print_null_str.34 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@74 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@75 = private unnamed_addr constant [12 x i8] c"Prefab Test\00", align 1
@76 = private unnamed_addr constant [30 x i8] c"=== All Prefab Tests Done ===\00", align 1
@print_null_str.35 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@77 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [3 x [16 x ptr]] [[16 x ptr] [ptr @test_prefab_registration, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @test_prefab_instantiate, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null], [16 x ptr] [ptr @main, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [3 x ptr] [ptr @test_prefab_registration, ptr @test_prefab_instantiate, ptr @main]
@__aiml_fptable_versions = global [3 x i32] [i32 1, i32 1, i32 1]
@__aiml_fname_test_prefab_registration = private constant [25 x i8] c"test_prefab_registration\00"
@__aiml_fname_test_prefab_instantiate = private constant [24 x i8] c"test_prefab_instantiate\00"
@__aiml_fname_main = private constant [5 x i8] c"main\00"
@__aiml_func_names = constant [3 x ptr] [ptr @__aiml_fname_test_prefab_registration, ptr @__aiml_fname_test_prefab_instantiate, ptr @__aiml_fname_main]
@__aiml_func_count = constant i32 3
@__aiml_schema_count = constant i32 0

define void @test_prefab_registration() personality ptr @__gxx_personality_v0 {
entry:
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr @0)
  ret void
}

define void @test_prefab_instantiate(ptr %0) personality ptr @__gxx_personality_v0 {
entry:
  %win = alloca ptr, align 8
  store ptr %0, ptr %win, align 8
  call void @aiml_prefab_scene_init(i32 1280, i32 720)
  %font = alloca ptr, align 8
  store ptr null, ptr %font, align 8
  %1 = call ptr @aiml_ui_load_font_default(float 1.400000e+01)
  store ptr %1, ptr %font, align 8
  %font_val = load ptr, ptr %font, align 8
  call void @aiml_prefab_set_font(ptr %font_val)
  %card = alloca ptr, align 8
  store ptr null, ptr %card, align 8
  %2 = call ptr @aiml_prefab_instantiate(ptr @2)
  store ptr %2, ptr %card, align 8
  %card_val = load ptr, ptr %card, align 8
  %3 = icmp eq ptr %card_val, null
  br i1 %3, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  %print_call = call i32 (ptr, ...) @printf(ptr @4, ptr @3)
  ret void

if_merge:                                         ; preds = %entry
  %print_call1 = call i32 (ptr, ...) @printf(ptr @7, ptr @6)
  call void @aiml_prefab_set_pos(ptr %card, float 1.000000e+02, float 2.000000e+02)
  %px = alloca float, align 4
  store float 0.000000e+00, ptr %px, align 4
  %4 = call float @aiml_prefab_get_x(ptr %card)
  store float %4, ptr %px, align 4
  %py = alloca float, align 4
  store float 0.000000e+00, ptr %py, align 4
  %5 = call float @aiml_prefab_get_y(ptr %card)
  store float %5, ptr %py, align 4
  %px_val = load float, ptr %px, align 4
  %6 = fcmp oeq float %px_val, 1.000000e+02
  br i1 %6, label %if_then2, label %if_else

if_then2:                                         ; preds = %if_merge
  %print_call4 = call i32 (ptr, ...) @printf(ptr @9, ptr @8)
  br label %if_merge3

if_else:                                          ; preds = %if_merge
  %print_call5 = call i32 (ptr, ...) @printf(ptr @11, ptr @10)
  br label %if_merge3

if_merge3:                                        ; preds = %if_else, %if_then2
  %py_val = load float, ptr %py, align 4
  %7 = fcmp oeq float %py_val, 2.000000e+02
  br i1 %7, label %if_then6, label %if_else7

if_then6:                                         ; preds = %if_merge3
  %print_call9 = call i32 (ptr, ...) @printf(ptr @13, ptr @12)
  br label %if_merge8

if_else7:                                         ; preds = %if_merge3
  %print_call10 = call i32 (ptr, ...) @printf(ptr @15, ptr @14)
  br label %if_merge8

if_merge8:                                        ; preds = %if_else7, %if_then6
  call void @aiml_prefab_set_scale(ptr %card, float 1.500000e+00)
  %sc = alloca float, align 4
  store float 0.000000e+00, ptr %sc, align 4
  %8 = call float @aiml_prefab_get_scale(ptr %card)
  store float %8, ptr %sc, align 4
  %sc_val = load float, ptr %sc, align 4
  %9 = fcmp oeq float %sc_val, 1.500000e+00
  br i1 %9, label %if_then11, label %if_else12

if_then11:                                        ; preds = %if_merge8
  %print_call14 = call i32 (ptr, ...) @printf(ptr @17, ptr @16)
  br label %if_merge13

if_else12:                                        ; preds = %if_merge8
  %print_call15 = call i32 (ptr, ...) @printf(ptr @19, ptr @18)
  br label %if_merge13

if_merge13:                                       ; preds = %if_else12, %if_then11
  call void @aiml_prefab_set_z(ptr %card, i32 10)
  %z = alloca i32, align 4
  store i32 0, ptr %z, align 4
  %10 = call i32 @aiml_prefab_get_z(ptr %card)
  store i32 %10, ptr %z, align 4
  %z_val = load i32, ptr %z, align 4
  %11 = icmp eq i32 %z_val, 10
  br i1 %11, label %if_then16, label %if_else17

if_then16:                                        ; preds = %if_merge13
  %print_call19 = call i32 (ptr, ...) @printf(ptr @21, ptr @20)
  br label %if_merge18

if_else17:                                        ; preds = %if_merge13
  %print_call20 = call i32 (ptr, ...) @printf(ptr @23, ptr @22)
  br label %if_merge18

if_merge18:                                       ; preds = %if_else17, %if_then16
  call void @aiml_prefab_set_visible(ptr %card, i32 0)
  %vis = alloca i32, align 4
  store i32 0, ptr %vis, align 4
  %12 = call i32 @aiml_prefab_get_visible(ptr %card)
  store i32 %12, ptr %vis, align 4
  %vis_val = load i32, ptr %vis, align 4
  %13 = icmp eq i32 %vis_val, 0
  br i1 %13, label %if_then21, label %if_else22

if_then21:                                        ; preds = %if_merge18
  %print_call24 = call i32 (ptr, ...) @printf(ptr @25, ptr @24)
  br label %if_merge23

if_else22:                                        ; preds = %if_merge18
  %print_call25 = call i32 (ptr, ...) @printf(ptr @27, ptr @26)
  br label %if_merge23

if_merge23:                                       ; preds = %if_else22, %if_then21
  call void @aiml_prefab_set_visible(ptr %card, i32 1)
  call void @aiml_prefab_set_alpha(ptr %card, float 5.000000e-01)
  %al = alloca float, align 4
  store float 0.000000e+00, ptr %al, align 4
  %14 = call float @aiml_prefab_get_alpha(ptr %card)
  store float %14, ptr %al, align 4
  %al_val = load float, ptr %al, align 4
  %15 = fcmp oeq float %al_val, 5.000000e-01
  br i1 %15, label %if_then26, label %if_else27

if_then26:                                        ; preds = %if_merge23
  %print_call29 = call i32 (ptr, ...) @printf(ptr @29, ptr @28)
  br label %if_merge28

if_else27:                                        ; preds = %if_merge23
  %print_call30 = call i32 (ptr, ...) @printf(ptr @31, ptr @30)
  br label %if_merge28

if_merge28:                                       ; preds = %if_else27, %if_then26
  %w = alloca float, align 4
  store float 0.000000e+00, ptr %w, align 4
  %16 = call float @aiml_prefab_get_width(ptr %card)
  store float %16, ptr %w, align 4
  %h = alloca float, align 4
  store float 0.000000e+00, ptr %h, align 4
  %17 = call float @aiml_prefab_get_height(ptr %card)
  store float %17, ptr %h, align 4
  %w_val = load float, ptr %w, align 4
  %18 = call ptr @__aiml_f32_to_string(float %w_val)
  %19 = call ptr @__aiml_str_concat(ptr @32, ptr %18)
  %20 = call ptr @__aiml_str_concat(ptr %19, ptr @33)
  %h_val = load float, ptr %h, align 4
  %21 = call ptr @__aiml_f32_to_string(float %h_val)
  %22 = call ptr @__aiml_str_concat(ptr %20, ptr %21)
  %print_null_check = icmp eq ptr %22, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.18, ptr %22
  %print_call31 = call i32 (ptr, ...) @printf(ptr @34, ptr %print_safe_str)
  %w_val32 = load float, ptr %w, align 4
  %23 = fcmp oeq float %w_val32, 2.100000e+02
  br i1 %23, label %if_then33, label %if_else34

if_then33:                                        ; preds = %if_merge28
  %print_call36 = call i32 (ptr, ...) @printf(ptr @36, ptr @35)
  br label %if_merge35

if_else34:                                        ; preds = %if_merge28
  %print_call37 = call i32 (ptr, ...) @printf(ptr @38, ptr @37)
  br label %if_merge35

if_merge35:                                       ; preds = %if_else34, %if_then33
  call void @aiml_prefab_set_text(ptr %card, ptr @39, ptr @40)
  %print_call38 = call i32 (ptr, ...) @printf(ptr @42, ptr @41)
  call void @aiml_prefab_set_color(ptr %card, ptr @43, i32 16711680)
  %print_call39 = call i32 (ptr, ...) @printf(ptr @45, ptr @44)
  call void @aiml_prefab_set_child_visible(ptr %card, ptr @46, i32 0)
  %print_call40 = call i32 (ptr, ...) @printf(ptr @48, ptr @47)
  %zone = alloca ptr, align 8
  store ptr null, ptr %zone, align 8
  %24 = call ptr @aiml_prefab_instantiate(ptr @49)
  store ptr %24, ptr %zone, align 8
  %zone_val = load ptr, ptr %zone, align 8
  %25 = icmp ne ptr %zone_val, null
  br i1 %25, label %if_then41, label %if_else42

if_then41:                                        ; preds = %if_merge35
  %print_call44 = call i32 (ptr, ...) @printf(ptr @51, ptr @50)
  br label %if_merge43

if_else42:                                        ; preds = %if_merge35
  %print_call45 = call i32 (ptr, ...) @printf(ptr @53, ptr @52)
  br label %if_merge43

if_merge43:                                       ; preds = %if_else42, %if_then41
  call void @aiml_prefab_set_pos(ptr %zone, float 3.000000e+02, float 1.000000e+02)
  call void @aiml_prefab_set_text(ptr %zone, ptr @54, ptr @55)
  %hit = alloca ptr, align 8
  store ptr null, ptr %hit, align 8
  %26 = call ptr @aiml_prefab_hit_test(float 1.100000e+02, float 2.100000e+02)
  store ptr %26, ptr %hit, align 8
  %hit_val = load ptr, ptr %hit, align 8
  %27 = icmp ne ptr %hit_val, null
  br i1 %27, label %if_then46, label %if_else47

if_then46:                                        ; preds = %if_merge43
  %print_call49 = call i32 (ptr, ...) @printf(ptr @57, ptr @56)
  br label %if_merge48

if_else47:                                        ; preds = %if_merge43
  %print_call50 = call i32 (ptr, ...) @printf(ptr @59, ptr @58)
  br label %if_merge48

if_merge48:                                       ; preds = %if_else47, %if_then46
  %28 = call ptr @aiml_prefab_hit_test(float 0.000000e+00, float 0.000000e+00)
  store ptr %28, ptr %hit, align 8
  %hit_val51 = load ptr, ptr %hit, align 8
  %29 = icmp eq ptr %hit_val51, null
  br i1 %29, label %if_then52, label %if_else53

if_then52:                                        ; preds = %if_merge48
  %print_call55 = call i32 (ptr, ...) @printf(ptr @61, ptr @60)
  br label %if_merge54

if_else53:                                        ; preds = %if_merge48
  %print_call56 = call i32 (ptr, ...) @printf(ptr @63, ptr @62)
  br label %if_merge54

if_merge54:                                       ; preds = %if_else53, %if_then52
  call void @aiml_prefab_draw_scene()
  %print_call57 = call i32 (ptr, ...) @printf(ptr @65, ptr @64)
  %card_val58 = load ptr, ptr %card, align 8
  call void @aiml_prefab_destroy(ptr %card_val58)
  %print_call59 = call i32 (ptr, ...) @printf(ptr @67, ptr @66)
  %drag = alloca ptr, align 8
  store ptr null, ptr %drag, align 8
  %30 = call ptr @aiml_prefab_get_dragged()
  store ptr %30, ptr %drag, align 8
  %drag_val = load ptr, ptr %drag, align 8
  %31 = icmp eq ptr %drag_val, null
  br i1 %31, label %if_then60, label %if_else61

if_then60:                                        ; preds = %if_merge54
  %print_call63 = call i32 (ptr, ...) @printf(ptr @69, ptr @68)
  br label %if_merge62

if_else61:                                        ; preds = %if_merge54
  %print_call64 = call i32 (ptr, ...) @printf(ptr @71, ptr @70)
  br label %if_merge62

if_merge62:                                       ; preds = %if_else61, %if_then60
  %zone_val65 = load ptr, ptr %zone, align 8
  call void @aiml_prefab_destroy(ptr %zone_val65)
  ret void
}

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %print_call = call i32 (ptr, ...) @printf(ptr @74, ptr @73)
  call void @test_prefab_registration()
  %win = alloca ptr, align 8
  store ptr null, ptr %win, align 8
  %0 = call ptr @aiml_engine_create_window(i32 800, i32 600, ptr @75)
  store ptr %0, ptr %win, align 8
  call void @aiml_gl_init()
  call void @aiml_ui_init()
  %win_val = load ptr, ptr %win, align 8
  call void @test_prefab_instantiate(ptr %win_val)
  %win_val1 = load ptr, ptr %win, align 8
  call void @aiml_engine_destroy_window(ptr %win_val1)
  %print_call2 = call i32 (ptr, ...) @printf(ptr @77, ptr @76)
  ret i32 0
}

define void @__aiml_init() {
entry:
  call void @aiml_prefab_register(ptr @prefab_name, float 1.400000e+02, float 1.960000e+02, ptr @prefab_tex, i32 1, ptr @prefab_children)
  call void @aiml_prefab_register(ptr @prefab_name.1, float 1.800000e+02, float 1.400000e+02, ptr @prefab_tex.2, i32 0, ptr @prefab_children.3)
  ret void
}

declare void @aiml_prefab_register(ptr, float, float, ptr, i32, ptr)

declare i32 @__gxx_personality_v0(...)

declare i32 @printf(ptr, ...)

declare void @aiml_prefab_scene_init(i32, i32)

declare ptr @aiml_ui_load_font_default(float)

declare void @aiml_prefab_set_font(ptr)

declare ptr @aiml_prefab_instantiate(ptr)

declare void @aiml_prefab_set_pos(ptr, float, float)

declare float @aiml_prefab_get_x(ptr)

declare float @aiml_prefab_get_y(ptr)

declare void @aiml_prefab_set_scale(ptr, float)

declare float @aiml_prefab_get_scale(ptr)

declare void @aiml_prefab_set_z(ptr, i32)

declare i32 @aiml_prefab_get_z(ptr)

declare void @aiml_prefab_set_visible(ptr, i32)

declare i32 @aiml_prefab_get_visible(ptr)

declare void @aiml_prefab_set_alpha(ptr, float)

declare float @aiml_prefab_get_alpha(ptr)

declare float @aiml_prefab_get_width(ptr)

declare float @aiml_prefab_get_height(ptr)

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

declare void @aiml_prefab_set_text(ptr, ptr, ptr)

declare void @aiml_prefab_set_color(ptr, ptr, i32)

declare void @aiml_prefab_set_child_visible(ptr, ptr, i32)

declare ptr @aiml_prefab_hit_test(float, float)

declare void @aiml_prefab_draw_scene()

declare void @aiml_prefab_destroy(ptr)

declare ptr @aiml_prefab_get_dragged()

declare ptr @aiml_engine_create_window(i32, i32, ptr)

declare void @aiml_gl_init()

declare void @aiml_ui_init()

declare void @aiml_engine_destroy_window(ptr)

attributes #0 = { noinline }
