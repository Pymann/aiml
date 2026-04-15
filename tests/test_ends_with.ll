; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

@__argc = external global i32
@__argv = external global ptr
@tr_fname = private unnamed_addr constant [5 x i8] c"main\00", align 1
@tr_args = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@tr_dir = private unnamed_addr constant [3 x i8] c"=>\00", align 1
@0 = private unnamed_addr constant [11 x i8] c"hello.json\00", align 1
@1 = private unnamed_addr constant [6 x i8] c".json\00", align 1
@2 = private unnamed_addr constant [37 x i8] c"PASS: 'hello.json' ends_with '.json'\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [54 x i8] c"FAIL: 'hello.json' ends_with '.json' expected 1, got \00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [4 x i8] c".md\00", align 1
@7 = private unnamed_addr constant [39 x i8] c"PASS: 'hello.json' not ends_with '.md'\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@9 = private unnamed_addr constant [52 x i8] c"FAIL: 'hello.json' ends_with '.md' expected 0, got \00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [3 x i8] c"ab\00", align 1
@12 = private unnamed_addr constant [7 x i8] c"abcdef\00", align 1
@13 = private unnamed_addr constant [52 x i8] c"PASS: 'ab' not ends_with 'abcdef' (suffix too long)\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [47 x i8] c"FAIL: 'ab' ends_with 'abcdef' expected 0, got \00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [4 x i8] c".md\00", align 1
@18 = private unnamed_addr constant [4 x i8] c".md\00", align 1
@19 = private unnamed_addr constant [42 x i8] c"PASS: '.md' ends_with '.md' (exact match)\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [45 x i8] c"FAIL: '.md' ends_with '.md' expected 1, got \00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@23 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@24 = private unnamed_addr constant [47 x i8] c"PASS: 'hello.json' ends_with '' (empty suffix)\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@26 = private unnamed_addr constant [49 x i8] c"FAIL: 'hello.json' ends_with '' expected 1, got \00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@28 = private unnamed_addr constant [20 x i8] c"docs/v1.2/readme.md\00", align 1
@29 = private unnamed_addr constant [4 x i8] c".md\00", align 1
@30 = private unnamed_addr constant [44 x i8] c"PASS: 'docs/v1.2/readme.md' ends_with '.md'\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@31 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@32 = private unnamed_addr constant [61 x i8] c"FAIL: 'docs/v1.2/readme.md' ends_with '.md' expected 1, got \00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@33 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@34 = private unnamed_addr constant [10 x i8] c"notes.txt\00", align 1
@35 = private unnamed_addr constant [5 x i8] c".txt\00", align 1
@36 = private unnamed_addr constant [35 x i8] c"PASS: 'notes.txt' ends_with '.txt'\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@37 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@38 = private unnamed_addr constant [52 x i8] c"FAIL: 'notes.txt' ends_with '.txt' expected 1, got \00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@40 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str.14 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [12 x i8] c"ends_with: \00", align 1
@43 = private unnamed_addr constant [10 x i8] c" passed, \00", align 1
@44 = private unnamed_addr constant [8 x i8] c" failed\00", align 1
@print_null_str.15 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@45 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @main, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @main]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_main = private constant [5 x i8] c"main\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_main]
@__aiml_func_count = constant i32 1
@__aiml_schema_count = constant i32 0

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %tr_active = call i32 @aiml_trace_active(ptr @tr_fname)
  %tr_cond = icmp ne i32 %tr_active, 0
  br i1 %tr_cond, label %tr_enter, label %tr_body

tr_enter:                                         ; preds = %entry
  call void @aiml_trace(ptr @tr_fname, ptr @tr_dir, ptr @tr_args)
  br label %tr_body

tr_body:                                          ; preds = %tr_enter, %entry
  %pass = alloca i32, align 4
  store i32 0, ptr %pass, align 4
  store i32 0, ptr %pass, align 4
  %fail = alloca i32, align 4
  store i32 0, ptr %fail, align 4
  store i32 0, ptr %fail, align 4
  %s1 = alloca ptr, align 8
  store ptr null, ptr %s1, align 8
  store ptr @0, ptr %s1, align 8
  %r1 = alloca i32, align 4
  store i32 0, ptr %r1, align 4
  %0 = load ptr, ptr %s1, align 8
  %1 = call i64 @strlen(ptr %0)
  %2 = call i64 @strlen(ptr @1)
  %3 = icmp ugt i64 %2, %1
  %4 = sub i64 %1, %2
  %5 = getelementptr i8, ptr %0, i64 %4
  %6 = call i32 @strncmp(ptr %5, ptr @1, i64 %2)
  %7 = icmp eq i32 %6, 0
  %8 = zext i1 %7 to i32
  %9 = select i1 %3, i32 0, i32 %8
  store i32 %9, ptr %r1, align 4
  %r1_val = load i32, ptr %r1, align 4
  %10 = icmp eq i32 %r1_val, 1
  br i1 %10, label %if_then, label %if_else

if_then:                                          ; preds = %tr_body
  %print_call = call i32 (ptr, ...) @printf(ptr @3, ptr @2)
  %pass_val = load i32, ptr %pass, align 4
  %11 = add i32 %pass_val, 1
  store i32 %11, ptr %pass, align 4
  br label %if_merge

if_else:                                          ; preds = %tr_body
  %r1_val1 = load i32, ptr %r1, align 4
  %12 = call ptr @__aiml_int_to_string(i32 %r1_val1)
  %13 = call ptr @__aiml_str_concat(ptr @4, ptr %12)
  %print_null_check = icmp eq ptr %13, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.1, ptr %13
  %print_call2 = call i32 (ptr, ...) @printf(ptr @5, ptr %print_safe_str)
  %fail_val = load i32, ptr %fail, align 4
  %14 = add i32 %fail_val, 1
  store i32 %14, ptr %fail, align 4
  br label %if_merge

if_merge:                                         ; preds = %if_else, %if_then
  %r2 = alloca i32, align 4
  store i32 0, ptr %r2, align 4
  %15 = load ptr, ptr %s1, align 8
  %16 = call i64 @strlen(ptr %15)
  %17 = call i64 @strlen(ptr @6)
  %18 = icmp ugt i64 %17, %16
  %19 = sub i64 %16, %17
  %20 = getelementptr i8, ptr %15, i64 %19
  %21 = call i32 @strncmp(ptr %20, ptr @6, i64 %17)
  %22 = icmp eq i32 %21, 0
  %23 = zext i1 %22 to i32
  %24 = select i1 %18, i32 0, i32 %23
  store i32 %24, ptr %r2, align 4
  %r2_val = load i32, ptr %r2, align 4
  %25 = icmp eq i32 %r2_val, 0
  br i1 %25, label %if_then3, label %if_else4

if_then3:                                         ; preds = %if_merge
  %print_call6 = call i32 (ptr, ...) @printf(ptr @8, ptr @7)
  %pass_val7 = load i32, ptr %pass, align 4
  %26 = add i32 %pass_val7, 1
  store i32 %26, ptr %pass, align 4
  br label %if_merge5

if_else4:                                         ; preds = %if_merge
  %r2_val8 = load i32, ptr %r2, align 4
  %27 = call ptr @__aiml_int_to_string(i32 %r2_val8)
  %28 = call ptr @__aiml_str_concat(ptr @9, ptr %27)
  %print_null_check9 = icmp eq ptr %28, null
  %print_safe_str10 = select i1 %print_null_check9, ptr @print_null_str.3, ptr %28
  %print_call11 = call i32 (ptr, ...) @printf(ptr @10, ptr %print_safe_str10)
  %fail_val12 = load i32, ptr %fail, align 4
  %29 = add i32 %fail_val12, 1
  store i32 %29, ptr %fail, align 4
  br label %if_merge5

if_merge5:                                        ; preds = %if_else4, %if_then3
  %s3 = alloca ptr, align 8
  store ptr null, ptr %s3, align 8
  store ptr @11, ptr %s3, align 8
  %r3 = alloca i32, align 4
  store i32 0, ptr %r3, align 4
  %30 = load ptr, ptr %s3, align 8
  %31 = call i64 @strlen(ptr %30)
  %32 = call i64 @strlen(ptr @12)
  %33 = icmp ugt i64 %32, %31
  %34 = sub i64 %31, %32
  %35 = getelementptr i8, ptr %30, i64 %34
  %36 = call i32 @strncmp(ptr %35, ptr @12, i64 %32)
  %37 = icmp eq i32 %36, 0
  %38 = zext i1 %37 to i32
  %39 = select i1 %33, i32 0, i32 %38
  store i32 %39, ptr %r3, align 4
  %r3_val = load i32, ptr %r3, align 4
  %40 = icmp eq i32 %r3_val, 0
  br i1 %40, label %if_then13, label %if_else14

if_then13:                                        ; preds = %if_merge5
  %print_call16 = call i32 (ptr, ...) @printf(ptr @14, ptr @13)
  %pass_val17 = load i32, ptr %pass, align 4
  %41 = add i32 %pass_val17, 1
  store i32 %41, ptr %pass, align 4
  br label %if_merge15

if_else14:                                        ; preds = %if_merge5
  %r3_val18 = load i32, ptr %r3, align 4
  %42 = call ptr @__aiml_int_to_string(i32 %r3_val18)
  %43 = call ptr @__aiml_str_concat(ptr @15, ptr %42)
  %print_null_check19 = icmp eq ptr %43, null
  %print_safe_str20 = select i1 %print_null_check19, ptr @print_null_str.5, ptr %43
  %print_call21 = call i32 (ptr, ...) @printf(ptr @16, ptr %print_safe_str20)
  %fail_val22 = load i32, ptr %fail, align 4
  %44 = add i32 %fail_val22, 1
  store i32 %44, ptr %fail, align 4
  br label %if_merge15

if_merge15:                                       ; preds = %if_else14, %if_then13
  %s4 = alloca ptr, align 8
  store ptr null, ptr %s4, align 8
  store ptr @17, ptr %s4, align 8
  %r4 = alloca i32, align 4
  store i32 0, ptr %r4, align 4
  %45 = load ptr, ptr %s4, align 8
  %46 = call i64 @strlen(ptr %45)
  %47 = call i64 @strlen(ptr @18)
  %48 = icmp ugt i64 %47, %46
  %49 = sub i64 %46, %47
  %50 = getelementptr i8, ptr %45, i64 %49
  %51 = call i32 @strncmp(ptr %50, ptr @18, i64 %47)
  %52 = icmp eq i32 %51, 0
  %53 = zext i1 %52 to i32
  %54 = select i1 %48, i32 0, i32 %53
  store i32 %54, ptr %r4, align 4
  %r4_val = load i32, ptr %r4, align 4
  %55 = icmp eq i32 %r4_val, 1
  br i1 %55, label %if_then23, label %if_else24

if_then23:                                        ; preds = %if_merge15
  %print_call26 = call i32 (ptr, ...) @printf(ptr @20, ptr @19)
  %pass_val27 = load i32, ptr %pass, align 4
  %56 = add i32 %pass_val27, 1
  store i32 %56, ptr %pass, align 4
  br label %if_merge25

if_else24:                                        ; preds = %if_merge15
  %r4_val28 = load i32, ptr %r4, align 4
  %57 = call ptr @__aiml_int_to_string(i32 %r4_val28)
  %58 = call ptr @__aiml_str_concat(ptr @21, ptr %57)
  %print_null_check29 = icmp eq ptr %58, null
  %print_safe_str30 = select i1 %print_null_check29, ptr @print_null_str.7, ptr %58
  %print_call31 = call i32 (ptr, ...) @printf(ptr @22, ptr %print_safe_str30)
  %fail_val32 = load i32, ptr %fail, align 4
  %59 = add i32 %fail_val32, 1
  store i32 %59, ptr %fail, align 4
  br label %if_merge25

if_merge25:                                       ; preds = %if_else24, %if_then23
  %r5 = alloca i32, align 4
  store i32 0, ptr %r5, align 4
  %60 = load ptr, ptr %s1, align 8
  %61 = call i64 @strlen(ptr %60)
  %62 = call i64 @strlen(ptr @23)
  %63 = icmp ugt i64 %62, %61
  %64 = sub i64 %61, %62
  %65 = getelementptr i8, ptr %60, i64 %64
  %66 = call i32 @strncmp(ptr %65, ptr @23, i64 %62)
  %67 = icmp eq i32 %66, 0
  %68 = zext i1 %67 to i32
  %69 = select i1 %63, i32 0, i32 %68
  store i32 %69, ptr %r5, align 4
  %r5_val = load i32, ptr %r5, align 4
  %70 = icmp eq i32 %r5_val, 1
  br i1 %70, label %if_then33, label %if_else34

if_then33:                                        ; preds = %if_merge25
  %print_call36 = call i32 (ptr, ...) @printf(ptr @25, ptr @24)
  %pass_val37 = load i32, ptr %pass, align 4
  %71 = add i32 %pass_val37, 1
  store i32 %71, ptr %pass, align 4
  br label %if_merge35

if_else34:                                        ; preds = %if_merge25
  %r5_val38 = load i32, ptr %r5, align 4
  %72 = call ptr @__aiml_int_to_string(i32 %r5_val38)
  %73 = call ptr @__aiml_str_concat(ptr @26, ptr %72)
  %print_null_check39 = icmp eq ptr %73, null
  %print_safe_str40 = select i1 %print_null_check39, ptr @print_null_str.9, ptr %73
  %print_call41 = call i32 (ptr, ...) @printf(ptr @27, ptr %print_safe_str40)
  %fail_val42 = load i32, ptr %fail, align 4
  %74 = add i32 %fail_val42, 1
  store i32 %74, ptr %fail, align 4
  br label %if_merge35

if_merge35:                                       ; preds = %if_else34, %if_then33
  %s6 = alloca ptr, align 8
  store ptr null, ptr %s6, align 8
  store ptr @28, ptr %s6, align 8
  %r6 = alloca i32, align 4
  store i32 0, ptr %r6, align 4
  %75 = load ptr, ptr %s6, align 8
  %76 = call i64 @strlen(ptr %75)
  %77 = call i64 @strlen(ptr @29)
  %78 = icmp ugt i64 %77, %76
  %79 = sub i64 %76, %77
  %80 = getelementptr i8, ptr %75, i64 %79
  %81 = call i32 @strncmp(ptr %80, ptr @29, i64 %77)
  %82 = icmp eq i32 %81, 0
  %83 = zext i1 %82 to i32
  %84 = select i1 %78, i32 0, i32 %83
  store i32 %84, ptr %r6, align 4
  %r6_val = load i32, ptr %r6, align 4
  %85 = icmp eq i32 %r6_val, 1
  br i1 %85, label %if_then43, label %if_else44

if_then43:                                        ; preds = %if_merge35
  %print_call46 = call i32 (ptr, ...) @printf(ptr @31, ptr @30)
  %pass_val47 = load i32, ptr %pass, align 4
  %86 = add i32 %pass_val47, 1
  store i32 %86, ptr %pass, align 4
  br label %if_merge45

if_else44:                                        ; preds = %if_merge35
  %r6_val48 = load i32, ptr %r6, align 4
  %87 = call ptr @__aiml_int_to_string(i32 %r6_val48)
  %88 = call ptr @__aiml_str_concat(ptr @32, ptr %87)
  %print_null_check49 = icmp eq ptr %88, null
  %print_safe_str50 = select i1 %print_null_check49, ptr @print_null_str.11, ptr %88
  %print_call51 = call i32 (ptr, ...) @printf(ptr @33, ptr %print_safe_str50)
  %fail_val52 = load i32, ptr %fail, align 4
  %89 = add i32 %fail_val52, 1
  store i32 %89, ptr %fail, align 4
  br label %if_merge45

if_merge45:                                       ; preds = %if_else44, %if_then43
  %s7 = alloca ptr, align 8
  store ptr null, ptr %s7, align 8
  store ptr @34, ptr %s7, align 8
  %r7 = alloca i32, align 4
  store i32 0, ptr %r7, align 4
  %90 = load ptr, ptr %s7, align 8
  %91 = call i64 @strlen(ptr %90)
  %92 = call i64 @strlen(ptr @35)
  %93 = icmp ugt i64 %92, %91
  %94 = sub i64 %91, %92
  %95 = getelementptr i8, ptr %90, i64 %94
  %96 = call i32 @strncmp(ptr %95, ptr @35, i64 %92)
  %97 = icmp eq i32 %96, 0
  %98 = zext i1 %97 to i32
  %99 = select i1 %93, i32 0, i32 %98
  store i32 %99, ptr %r7, align 4
  %r7_val = load i32, ptr %r7, align 4
  %100 = icmp eq i32 %r7_val, 1
  br i1 %100, label %if_then53, label %if_else54

if_then53:                                        ; preds = %if_merge45
  %print_call56 = call i32 (ptr, ...) @printf(ptr @37, ptr @36)
  %pass_val57 = load i32, ptr %pass, align 4
  %101 = add i32 %pass_val57, 1
  store i32 %101, ptr %pass, align 4
  br label %if_merge55

if_else54:                                        ; preds = %if_merge45
  %r7_val58 = load i32, ptr %r7, align 4
  %102 = call ptr @__aiml_int_to_string(i32 %r7_val58)
  %103 = call ptr @__aiml_str_concat(ptr @38, ptr %102)
  %print_null_check59 = icmp eq ptr %103, null
  %print_safe_str60 = select i1 %print_null_check59, ptr @print_null_str.13, ptr %103
  %print_call61 = call i32 (ptr, ...) @printf(ptr @39, ptr %print_safe_str60)
  %fail_val62 = load i32, ptr %fail, align 4
  %104 = add i32 %fail_val62, 1
  store i32 %104, ptr %fail, align 4
  br label %if_merge55

if_merge55:                                       ; preds = %if_else54, %if_then53
  %print_call63 = call i32 (ptr, ...) @printf(ptr @41, ptr @40)
  %pass_val64 = load i32, ptr %pass, align 4
  %105 = call ptr @__aiml_int_to_string(i32 %pass_val64)
  %106 = call ptr @__aiml_str_concat(ptr @42, ptr %105)
  %107 = call ptr @__aiml_str_concat(ptr %106, ptr @43)
  %fail_val65 = load i32, ptr %fail, align 4
  %108 = call ptr @__aiml_int_to_string(i32 %fail_val65)
  %109 = call ptr @__aiml_str_concat(ptr %107, ptr %108)
  %110 = call ptr @__aiml_str_concat(ptr %109, ptr @44)
  %print_null_check66 = icmp eq ptr %110, null
  %print_safe_str67 = select i1 %print_null_check66, ptr @print_null_str.15, ptr %110
  %print_call68 = call i32 (ptr, ...) @printf(ptr @45, ptr %print_safe_str67)
  %fail_val69 = load i32, ptr %fail, align 4
  ret i32 %fail_val69

ret_dead:                                         ; No predecessors!
  ret i32 0
}

define void @__aiml_init() {
entry:
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare i32 @aiml_trace_active(ptr)

declare void @aiml_trace(ptr, ptr, ptr)

declare i32 @strncmp(ptr, ptr, i64)

declare i64 @strlen(ptr)

declare i32 @printf(ptr, ...)

declare ptr @malloc(i64)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

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
