; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%Point = type { i32, i32 }
%AimlClassSchema = type { ptr, i32, i32, i32, i32, [256 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }

@__argc = external global i32
@__argv = external global ptr
@__global_a = global i32 0
@__global_b = global i32 0
@__global_c = global i32 0
@__global_s1 = global ptr null
@__global_s2 = global ptr null
@__global_s3 = global ptr null
@__global_x = global i32 0
@__global_y = global i32 0
@__global_p1 = global ptr null
@__global_p2 = global ptr null
@__global_p3 = global ptr null
@__global_pt1 = global %Point zeroinitializer
@__global_pt2 = global %Point zeroinitializer
@__global_pt3 = global %Point zeroinitializer
@0 = private unnamed_addr constant [20 x i8] c"FAIL A1: eq integer\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [21 x i8] c"FAIL A2: neq integer\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [21 x i8] c"A ok: eq/neq integer\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@7 = private unnamed_addr constant [6 x i8] c"hello\00", align 1
@8 = private unnamed_addr constant [6 x i8] c"world\00", align 1
@9 = private unnamed_addr constant [27 x i8] c"FAIL B1: eq string content\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [28 x i8] c"FAIL B2: neq string content\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@12 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@13 = private unnamed_addr constant [20 x i8] c"B ok: eq/neq string\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [12 x i8] c"FAIL C1: lt\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [12 x i8] c"FAIL C2: gt\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [13 x i8] c"FAIL C3: lte\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [13 x i8] c"FAIL C4: gte\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@23 = private unnamed_addr constant [19 x i8] c"FAIL C5: lte equal\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@25 = private unnamed_addr constant [19 x i8] c"FAIL C6: gte equal\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@27 = private unnamed_addr constant [20 x i8] c"C ok: lt/gt/lte/gte\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@28 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@29 = private unnamed_addr constant [5 x i8] c"same\00", align 1
@30 = private unnamed_addr constant [5 x i8] c"same\00", align 1
@31 = private unnamed_addr constant [6 x i8] c"other\00", align 1
@32 = private unnamed_addr constant [18 x i8] c"FAIL D1: peq self\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@33 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@34 = private unnamed_addr constant [24 x i8] c"FAIL D2: pneq different\00", align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@36 = private unnamed_addr constant [15 x i8] c"D ok: peq/pneq\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@37 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@tr_fname = private unnamed_addr constant [13 x i8] c"Point___eq__\00", align 1
@tr_args = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@tr_pfx = private unnamed_addr constant [6 x i8] c"this=\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@tr_pfx.16 = private unnamed_addr constant [9 x i8] c", other=\00", align 1
@tr_dir = private unnamed_addr constant [3 x i8] c"=>\00", align 1
@38 = private unnamed_addr constant [28 x i8] c"FAIL E1: __eq__ same values\00", align 1
@print_null_str.17 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@40 = private unnamed_addr constant [33 x i8] c"FAIL E2: __eq__ different values\00", align 1
@print_null_str.18 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [22 x i8] c"E ok: __eq__ overload\00", align 1
@print_null_str.19 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@43 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@44 = private unnamed_addr constant [27 x i8] c"All operator tests passed.\00", align 1
@print_null_str.20 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@45 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @Point___eq__, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @Point___eq__]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_Point___eq__ = private constant [13 x i8] c"Point___eq__\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_Point___eq__]
@__aiml_func_count = constant i32 1
@__aiml_sname_Point = private constant [6 x i8] c"Point\00"
@__aiml_sfield_Point_px = private constant [3 x i8] c"px\00"
@__aiml_sfield_Point_py = private constant [3 x i8] c"py\00"
@__aiml_schemas = global [1 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_Point, i32 1, i32 2, i32 8, i32 8, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Point_px }, %AimlFieldInfo { ptr @__aiml_sfield_Point_py }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 1

define void @__aiml_init() {
entry:
  store i32 5, ptr @__global_a, align 4
  store i32 5, ptr @__global_b, align 4
  store i32 7, ptr @__global_c, align 4
  %a_val = load i32, ptr @__global_a, align 4
  %b_val = load i32, ptr @__global_b, align 4
  %0 = icmp ne i32 %a_val, %b_val
  br i1 %0, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr @0)
  call void @exit(i32 1)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %entry
  %a_val1 = load i32, ptr @__global_a, align 4
  %c_val = load i32, ptr @__global_c, align 4
  %1 = icmp eq i32 %a_val1, %c_val
  br i1 %1, label %if_then2, label %if_merge3

if_then2:                                         ; preds = %if_merge
  %print_call4 = call i32 (ptr, ...) @printf(ptr @3, ptr @2)
  call void @exit(i32 1)
  br label %if_merge3

if_merge3:                                        ; preds = %if_then2, %if_merge
  %print_call5 = call i32 (ptr, ...) @printf(ptr @5, ptr @4)
  store ptr @6, ptr @__global_s1, align 8
  store ptr @7, ptr @__global_s2, align 8
  store ptr @8, ptr @__global_s3, align 8
  %s1_val = load ptr, ptr @__global_s1, align 8
  %s2_val = load ptr, ptr @__global_s2, align 8
  %2 = icmp eq ptr %s1_val, null
  %3 = icmp eq ptr %s1_val, %s2_val
  %4 = xor i1 %3, true
  %5 = icmp eq ptr %s2_val, null
  %6 = or i1 %2, %5
  br i1 %6, label %cmp_check_null_ne, label %cmp_do_strcmp_ne

cmp_check_null_ne:                                ; preds = %if_merge3
  br label %cmp_merge_ne

cmp_do_strcmp_ne:                                 ; preds = %if_merge3
  %7 = call i32 @strcmp(ptr %s1_val, ptr %s2_val)
  %8 = icmp ne i32 %7, 0
  br label %cmp_merge_ne

cmp_merge_ne:                                     ; preds = %cmp_do_strcmp_ne, %cmp_check_null_ne
  %strcmp_res_ne = phi i1 [ %4, %cmp_check_null_ne ], [ %8, %cmp_do_strcmp_ne ]
  br i1 %strcmp_res_ne, label %if_then6, label %if_merge7

if_then6:                                         ; preds = %cmp_merge_ne
  %print_call8 = call i32 (ptr, ...) @printf(ptr @10, ptr @9)
  call void @exit(i32 1)
  br label %if_merge7

if_merge7:                                        ; preds = %if_then6, %cmp_merge_ne
  %s1_val9 = load ptr, ptr @__global_s1, align 8
  %s3_val = load ptr, ptr @__global_s3, align 8
  %9 = icmp eq ptr %s1_val9, null
  %10 = icmp eq ptr %s1_val9, %s3_val
  %11 = icmp eq ptr %s3_val, null
  %12 = or i1 %9, %11
  br i1 %12, label %cmp_check_null, label %cmp_do_strcmp

cmp_check_null:                                   ; preds = %if_merge7
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %if_merge7
  %13 = call i32 @strcmp(ptr %s1_val9, ptr %s3_val)
  %14 = icmp eq i32 %13, 0
  br label %cmp_merge

cmp_merge:                                        ; preds = %cmp_do_strcmp, %cmp_check_null
  %strcmp_res = phi i1 [ %10, %cmp_check_null ], [ %14, %cmp_do_strcmp ]
  br i1 %strcmp_res, label %if_then10, label %if_merge11

if_then10:                                        ; preds = %cmp_merge
  %print_call12 = call i32 (ptr, ...) @printf(ptr @12, ptr @11)
  call void @exit(i32 1)
  br label %if_merge11

if_merge11:                                       ; preds = %if_then10, %cmp_merge
  %print_call13 = call i32 (ptr, ...) @printf(ptr @14, ptr @13)
  store i32 3, ptr @__global_x, align 4
  store i32 7, ptr @__global_y, align 4
  %x_val = load i32, ptr @__global_x, align 4
  %y_val = load i32, ptr @__global_y, align 4
  %15 = icmp sgt i32 %x_val, %y_val
  br i1 %15, label %if_then14, label %if_merge15

if_then14:                                        ; preds = %if_merge11
  %print_call16 = call i32 (ptr, ...) @printf(ptr @16, ptr @15)
  call void @exit(i32 1)
  br label %if_merge15

if_merge15:                                       ; preds = %if_then14, %if_merge11
  %y_val17 = load i32, ptr @__global_y, align 4
  %x_val18 = load i32, ptr @__global_x, align 4
  %16 = icmp slt i32 %y_val17, %x_val18
  br i1 %16, label %if_then19, label %if_merge20

if_then19:                                        ; preds = %if_merge15
  %print_call21 = call i32 (ptr, ...) @printf(ptr @18, ptr @17)
  call void @exit(i32 1)
  br label %if_merge20

if_merge20:                                       ; preds = %if_then19, %if_merge15
  %x_val22 = load i32, ptr @__global_x, align 4
  %y_val23 = load i32, ptr @__global_y, align 4
  %17 = icmp sge i32 %x_val22, %y_val23
  br i1 %17, label %if_then24, label %if_merge25

if_then24:                                        ; preds = %if_merge20
  %print_call26 = call i32 (ptr, ...) @printf(ptr @20, ptr @19)
  call void @exit(i32 1)
  br label %if_merge25

if_merge25:                                       ; preds = %if_then24, %if_merge20
  %y_val27 = load i32, ptr @__global_y, align 4
  %x_val28 = load i32, ptr @__global_x, align 4
  %18 = icmp sle i32 %y_val27, %x_val28
  br i1 %18, label %if_then29, label %if_merge30

if_then29:                                        ; preds = %if_merge25
  %print_call31 = call i32 (ptr, ...) @printf(ptr @22, ptr @21)
  call void @exit(i32 1)
  br label %if_merge30

if_merge30:                                       ; preds = %if_then29, %if_merge25
  %x_val32 = load i32, ptr @__global_x, align 4
  %x_val33 = load i32, ptr @__global_x, align 4
  %19 = icmp sgt i32 %x_val32, %x_val33
  br i1 %19, label %if_then34, label %if_merge35

if_then34:                                        ; preds = %if_merge30
  %print_call36 = call i32 (ptr, ...) @printf(ptr @24, ptr @23)
  call void @exit(i32 1)
  br label %if_merge35

if_merge35:                                       ; preds = %if_then34, %if_merge30
  %x_val37 = load i32, ptr @__global_x, align 4
  %x_val38 = load i32, ptr @__global_x, align 4
  %20 = icmp slt i32 %x_val37, %x_val38
  br i1 %20, label %if_then39, label %if_merge40

if_then39:                                        ; preds = %if_merge35
  %print_call41 = call i32 (ptr, ...) @printf(ptr @26, ptr @25)
  call void @exit(i32 1)
  br label %if_merge40

if_merge40:                                       ; preds = %if_then39, %if_merge35
  %print_call42 = call i32 (ptr, ...) @printf(ptr @28, ptr @27)
  store ptr @29, ptr @__global_p1, align 8
  store ptr @30, ptr @__global_p2, align 8
  store ptr @31, ptr @__global_p3, align 8
  %p1_val = load ptr, ptr @__global_p1, align 8
  %p1_val43 = load ptr, ptr @__global_p1, align 8
  %21 = icmp ne ptr %p1_val, %p1_val43
  br i1 %21, label %if_then44, label %if_merge45

if_then44:                                        ; preds = %if_merge40
  %print_call46 = call i32 (ptr, ...) @printf(ptr @33, ptr @32)
  call void @exit(i32 1)
  br label %if_merge45

if_merge45:                                       ; preds = %if_then44, %if_merge40
  %p1_val47 = load ptr, ptr @__global_p1, align 8
  %p3_val = load ptr, ptr @__global_p3, align 8
  %22 = icmp eq ptr %p1_val47, %p3_val
  br i1 %22, label %if_then48, label %if_merge49

if_then48:                                        ; preds = %if_merge45
  %print_call50 = call i32 (ptr, ...) @printf(ptr @35, ptr @34)
  call void @exit(i32 1)
  br label %if_merge49

if_merge49:                                       ; preds = %if_then48, %if_merge45
  %print_call51 = call i32 (ptr, ...) @printf(ptr @37, ptr @36)
  store i32 3, ptr @__global_pt1, align 4
  store i32 4, ptr getelementptr inbounds (%Point, ptr @__global_pt1, i32 0, i32 1), align 4
  store i32 3, ptr @__global_pt2, align 4
  store i32 4, ptr getelementptr inbounds (%Point, ptr @__global_pt2, i32 0, i32 1), align 4
  store i32 1, ptr @__global_pt3, align 4
  store i32 2, ptr getelementptr inbounds (%Point, ptr @__global_pt3, i32 0, i32 1), align 4
  %pt1_val = load %Point, ptr @__global_pt1, align 4
  %pt2_val = load %Point, ptr @__global_pt2, align 4
  %23 = call i32 @Point___eq__(ptr @__global_pt1, ptr @__global_pt2)
  %24 = icmp eq i32 %23, 0
  br i1 %24, label %if_then52, label %if_merge53

if_then52:                                        ; preds = %if_merge49
  %print_call54 = call i32 (ptr, ...) @printf(ptr @39, ptr @38)
  call void @exit(i32 1)
  br label %if_merge53

if_merge53:                                       ; preds = %if_then52, %if_merge49
  %pt1_val55 = load %Point, ptr @__global_pt1, align 4
  %pt3_val = load %Point, ptr @__global_pt3, align 4
  %25 = call i32 @Point___eq__(ptr @__global_pt1, ptr @__global_pt3)
  %26 = icmp ne i32 %25, 0
  br i1 %26, label %if_then56, label %if_merge57

if_then56:                                        ; preds = %if_merge53
  %print_call58 = call i32 (ptr, ...) @printf(ptr @41, ptr @40)
  call void @exit(i32 1)
  br label %if_merge57

if_merge57:                                       ; preds = %if_then56, %if_merge53
  %print_call59 = call i32 (ptr, ...) @printf(ptr @43, ptr @42)
  %print_call60 = call i32 (ptr, ...) @printf(ptr @45, ptr @44)
  ret void
}

declare i32 @printf(ptr, ...)

declare void @exit(i32)

declare i32 @strcmp(ptr, ptr)

define i32 @Point___eq__(ptr %0, ptr %1) personality ptr @__gxx_personality_v0 {
entry:
  %this = alloca ptr, align 8
  store ptr %0, ptr %this, align 8
  %other = alloca ptr, align 8
  store ptr %1, ptr %other, align 8
  %tr_active = call i32 @aiml_trace_active(ptr @tr_fname)
  %tr_cond = icmp ne i32 %tr_active, 0
  br i1 %tr_cond, label %tr_enter, label %tr_body

tr_enter:                                         ; preds = %entry
  %this_tr = load ptr, ptr %this, align 8
  %2 = call ptr @__aiml_str_concat(ptr @tr_args, ptr @tr_pfx)
  %3 = call ptr @__aiml_str_concat(ptr %2, ptr %this_tr)
  %other_tr = load ptr, ptr %other, align 8
  %4 = call ptr @__aiml_str_concat(ptr %3, ptr @tr_pfx.16)
  %5 = call ptr @__aiml_str_concat(ptr %4, ptr %other_tr)
  call void @aiml_trace(ptr @tr_fname, ptr @tr_dir, ptr %5)
  br label %tr_body

tr_body:                                          ; preds = %tr_enter, %entry
  %prop_ptr_base = load ptr, ptr %this, align 8
  %prop_ptr = getelementptr inbounds %Point, ptr %prop_ptr_base, i32 0, i32 0
  %px_val = load i32, ptr %prop_ptr, align 4
  %prop_ptr_base1 = load ptr, ptr %other, align 8
  %prop_ptr2 = getelementptr inbounds %Point, ptr %prop_ptr_base1, i32 0, i32 0
  %px_val3 = load i32, ptr %prop_ptr2, align 4
  %6 = icmp ne i32 %px_val, %px_val3
  br i1 %6, label %if_then, label %if_merge

if_then:                                          ; preds = %tr_body
  ret i32 0

if_merge:                                         ; preds = %ret_dead, %tr_body
  %prop_ptr_base4 = load ptr, ptr %this, align 8
  %prop_ptr5 = getelementptr inbounds %Point, ptr %prop_ptr_base4, i32 0, i32 1
  %py_val = load i32, ptr %prop_ptr5, align 4
  %prop_ptr_base6 = load ptr, ptr %other, align 8
  %prop_ptr7 = getelementptr inbounds %Point, ptr %prop_ptr_base6, i32 0, i32 1
  %py_val8 = load i32, ptr %prop_ptr7, align 4
  %7 = icmp ne i32 %py_val, %py_val8
  br i1 %7, label %if_then9, label %if_merge10

ret_dead:                                         ; No predecessors!
  br label %if_merge

if_then9:                                         ; preds = %if_merge
  ret i32 0

if_merge10:                                       ; preds = %ret_dead11, %if_merge
  ret i32 1

ret_dead11:                                       ; No predecessors!
  br label %if_merge10

ret_dead12:                                       ; No predecessors!
  ret i32 0
}

declare i32 @__gxx_personality_v0(...)

declare i32 @aiml_trace_active(ptr)

declare void @aiml_trace(ptr, ptr, ptr)

declare i64 @strlen(ptr)

declare ptr @malloc(i64)

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

attributes #0 = { noinline }
