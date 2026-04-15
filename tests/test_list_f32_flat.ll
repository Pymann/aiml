; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%list_f32 = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@__global_pass = global i32 0
@__global_fail = global i32 0
@__global_data = global %list_f32 zeroinitializer
@__global_v0 = global float 0.000000e+00
@__global_v1 = global float 0.000000e+00
@__global_idx = global i32 0
@__global_v2 = global float 0.000000e+00
@__global_w0 = global float 0.000000e+00
@__global_wi = global i32 0
@__global_w1 = global float 0.000000e+00
@__global_dim = global i32 0
@__global_n_tokens = global i32 0
@__global_flat = global %list_f32 zeroinitializer
@__global_total = global i32 0
@__global_fi = global i32 0
@__global_t0_base = global i32 0
@__global_t0_1 = global i32 0
@__global_t0_2 = global i32 0
@__global_t0_3 = global i32 0
@__global_t2_base = global i32 0
@__global_t2_1 = global i32 0
@__global_t2_2 = global i32 0
@__global_t2_3 = global i32 0
@__global_r0 = global float 0.000000e+00
@__global_r1 = global float 0.000000e+00
@__global_t2_read_base = global i32 0
@__global_t2r0 = global float 0.000000e+00
@__global_t2r1_idx = global i32 0
@__global_t2r1 = global float 0.000000e+00
@__global_t1_base = global i32 0
@__global_t1r0 = global float 0.000000e+00
@__global_lr = global float 0.000000e+00
@__global_grad0 = global float 0.000000e+00
@__global_cur = global float 0.000000e+00
@__global_updated = global float 0.000000e+00
@__global_after_sgd = global float 0.000000e+00
@__global_g1 = global float 0.000000e+00
@0 = private unnamed_addr constant [21 x i8] c"len after 3 pushes: \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [9 x i8] c"FAIL len\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [9 x i8] c"data-0: \00", align 1
@fmt_f32 = private unnamed_addr constant [5 x i8] c"%.6g\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [18 x i8] c"FAIL read index 0\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [9 x i8] c"data-1: \00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [18 x i8] c"FAIL read index 1\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [6 x i8] c"data-\00", align 1
@13 = private unnamed_addr constant [3 x i8] c": \00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [24 x i8] c"FAIL dynamic index read\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [19 x i8] c"after set data-0: \00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [25 x i8] c"FAIL index write literal\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [16 x i8] c"after set data-\00", align 1
@22 = private unnamed_addr constant [3 x i8] c": \00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@24 = private unnamed_addr constant [25 x i8] c"FAIL dynamic index write\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@26 = private unnamed_addr constant [20 x i8] c"flat len (3*4=12): \00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@28 = private unnamed_addr constant [19 x i8] c"FAIL flat init len\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@30 = private unnamed_addr constant [10 x i8] c"token0: [\00", align 1
@31 = private unnamed_addr constant [3 x i8] c", \00", align 1
@32 = private unnamed_addr constant [7 x i8] c", ...]\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@33 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@34 = private unnamed_addr constant [20 x i8] c"FAIL flat token0[0]\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@36 = private unnamed_addr constant [10 x i8] c"token2: [\00", align 1
@37 = private unnamed_addr constant [3 x i8] c", \00", align 1
@38 = private unnamed_addr constant [7 x i8] c", ...]\00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@40 = private unnamed_addr constant [20 x i8] c"FAIL flat token2[0]\00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [20 x i8] c"FAIL flat token2[1]\00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@43 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@44 = private unnamed_addr constant [26 x i8] c"token1[0] (should be 0): \00", align 1
@print_null_str.19 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@45 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@46 = private unnamed_addr constant [22 x i8] c"FAIL flat token1 zero\00", align 1
@print_null_str.20 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@47 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@48 = private unnamed_addr constant [29 x i8] c"after SGD: 0.1 - 0.01*2.0 = \00", align 1
@print_null_str.21 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@49 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@50 = private unnamed_addr constant [16 x i8] c"FAIL SGD update\00", align 1
@print_null_str.22 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@51 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@52 = private unnamed_addr constant [14 x i8] c"flat.get(1): \00", align 1
@print_null_str.23 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@53 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@54 = private unnamed_addr constant [18 x i8] c"FAIL list.get f32\00", align 1
@print_null_str.24 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@55 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@56 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str.25 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@57 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@58 = private unnamed_addr constant [25 x i8] c"list<f32> flat storage: \00", align 1
@59 = private unnamed_addr constant [10 x i8] c" passed, \00", align 1
@60 = private unnamed_addr constant [8 x i8] c" failed\00", align 1
@print_null_str.26 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@61 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  store i32 0, ptr @__global_pass, align 4
  store i32 0, ptr @__global_fail, align 4
  store i32 0, ptr @__global_data, align 4
  store i32 4, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 1), align 4
  %0 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  store ptr %0, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %size = load i32, ptr @__global_data, align 4
  %capacity = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 1), align 4
  %1 = icmp eq i32 %size, %capacity
  br i1 %1, label %grow, label %cont

grow:                                             ; preds = %entry
  %2 = mul i32 %capacity, 2
  %3 = icmp sgt i32 %2, 0
  %4 = select i1 %3, i32 %2, i32 4
  store i32 %4, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 1), align 4
  %5 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %6 = zext i32 %4 to i64
  %7 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %6
  %8 = call ptr @realloc(ptr %5, i64 %7)
  store ptr %8, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  br label %cont

cont:                                             ; preds = %grow, %entry
  %dataBase = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %9 = getelementptr inbounds float, ptr %dataBase, i32 %size
  store float 1.000000e+00, ptr %9, align 4
  %10 = add i32 %size, 1
  store i32 %10, ptr @__global_data, align 4
  %size1 = load i32, ptr @__global_data, align 4
  %capacity2 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 1), align 4
  %11 = icmp eq i32 %size1, %capacity2
  br i1 %11, label %grow3, label %cont4

grow3:                                            ; preds = %cont
  %12 = mul i32 %capacity2, 2
  %13 = icmp sgt i32 %12, 0
  %14 = select i1 %13, i32 %12, i32 4
  store i32 %14, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 1), align 4
  %15 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %16 = zext i32 %14 to i64
  %17 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %16
  %18 = call ptr @realloc(ptr %15, i64 %17)
  store ptr %18, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  br label %cont4

cont4:                                            ; preds = %grow3, %cont
  %dataBase5 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %19 = getelementptr inbounds float, ptr %dataBase5, i32 %size1
  store float 2.500000e+00, ptr %19, align 4
  %20 = add i32 %size1, 1
  store i32 %20, ptr @__global_data, align 4
  %size6 = load i32, ptr @__global_data, align 4
  %capacity7 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 1), align 4
  %21 = icmp eq i32 %size6, %capacity7
  br i1 %21, label %grow8, label %cont9

grow8:                                            ; preds = %cont4
  %22 = mul i32 %capacity7, 2
  %23 = icmp sgt i32 %22, 0
  %24 = select i1 %23, i32 %22, i32 4
  store i32 %24, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 1), align 4
  %25 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %26 = zext i32 %24 to i64
  %27 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %26
  %28 = call ptr @realloc(ptr %25, i64 %27)
  store ptr %28, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  br label %cont9

cont9:                                            ; preds = %grow8, %cont4
  %dataBase10 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %29 = getelementptr inbounds float, ptr %dataBase10, i32 %size6
  store float 0xBFD3333340000000, ptr %29, align 4
  %30 = add i32 %size6, 1
  store i32 %30, ptr @__global_data, align 4
  %sizeVal = load i32, ptr @__global_data, align 4
  %31 = call ptr @__aiml_int_to_string(i32 %sizeVal)
  %32 = call ptr @__aiml_str_concat(ptr @0, ptr %31)
  %print_null_check = icmp eq ptr %32, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %32
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr %print_safe_str)
  %sizeVal11 = load i32, ptr @__global_data, align 4
  %33 = icmp eq i32 %sizeVal11, 3
  br i1 %33, label %if_then, label %if_else

if_then:                                          ; preds = %cont9
  %pass_val = load i32, ptr @__global_pass, align 4
  %34 = add i32 %pass_val, 1
  store i32 %34, ptr @__global_pass, align 4
  br label %if_merge

if_else:                                          ; preds = %cont9
  %fail_val = load i32, ptr @__global_fail, align 4
  %35 = add i32 %fail_val, 1
  store i32 %35, ptr @__global_fail, align 4
  %print_call12 = call i32 (ptr, ...) @printf(ptr @3, ptr @2)
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  %dataBase13 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %36 = getelementptr inbounds float, ptr %dataBase13, i32 0
  %lst_elem = load float, ptr %36, align 4
  store float %lst_elem, ptr @__global_v0, align 4
  %v0_val = load float, ptr @__global_v0, align 4
  %37 = call ptr @__aiml_f32_to_string(float %v0_val)
  %38 = call ptr @__aiml_str_concat(ptr @4, ptr %37)
  %print_null_check14 = icmp eq ptr %38, null
  %print_safe_str15 = select i1 %print_null_check14, ptr @print_null_str.2, ptr %38
  %print_call16 = call i32 (ptr, ...) @printf(ptr @5, ptr %print_safe_str15)
  %v0_val17 = load float, ptr @__global_v0, align 4
  %39 = fcmp ogt float %v0_val17, 0x3FEFAE1480000000
  %v0_val18 = load float, ptr @__global_v0, align 4
  %40 = fcmp olt float %v0_val18, 0x3FF028F5C0000000
  %and = and i1 %39, %40
  br i1 %and, label %if_then19, label %if_else20

if_then19:                                        ; preds = %if_merge
  %pass_val22 = load i32, ptr @__global_pass, align 4
  %41 = add i32 %pass_val22, 1
  store i32 %41, ptr @__global_pass, align 4
  br label %if_merge21

if_else20:                                        ; preds = %if_merge
  %fail_val23 = load i32, ptr @__global_fail, align 4
  %42 = add i32 %fail_val23, 1
  store i32 %42, ptr @__global_fail, align 4
  %print_call24 = call i32 (ptr, ...) @printf(ptr @7, ptr @6)
  br label %if_merge21

if_merge21:                                       ; preds = %if_else20, %if_then19
  %dataBase25 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %43 = getelementptr inbounds float, ptr %dataBase25, i32 1
  %lst_elem26 = load float, ptr %43, align 4
  store float %lst_elem26, ptr @__global_v1, align 4
  %v1_val = load float, ptr @__global_v1, align 4
  %44 = call ptr @__aiml_f32_to_string(float %v1_val)
  %45 = call ptr @__aiml_str_concat(ptr @8, ptr %44)
  %print_null_check27 = icmp eq ptr %45, null
  %print_safe_str28 = select i1 %print_null_check27, ptr @print_null_str.4, ptr %45
  %print_call29 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str28)
  %v1_val30 = load float, ptr @__global_v1, align 4
  %46 = fcmp ogt float %v1_val30, 0x4003EB8520000000
  %v1_val31 = load float, ptr @__global_v1, align 4
  %47 = fcmp olt float %v1_val31, 0x4004147AE0000000
  %and32 = and i1 %46, %47
  br i1 %and32, label %if_then33, label %if_else34

if_then33:                                        ; preds = %if_merge21
  %pass_val36 = load i32, ptr @__global_pass, align 4
  %48 = add i32 %pass_val36, 1
  store i32 %48, ptr @__global_pass, align 4
  br label %if_merge35

if_else34:                                        ; preds = %if_merge21
  %fail_val37 = load i32, ptr @__global_fail, align 4
  %49 = add i32 %fail_val37, 1
  store i32 %49, ptr @__global_fail, align 4
  %print_call38 = call i32 (ptr, ...) @printf(ptr @11, ptr @10)
  br label %if_merge35

if_merge35:                                       ; preds = %if_else34, %if_then33
  store i32 2, ptr @__global_idx, align 4
  %50 = load i32, ptr @__global_idx, align 4
  %dataBase39 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %51 = getelementptr inbounds float, ptr %dataBase39, i32 %50
  %lst_elem40 = load float, ptr %51, align 4
  store float %lst_elem40, ptr @__global_v2, align 4
  %idx_val = load i32, ptr @__global_idx, align 4
  %52 = call ptr @__aiml_int_to_string(i32 %idx_val)
  %53 = call ptr @__aiml_str_concat(ptr @12, ptr %52)
  %54 = call ptr @__aiml_str_concat(ptr %53, ptr @13)
  %v2_val = load float, ptr @__global_v2, align 4
  %55 = call ptr @__aiml_f32_to_string(float %v2_val)
  %56 = call ptr @__aiml_str_concat(ptr %54, ptr %55)
  %print_null_check41 = icmp eq ptr %56, null
  %print_safe_str42 = select i1 %print_null_check41, ptr @print_null_str.6, ptr %56
  %print_call43 = call i32 (ptr, ...) @printf(ptr @14, ptr %print_safe_str42)
  %v2_val44 = load float, ptr @__global_v2, align 4
  %57 = fcmp ogt float %v2_val44, 0xBFD3D70A40000000
  %v2_val45 = load float, ptr @__global_v2, align 4
  %58 = fcmp olt float %v2_val45, 0xBFD28F5C20000000
  %and46 = and i1 %57, %58
  br i1 %and46, label %if_then47, label %if_else48

if_then47:                                        ; preds = %if_merge35
  %pass_val50 = load i32, ptr @__global_pass, align 4
  %59 = add i32 %pass_val50, 1
  store i32 %59, ptr @__global_pass, align 4
  br label %if_merge49

if_else48:                                        ; preds = %if_merge35
  %fail_val51 = load i32, ptr @__global_fail, align 4
  %60 = add i32 %fail_val51, 1
  store i32 %60, ptr @__global_fail, align 4
  %print_call52 = call i32 (ptr, ...) @printf(ptr @16, ptr @15)
  br label %if_merge49

if_merge49:                                       ; preds = %if_else48, %if_then47
  %dataBase53 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %61 = getelementptr inbounds float, ptr %dataBase53, i32 0
  store float 0x4023FAE140000000, ptr %61, align 4
  %dataBase54 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %62 = getelementptr inbounds float, ptr %dataBase54, i32 0
  %lst_elem55 = load float, ptr %62, align 4
  store float %lst_elem55, ptr @__global_w0, align 4
  %w0_val = load float, ptr @__global_w0, align 4
  %63 = call ptr @__aiml_f32_to_string(float %w0_val)
  %64 = call ptr @__aiml_str_concat(ptr @17, ptr %63)
  %print_null_check56 = icmp eq ptr %64, null
  %print_safe_str57 = select i1 %print_null_check56, ptr @print_null_str.8, ptr %64
  %print_call58 = call i32 (ptr, ...) @printf(ptr @18, ptr %print_safe_str57)
  %w0_val59 = load float, ptr @__global_w0, align 4
  %65 = fcmp ogt float %w0_val59, 0x4023F5C280000000
  %w0_val60 = load float, ptr @__global_w0, align 4
  %66 = fcmp olt float %w0_val60, 1.000000e+01
  %and61 = and i1 %65, %66
  br i1 %and61, label %if_then62, label %if_else63

if_then62:                                        ; preds = %if_merge49
  %pass_val65 = load i32, ptr @__global_pass, align 4
  %67 = add i32 %pass_val65, 1
  store i32 %67, ptr @__global_pass, align 4
  br label %if_merge64

if_else63:                                        ; preds = %if_merge49
  %fail_val66 = load i32, ptr @__global_fail, align 4
  %68 = add i32 %fail_val66, 1
  store i32 %68, ptr @__global_fail, align 4
  %print_call67 = call i32 (ptr, ...) @printf(ptr @20, ptr @19)
  br label %if_merge64

if_merge64:                                       ; preds = %if_else63, %if_then62
  store i32 1, ptr @__global_wi, align 4
  %69 = load i32, ptr @__global_wi, align 4
  %dataBase68 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %70 = getelementptr inbounds float, ptr %dataBase68, i32 %69
  store float 0x401F147AE0000000, ptr %70, align 4
  %dataBase69 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_data, i32 0, i32 2), align 8
  %71 = getelementptr inbounds float, ptr %dataBase69, i32 1
  %lst_elem70 = load float, ptr %71, align 4
  store float %lst_elem70, ptr @__global_w1, align 4
  %wi_val = load i32, ptr @__global_wi, align 4
  %72 = call ptr @__aiml_int_to_string(i32 %wi_val)
  %73 = call ptr @__aiml_str_concat(ptr @21, ptr %72)
  %74 = call ptr @__aiml_str_concat(ptr %73, ptr @22)
  %w1_val = load float, ptr @__global_w1, align 4
  %75 = call ptr @__aiml_f32_to_string(float %w1_val)
  %76 = call ptr @__aiml_str_concat(ptr %74, ptr %75)
  %print_null_check71 = icmp eq ptr %76, null
  %print_safe_str72 = select i1 %print_null_check71, ptr @print_null_str.10, ptr %76
  %print_call73 = call i32 (ptr, ...) @printf(ptr @23, ptr %print_safe_str72)
  %w1_val74 = load float, ptr @__global_w1, align 4
  %77 = fcmp ogt float %w1_val74, 0x401F0A3D80000000
  %w1_val75 = load float, ptr @__global_w1, align 4
  %78 = fcmp olt float %w1_val75, 0x401F1EB860000000
  %and76 = and i1 %77, %78
  br i1 %and76, label %if_then77, label %if_else78

if_then77:                                        ; preds = %if_merge64
  %pass_val80 = load i32, ptr @__global_pass, align 4
  %79 = add i32 %pass_val80, 1
  store i32 %79, ptr @__global_pass, align 4
  br label %if_merge79

if_else78:                                        ; preds = %if_merge64
  %fail_val81 = load i32, ptr @__global_fail, align 4
  %80 = add i32 %fail_val81, 1
  store i32 %80, ptr @__global_fail, align 4
  %print_call82 = call i32 (ptr, ...) @printf(ptr @25, ptr @24)
  br label %if_merge79

if_merge79:                                       ; preds = %if_else78, %if_then77
  store i32 4, ptr @__global_dim, align 4
  store i32 3, ptr @__global_n_tokens, align 4
  store i32 0, ptr @__global_flat, align 4
  store i32 4, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 1), align 4
  %81 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), i64 4))
  store ptr %81, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %n_tokens_val = load i32, ptr @__global_n_tokens, align 4
  %dim_val = load i32, ptr @__global_dim, align 4
  %82 = mul i32 %n_tokens_val, %dim_val
  store i32 %82, ptr @__global_total, align 4
  store i32 0, ptr @__global_fi, align 4
  br label %while_loop

while_loop:                                       ; preds = %cont86, %if_merge79
  %fi_val = load i32, ptr @__global_fi, align 4
  %total_val = load i32, ptr @__global_total, align 4
  %83 = icmp slt i32 %fi_val, %total_val
  br i1 %83, label %while_body, label %while_end

while_body:                                       ; preds = %while_loop
  %size83 = load i32, ptr @__global_flat, align 4
  %capacity84 = load i32, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 1), align 4
  %84 = icmp eq i32 %size83, %capacity84
  br i1 %84, label %grow85, label %cont86

while_end:                                        ; preds = %while_loop
  %sizeVal89 = load i32, ptr @__global_flat, align 4
  %85 = call ptr @__aiml_int_to_string(i32 %sizeVal89)
  %86 = call ptr @__aiml_str_concat(ptr @26, ptr %85)
  %print_null_check90 = icmp eq ptr %86, null
  %print_safe_str91 = select i1 %print_null_check90, ptr @print_null_str.12, ptr %86
  %print_call92 = call i32 (ptr, ...) @printf(ptr @27, ptr %print_safe_str91)
  %sizeVal93 = load i32, ptr @__global_flat, align 4
  %87 = icmp eq i32 %sizeVal93, 12
  br i1 %87, label %if_then94, label %if_else95

grow85:                                           ; preds = %while_body
  %88 = mul i32 %capacity84, 2
  %89 = icmp sgt i32 %88, 0
  %90 = select i1 %89, i32 %88, i32 4
  store i32 %90, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 1), align 4
  %91 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %92 = zext i32 %90 to i64
  %93 = mul i64 ptrtoint (ptr getelementptr inbounds (float, ptr null, i32 1) to i64), %92
  %94 = call ptr @realloc(ptr %91, i64 %93)
  store ptr %94, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  br label %cont86

cont86:                                           ; preds = %grow85, %while_body
  %dataBase87 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %95 = getelementptr inbounds float, ptr %dataBase87, i32 %size83
  store float 0.000000e+00, ptr %95, align 4
  %96 = add i32 %size83, 1
  store i32 %96, ptr @__global_flat, align 4
  %fi_val88 = load i32, ptr @__global_fi, align 4
  %97 = add i32 %fi_val88, 1
  store i32 %97, ptr @__global_fi, align 4
  br label %while_loop

if_then94:                                        ; preds = %while_end
  %pass_val97 = load i32, ptr @__global_pass, align 4
  %98 = add i32 %pass_val97, 1
  store i32 %98, ptr @__global_pass, align 4
  br label %if_merge96

if_else95:                                        ; preds = %while_end
  %fail_val98 = load i32, ptr @__global_fail, align 4
  %99 = add i32 %fail_val98, 1
  store i32 %99, ptr @__global_fail, align 4
  %print_call99 = call i32 (ptr, ...) @printf(ptr @29, ptr @28)
  br label %if_merge96

if_merge96:                                       ; preds = %if_else95, %if_then94
  store i32 0, ptr @__global_t0_base, align 4
  %100 = load i32, ptr @__global_t0_base, align 4
  %dataBase100 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %101 = getelementptr inbounds float, ptr %dataBase100, i32 %100
  store float 0x3FB99999A0000000, ptr %101, align 4
  %t0_base_val = load i32, ptr @__global_t0_base, align 4
  %102 = add i32 %t0_base_val, 1
  store i32 %102, ptr @__global_t0_1, align 4
  %103 = load i32, ptr @__global_t0_1, align 4
  %dataBase101 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %104 = getelementptr inbounds float, ptr %dataBase101, i32 %103
  store float 0x3FC99999A0000000, ptr %104, align 4
  %t0_base_val102 = load i32, ptr @__global_t0_base, align 4
  %105 = add i32 %t0_base_val102, 2
  store i32 %105, ptr @__global_t0_2, align 4
  %106 = load i32, ptr @__global_t0_2, align 4
  %dataBase103 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %107 = getelementptr inbounds float, ptr %dataBase103, i32 %106
  store float 0x3FD3333340000000, ptr %107, align 4
  %t0_base_val104 = load i32, ptr @__global_t0_base, align 4
  %108 = add i32 %t0_base_val104, 3
  store i32 %108, ptr @__global_t0_3, align 4
  %109 = load i32, ptr @__global_t0_3, align 4
  %dataBase105 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %110 = getelementptr inbounds float, ptr %dataBase105, i32 %109
  store float 0x3FD99999A0000000, ptr %110, align 4
  %dim_val106 = load i32, ptr @__global_dim, align 4
  %111 = mul i32 2, %dim_val106
  store i32 %111, ptr @__global_t2_base, align 4
  %112 = load i32, ptr @__global_t2_base, align 4
  %dataBase107 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %113 = getelementptr inbounds float, ptr %dataBase107, i32 %112
  store float 1.000000e+00, ptr %113, align 4
  %t2_base_val = load i32, ptr @__global_t2_base, align 4
  %114 = add i32 %t2_base_val, 1
  store i32 %114, ptr @__global_t2_1, align 4
  %115 = load i32, ptr @__global_t2_1, align 4
  %dataBase108 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %116 = getelementptr inbounds float, ptr %dataBase108, i32 %115
  store float 2.000000e+00, ptr %116, align 4
  %t2_base_val109 = load i32, ptr @__global_t2_base, align 4
  %117 = add i32 %t2_base_val109, 2
  store i32 %117, ptr @__global_t2_2, align 4
  %118 = load i32, ptr @__global_t2_2, align 4
  %dataBase110 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %119 = getelementptr inbounds float, ptr %dataBase110, i32 %118
  store float 3.000000e+00, ptr %119, align 4
  %t2_base_val111 = load i32, ptr @__global_t2_base, align 4
  %120 = add i32 %t2_base_val111, 3
  store i32 %120, ptr @__global_t2_3, align 4
  %121 = load i32, ptr @__global_t2_3, align 4
  %dataBase112 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %122 = getelementptr inbounds float, ptr %dataBase112, i32 %121
  store float 4.000000e+00, ptr %122, align 4
  %dataBase113 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %123 = getelementptr inbounds float, ptr %dataBase113, i32 0
  %lst_elem114 = load float, ptr %123, align 4
  store float %lst_elem114, ptr @__global_r0, align 4
  %dataBase115 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %124 = getelementptr inbounds float, ptr %dataBase115, i32 1
  %lst_elem116 = load float, ptr %124, align 4
  store float %lst_elem116, ptr @__global_r1, align 4
  %r0_val = load float, ptr @__global_r0, align 4
  %125 = call ptr @__aiml_f32_to_string(float %r0_val)
  %126 = call ptr @__aiml_str_concat(ptr @30, ptr %125)
  %127 = call ptr @__aiml_str_concat(ptr %126, ptr @31)
  %r1_val = load float, ptr @__global_r1, align 4
  %128 = call ptr @__aiml_f32_to_string(float %r1_val)
  %129 = call ptr @__aiml_str_concat(ptr %127, ptr %128)
  %130 = call ptr @__aiml_str_concat(ptr %129, ptr @32)
  %print_null_check117 = icmp eq ptr %130, null
  %print_safe_str118 = select i1 %print_null_check117, ptr @print_null_str.14, ptr %130
  %print_call119 = call i32 (ptr, ...) @printf(ptr @33, ptr %print_safe_str118)
  %r0_val120 = load float, ptr @__global_r0, align 4
  %131 = fcmp ogt float %r0_val120, 0x3FB70A3D80000000
  %r0_val121 = load float, ptr @__global_r0, align 4
  %132 = fcmp olt float %r0_val121, 0x3FBC28F5C0000000
  %and122 = and i1 %131, %132
  br i1 %and122, label %if_then123, label %if_else124

if_then123:                                       ; preds = %if_merge96
  %pass_val126 = load i32, ptr @__global_pass, align 4
  %133 = add i32 %pass_val126, 1
  store i32 %133, ptr @__global_pass, align 4
  br label %if_merge125

if_else124:                                       ; preds = %if_merge96
  %fail_val127 = load i32, ptr @__global_fail, align 4
  %134 = add i32 %fail_val127, 1
  store i32 %134, ptr @__global_fail, align 4
  %print_call128 = call i32 (ptr, ...) @printf(ptr @35, ptr @34)
  br label %if_merge125

if_merge125:                                      ; preds = %if_else124, %if_then123
  %dim_val129 = load i32, ptr @__global_dim, align 4
  %135 = mul i32 2, %dim_val129
  store i32 %135, ptr @__global_t2_read_base, align 4
  %136 = load i32, ptr @__global_t2_read_base, align 4
  %dataBase130 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %137 = getelementptr inbounds float, ptr %dataBase130, i32 %136
  %lst_elem131 = load float, ptr %137, align 4
  store float %lst_elem131, ptr @__global_t2r0, align 4
  %t2_read_base_val = load i32, ptr @__global_t2_read_base, align 4
  %138 = add i32 %t2_read_base_val, 1
  store i32 %138, ptr @__global_t2r1_idx, align 4
  %139 = load i32, ptr @__global_t2r1_idx, align 4
  %dataBase132 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %140 = getelementptr inbounds float, ptr %dataBase132, i32 %139
  %lst_elem133 = load float, ptr %140, align 4
  store float %lst_elem133, ptr @__global_t2r1, align 4
  %t2r0_val = load float, ptr @__global_t2r0, align 4
  %141 = call ptr @__aiml_f32_to_string(float %t2r0_val)
  %142 = call ptr @__aiml_str_concat(ptr @36, ptr %141)
  %143 = call ptr @__aiml_str_concat(ptr %142, ptr @37)
  %t2r1_val = load float, ptr @__global_t2r1, align 4
  %144 = call ptr @__aiml_f32_to_string(float %t2r1_val)
  %145 = call ptr @__aiml_str_concat(ptr %143, ptr %144)
  %146 = call ptr @__aiml_str_concat(ptr %145, ptr @38)
  %print_null_check134 = icmp eq ptr %146, null
  %print_safe_str135 = select i1 %print_null_check134, ptr @print_null_str.16, ptr %146
  %print_call136 = call i32 (ptr, ...) @printf(ptr @39, ptr %print_safe_str135)
  %t2r0_val137 = load float, ptr @__global_t2r0, align 4
  %147 = fcmp ogt float %t2r0_val137, 0x3FEFAE1480000000
  %t2r0_val138 = load float, ptr @__global_t2r0, align 4
  %148 = fcmp olt float %t2r0_val138, 0x3FF028F5C0000000
  %and139 = and i1 %147, %148
  br i1 %and139, label %if_then140, label %if_else141

if_then140:                                       ; preds = %if_merge125
  %pass_val143 = load i32, ptr @__global_pass, align 4
  %149 = add i32 %pass_val143, 1
  store i32 %149, ptr @__global_pass, align 4
  br label %if_merge142

if_else141:                                       ; preds = %if_merge125
  %fail_val144 = load i32, ptr @__global_fail, align 4
  %150 = add i32 %fail_val144, 1
  store i32 %150, ptr @__global_fail, align 4
  %print_call145 = call i32 (ptr, ...) @printf(ptr @41, ptr @40)
  br label %if_merge142

if_merge142:                                      ; preds = %if_else141, %if_then140
  %t2r1_val146 = load float, ptr @__global_t2r1, align 4
  %151 = fcmp ogt float %t2r1_val146, 0x3FFFD70A40000000
  %t2r1_val147 = load float, ptr @__global_t2r1, align 4
  %152 = fcmp olt float %t2r1_val147, 0x4000147AE0000000
  %and148 = and i1 %151, %152
  br i1 %and148, label %if_then149, label %if_else150

if_then149:                                       ; preds = %if_merge142
  %pass_val152 = load i32, ptr @__global_pass, align 4
  %153 = add i32 %pass_val152, 1
  store i32 %153, ptr @__global_pass, align 4
  br label %if_merge151

if_else150:                                       ; preds = %if_merge142
  %fail_val153 = load i32, ptr @__global_fail, align 4
  %154 = add i32 %fail_val153, 1
  store i32 %154, ptr @__global_fail, align 4
  %print_call154 = call i32 (ptr, ...) @printf(ptr @43, ptr @42)
  br label %if_merge151

if_merge151:                                      ; preds = %if_else150, %if_then149
  %dim_val155 = load i32, ptr @__global_dim, align 4
  %155 = mul i32 1, %dim_val155
  store i32 %155, ptr @__global_t1_base, align 4
  %156 = load i32, ptr @__global_t1_base, align 4
  %dataBase156 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %157 = getelementptr inbounds float, ptr %dataBase156, i32 %156
  %lst_elem157 = load float, ptr %157, align 4
  store float %lst_elem157, ptr @__global_t1r0, align 4
  %t1r0_val = load float, ptr @__global_t1r0, align 4
  %158 = call ptr @__aiml_f32_to_string(float %t1r0_val)
  %159 = call ptr @__aiml_str_concat(ptr @44, ptr %158)
  %print_null_check158 = icmp eq ptr %159, null
  %print_safe_str159 = select i1 %print_null_check158, ptr @print_null_str.19, ptr %159
  %print_call160 = call i32 (ptr, ...) @printf(ptr @45, ptr %print_safe_str159)
  %t1r0_val161 = load float, ptr @__global_t1r0, align 4
  %160 = fcmp oge float %t1r0_val161, 0xBF847AE140000000
  %t1r0_val162 = load float, ptr @__global_t1r0, align 4
  %161 = fcmp ole float %t1r0_val162, 0x3F847AE140000000
  %and163 = and i1 %160, %161
  br i1 %and163, label %if_then164, label %if_else165

if_then164:                                       ; preds = %if_merge151
  %pass_val167 = load i32, ptr @__global_pass, align 4
  %162 = add i32 %pass_val167, 1
  store i32 %162, ptr @__global_pass, align 4
  br label %if_merge166

if_else165:                                       ; preds = %if_merge151
  %fail_val168 = load i32, ptr @__global_fail, align 4
  %163 = add i32 %fail_val168, 1
  store i32 %163, ptr @__global_fail, align 4
  %print_call169 = call i32 (ptr, ...) @printf(ptr @47, ptr @46)
  br label %if_merge166

if_merge166:                                      ; preds = %if_else165, %if_then164
  store float 0x3F847AE140000000, ptr @__global_lr, align 4
  store float 2.000000e+00, ptr @__global_grad0, align 4
  %dataBase170 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %164 = getelementptr inbounds float, ptr %dataBase170, i32 0
  %lst_elem171 = load float, ptr %164, align 4
  store float %lst_elem171, ptr @__global_cur, align 4
  %cur_val = load float, ptr @__global_cur, align 4
  %lr_val = load float, ptr @__global_lr, align 4
  %grad0_val = load float, ptr @__global_grad0, align 4
  %165 = fmul float %lr_val, %grad0_val
  %166 = fsub float %cur_val, %165
  store float %166, ptr @__global_updated, align 4
  %updated_val = load float, ptr @__global_updated, align 4
  %dataBase172 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %167 = getelementptr inbounds float, ptr %dataBase172, i32 0
  store float %updated_val, ptr %167, align 4
  %dataBase173 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %168 = getelementptr inbounds float, ptr %dataBase173, i32 0
  %lst_elem174 = load float, ptr %168, align 4
  store float %lst_elem174, ptr @__global_after_sgd, align 4
  %after_sgd_val = load float, ptr @__global_after_sgd, align 4
  %169 = call ptr @__aiml_f32_to_string(float %after_sgd_val)
  %170 = call ptr @__aiml_str_concat(ptr @48, ptr %169)
  %print_null_check175 = icmp eq ptr %170, null
  %print_safe_str176 = select i1 %print_null_check175, ptr @print_null_str.21, ptr %170
  %print_call177 = call i32 (ptr, ...) @printf(ptr @49, ptr %print_safe_str176)
  %after_sgd_val178 = load float, ptr @__global_after_sgd, align 4
  %171 = fcmp ogt float %after_sgd_val178, 0x3FB4395820000000
  %after_sgd_val179 = load float, ptr @__global_after_sgd, align 4
  %172 = fcmp olt float %after_sgd_val179, 0x3FB4BC6A80000000
  %and180 = and i1 %171, %172
  br i1 %and180, label %if_then181, label %if_else182

if_then181:                                       ; preds = %if_merge166
  %pass_val184 = load i32, ptr @__global_pass, align 4
  %173 = add i32 %pass_val184, 1
  store i32 %173, ptr @__global_pass, align 4
  br label %if_merge183

if_else182:                                       ; preds = %if_merge166
  %fail_val185 = load i32, ptr @__global_fail, align 4
  %174 = add i32 %fail_val185, 1
  store i32 %174, ptr @__global_fail, align 4
  %print_call186 = call i32 (ptr, ...) @printf(ptr @51, ptr @50)
  br label %if_merge183

if_merge183:                                      ; preds = %if_else182, %if_then181
  %dataBase187 = load ptr, ptr getelementptr inbounds (%GenericList, ptr @__global_flat, i32 0, i32 2), align 8
  %175 = getelementptr inbounds float, ptr %dataBase187, i32 1
  %getVal = load float, ptr %175, align 4
  store float %getVal, ptr @__global_g1, align 4
  %g1_val = load float, ptr @__global_g1, align 4
  %176 = call ptr @__aiml_f32_to_string(float %g1_val)
  %177 = call ptr @__aiml_str_concat(ptr @52, ptr %176)
  %print_null_check188 = icmp eq ptr %177, null
  %print_safe_str189 = select i1 %print_null_check188, ptr @print_null_str.23, ptr %177
  %print_call190 = call i32 (ptr, ...) @printf(ptr @53, ptr %print_safe_str189)
  %g1_val191 = load float, ptr @__global_g1, align 4
  %178 = fcmp ogt float %g1_val191, 0x3FC851EB80000000
  %g1_val192 = load float, ptr @__global_g1, align 4
  %179 = fcmp olt float %g1_val192, 0x3FCAE147A0000000
  %and193 = and i1 %178, %179
  br i1 %and193, label %if_then194, label %if_else195

if_then194:                                       ; preds = %if_merge183
  %pass_val197 = load i32, ptr @__global_pass, align 4
  %180 = add i32 %pass_val197, 1
  store i32 %180, ptr @__global_pass, align 4
  br label %if_merge196

if_else195:                                       ; preds = %if_merge183
  %fail_val198 = load i32, ptr @__global_fail, align 4
  %181 = add i32 %fail_val198, 1
  store i32 %181, ptr @__global_fail, align 4
  %print_call199 = call i32 (ptr, ...) @printf(ptr @55, ptr @54)
  br label %if_merge196

if_merge196:                                      ; preds = %if_else195, %if_then194
  %print_call200 = call i32 (ptr, ...) @printf(ptr @57, ptr @56)
  %pass_val201 = load i32, ptr @__global_pass, align 4
  %182 = call ptr @__aiml_int_to_string(i32 %pass_val201)
  %183 = call ptr @__aiml_str_concat(ptr @58, ptr %182)
  %184 = call ptr @__aiml_str_concat(ptr %183, ptr @59)
  %fail_val202 = load i32, ptr @__global_fail, align 4
  %185 = call ptr @__aiml_int_to_string(i32 %fail_val202)
  %186 = call ptr @__aiml_str_concat(ptr %184, ptr %185)
  %187 = call ptr @__aiml_str_concat(ptr %186, ptr @60)
  %print_null_check203 = icmp eq ptr %187, null
  %print_safe_str204 = select i1 %print_null_check203, ptr @print_null_str.26, ptr %187
  %print_call205 = call i32 (ptr, ...) @printf(ptr @61, ptr %print_safe_str204)
  %fail_val206 = load i32, ptr @__global_fail, align 4
  %188 = icmp eq i32 %fail_val206, 0
  br i1 %188, label %if_then207, label %if_else208

if_then207:                                       ; preds = %if_merge196
  call void @exit(i32 0)
  br label %if_merge209

if_else208:                                       ; preds = %if_merge196
  call void @exit(i32 1)
  br label %if_merge209

if_merge209:                                      ; preds = %if_else208, %if_then207
  ret void
}

declare ptr @malloc(i64)

declare ptr @realloc(ptr, i64)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
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

; Function Attrs: noinline
define internal ptr @__aiml_f32_to_string(float %0) #0 {
entry:
  %1 = call ptr @malloc(i64 32)
  %2 = fpext float %0 to double
  %3 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_f32, double %2)
  ret ptr %1
}

declare void @exit(i32)

attributes #0 = { noinline }
