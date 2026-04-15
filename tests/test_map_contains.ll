; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%map_string_i32 = type { i32, i32, ptr, ptr, ptr }
%map_path_i32 = type { i32, i32, ptr, ptr, ptr }

@__argc = external global i32
@__argv = external global ptr
@__global_ok = global i32 0
@__global_m_a = global %map_string_i32 zeroinitializer
@__global_m_b = global %map_string_i32 zeroinitializer
@__global_key_b = global ptr null
@__global_key_b2 = global ptr null
@__global_m_c = global %map_string_i32 zeroinitializer
@__global_prefix = global ptr null
@__global_idx = global i32 0
@__global_interp_key = global ptr null
@__global_m_d = global %map_path_i32 zeroinitializer
@__global_p1 = global ptr null
@__global_p2 = global ptr null
@__global_p3 = global ptr null
@__global_m_e = global %map_string_i32 zeroinitializer
@__global_m_f = global %map_string_i32 zeroinitializer
@__global_m_g = global %map_string_i32 zeroinitializer
@0 = private unnamed_addr constant [6 x i8] c"alpha\00", align 1
@1 = private unnamed_addr constant [5 x i8] c"beta\00", align 1
@2 = private unnamed_addr constant [6 x i8] c"alpha\00", align 1
@3 = private unnamed_addr constant [39 x i8] c"OK A1: inline contains literal 'alpha'\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [59 x i8] c"FAIL A1: inline contains literal 'alpha' \E2\80\94 expected true\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [6 x i8] c"gamma\00", align 1
@8 = private unnamed_addr constant [49 x i8] c"FAIL A2: inline contains 'gamma' should be false\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [43 x i8] c"OK A2: inline contains missing key 'gamma'\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@13 = private unnamed_addr constant [36 x i8] c"OK B1: inline contains variable key\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [56 x i8] c"FAIL B1: inline contains variable key \E2\80\94 expected true\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [6 x i8] c"world\00", align 1
@18 = private unnamed_addr constant [64 x i8] c"FAIL B2: inline contains absent variable key \E2\80\94 expected false\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [43 x i8] c"OK B2: inline contains absent variable key\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@22 = private unnamed_addr constant [5 x i8] c"item\00", align 1
@23 = private unnamed_addr constant [2 x i8] c"_\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@24 = private unnamed_addr constant [51 x i8] c"OK C1: inline contains interpolated key (same var)\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@26 = private unnamed_addr constant [71 x i8] c"FAIL C1: inline contains interpolated key (same var) \E2\80\94 expected true\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@28 = private unnamed_addr constant [7 x i8] c"item_7\00", align 1
@29 = private unnamed_addr constant [57 x i8] c"OK C2: inline contains literal equiv of interpolated key\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@30 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@31 = private unnamed_addr constant [57 x i8] c"FAIL C2: inline contains literal equiv \E2\80\94 expected true\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@33 = private unnamed_addr constant [9 x i8] c"/tmp/foo\00", align 1
@34 = private unnamed_addr constant [9 x i8] c"/tmp/bar\00", align 1
@35 = private unnamed_addr constant [9 x i8] c"/tmp/baz\00", align 1
@36 = private unnamed_addr constant [35 x i8] c"OK D1: inline contains path key p1\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@37 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@38 = private unnamed_addr constant [55 x i8] c"FAIL D1: inline contains path key p1 \E2\80\94 expected true\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@40 = private unnamed_addr constant [60 x i8] c"FAIL D3: inline contains absent path key \E2\80\94 expected false\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [39 x i8] c"OK D3: inline contains absent path key\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@43 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@44 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@45 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@46 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@47 = private unnamed_addr constant [57 x i8] c"FAIL E1: inline contains after remove \E2\80\94 expected false\00", align 1
@print_null_str.16 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@48 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@49 = private unnamed_addr constant [36 x i8] c"OK E1: inline contains after remove\00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@50 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@51 = private unnamed_addr constant [7 x i8] c"frozen\00", align 1
@52 = private unnamed_addr constant [7 x i8] c"frozen\00", align 1
@53 = private unnamed_addr constant [41 x i8] c"OK F1: shortform if contains present key\00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@54 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@55 = private unnamed_addr constant [61 x i8] c"FAIL F1: shortform if contains present key \E2\80\94 expected true\00", align 1
@print_null_str.19 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@56 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@57 = private unnamed_addr constant [8 x i8] c"stunned\00", align 1
@58 = private unnamed_addr constant [61 x i8] c"FAIL F2: shortform if contains absent key \E2\80\94 expected false\00", align 1
@print_null_str.20 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@59 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@60 = private unnamed_addr constant [40 x i8] c"OK F2: shortform if contains absent key\00", align 1
@print_null_str.21 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@61 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@62 = private unnamed_addr constant [7 x i8] c"frozen\00", align 1
@63 = private unnamed_addr constant [7 x i8] c"frozen\00", align 1
@64 = private unnamed_addr constant [50 x i8] c"OK G1: shortform eq-wrapping contains present key\00", align 1
@print_null_str.22 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@65 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@66 = private unnamed_addr constant [70 x i8] c"FAIL G1: shortform eq-wrapping contains present key \E2\80\94 expected true\00", align 1
@print_null_str.23 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@67 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@68 = private unnamed_addr constant [8 x i8] c"stunned\00", align 1
@69 = private unnamed_addr constant [70 x i8] c"FAIL G2: shortform eq-wrapping contains absent key \E2\80\94 expected false\00", align 1
@print_null_str.24 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@70 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@71 = private unnamed_addr constant [49 x i8] c"OK G2: shortform eq-wrapping contains absent key\00", align 1
@print_null_str.25 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@72 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@73 = private unnamed_addr constant [51 x i8] c"PASS map.contains inline \E2\80\94 all scenarios correct\00", align 1
@print_null_str.26 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@74 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@75 = private unnamed_addr constant [39 x i8] c"FAIL map.contains inline \E2\80\94 see above\00", align 1
@print_null_str.27 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@76 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  store i32 1, ptr @__global_ok, align 4
  store i32 0, ptr @__global_m_a, align 4
  store i32 4, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_a, i32 0, i32 1), align 4
  %0 = call ptr @calloc(i64 4, i64 1)
  store ptr %0, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_a, i32 0, i32 2), align 8
  %1 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %1, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_a, i32 0, i32 3), align 8
  %2 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  store ptr %2, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_a, i32 0, i32 4), align 8
  %size = load i32, ptr @__global_m_a, align 4
  %capacity = load i32, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 1), align 4
  %keysBase = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 3), align 8
  %valsBase = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 4), align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

put_loop:                                         ; preds = %put_next, %entry
  %3 = load i32, ptr %i_put, align 4
  %4 = icmp slt i32 %3, %size
  br i1 %4, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %5 = getelementptr inbounds ptr, ptr %keysBase, i32 %3
  %6 = load ptr, ptr %5, align 8
  %7 = call i32 @strcmp(ptr @0, ptr %6)
  %str_eq = icmp eq i32 %7, 0
  br i1 %str_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %8 = getelementptr inbounds i32, ptr %valsBase, i32 %3
  store i32 1, ptr %8, align 4
  br label %put_end

put_next:                                         ; preds = %put_check
  %9 = add i32 %3, 1
  store i32 %9, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %10 = icmp eq i32 %size, %capacity
  br i1 %10, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %size1 = load i32, ptr @__global_m_a, align 4
  %capacity2 = load i32, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 1), align 4
  %keysBase3 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 3), align 8
  %valsBase4 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 4), align 8
  %i_put11 = alloca i32, align 4
  store i32 0, ptr %i_put11, align 4
  br label %put_loop5

put_grow:                                         ; preds = %put_not_found
  %11 = mul i32 %capacity, 2
  %12 = icmp sgt i32 %11, 3
  %growCap = select i1 %12, i32 %11, i32 4
  store i32 %growCap, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 1), align 4
  %13 = zext i32 %growCap to i64
  %14 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %13
  %15 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 3), align 8
  %16 = call ptr @realloc(ptr %15, i64 %14)
  store ptr %16, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 3), align 8
  %17 = zext i32 %growCap to i64
  %18 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %17
  %19 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 4), align 8
  %20 = call ptr @realloc(ptr %19, i64 %18)
  store ptr %20, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 4), align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %21 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 3), align 8
  %22 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 4), align 8
  %23 = getelementptr inbounds ptr, ptr %21, i32 %size
  store ptr @0, ptr %23, align 8
  %24 = getelementptr inbounds i32, ptr %22, i32 %size
  store i32 1, ptr %24, align 4
  %25 = add i32 %size, 1
  store i32 %25, ptr @__global_m_a, align 4
  br label %put_end

put_loop5:                                        ; preds = %put_next8, %put_end
  %26 = load i32, ptr %i_put11, align 4
  %27 = icmp slt i32 %26, %size1
  br i1 %27, label %put_check6, label %put_not_found9

put_check6:                                       ; preds = %put_loop5
  %28 = getelementptr inbounds ptr, ptr %keysBase3, i32 %26
  %29 = load ptr, ptr %28, align 8
  %30 = call i32 @strcmp(ptr @1, ptr %29)
  %str_eq12 = icmp eq i32 %30, 0
  br i1 %str_eq12, label %put_found7, label %put_next8

put_found7:                                       ; preds = %put_check6
  %31 = getelementptr inbounds i32, ptr %valsBase4, i32 %26
  store i32 2, ptr %31, align 4
  br label %put_end10

put_next8:                                        ; preds = %put_check6
  %32 = add i32 %26, 1
  store i32 %32, ptr %i_put11, align 4
  br label %put_loop5

put_not_found9:                                   ; preds = %put_loop5
  %33 = icmp eq i32 %size1, %capacity2
  br i1 %33, label %put_grow13, label %put_append14

put_end10:                                        ; preds = %put_append14, %put_found7
  %34 = load i32, ptr @__global_m_a, align 4
  %35 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 3), align 8
  %i_cont = alloca i32, align 4
  store i32 0, ptr %i_cont, align 4
  %res_cont = alloca i1, align 1
  store i1 false, ptr %res_cont, align 1
  br label %cont_loop

put_grow13:                                       ; preds = %put_not_found9
  %36 = mul i32 %capacity2, 2
  %37 = icmp sgt i32 %36, 3
  %growCap15 = select i1 %37, i32 %36, i32 4
  store i32 %growCap15, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 1), align 4
  %38 = zext i32 %growCap15 to i64
  %39 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %38
  %40 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 3), align 8
  %41 = call ptr @realloc(ptr %40, i64 %39)
  store ptr %41, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 3), align 8
  %42 = zext i32 %growCap15 to i64
  %43 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %42
  %44 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 4), align 8
  %45 = call ptr @realloc(ptr %44, i64 %43)
  store ptr %45, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 4), align 8
  br label %put_append14

put_append14:                                     ; preds = %put_grow13, %put_not_found9
  %46 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 3), align 8
  %47 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 4), align 8
  %48 = getelementptr inbounds ptr, ptr %46, i32 %size1
  store ptr @1, ptr %48, align 8
  %49 = getelementptr inbounds i32, ptr %47, i32 %size1
  store i32 2, ptr %49, align 4
  %50 = add i32 %size1, 1
  store i32 %50, ptr @__global_m_a, align 4
  br label %put_end10

cont_loop:                                        ; preds = %cont_next, %put_end10
  %51 = load i32, ptr %i_cont, align 4
  %52 = icmp slt i32 %51, %34
  br i1 %52, label %cont_check, label %cont_end

cont_check:                                       ; preds = %cont_loop
  %53 = getelementptr inbounds ptr, ptr %35, i32 %51
  %54 = load ptr, ptr %53, align 8
  %55 = call i32 @strcmp(ptr @2, ptr %54)
  %str_eq16 = icmp eq i32 %55, 0
  br i1 %str_eq16, label %cont_found, label %cont_next

cont_found:                                       ; preds = %cont_check
  store i1 true, ptr %res_cont, align 1
  br label %cont_end

cont_end:                                         ; preds = %cont_found, %cont_loop
  %56 = load i1, ptr %res_cont, align 1
  br i1 %56, label %if_then, label %if_else

cont_next:                                        ; preds = %cont_check
  %57 = add i32 %51, 1
  store i32 %57, ptr %i_cont, align 4
  br label %cont_loop

if_then:                                          ; preds = %cont_end
  %print_call = call i32 (ptr, ...) @printf(ptr @4, ptr @3)
  br label %if_merge

if_else:                                          ; preds = %cont_end
  %print_call17 = call i32 (ptr, ...) @printf(ptr @6, ptr @5)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  %58 = load i32, ptr @__global_m_a, align 4
  %59 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_a, i32 0, i32 3), align 8
  %i_cont22 = alloca i32, align 4
  store i32 0, ptr %i_cont22, align 4
  %res_cont23 = alloca i1, align 1
  store i1 false, ptr %res_cont23, align 1
  br label %cont_loop18

cont_loop18:                                      ; preds = %cont_next25, %if_merge
  %60 = load i32, ptr %i_cont22, align 4
  %61 = icmp slt i32 %60, %58
  br i1 %61, label %cont_check19, label %cont_end21

cont_check19:                                     ; preds = %cont_loop18
  %62 = getelementptr inbounds ptr, ptr %59, i32 %60
  %63 = load ptr, ptr %62, align 8
  %64 = call i32 @strcmp(ptr @7, ptr %63)
  %str_eq24 = icmp eq i32 %64, 0
  br i1 %str_eq24, label %cont_found20, label %cont_next25

cont_found20:                                     ; preds = %cont_check19
  store i1 true, ptr %res_cont23, align 1
  br label %cont_end21

cont_end21:                                       ; preds = %cont_found20, %cont_loop18
  %65 = load i1, ptr %res_cont23, align 1
  br i1 %65, label %if_then26, label %if_else27

cont_next25:                                      ; preds = %cont_check19
  %66 = add i32 %60, 1
  store i32 %66, ptr %i_cont22, align 4
  br label %cont_loop18

if_then26:                                        ; preds = %cont_end21
  %print_call29 = call i32 (ptr, ...) @printf(ptr @9, ptr @8)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge28

if_else27:                                        ; preds = %cont_end21
  %print_call30 = call i32 (ptr, ...) @printf(ptr @11, ptr @10)
  br label %if_merge28

if_merge28:                                       ; preds = %if_else27, %if_then26
  store i32 0, ptr @__global_m_b, align 4
  store i32 4, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_b, i32 0, i32 1), align 4
  %67 = call ptr @calloc(i64 4, i64 1)
  store ptr %67, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_b, i32 0, i32 2), align 8
  %68 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %68, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_b, i32 0, i32 3), align 8
  %69 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  store ptr %69, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_b, i32 0, i32 4), align 8
  store ptr @12, ptr @__global_key_b, align 8
  %key_b_val = load ptr, ptr @__global_key_b, align 8
  %size31 = load i32, ptr @__global_m_b, align 4
  %capacity32 = load i32, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 1), align 4
  %keysBase33 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 3), align 8
  %valsBase34 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 4), align 8
  %i_put41 = alloca i32, align 4
  store i32 0, ptr %i_put41, align 4
  br label %put_loop35

put_loop35:                                       ; preds = %put_next38, %if_merge28
  %70 = load i32, ptr %i_put41, align 4
  %71 = icmp slt i32 %70, %size31
  br i1 %71, label %put_check36, label %put_not_found39

put_check36:                                      ; preds = %put_loop35
  %72 = getelementptr inbounds ptr, ptr %keysBase33, i32 %70
  %73 = load ptr, ptr %72, align 8
  %74 = call i32 @strcmp(ptr %key_b_val, ptr %73)
  %str_eq42 = icmp eq i32 %74, 0
  br i1 %str_eq42, label %put_found37, label %put_next38

put_found37:                                      ; preds = %put_check36
  %75 = getelementptr inbounds i32, ptr %valsBase34, i32 %70
  store i32 42, ptr %75, align 4
  br label %put_end40

put_next38:                                       ; preds = %put_check36
  %76 = add i32 %70, 1
  store i32 %76, ptr %i_put41, align 4
  br label %put_loop35

put_not_found39:                                  ; preds = %put_loop35
  %77 = icmp eq i32 %size31, %capacity32
  br i1 %77, label %put_grow43, label %put_append44

put_end40:                                        ; preds = %put_append44, %put_found37
  %key_b_val46 = load ptr, ptr @__global_key_b, align 8
  %78 = load i32, ptr @__global_m_b, align 4
  %79 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 3), align 8
  %i_cont51 = alloca i32, align 4
  store i32 0, ptr %i_cont51, align 4
  %res_cont52 = alloca i1, align 1
  store i1 false, ptr %res_cont52, align 1
  br label %cont_loop47

put_grow43:                                       ; preds = %put_not_found39
  %80 = mul i32 %capacity32, 2
  %81 = icmp sgt i32 %80, 3
  %growCap45 = select i1 %81, i32 %80, i32 4
  store i32 %growCap45, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 1), align 4
  %82 = zext i32 %growCap45 to i64
  %83 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %82
  %84 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 3), align 8
  %85 = call ptr @realloc(ptr %84, i64 %83)
  store ptr %85, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 3), align 8
  %86 = zext i32 %growCap45 to i64
  %87 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %86
  %88 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 4), align 8
  %89 = call ptr @realloc(ptr %88, i64 %87)
  store ptr %89, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 4), align 8
  br label %put_append44

put_append44:                                     ; preds = %put_grow43, %put_not_found39
  %90 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 3), align 8
  %91 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 4), align 8
  %92 = getelementptr inbounds ptr, ptr %90, i32 %size31
  store ptr %key_b_val, ptr %92, align 8
  %93 = getelementptr inbounds i32, ptr %91, i32 %size31
  store i32 42, ptr %93, align 4
  %94 = add i32 %size31, 1
  store i32 %94, ptr @__global_m_b, align 4
  br label %put_end40

cont_loop47:                                      ; preds = %cont_next54, %put_end40
  %95 = load i32, ptr %i_cont51, align 4
  %96 = icmp slt i32 %95, %78
  br i1 %96, label %cont_check48, label %cont_end50

cont_check48:                                     ; preds = %cont_loop47
  %97 = getelementptr inbounds ptr, ptr %79, i32 %95
  %98 = load ptr, ptr %97, align 8
  %99 = call i32 @strcmp(ptr %key_b_val46, ptr %98)
  %str_eq53 = icmp eq i32 %99, 0
  br i1 %str_eq53, label %cont_found49, label %cont_next54

cont_found49:                                     ; preds = %cont_check48
  store i1 true, ptr %res_cont52, align 1
  br label %cont_end50

cont_end50:                                       ; preds = %cont_found49, %cont_loop47
  %100 = load i1, ptr %res_cont52, align 1
  br i1 %100, label %if_then55, label %if_else56

cont_next54:                                      ; preds = %cont_check48
  %101 = add i32 %95, 1
  store i32 %101, ptr %i_cont51, align 4
  br label %cont_loop47

if_then55:                                        ; preds = %cont_end50
  %print_call58 = call i32 (ptr, ...) @printf(ptr @14, ptr @13)
  br label %if_merge57

if_else56:                                        ; preds = %cont_end50
  %print_call59 = call i32 (ptr, ...) @printf(ptr @16, ptr @15)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge57

if_merge57:                                       ; preds = %if_else56, %if_then55
  store ptr @17, ptr @__global_key_b2, align 8
  %key_b2_val = load ptr, ptr @__global_key_b2, align 8
  %102 = load i32, ptr @__global_m_b, align 4
  %103 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_b, i32 0, i32 3), align 8
  %i_cont64 = alloca i32, align 4
  store i32 0, ptr %i_cont64, align 4
  %res_cont65 = alloca i1, align 1
  store i1 false, ptr %res_cont65, align 1
  br label %cont_loop60

cont_loop60:                                      ; preds = %cont_next67, %if_merge57
  %104 = load i32, ptr %i_cont64, align 4
  %105 = icmp slt i32 %104, %102
  br i1 %105, label %cont_check61, label %cont_end63

cont_check61:                                     ; preds = %cont_loop60
  %106 = getelementptr inbounds ptr, ptr %103, i32 %104
  %107 = load ptr, ptr %106, align 8
  %108 = call i32 @strcmp(ptr %key_b2_val, ptr %107)
  %str_eq66 = icmp eq i32 %108, 0
  br i1 %str_eq66, label %cont_found62, label %cont_next67

cont_found62:                                     ; preds = %cont_check61
  store i1 true, ptr %res_cont65, align 1
  br label %cont_end63

cont_end63:                                       ; preds = %cont_found62, %cont_loop60
  %109 = load i1, ptr %res_cont65, align 1
  br i1 %109, label %if_then68, label %if_else69

cont_next67:                                      ; preds = %cont_check61
  %110 = add i32 %104, 1
  store i32 %110, ptr %i_cont64, align 4
  br label %cont_loop60

if_then68:                                        ; preds = %cont_end63
  %print_call71 = call i32 (ptr, ...) @printf(ptr @19, ptr @18)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge70

if_else69:                                        ; preds = %cont_end63
  %print_call72 = call i32 (ptr, ...) @printf(ptr @21, ptr @20)
  br label %if_merge70

if_merge70:                                       ; preds = %if_else69, %if_then68
  store i32 0, ptr @__global_m_c, align 4
  store i32 4, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_c, i32 0, i32 1), align 4
  %111 = call ptr @calloc(i64 4, i64 1)
  store ptr %111, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_c, i32 0, i32 2), align 8
  %112 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %112, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_c, i32 0, i32 3), align 8
  %113 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  store ptr %113, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_c, i32 0, i32 4), align 8
  store ptr @22, ptr @__global_prefix, align 8
  store i32 7, ptr @__global_idx, align 4
  %prefix_val = load ptr, ptr @__global_prefix, align 8
  %114 = call ptr @__aiml_str_concat(ptr %prefix_val, ptr @23)
  %idx_val = load i32, ptr @__global_idx, align 4
  %115 = call ptr @__aiml_int_to_string(i32 %idx_val)
  %116 = call ptr @__aiml_str_concat(ptr %114, ptr %115)
  store ptr %116, ptr @__global_interp_key, align 8
  %interp_key_val = load ptr, ptr @__global_interp_key, align 8
  %size73 = load i32, ptr @__global_m_c, align 4
  %capacity74 = load i32, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 1), align 4
  %keysBase75 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 3), align 8
  %valsBase76 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 4), align 8
  %i_put83 = alloca i32, align 4
  store i32 0, ptr %i_put83, align 4
  br label %put_loop77

put_loop77:                                       ; preds = %put_next80, %if_merge70
  %117 = load i32, ptr %i_put83, align 4
  %118 = icmp slt i32 %117, %size73
  br i1 %118, label %put_check78, label %put_not_found81

put_check78:                                      ; preds = %put_loop77
  %119 = getelementptr inbounds ptr, ptr %keysBase75, i32 %117
  %120 = load ptr, ptr %119, align 8
  %121 = call i32 @strcmp(ptr %interp_key_val, ptr %120)
  %str_eq84 = icmp eq i32 %121, 0
  br i1 %str_eq84, label %put_found79, label %put_next80

put_found79:                                      ; preds = %put_check78
  %122 = getelementptr inbounds i32, ptr %valsBase76, i32 %117
  store i32 99, ptr %122, align 4
  br label %put_end82

put_next80:                                       ; preds = %put_check78
  %123 = add i32 %117, 1
  store i32 %123, ptr %i_put83, align 4
  br label %put_loop77

put_not_found81:                                  ; preds = %put_loop77
  %124 = icmp eq i32 %size73, %capacity74
  br i1 %124, label %put_grow85, label %put_append86

put_end82:                                        ; preds = %put_append86, %put_found79
  %interp_key_val88 = load ptr, ptr @__global_interp_key, align 8
  %125 = load i32, ptr @__global_m_c, align 4
  %126 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 3), align 8
  %i_cont93 = alloca i32, align 4
  store i32 0, ptr %i_cont93, align 4
  %res_cont94 = alloca i1, align 1
  store i1 false, ptr %res_cont94, align 1
  br label %cont_loop89

put_grow85:                                       ; preds = %put_not_found81
  %127 = mul i32 %capacity74, 2
  %128 = icmp sgt i32 %127, 3
  %growCap87 = select i1 %128, i32 %127, i32 4
  store i32 %growCap87, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 1), align 4
  %129 = zext i32 %growCap87 to i64
  %130 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %129
  %131 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 3), align 8
  %132 = call ptr @realloc(ptr %131, i64 %130)
  store ptr %132, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 3), align 8
  %133 = zext i32 %growCap87 to i64
  %134 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %133
  %135 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 4), align 8
  %136 = call ptr @realloc(ptr %135, i64 %134)
  store ptr %136, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 4), align 8
  br label %put_append86

put_append86:                                     ; preds = %put_grow85, %put_not_found81
  %137 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 3), align 8
  %138 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 4), align 8
  %139 = getelementptr inbounds ptr, ptr %137, i32 %size73
  store ptr %interp_key_val, ptr %139, align 8
  %140 = getelementptr inbounds i32, ptr %138, i32 %size73
  store i32 99, ptr %140, align 4
  %141 = add i32 %size73, 1
  store i32 %141, ptr @__global_m_c, align 4
  br label %put_end82

cont_loop89:                                      ; preds = %cont_next96, %put_end82
  %142 = load i32, ptr %i_cont93, align 4
  %143 = icmp slt i32 %142, %125
  br i1 %143, label %cont_check90, label %cont_end92

cont_check90:                                     ; preds = %cont_loop89
  %144 = getelementptr inbounds ptr, ptr %126, i32 %142
  %145 = load ptr, ptr %144, align 8
  %146 = call i32 @strcmp(ptr %interp_key_val88, ptr %145)
  %str_eq95 = icmp eq i32 %146, 0
  br i1 %str_eq95, label %cont_found91, label %cont_next96

cont_found91:                                     ; preds = %cont_check90
  store i1 true, ptr %res_cont94, align 1
  br label %cont_end92

cont_end92:                                       ; preds = %cont_found91, %cont_loop89
  %147 = load i1, ptr %res_cont94, align 1
  br i1 %147, label %if_then97, label %if_else98

cont_next96:                                      ; preds = %cont_check90
  %148 = add i32 %142, 1
  store i32 %148, ptr %i_cont93, align 4
  br label %cont_loop89

if_then97:                                        ; preds = %cont_end92
  %print_call100 = call i32 (ptr, ...) @printf(ptr @25, ptr @24)
  br label %if_merge99

if_else98:                                        ; preds = %cont_end92
  %print_call101 = call i32 (ptr, ...) @printf(ptr @27, ptr @26)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge99

if_merge99:                                       ; preds = %if_else98, %if_then97
  %149 = load i32, ptr @__global_m_c, align 4
  %150 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_c, i32 0, i32 3), align 8
  %i_cont106 = alloca i32, align 4
  store i32 0, ptr %i_cont106, align 4
  %res_cont107 = alloca i1, align 1
  store i1 false, ptr %res_cont107, align 1
  br label %cont_loop102

cont_loop102:                                     ; preds = %cont_next109, %if_merge99
  %151 = load i32, ptr %i_cont106, align 4
  %152 = icmp slt i32 %151, %149
  br i1 %152, label %cont_check103, label %cont_end105

cont_check103:                                    ; preds = %cont_loop102
  %153 = getelementptr inbounds ptr, ptr %150, i32 %151
  %154 = load ptr, ptr %153, align 8
  %155 = call i32 @strcmp(ptr @28, ptr %154)
  %str_eq108 = icmp eq i32 %155, 0
  br i1 %str_eq108, label %cont_found104, label %cont_next109

cont_found104:                                    ; preds = %cont_check103
  store i1 true, ptr %res_cont107, align 1
  br label %cont_end105

cont_end105:                                      ; preds = %cont_found104, %cont_loop102
  %156 = load i1, ptr %res_cont107, align 1
  br i1 %156, label %if_then110, label %if_else111

cont_next109:                                     ; preds = %cont_check103
  %157 = add i32 %151, 1
  store i32 %157, ptr %i_cont106, align 4
  br label %cont_loop102

if_then110:                                       ; preds = %cont_end105
  %print_call113 = call i32 (ptr, ...) @printf(ptr @30, ptr @29)
  br label %if_merge112

if_else111:                                       ; preds = %cont_end105
  %print_call114 = call i32 (ptr, ...) @printf(ptr @32, ptr @31)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge112

if_merge112:                                      ; preds = %if_else111, %if_then110
  store i32 0, ptr @__global_m_d, align 4
  store i32 4, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_d, i32 0, i32 1), align 4
  %158 = call ptr @calloc(i64 4, i64 1)
  store ptr %158, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_d, i32 0, i32 2), align 8
  %159 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %159, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_d, i32 0, i32 3), align 8
  %160 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  store ptr %160, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_d, i32 0, i32 4), align 8
  %161 = call ptr @aiml_path_intern(ptr @33)
  %path_init = call ptr @aiml_path_intern(ptr %161)
  store ptr %path_init, ptr @__global_p1, align 8
  %162 = call ptr @aiml_path_intern(ptr @34)
  %path_init115 = call ptr @aiml_path_intern(ptr %162)
  store ptr %path_init115, ptr @__global_p2, align 8
  %163 = call ptr @aiml_path_intern(ptr @35)
  %path_init116 = call ptr @aiml_path_intern(ptr %163)
  store ptr %path_init116, ptr @__global_p3, align 8
  %p1_val = load ptr, ptr @__global_p1, align 8
  %size117 = load i32, ptr @__global_m_d, align 4
  %capacity118 = load i32, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 1), align 4
  %keysBase119 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 3), align 8
  %valsBase120 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 4), align 8
  %i_put127 = alloca i32, align 4
  store i32 0, ptr %i_put127, align 4
  br label %put_loop121

put_loop121:                                      ; preds = %put_next124, %if_merge112
  %164 = load i32, ptr %i_put127, align 4
  %165 = icmp slt i32 %164, %size117
  br i1 %165, label %put_check122, label %put_not_found125

put_check122:                                     ; preds = %put_loop121
  %166 = getelementptr inbounds ptr, ptr %keysBase119, i32 %164
  %167 = load ptr, ptr %166, align 8
  %168 = call i32 @strcmp(ptr %p1_val, ptr %167)
  %str_eq128 = icmp eq i32 %168, 0
  br i1 %str_eq128, label %put_found123, label %put_next124

put_found123:                                     ; preds = %put_check122
  %169 = getelementptr inbounds i32, ptr %valsBase120, i32 %164
  store i32 10, ptr %169, align 4
  br label %put_end126

put_next124:                                      ; preds = %put_check122
  %170 = add i32 %164, 1
  store i32 %170, ptr %i_put127, align 4
  br label %put_loop121

put_not_found125:                                 ; preds = %put_loop121
  %171 = icmp eq i32 %size117, %capacity118
  br i1 %171, label %put_grow129, label %put_append130

put_end126:                                       ; preds = %put_append130, %put_found123
  %p2_val = load ptr, ptr @__global_p2, align 8
  %size132 = load i32, ptr @__global_m_d, align 4
  %capacity133 = load i32, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 1), align 4
  %keysBase134 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 3), align 8
  %valsBase135 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 4), align 8
  %i_put142 = alloca i32, align 4
  store i32 0, ptr %i_put142, align 4
  br label %put_loop136

put_grow129:                                      ; preds = %put_not_found125
  %172 = mul i32 %capacity118, 2
  %173 = icmp sgt i32 %172, 3
  %growCap131 = select i1 %173, i32 %172, i32 4
  store i32 %growCap131, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 1), align 4
  %174 = zext i32 %growCap131 to i64
  %175 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %174
  %176 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 3), align 8
  %177 = call ptr @realloc(ptr %176, i64 %175)
  store ptr %177, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 3), align 8
  %178 = zext i32 %growCap131 to i64
  %179 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %178
  %180 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 4), align 8
  %181 = call ptr @realloc(ptr %180, i64 %179)
  store ptr %181, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 4), align 8
  br label %put_append130

put_append130:                                    ; preds = %put_grow129, %put_not_found125
  %182 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 3), align 8
  %183 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 4), align 8
  %184 = getelementptr inbounds ptr, ptr %182, i32 %size117
  store ptr %p1_val, ptr %184, align 8
  %185 = getelementptr inbounds i32, ptr %183, i32 %size117
  store i32 10, ptr %185, align 4
  %186 = add i32 %size117, 1
  store i32 %186, ptr @__global_m_d, align 4
  br label %put_end126

put_loop136:                                      ; preds = %put_next139, %put_end126
  %187 = load i32, ptr %i_put142, align 4
  %188 = icmp slt i32 %187, %size132
  br i1 %188, label %put_check137, label %put_not_found140

put_check137:                                     ; preds = %put_loop136
  %189 = getelementptr inbounds ptr, ptr %keysBase134, i32 %187
  %190 = load ptr, ptr %189, align 8
  %191 = call i32 @strcmp(ptr %p2_val, ptr %190)
  %str_eq143 = icmp eq i32 %191, 0
  br i1 %str_eq143, label %put_found138, label %put_next139

put_found138:                                     ; preds = %put_check137
  %192 = getelementptr inbounds i32, ptr %valsBase135, i32 %187
  store i32 20, ptr %192, align 4
  br label %put_end141

put_next139:                                      ; preds = %put_check137
  %193 = add i32 %187, 1
  store i32 %193, ptr %i_put142, align 4
  br label %put_loop136

put_not_found140:                                 ; preds = %put_loop136
  %194 = icmp eq i32 %size132, %capacity133
  br i1 %194, label %put_grow144, label %put_append145

put_end141:                                       ; preds = %put_append145, %put_found138
  %p1_val147 = load ptr, ptr @__global_p1, align 8
  %195 = load i32, ptr @__global_m_d, align 4
  %196 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 3), align 8
  %i_cont152 = alloca i32, align 4
  store i32 0, ptr %i_cont152, align 4
  %res_cont153 = alloca i1, align 1
  store i1 false, ptr %res_cont153, align 1
  br label %cont_loop148

put_grow144:                                      ; preds = %put_not_found140
  %197 = mul i32 %capacity133, 2
  %198 = icmp sgt i32 %197, 3
  %growCap146 = select i1 %198, i32 %197, i32 4
  store i32 %growCap146, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 1), align 4
  %199 = zext i32 %growCap146 to i64
  %200 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %199
  %201 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 3), align 8
  %202 = call ptr @realloc(ptr %201, i64 %200)
  store ptr %202, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 3), align 8
  %203 = zext i32 %growCap146 to i64
  %204 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %203
  %205 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 4), align 8
  %206 = call ptr @realloc(ptr %205, i64 %204)
  store ptr %206, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 4), align 8
  br label %put_append145

put_append145:                                    ; preds = %put_grow144, %put_not_found140
  %207 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 3), align 8
  %208 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 4), align 8
  %209 = getelementptr inbounds ptr, ptr %207, i32 %size132
  store ptr %p2_val, ptr %209, align 8
  %210 = getelementptr inbounds i32, ptr %208, i32 %size132
  store i32 20, ptr %210, align 4
  %211 = add i32 %size132, 1
  store i32 %211, ptr @__global_m_d, align 4
  br label %put_end141

cont_loop148:                                     ; preds = %cont_next155, %put_end141
  %212 = load i32, ptr %i_cont152, align 4
  %213 = icmp slt i32 %212, %195
  br i1 %213, label %cont_check149, label %cont_end151

cont_check149:                                    ; preds = %cont_loop148
  %214 = getelementptr inbounds ptr, ptr %196, i32 %212
  %215 = load ptr, ptr %214, align 8
  %216 = call i32 @strcmp(ptr %p1_val147, ptr %215)
  %str_eq154 = icmp eq i32 %216, 0
  br i1 %str_eq154, label %cont_found150, label %cont_next155

cont_found150:                                    ; preds = %cont_check149
  store i1 true, ptr %res_cont153, align 1
  br label %cont_end151

cont_end151:                                      ; preds = %cont_found150, %cont_loop148
  %217 = load i1, ptr %res_cont153, align 1
  br i1 %217, label %if_then156, label %if_else157

cont_next155:                                     ; preds = %cont_check149
  %218 = add i32 %212, 1
  store i32 %218, ptr %i_cont152, align 4
  br label %cont_loop148

if_then156:                                       ; preds = %cont_end151
  %print_call159 = call i32 (ptr, ...) @printf(ptr @37, ptr @36)
  br label %if_merge158

if_else157:                                       ; preds = %cont_end151
  %print_call160 = call i32 (ptr, ...) @printf(ptr @39, ptr @38)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge158

if_merge158:                                      ; preds = %if_else157, %if_then156
  %p3_val = load ptr, ptr @__global_p3, align 8
  %219 = load i32, ptr @__global_m_d, align 4
  %220 = load ptr, ptr getelementptr inbounds (%map_path_i32, ptr @__global_m_d, i32 0, i32 3), align 8
  %i_cont165 = alloca i32, align 4
  store i32 0, ptr %i_cont165, align 4
  %res_cont166 = alloca i1, align 1
  store i1 false, ptr %res_cont166, align 1
  br label %cont_loop161

cont_loop161:                                     ; preds = %cont_next168, %if_merge158
  %221 = load i32, ptr %i_cont165, align 4
  %222 = icmp slt i32 %221, %219
  br i1 %222, label %cont_check162, label %cont_end164

cont_check162:                                    ; preds = %cont_loop161
  %223 = getelementptr inbounds ptr, ptr %220, i32 %221
  %224 = load ptr, ptr %223, align 8
  %225 = call i32 @strcmp(ptr %p3_val, ptr %224)
  %str_eq167 = icmp eq i32 %225, 0
  br i1 %str_eq167, label %cont_found163, label %cont_next168

cont_found163:                                    ; preds = %cont_check162
  store i1 true, ptr %res_cont166, align 1
  br label %cont_end164

cont_end164:                                      ; preds = %cont_found163, %cont_loop161
  %226 = load i1, ptr %res_cont166, align 1
  br i1 %226, label %if_then169, label %if_else170

cont_next168:                                     ; preds = %cont_check162
  %227 = add i32 %221, 1
  store i32 %227, ptr %i_cont165, align 4
  br label %cont_loop161

if_then169:                                       ; preds = %cont_end164
  %print_call172 = call i32 (ptr, ...) @printf(ptr @41, ptr @40)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge171

if_else170:                                       ; preds = %cont_end164
  %print_call173 = call i32 (ptr, ...) @printf(ptr @43, ptr @42)
  br label %if_merge171

if_merge171:                                      ; preds = %if_else170, %if_then169
  store i32 0, ptr @__global_m_e, align 4
  store i32 4, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_e, i32 0, i32 1), align 4
  %228 = call ptr @calloc(i64 4, i64 1)
  store ptr %228, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_e, i32 0, i32 2), align 8
  %229 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %229, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_e, i32 0, i32 3), align 8
  %230 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  store ptr %230, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_e, i32 0, i32 4), align 8
  %size174 = load i32, ptr @__global_m_e, align 4
  %capacity175 = load i32, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 1), align 4
  %keysBase176 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 3), align 8
  %valsBase177 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 4), align 8
  %i_put184 = alloca i32, align 4
  store i32 0, ptr %i_put184, align 4
  br label %put_loop178

put_loop178:                                      ; preds = %put_next181, %if_merge171
  %231 = load i32, ptr %i_put184, align 4
  %232 = icmp slt i32 %231, %size174
  br i1 %232, label %put_check179, label %put_not_found182

put_check179:                                     ; preds = %put_loop178
  %233 = getelementptr inbounds ptr, ptr %keysBase176, i32 %231
  %234 = load ptr, ptr %233, align 8
  %235 = call i32 @strcmp(ptr @44, ptr %234)
  %str_eq185 = icmp eq i32 %235, 0
  br i1 %str_eq185, label %put_found180, label %put_next181

put_found180:                                     ; preds = %put_check179
  %236 = getelementptr inbounds i32, ptr %valsBase177, i32 %231
  store i32 5, ptr %236, align 4
  br label %put_end183

put_next181:                                      ; preds = %put_check179
  %237 = add i32 %231, 1
  store i32 %237, ptr %i_put184, align 4
  br label %put_loop178

put_not_found182:                                 ; preds = %put_loop178
  %238 = icmp eq i32 %size174, %capacity175
  br i1 %238, label %put_grow186, label %put_append187

put_end183:                                       ; preds = %put_append187, %put_found180
  %239 = load i32, ptr @__global_m_e, align 4
  %240 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 3), align 8
  %241 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 4), align 8
  %i_rm = alloca i32, align 4
  store i32 0, ptr %i_rm, align 4
  br label %rm_loop

put_grow186:                                      ; preds = %put_not_found182
  %242 = mul i32 %capacity175, 2
  %243 = icmp sgt i32 %242, 3
  %growCap188 = select i1 %243, i32 %242, i32 4
  store i32 %growCap188, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 1), align 4
  %244 = zext i32 %growCap188 to i64
  %245 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %244
  %246 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 3), align 8
  %247 = call ptr @realloc(ptr %246, i64 %245)
  store ptr %247, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 3), align 8
  %248 = zext i32 %growCap188 to i64
  %249 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %248
  %250 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 4), align 8
  %251 = call ptr @realloc(ptr %250, i64 %249)
  store ptr %251, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 4), align 8
  br label %put_append187

put_append187:                                    ; preds = %put_grow186, %put_not_found182
  %252 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 3), align 8
  %253 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 4), align 8
  %254 = getelementptr inbounds ptr, ptr %252, i32 %size174
  store ptr @44, ptr %254, align 8
  %255 = getelementptr inbounds i32, ptr %253, i32 %size174
  store i32 5, ptr %255, align 4
  %256 = add i32 %size174, 1
  store i32 %256, ptr @__global_m_e, align 4
  br label %put_end183

rm_loop:                                          ; preds = %rm_next, %put_end183
  %257 = load i32, ptr %i_rm, align 4
  %258 = icmp slt i32 %257, %239
  br i1 %258, label %rm_check, label %rm_end

rm_check:                                         ; preds = %rm_loop
  %259 = getelementptr inbounds ptr, ptr %240, i32 %257
  %260 = load ptr, ptr %259, align 8
  %261 = call i32 @strcmp(ptr @45, ptr %260)
  %str_eq189 = icmp eq i32 %261, 0
  br i1 %str_eq189, label %rm_found, label %rm_next

rm_found:                                         ; preds = %rm_check
  %262 = add i32 %257, 1
  %263 = sub i32 %239, %262
  %264 = getelementptr inbounds ptr, ptr %240, i32 %257
  %265 = getelementptr inbounds ptr, ptr %240, i32 %262
  %266 = zext i32 %263 to i64
  %267 = mul i64 %266, ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64)
  %268 = call ptr @memmove(ptr %264, ptr %265, i64 %267)
  %269 = getelementptr inbounds i32, ptr %241, i32 %257
  %270 = getelementptr inbounds i32, ptr %241, i32 %262
  %271 = zext i32 %263 to i64
  %272 = mul i64 %271, ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64)
  %273 = call ptr @memmove(ptr %269, ptr %270, i64 %272)
  %274 = sub i32 %239, 1
  store i32 %274, ptr @__global_m_e, align 4
  br label %rm_end

rm_end:                                           ; preds = %rm_found, %rm_loop
  %275 = load i32, ptr @__global_m_e, align 4
  %276 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_e, i32 0, i32 3), align 8
  %i_cont194 = alloca i32, align 4
  store i32 0, ptr %i_cont194, align 4
  %res_cont195 = alloca i1, align 1
  store i1 false, ptr %res_cont195, align 1
  br label %cont_loop190

rm_next:                                          ; preds = %rm_check
  %277 = add i32 %257, 1
  store i32 %277, ptr %i_rm, align 4
  br label %rm_loop

cont_loop190:                                     ; preds = %cont_next197, %rm_end
  %278 = load i32, ptr %i_cont194, align 4
  %279 = icmp slt i32 %278, %275
  br i1 %279, label %cont_check191, label %cont_end193

cont_check191:                                    ; preds = %cont_loop190
  %280 = getelementptr inbounds ptr, ptr %276, i32 %278
  %281 = load ptr, ptr %280, align 8
  %282 = call i32 @strcmp(ptr @46, ptr %281)
  %str_eq196 = icmp eq i32 %282, 0
  br i1 %str_eq196, label %cont_found192, label %cont_next197

cont_found192:                                    ; preds = %cont_check191
  store i1 true, ptr %res_cont195, align 1
  br label %cont_end193

cont_end193:                                      ; preds = %cont_found192, %cont_loop190
  %283 = load i1, ptr %res_cont195, align 1
  br i1 %283, label %if_then198, label %if_else199

cont_next197:                                     ; preds = %cont_check191
  %284 = add i32 %278, 1
  store i32 %284, ptr %i_cont194, align 4
  br label %cont_loop190

if_then198:                                       ; preds = %cont_end193
  %print_call201 = call i32 (ptr, ...) @printf(ptr @48, ptr @47)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge200

if_else199:                                       ; preds = %cont_end193
  %print_call202 = call i32 (ptr, ...) @printf(ptr @50, ptr @49)
  br label %if_merge200

if_merge200:                                      ; preds = %if_else199, %if_then198
  store i32 0, ptr @__global_m_f, align 4
  store i32 4, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_f, i32 0, i32 1), align 4
  %285 = call ptr @calloc(i64 4, i64 1)
  store ptr %285, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_f, i32 0, i32 2), align 8
  %286 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %286, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_f, i32 0, i32 3), align 8
  %287 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  store ptr %287, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_f, i32 0, i32 4), align 8
  %size203 = load i32, ptr @__global_m_f, align 4
  %capacity204 = load i32, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 1), align 4
  %keysBase205 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 3), align 8
  %valsBase206 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 4), align 8
  %i_put213 = alloca i32, align 4
  store i32 0, ptr %i_put213, align 4
  br label %put_loop207

put_loop207:                                      ; preds = %put_next210, %if_merge200
  %288 = load i32, ptr %i_put213, align 4
  %289 = icmp slt i32 %288, %size203
  br i1 %289, label %put_check208, label %put_not_found211

put_check208:                                     ; preds = %put_loop207
  %290 = getelementptr inbounds ptr, ptr %keysBase205, i32 %288
  %291 = load ptr, ptr %290, align 8
  %292 = call i32 @strcmp(ptr @51, ptr %291)
  %str_eq214 = icmp eq i32 %292, 0
  br i1 %str_eq214, label %put_found209, label %put_next210

put_found209:                                     ; preds = %put_check208
  %293 = getelementptr inbounds i32, ptr %valsBase206, i32 %288
  store i32 1, ptr %293, align 4
  br label %put_end212

put_next210:                                      ; preds = %put_check208
  %294 = add i32 %288, 1
  store i32 %294, ptr %i_put213, align 4
  br label %put_loop207

put_not_found211:                                 ; preds = %put_loop207
  %295 = icmp eq i32 %size203, %capacity204
  br i1 %295, label %put_grow215, label %put_append216

put_end212:                                       ; preds = %put_append216, %put_found209
  %296 = load i32, ptr @__global_m_f, align 4
  %297 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 3), align 8
  %i_cont222 = alloca i32, align 4
  store i32 0, ptr %i_cont222, align 4
  %res_cont223 = alloca i1, align 1
  store i1 false, ptr %res_cont223, align 1
  br label %cont_loop218

put_grow215:                                      ; preds = %put_not_found211
  %298 = mul i32 %capacity204, 2
  %299 = icmp sgt i32 %298, 3
  %growCap217 = select i1 %299, i32 %298, i32 4
  store i32 %growCap217, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 1), align 4
  %300 = zext i32 %growCap217 to i64
  %301 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %300
  %302 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 3), align 8
  %303 = call ptr @realloc(ptr %302, i64 %301)
  store ptr %303, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 3), align 8
  %304 = zext i32 %growCap217 to i64
  %305 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %304
  %306 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 4), align 8
  %307 = call ptr @realloc(ptr %306, i64 %305)
  store ptr %307, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 4), align 8
  br label %put_append216

put_append216:                                    ; preds = %put_grow215, %put_not_found211
  %308 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 3), align 8
  %309 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 4), align 8
  %310 = getelementptr inbounds ptr, ptr %308, i32 %size203
  store ptr @51, ptr %310, align 8
  %311 = getelementptr inbounds i32, ptr %309, i32 %size203
  store i32 1, ptr %311, align 4
  %312 = add i32 %size203, 1
  store i32 %312, ptr @__global_m_f, align 4
  br label %put_end212

cont_loop218:                                     ; preds = %cont_next225, %put_end212
  %313 = load i32, ptr %i_cont222, align 4
  %314 = icmp slt i32 %313, %296
  br i1 %314, label %cont_check219, label %cont_end221

cont_check219:                                    ; preds = %cont_loop218
  %315 = getelementptr inbounds ptr, ptr %297, i32 %313
  %316 = load ptr, ptr %315, align 8
  %317 = call i32 @strcmp(ptr @52, ptr %316)
  %str_eq224 = icmp eq i32 %317, 0
  br i1 %str_eq224, label %cont_found220, label %cont_next225

cont_found220:                                    ; preds = %cont_check219
  store i1 true, ptr %res_cont223, align 1
  br label %cont_end221

cont_end221:                                      ; preds = %cont_found220, %cont_loop218
  %318 = load i1, ptr %res_cont223, align 1
  br i1 %318, label %if_then226, label %if_else227

cont_next225:                                     ; preds = %cont_check219
  %319 = add i32 %313, 1
  store i32 %319, ptr %i_cont222, align 4
  br label %cont_loop218

if_then226:                                       ; preds = %cont_end221
  %print_call229 = call i32 (ptr, ...) @printf(ptr @54, ptr @53)
  br label %if_merge228

if_else227:                                       ; preds = %cont_end221
  %print_call230 = call i32 (ptr, ...) @printf(ptr @56, ptr @55)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge228

if_merge228:                                      ; preds = %if_else227, %if_then226
  %320 = load i32, ptr @__global_m_f, align 4
  %321 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_f, i32 0, i32 3), align 8
  %i_cont235 = alloca i32, align 4
  store i32 0, ptr %i_cont235, align 4
  %res_cont236 = alloca i1, align 1
  store i1 false, ptr %res_cont236, align 1
  br label %cont_loop231

cont_loop231:                                     ; preds = %cont_next238, %if_merge228
  %322 = load i32, ptr %i_cont235, align 4
  %323 = icmp slt i32 %322, %320
  br i1 %323, label %cont_check232, label %cont_end234

cont_check232:                                    ; preds = %cont_loop231
  %324 = getelementptr inbounds ptr, ptr %321, i32 %322
  %325 = load ptr, ptr %324, align 8
  %326 = call i32 @strcmp(ptr @57, ptr %325)
  %str_eq237 = icmp eq i32 %326, 0
  br i1 %str_eq237, label %cont_found233, label %cont_next238

cont_found233:                                    ; preds = %cont_check232
  store i1 true, ptr %res_cont236, align 1
  br label %cont_end234

cont_end234:                                      ; preds = %cont_found233, %cont_loop231
  %327 = load i1, ptr %res_cont236, align 1
  br i1 %327, label %if_then239, label %if_else240

cont_next238:                                     ; preds = %cont_check232
  %328 = add i32 %322, 1
  store i32 %328, ptr %i_cont235, align 4
  br label %cont_loop231

if_then239:                                       ; preds = %cont_end234
  %print_call242 = call i32 (ptr, ...) @printf(ptr @59, ptr @58)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge241

if_else240:                                       ; preds = %cont_end234
  %print_call243 = call i32 (ptr, ...) @printf(ptr @61, ptr @60)
  br label %if_merge241

if_merge241:                                      ; preds = %if_else240, %if_then239
  store i32 0, ptr @__global_m_g, align 4
  store i32 4, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_g, i32 0, i32 1), align 4
  %329 = call ptr @calloc(i64 4, i64 1)
  store ptr %329, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_g, i32 0, i32 2), align 8
  %330 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %330, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_g, i32 0, i32 3), align 8
  %331 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  store ptr %331, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_m_g, i32 0, i32 4), align 8
  %size244 = load i32, ptr @__global_m_g, align 4
  %capacity245 = load i32, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 1), align 4
  %keysBase246 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 3), align 8
  %valsBase247 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 4), align 8
  %i_put254 = alloca i32, align 4
  store i32 0, ptr %i_put254, align 4
  br label %put_loop248

put_loop248:                                      ; preds = %put_next251, %if_merge241
  %332 = load i32, ptr %i_put254, align 4
  %333 = icmp slt i32 %332, %size244
  br i1 %333, label %put_check249, label %put_not_found252

put_check249:                                     ; preds = %put_loop248
  %334 = getelementptr inbounds ptr, ptr %keysBase246, i32 %332
  %335 = load ptr, ptr %334, align 8
  %336 = call i32 @strcmp(ptr @62, ptr %335)
  %str_eq255 = icmp eq i32 %336, 0
  br i1 %str_eq255, label %put_found250, label %put_next251

put_found250:                                     ; preds = %put_check249
  %337 = getelementptr inbounds i32, ptr %valsBase247, i32 %332
  store i32 1, ptr %337, align 4
  br label %put_end253

put_next251:                                      ; preds = %put_check249
  %338 = add i32 %332, 1
  store i32 %338, ptr %i_put254, align 4
  br label %put_loop248

put_not_found252:                                 ; preds = %put_loop248
  %339 = icmp eq i32 %size244, %capacity245
  br i1 %339, label %put_grow256, label %put_append257

put_end253:                                       ; preds = %put_append257, %put_found250
  %340 = load i32, ptr @__global_m_g, align 4
  %341 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 3), align 8
  %i_cont263 = alloca i32, align 4
  store i32 0, ptr %i_cont263, align 4
  %res_cont264 = alloca i1, align 1
  store i1 false, ptr %res_cont264, align 1
  br label %cont_loop259

put_grow256:                                      ; preds = %put_not_found252
  %342 = mul i32 %capacity245, 2
  %343 = icmp sgt i32 %342, 3
  %growCap258 = select i1 %343, i32 %342, i32 4
  store i32 %growCap258, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 1), align 4
  %344 = zext i32 %growCap258 to i64
  %345 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %344
  %346 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 3), align 8
  %347 = call ptr @realloc(ptr %346, i64 %345)
  store ptr %347, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 3), align 8
  %348 = zext i32 %growCap258 to i64
  %349 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %348
  %350 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 4), align 8
  %351 = call ptr @realloc(ptr %350, i64 %349)
  store ptr %351, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 4), align 8
  br label %put_append257

put_append257:                                    ; preds = %put_grow256, %put_not_found252
  %352 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 3), align 8
  %353 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 4), align 8
  %354 = getelementptr inbounds ptr, ptr %352, i32 %size244
  store ptr @62, ptr %354, align 8
  %355 = getelementptr inbounds i32, ptr %353, i32 %size244
  store i32 1, ptr %355, align 4
  %356 = add i32 %size244, 1
  store i32 %356, ptr @__global_m_g, align 4
  br label %put_end253

cont_loop259:                                     ; preds = %cont_next266, %put_end253
  %357 = load i32, ptr %i_cont263, align 4
  %358 = icmp slt i32 %357, %340
  br i1 %358, label %cont_check260, label %cont_end262

cont_check260:                                    ; preds = %cont_loop259
  %359 = getelementptr inbounds ptr, ptr %341, i32 %357
  %360 = load ptr, ptr %359, align 8
  %361 = call i32 @strcmp(ptr @63, ptr %360)
  %str_eq265 = icmp eq i32 %361, 0
  br i1 %str_eq265, label %cont_found261, label %cont_next266

cont_found261:                                    ; preds = %cont_check260
  store i1 true, ptr %res_cont264, align 1
  br label %cont_end262

cont_end262:                                      ; preds = %cont_found261, %cont_loop259
  %362 = load i1, ptr %res_cont264, align 1
  %363 = sext i1 %362 to i32
  %364 = icmp eq i32 %363, 1
  br i1 %364, label %if_then267, label %if_else268

cont_next266:                                     ; preds = %cont_check260
  %365 = add i32 %357, 1
  store i32 %365, ptr %i_cont263, align 4
  br label %cont_loop259

if_then267:                                       ; preds = %cont_end262
  %print_call270 = call i32 (ptr, ...) @printf(ptr @65, ptr @64)
  br label %if_merge269

if_else268:                                       ; preds = %cont_end262
  %print_call271 = call i32 (ptr, ...) @printf(ptr @67, ptr @66)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge269

if_merge269:                                      ; preds = %if_else268, %if_then267
  %366 = load i32, ptr @__global_m_g, align 4
  %367 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_m_g, i32 0, i32 3), align 8
  %i_cont276 = alloca i32, align 4
  store i32 0, ptr %i_cont276, align 4
  %res_cont277 = alloca i1, align 1
  store i1 false, ptr %res_cont277, align 1
  br label %cont_loop272

cont_loop272:                                     ; preds = %cont_next279, %if_merge269
  %368 = load i32, ptr %i_cont276, align 4
  %369 = icmp slt i32 %368, %366
  br i1 %369, label %cont_check273, label %cont_end275

cont_check273:                                    ; preds = %cont_loop272
  %370 = getelementptr inbounds ptr, ptr %367, i32 %368
  %371 = load ptr, ptr %370, align 8
  %372 = call i32 @strcmp(ptr @68, ptr %371)
  %str_eq278 = icmp eq i32 %372, 0
  br i1 %str_eq278, label %cont_found274, label %cont_next279

cont_found274:                                    ; preds = %cont_check273
  store i1 true, ptr %res_cont277, align 1
  br label %cont_end275

cont_end275:                                      ; preds = %cont_found274, %cont_loop272
  %373 = load i1, ptr %res_cont277, align 1
  %374 = sext i1 %373 to i32
  %375 = icmp eq i32 %374, 1
  br i1 %375, label %if_then280, label %if_else281

cont_next279:                                     ; preds = %cont_check273
  %376 = add i32 %368, 1
  store i32 %376, ptr %i_cont276, align 4
  br label %cont_loop272

if_then280:                                       ; preds = %cont_end275
  %print_call283 = call i32 (ptr, ...) @printf(ptr @70, ptr @69)
  store i32 0, ptr @__global_ok, align 4
  br label %if_merge282

if_else281:                                       ; preds = %cont_end275
  %print_call284 = call i32 (ptr, ...) @printf(ptr @72, ptr @71)
  br label %if_merge282

if_merge282:                                      ; preds = %if_else281, %if_then280
  %ok_val = load i32, ptr @__global_ok, align 4
  %377 = icmp eq i32 %ok_val, 1
  br i1 %377, label %if_then285, label %if_else286

if_then285:                                       ; preds = %if_merge282
  %print_call288 = call i32 (ptr, ...) @printf(ptr @74, ptr @73)
  call void @exit(i32 0)
  br label %if_merge287

if_else286:                                       ; preds = %if_merge282
  %print_call289 = call i32 (ptr, ...) @printf(ptr @76, ptr @75)
  call void @exit(i32 1)
  br label %if_merge287

if_merge287:                                      ; preds = %if_else286, %if_then285
  ret void
}

declare ptr @calloc(i64, i64)

declare ptr @malloc(i64)

declare i32 @strcmp(ptr, ptr)

declare ptr @realloc(ptr, i64)

declare i32 @printf(ptr, ...)

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

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare ptr @aiml_path_intern(ptr)

declare ptr @memmove(ptr, ptr, i64)

declare void @exit(i32)

attributes #0 = { noinline }
