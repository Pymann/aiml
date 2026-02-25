; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%list_string = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }
%map_string_string = type { i32, i32, ptr, ptr, ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [24 x i8] c"Running System Tests...\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [17 x i8] c"--- File I/O ---\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@4 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@5 = private unnamed_addr constant [2 x i8] c"w\00", align 1
@6 = private unnamed_addr constant [18 x i8] c"Hello AIML World\0A\00", align 1
@7 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@8 = private unnamed_addr constant [2 x i8] c"r\00", align 1
@9 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@10 = private unnamed_addr constant [15 x i8] c"Read content: \00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [2 x i8] c"w\00", align 1
@13 = private unnamed_addr constant [24 x i8] c"Direct write to stdout\0A\00", align 1
@14 = private unnamed_addr constant [13 x i8] c"--- Time ---\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [13 x i8] c"Start time: \00", align 1
@fmt_ld = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [11 x i8] c"End time: \00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [15 x i8] c"--- System ---\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@22 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@23 = private unnamed_addr constant [8 x i8] c"Arg 0: \00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@25 = private unnamed_addr constant [16 x i8] c"--- Process ---\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@27 = private unnamed_addr constant [3 x i8] c"ls\00", align 1
@28 = private unnamed_addr constant [3 x i8] c"-l\00", align 1
@29 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@30 = private unnamed_addr constant [19 x i8] c"--- Env & Size ---\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@31 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@32 = private unnamed_addr constant [31 x i8] c"sys.env evaluated successfully\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@33 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@34 = private unnamed_addr constant [4 x i8] c"i32\00", align 1
@35 = private unnamed_addr constant [17 x i8] c"sys.size(i32) = \00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@36 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@37 = private unnamed_addr constant [7 x i8] c"string\00", align 1
@38 = private unnamed_addr constant [20 x i8] c"sys.size(string) = \00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@40 = private unnamed_addr constant [19 x i8] c"System Tests Done.\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() {
entry:
  ret void

entry1:                                           ; No predecessors!
}

define i32 @main(i32 %0, ptr %1) personality ptr @__gxx_personality_v0 {
entry:
  %argc = alloca i32, align 4
  store i32 %0, ptr %argc, align 4
  %argv = alloca ptr, align 8
  store ptr %1, ptr %argv, align 8
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr @0)
  %print_call1 = call i32 (ptr, ...) @printf(ptr @3, ptr @2)
  %f = alloca ptr, align 8
  store ptr null, ptr %f, align 8
  %2 = call ptr @fopen(ptr @4, ptr @5)
  store ptr %2, ptr %f, align 8
  %f_val = load ptr, ptr %f, align 8
  %3 = icmp eq ptr %f_val, null
  br i1 %3, label %invalid_write_handle, label %valid_write_handle

valid_write_handle:                               ; preds = %entry
  %4 = call i64 @strlen(ptr @6)
  %5 = call i64 @fwrite(ptr @6, i64 1, i64 %4, ptr %f_val)
  br label %merge_write

invalid_write_handle:                             ; preds = %entry
  br label %merge_write

merge_write:                                      ; preds = %valid_write_handle, %invalid_write_handle
  %write_res = phi i64 [ 0, %invalid_write_handle ], [ %5, %valid_write_handle ]
  %6 = trunc i64 %write_res to i32
  %f_val2 = load ptr, ptr %f, align 8
  %7 = call i32 @fclose(ptr %f_val2)
  %8 = call ptr @fopen(ptr @7, ptr @8)
  store ptr %8, ptr %f, align 8
  %content = alloca ptr, align 8
  store ptr null, ptr %content, align 8
  %f_val3 = load ptr, ptr %f, align 8
  %9 = icmp eq ptr %f_val3, null
  br i1 %9, label %invalid_handle, label %valid_handle

valid_handle:                                     ; preds = %merge_write
  %10 = call i32 @fseek(ptr %f_val3, i64 0, i32 2)
  %11 = call i64 @ftell(ptr %f_val3)
  call void @rewind(ptr %f_val3)
  %12 = icmp sgt i64 %11, 0
  br i1 %12, label %do_read_all, label %invalid_handle

invalid_handle:                                   ; preds = %valid_handle, %merge_write
  br label %merge_read_all

merge_read_all:                                   ; preds = %do_read_all, %invalid_handle
  %read_all_res = phi ptr [ @9, %invalid_handle ], [ %18, %do_read_all ]
  store ptr %read_all_res, ptr %content, align 8
  %f_val4 = load ptr, ptr %f, align 8
  %13 = call i32 @fclose(ptr %f_val4)
  %content_val = load ptr, ptr %content, align 8
  %14 = call ptr @__aiml_str_concat(ptr @10, ptr %content_val)
  %print_null_check = icmp eq ptr %14, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.2, ptr %14
  %print_call5 = call i32 (ptr, ...) @printf(ptr @11, ptr %print_safe_str)
  %15 = call ptr @fdopen(i32 1, ptr @12)
  %16 = icmp eq ptr %15, null
  br i1 %16, label %invalid_write_handle7, label %valid_write_handle6

do_read_all:                                      ; preds = %valid_handle
  %17 = add i64 %11, 1
  %18 = call ptr @malloc(i64 %17)
  %19 = call i64 @fread(ptr %18, i64 1, i64 %11, ptr %f_val3)
  %20 = getelementptr inbounds i8, ptr %18, i64 %19
  store i8 0, ptr %20, align 1
  br label %merge_read_all

valid_write_handle6:                              ; preds = %merge_read_all
  %21 = call i64 @strlen(ptr @13)
  %22 = call i64 @fwrite(ptr @13, i64 1, i64 %21, ptr %15)
  br label %merge_write8

invalid_write_handle7:                            ; preds = %merge_read_all
  br label %merge_write8

merge_write8:                                     ; preds = %valid_write_handle6, %invalid_write_handle7
  %write_res9 = phi i64 [ 0, %invalid_write_handle7 ], [ %22, %valid_write_handle6 ]
  %23 = trunc i64 %write_res9 to i32
  %print_call10 = call i32 (ptr, ...) @printf(ptr @15, ptr @14)
  %t1 = alloca i64, align 8
  store i64 0, ptr %t1, align 8
  %24 = call i64 @time_now_ms()
  store i64 %24, ptr %t1, align 8
  %t1_val = load i64, ptr %t1, align 8
  %25 = call ptr @__aiml_int64_to_string(i64 %t1_val)
  %26 = call ptr @__aiml_str_concat(ptr @16, ptr %25)
  %print_null_check11 = icmp eq ptr %26, null
  %print_safe_str12 = select i1 %print_null_check11, ptr @print_null_str.4, ptr %26
  %print_call13 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str12)
  call void @time_sleep_ms(i32 500)
  %t2 = alloca i64, align 8
  store i64 0, ptr %t2, align 8
  %27 = call i64 @time_now_ms()
  store i64 %27, ptr %t2, align 8
  %t2_val = load i64, ptr %t2, align 8
  %28 = call ptr @__aiml_int64_to_string(i64 %t2_val)
  %29 = call ptr @__aiml_str_concat(ptr @18, ptr %28)
  %print_null_check14 = icmp eq ptr %29, null
  %print_safe_str15 = select i1 %print_null_check14, ptr @print_null_str.5, ptr %29
  %print_call16 = call i32 (ptr, ...) @printf(ptr @19, ptr %print_safe_str15)
  %diff = alloca i64, align 8
  store i64 0, ptr %diff, align 8
  %print_call17 = call i32 (ptr, ...) @printf(ptr @21, ptr @20)
  %arg0 = alloca ptr, align 8
  store ptr null, ptr %arg0, align 8
  %30 = call ptr @__aiml_sys_get_arg(i32 0)
  store ptr %30, ptr %arg0, align 8
  %arg0_val = load ptr, ptr %arg0, align 8
  %31 = call ptr @__aiml_str_concat(ptr @23, ptr %arg0_val)
  %print_null_check18 = icmp eq ptr %31, null
  %print_safe_str19 = select i1 %print_null_check18, ptr @print_null_str.7, ptr %31
  %print_call20 = call i32 (ptr, ...) @printf(ptr @24, ptr %print_safe_str19)
  %print_call21 = call i32 (ptr, ...) @printf(ptr @26, ptr @25)
  %args = alloca %list_string, align 8
  store %list_string zeroinitializer, ptr %args, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %32 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  store ptr %32, ptr %dataPtr, align 8
  %sizePtr22 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr23 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size = load i32, ptr %sizePtr22, align 4
  %capacity = load i32, ptr %capPtr23, align 4
  %33 = icmp eq i32 %size, %capacity
  br i1 %33, label %grow, label %cont

grow:                                             ; preds = %merge_write8
  %34 = mul i32 %capacity, 2
  store i32 %34, ptr %capPtr23, align 4
  %dataPtr24 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %35 = load ptr, ptr %dataPtr24, align 8
  %36 = zext i32 %34 to i64
  %37 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %36
  %38 = call ptr @realloc(ptr %35, i64 %37)
  store ptr %38, ptr %dataPtr24, align 8
  br label %cont

cont:                                             ; preds = %grow, %merge_write8
  %dataPtr25 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr25, align 8
  %39 = getelementptr inbounds ptr, ptr %dataBase, i32 %size
  store ptr @27, ptr %39, align 8
  %40 = add i32 %size, 1
  store i32 %40, ptr %sizePtr22, align 4
  %sizePtr26 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr27 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size28 = load i32, ptr %sizePtr26, align 4
  %capacity29 = load i32, ptr %capPtr27, align 4
  %41 = icmp eq i32 %size28, %capacity29
  br i1 %41, label %grow30, label %cont31

grow30:                                           ; preds = %cont
  %42 = mul i32 %capacity29, 2
  store i32 %42, ptr %capPtr27, align 4
  %dataPtr32 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %43 = load ptr, ptr %dataPtr32, align 8
  %44 = zext i32 %42 to i64
  %45 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %44
  %46 = call ptr @realloc(ptr %43, i64 %45)
  store ptr %46, ptr %dataPtr32, align 8
  br label %cont31

cont31:                                           ; preds = %grow30, %cont
  %dataPtr33 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase34 = load ptr, ptr %dataPtr33, align 8
  %47 = getelementptr inbounds ptr, ptr %dataBase34, i32 %size28
  store ptr @28, ptr %47, align 8
  %48 = add i32 %size28, 1
  store i32 %48, ptr %sizePtr26, align 4
  %sizePtr35 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr36 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size37 = load i32, ptr %sizePtr35, align 4
  %capacity38 = load i32, ptr %capPtr36, align 4
  %49 = icmp eq i32 %size37, %capacity38
  br i1 %49, label %grow39, label %cont40

grow39:                                           ; preds = %cont31
  %50 = mul i32 %capacity38, 2
  store i32 %50, ptr %capPtr36, align 4
  %dataPtr41 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %51 = load ptr, ptr %dataPtr41, align 8
  %52 = zext i32 %50 to i64
  %53 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %52
  %54 = call ptr @realloc(ptr %51, i64 %53)
  store ptr %54, ptr %dataPtr41, align 8
  br label %cont40

cont40:                                           ; preds = %grow39, %cont31
  %dataPtr42 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase43 = load ptr, ptr %dataPtr42, align 8
  %55 = getelementptr inbounds ptr, ptr %dataBase43, i32 %size37
  store ptr @29, ptr %55, align 8
  %56 = add i32 %size37, 1
  store i32 %56, ptr %sizePtr35, align 4
  %proc = alloca ptr, align 8
  store ptr null, ptr %proc, align 8
  %args_val = load %list_string, ptr %args, align 8
  %57 = alloca %list_string, align 8
  store %list_string %args_val, ptr %57, align 8
  %58 = call ptr @sys_exe_runtime(ptr %57, ptr null, ptr null, i1 false, i32 -1, i1 false)
  store ptr %58, ptr %proc, align 8
  %59 = load ptr, ptr %proc, align 8
  %60 = call i32 @process_wait(ptr %59)
  %print_call44 = call i32 (ptr, ...) @printf(ptr @31, ptr @30)
  %env_map = alloca %map_string_string, align 8
  store %map_string_string zeroinitializer, ptr %env_map, align 8
  %sizePtr45 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 0
  store i32 0, ptr %sizePtr45, align 4
  %capPtr46 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 1
  store i32 4, ptr %capPtr46, align 4
  %61 = call ptr @calloc(i64 4, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 2
  store ptr %61, ptr %occPtr, align 8
  %62 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 3
  store ptr %62, ptr %keysPtr, align 8
  %63 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 4
  store ptr %63, ptr %valsPtr, align 8
  %64 = call ptr @calloc(i64 1, i64 32)
  %sizePtr47 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %64, i32 0, i32 0
  store i32 0, ptr %sizePtr47, align 4
  %capPtr48 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %64, i32 0, i32 1
  store i32 4, ptr %capPtr48, align 4
  %65 = call ptr @calloc(i64 4, i64 1)
  %occPtr49 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %64, i32 0, i32 2
  store ptr %65, ptr %occPtr49, align 8
  %66 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %keysPtr50 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %64, i32 0, i32 3
  store ptr %66, ptr %keysPtr50, align 8
  %67 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %valsPtr51 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %64, i32 0, i32 4
  store ptr %67, ptr %valsPtr51, align 8
  store ptr %64, ptr %env_map, align 8
  %print_call52 = call i32 (ptr, ...) @printf(ptr @33, ptr @32)
  %size_i32 = alloca i64, align 8
  store i64 0, ptr %size_i32, align 8
  store i64 8, ptr %size_i32, align 8
  %size_i32_val = load i64, ptr %size_i32, align 8
  %68 = call ptr @__aiml_int64_to_string(i64 %size_i32_val)
  %69 = call ptr @__aiml_str_concat(ptr @35, ptr %68)
  %print_null_check53 = icmp eq ptr %69, null
  %print_safe_str54 = select i1 %print_null_check53, ptr @print_null_str.11, ptr %69
  %print_call55 = call i32 (ptr, ...) @printf(ptr @36, ptr %print_safe_str54)
  %size_str = alloca i64, align 8
  store i64 0, ptr %size_str, align 8
  store i64 8, ptr %size_str, align 8
  %size_str_val = load i64, ptr %size_str, align 8
  %70 = call ptr @__aiml_int64_to_string(i64 %size_str_val)
  %71 = call ptr @__aiml_str_concat(ptr @38, ptr %70)
  %print_null_check56 = icmp eq ptr %71, null
  %print_safe_str57 = select i1 %print_null_check56, ptr @print_null_str.12, ptr %71
  %print_call58 = call i32 (ptr, ...) @printf(ptr @39, ptr %print_safe_str57)
  %print_call59 = call i32 (ptr, ...) @printf(ptr @41, ptr @40)
  ret i32 0
}

declare i32 @__gxx_personality_v0(...)

declare i32 @printf(ptr, ...)

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

declare ptr @fdopen(i32, ptr)

declare i64 @time_now_ms()

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int64_to_string(i64 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 24)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_ld, i64 %0)
  ret ptr %1
}

declare void @time_sleep_ms(i32)

; Function Attrs: noinline
define internal ptr @__aiml_sys_get_arg(i32 %0) #0 {
entry:
  %1 = load ptr, ptr @__argv, align 8
  %2 = load i32, ptr @__argc, align 4
  %3 = icmp eq ptr %1, null
  %4 = icmp slt i32 %0, %2
  %5 = xor i1 %3, true
  %6 = and i1 %5, %4
  br i1 %6, label %arg_safe, label %arg_error

arg_safe:                                         ; preds = %entry
  %7 = getelementptr inbounds ptr, ptr %1, i32 %0
  %8 = load ptr, ptr %7, align 8
  br label %arg_merge

arg_error:                                        ; preds = %entry
  br label %arg_merge

arg_merge:                                        ; preds = %arg_error, %arg_safe
  %9 = phi ptr [ %8, %arg_safe ], [ @22, %arg_error ]
  ret ptr %9
}

declare ptr @realloc(ptr, i64)

declare ptr @sys_exe_runtime(ptr, ptr, ptr, i1, i32, i1)

declare i32 @process_wait(ptr)

declare ptr @calloc(i64, i64)

attributes #0 = { noinline }
