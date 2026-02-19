; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%list_string = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }

@__argc = internal global i32 0
@__argv = internal global ptr null
@0 = private unnamed_addr constant [24 x i8] c"Running System Tests...\00", align 1
@fmt_str = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@1 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@2 = private unnamed_addr constant [17 x i8] c"--- File I/O ---\00", align 1
@fmt_str.1 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@3 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@4 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@5 = private unnamed_addr constant [2 x i8] c"w\00", align 1
@6 = private unnamed_addr constant [18 x i8] c"Hello AIML World\0A\00", align 1
@7 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@8 = private unnamed_addr constant [2 x i8] c"r\00", align 1
@9 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@10 = private unnamed_addr constant [15 x i8] c"Read content: \00", align 1
@fmt_str.2 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@11 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@12 = private unnamed_addr constant [2 x i8] c"w\00", align 1
@13 = private unnamed_addr constant [24 x i8] c"Direct write to stdout\0A\00", align 1
@14 = private unnamed_addr constant [13 x i8] c"--- Time ---\00", align 1
@fmt_str.3 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@15 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@16 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@17 = private unnamed_addr constant [13 x i8] c"Start time: \00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@fmt_str.4 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@19 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@20 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@21 = private unnamed_addr constant [11 x i8] c"End time: \00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@fmt_str.5 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@23 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@24 = private unnamed_addr constant [15 x i8] c"--- System ---\00", align 1
@fmt_str.6 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@25 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@26 = private unnamed_addr constant [46 x i8] c"[SysGetArg] argc: %d, argvPtr: %p, index: %d\0A\00", align 1
@27 = private unnamed_addr constant [31 x i8] c"[SysGetArg] Returning arg: %s\0A\00", align 1
@28 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@29 = private unnamed_addr constant [45 x i8] c"[SysGetArg] Unsafe access! Returning empty.\0A\00", align 1
@30 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@31 = private unnamed_addr constant [8 x i8] c"Arg 0: \00", align 1
@fmt_str.7 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@32 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@33 = private unnamed_addr constant [16 x i8] c"--- Process ---\00", align 1
@fmt_str.8 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@34 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@35 = private unnamed_addr constant [3 x i8] c"ls\00", align 1
@36 = private unnamed_addr constant [3 x i8] c"-l\00", align 1
@37 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@38 = private unnamed_addr constant [19 x i8] c"System Tests Done.\00", align 1
@fmt_str.9 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@39 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define i32 @main(i32 %argc, ptr %argv) personality ptr @__gxx_personality_v0 {
entry:
  store i32 %argc, ptr @__argc, align 4
  store ptr %argv, ptr @__argv, align 8
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str, ptr @0)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @1)
  %2 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.1, ptr @2)
  %3 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @3)
  %f = alloca ptr, align 8
  store ptr null, ptr %f, align 8
  %4 = call ptr @fopen(ptr @4, ptr @5)
  store ptr %4, ptr %f, align 8
  %f_val = load ptr, ptr %f, align 8
  %5 = call i64 @strlen(ptr @6)
  %6 = call i64 @fwrite(ptr @6, i64 1, i64 %5, ptr %f_val)
  %f_val1 = load ptr, ptr %f, align 8
  %7 = call i32 @fclose(ptr %f_val1)
  %8 = call ptr @fopen(ptr @7, ptr @8)
  store ptr %8, ptr %f, align 8
  %content = alloca ptr, align 8
  store ptr null, ptr %content, align 8
  %f_val2 = load ptr, ptr %f, align 8
  %9 = call i32 @fseek(ptr %f_val2, i64 0, i32 2)
  %10 = call i64 @ftell(ptr %f_val2)
  call void @rewind(ptr %f_val2)
  %11 = add i64 %10, 1
  %12 = call ptr @malloc(i64 %11)
  %13 = call i64 @fread(ptr %12, i64 1, i64 %10, ptr %f_val2)
  %14 = getelementptr inbounds i8, ptr %12, i64 %13
  store i8 0, ptr %14, align 1
  store ptr %12, ptr %content, align 8
  %f_val3 = load ptr, ptr %f, align 8
  %15 = call i32 @fclose(ptr %f_val3)
  %16 = call i64 @strlen(ptr @9)
  %17 = call i64 @strlen(ptr @10)
  %18 = add i64 %16, %17
  %19 = add i64 %18, 1
  %20 = call ptr @malloc(i64 %19)
  %21 = call ptr @strcpy(ptr %20, ptr @9)
  %22 = call ptr @strcat(ptr %20, ptr @10)
  %content_val = load ptr, ptr %content, align 8
  %23 = call i64 @strlen(ptr %20)
  %24 = call i64 @strlen(ptr %content_val)
  %25 = add i64 %23, %24
  %26 = add i64 %25, 1
  %27 = call ptr @malloc(i64 %26)
  %28 = call ptr @strcpy(ptr %27, ptr %20)
  %29 = call ptr @strcat(ptr %27, ptr %content_val)
  %30 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.2, ptr %27)
  %31 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @11)
  %32 = call ptr @fdopen(i32 1, ptr @12)
  %33 = call i64 @strlen(ptr @13)
  %34 = call i64 @fwrite(ptr @13, i64 1, i64 %33, ptr %32)
  %35 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.3, ptr @14)
  %36 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @15)
  %t1 = alloca i64, align 8
  store i64 0, ptr %t1, align 8
  %37 = call i64 @time_now_ms()
  store i64 %37, ptr %t1, align 8
  %38 = call i64 @strlen(ptr @16)
  %39 = call i64 @strlen(ptr @17)
  %40 = add i64 %38, %39
  %41 = add i64 %40, 1
  %42 = call ptr @malloc(i64 %41)
  %43 = call ptr @strcpy(ptr %42, ptr @16)
  %44 = call ptr @strcat(ptr %42, ptr @17)
  %t1_val = load i64, ptr %t1, align 8
  %45 = call ptr @malloc(i64 24)
  %46 = call i32 (ptr, ptr, ...) @sprintf(ptr %45, ptr @18, i64 %t1_val)
  %47 = call i64 @strlen(ptr %42)
  %48 = call i64 @strlen(ptr %45)
  %49 = add i64 %47, %48
  %50 = add i64 %49, 1
  %51 = call ptr @malloc(i64 %50)
  %52 = call ptr @strcpy(ptr %51, ptr %42)
  %53 = call ptr @strcat(ptr %51, ptr %45)
  %54 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.4, ptr %51)
  %55 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @19)
  call void @time_sleep_ms(i32 500)
  %t2 = alloca i64, align 8
  store i64 0, ptr %t2, align 8
  %56 = call i64 @time_now_ms()
  store i64 %56, ptr %t2, align 8
  %57 = call i64 @strlen(ptr @20)
  %58 = call i64 @strlen(ptr @21)
  %59 = add i64 %57, %58
  %60 = add i64 %59, 1
  %61 = call ptr @malloc(i64 %60)
  %62 = call ptr @strcpy(ptr %61, ptr @20)
  %63 = call ptr @strcat(ptr %61, ptr @21)
  %t2_val = load i64, ptr %t2, align 8
  %64 = call ptr @malloc(i64 24)
  %65 = call i32 (ptr, ptr, ...) @sprintf(ptr %64, ptr @22, i64 %t2_val)
  %66 = call i64 @strlen(ptr %61)
  %67 = call i64 @strlen(ptr %64)
  %68 = add i64 %66, %67
  %69 = add i64 %68, 1
  %70 = call ptr @malloc(i64 %69)
  %71 = call ptr @strcpy(ptr %70, ptr %61)
  %72 = call ptr @strcat(ptr %70, ptr %64)
  %73 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.5, ptr %70)
  %74 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @23)
  %diff = alloca i64, align 8
  store i64 0, ptr %diff, align 8
  %75 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.6, ptr @24)
  %76 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @25)
  %arg0 = alloca ptr, align 8
  store ptr null, ptr %arg0, align 8
  %77 = load ptr, ptr @__argv, align 8
  %78 = load i32, ptr @__argc, align 4
  %79 = call i32 (ptr, ...) @printf(ptr @26, i32 %78, ptr %77, i32 0)
  %80 = icmp eq ptr %77, null
  %81 = icmp slt i32 0, %78
  %82 = xor i1 %80, true
  %83 = and i1 %82, %81
  br i1 %83, label %arg_safe, label %arg_error

arg_safe:                                         ; preds = %entry
  %84 = getelementptr inbounds ptr, ptr %77, i32 0
  %85 = load ptr, ptr %84, align 8
  %86 = call i32 (ptr, ...) @printf(ptr @27, ptr %85)
  br label %arg_merge

arg_error:                                        ; preds = %entry
  %87 = call i32 (ptr, ...) @printf(ptr @29)
  br label %arg_merge

arg_merge:                                        ; preds = %arg_error, %arg_safe
  %88 = phi ptr [ %85, %arg_safe ], [ @28, %arg_error ]
  store ptr %88, ptr %arg0, align 8
  %89 = call i64 @strlen(ptr @30)
  %90 = call i64 @strlen(ptr @31)
  %91 = add i64 %89, %90
  %92 = add i64 %91, 1
  %93 = call ptr @malloc(i64 %92)
  %94 = call ptr @strcpy(ptr %93, ptr @30)
  %95 = call ptr @strcat(ptr %93, ptr @31)
  %arg0_val = load ptr, ptr %arg0, align 8
  %96 = call i64 @strlen(ptr %93)
  %97 = call i64 @strlen(ptr %arg0_val)
  %98 = add i64 %96, %97
  %99 = add i64 %98, 1
  %100 = call ptr @malloc(i64 %99)
  %101 = call ptr @strcpy(ptr %100, ptr %93)
  %102 = call ptr @strcat(ptr %100, ptr %arg0_val)
  %103 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.7, ptr %100)
  %104 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @32)
  %105 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.8, ptr @33)
  %106 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @34)
  %args = alloca %list_string, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %107 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  store ptr %107, ptr %dataPtr, align 8
  %sizePtr4 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr5 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size = load i32, ptr %sizePtr4, align 4
  %capacity = load i32, ptr %capPtr5, align 4
  %108 = icmp eq i32 %size, %capacity
  br i1 %108, label %grow, label %cont

grow:                                             ; preds = %arg_merge
  %109 = mul i32 %capacity, 2
  store i32 %109, ptr %capPtr5, align 4
  %dataPtr6 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %110 = load ptr, ptr %dataPtr6, align 8
  %111 = zext i32 %109 to i64
  %112 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %111
  %113 = call ptr @realloc(ptr %110, i64 %112)
  store ptr %113, ptr %dataPtr6, align 8
  br label %cont

cont:                                             ; preds = %grow, %arg_merge
  %dataPtr7 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr7, align 8
  %114 = getelementptr inbounds ptr, ptr %dataBase, i32 %size
  store ptr @35, ptr %114, align 8
  %115 = add i32 %size, 1
  store i32 %115, ptr %sizePtr4, align 4
  %sizePtr8 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr9 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size10 = load i32, ptr %sizePtr8, align 4
  %capacity11 = load i32, ptr %capPtr9, align 4
  %116 = icmp eq i32 %size10, %capacity11
  br i1 %116, label %grow12, label %cont13

grow12:                                           ; preds = %cont
  %117 = mul i32 %capacity11, 2
  store i32 %117, ptr %capPtr9, align 4
  %dataPtr14 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %118 = load ptr, ptr %dataPtr14, align 8
  %119 = zext i32 %117 to i64
  %120 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %119
  %121 = call ptr @realloc(ptr %118, i64 %120)
  store ptr %121, ptr %dataPtr14, align 8
  br label %cont13

cont13:                                           ; preds = %grow12, %cont
  %dataPtr15 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase16 = load ptr, ptr %dataPtr15, align 8
  %122 = getelementptr inbounds ptr, ptr %dataBase16, i32 %size10
  store ptr @36, ptr %122, align 8
  %123 = add i32 %size10, 1
  store i32 %123, ptr %sizePtr8, align 4
  %sizePtr17 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr18 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size19 = load i32, ptr %sizePtr17, align 4
  %capacity20 = load i32, ptr %capPtr18, align 4
  %124 = icmp eq i32 %size19, %capacity20
  br i1 %124, label %grow21, label %cont22

grow21:                                           ; preds = %cont13
  %125 = mul i32 %capacity20, 2
  store i32 %125, ptr %capPtr18, align 4
  %dataPtr23 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %126 = load ptr, ptr %dataPtr23, align 8
  %127 = zext i32 %125 to i64
  %128 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %127
  %129 = call ptr @realloc(ptr %126, i64 %128)
  store ptr %129, ptr %dataPtr23, align 8
  br label %cont22

cont22:                                           ; preds = %grow21, %cont13
  %dataPtr24 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase25 = load ptr, ptr %dataPtr24, align 8
  %130 = getelementptr inbounds ptr, ptr %dataBase25, i32 %size19
  store ptr @37, ptr %130, align 8
  %131 = add i32 %size19, 1
  store i32 %131, ptr %sizePtr17, align 4
  %proc = alloca ptr, align 8
  store ptr null, ptr %proc, align 8
  %132 = call ptr @sys_exe_runtime(ptr %args, ptr null, ptr null, i1 false, i32 -1, i1 false)
  store ptr %132, ptr %proc, align 8
  %133 = load ptr, ptr %proc, align 8
  %134 = call i32 @process_wait(ptr %133)
  %135 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.9, ptr @38)
  %136 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @39)
  ret i32 0
}

declare i32 @__gxx_personality_v0(...)

declare i32 @dprintf(i32, ptr, ...)

declare ptr @fopen(ptr, ptr)

declare i64 @strlen(ptr)

declare i64 @fwrite(ptr, i64, i64, ptr)

declare i32 @fclose(ptr)

declare i32 @fseek(ptr, i64, i32)

declare i64 @ftell(ptr)

declare void @rewind(ptr)

declare ptr @malloc(i64)

declare i64 @fread(ptr, i64, i64, ptr)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare ptr @fdopen(i32, ptr)

declare i64 @time_now_ms()

declare i32 @sprintf(ptr, ptr, ...)

declare void @time_sleep_ms(i32)

declare i32 @printf(ptr, ...)

declare ptr @realloc(ptr, i64)

declare ptr @sys_exe_runtime(ptr, ptr, ptr, i1, i32, i1)

declare i32 @process_wait(ptr)

declare void @process_kill(ptr)
