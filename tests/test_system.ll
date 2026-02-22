; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%list_string = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }
%map_string_string = type { i32, i32, ptr, ptr, ptr }

@__argc = internal global i32 0
@__argv = internal global ptr null
@0 = private unnamed_addr constant [24 x i8] c"Running System Tests...\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [17 x i8] c"--- File I/O ---\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@7 = private unnamed_addr constant [2 x i8] c"w\00", align 1
@8 = private unnamed_addr constant [18 x i8] c"Hello AIML World\0A\00", align 1
@9 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@10 = private unnamed_addr constant [2 x i8] c"r\00", align 1
@11 = private unnamed_addr constant [15 x i8] c"Read content: \00", align 1
@12 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [2 x i8] c"w\00", align 1
@15 = private unnamed_addr constant [24 x i8] c"Direct write to stdout\0A\00", align 1
@16 = private unnamed_addr constant [13 x i8] c"--- Time ---\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [13 x i8] c"Start time: \00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@23 = private unnamed_addr constant [11 x i8] c"End time: \00", align 1
@24 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@27 = private unnamed_addr constant [15 x i8] c"--- System ---\00", align 1
@28 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@30 = private unnamed_addr constant [8 x i8] c"Arg 0: \00", align 1
@31 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@33 = private unnamed_addr constant [16 x i8] c"--- Process ---\00", align 1
@34 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@36 = private unnamed_addr constant [3 x i8] c"ls\00", align 1
@37 = private unnamed_addr constant [3 x i8] c"-l\00", align 1
@38 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@39 = private unnamed_addr constant [19 x i8] c"--- Env & Size ---\00", align 1
@40 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [31 x i8] c"sys.env evaluated successfully\00", align 1
@43 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@44 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@45 = private unnamed_addr constant [4 x i8] c"i32\00", align 1
@46 = private unnamed_addr constant [17 x i8] c"sys.size(i32) = \00", align 1
@47 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@48 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@49 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@50 = private unnamed_addr constant [7 x i8] c"string\00", align 1
@51 = private unnamed_addr constant [20 x i8] c"sys.size(string) = \00", align 1
@52 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@53 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@54 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@55 = private unnamed_addr constant [19 x i8] c"System Tests Done.\00", align 1
@56 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@57 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@58 = private unnamed_addr constant [24 x i8] c"Running System Tests...\00", align 1
@59 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@60 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@61 = private unnamed_addr constant [17 x i8] c"--- File I/O ---\00", align 1
@62 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@63 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@64 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@65 = private unnamed_addr constant [2 x i8] c"w\00", align 1
@66 = private unnamed_addr constant [18 x i8] c"Hello AIML World\0A\00", align 1
@67 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@68 = private unnamed_addr constant [2 x i8] c"r\00", align 1
@69 = private unnamed_addr constant [15 x i8] c"Read content: \00", align 1
@70 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@71 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@72 = private unnamed_addr constant [2 x i8] c"w\00", align 1
@73 = private unnamed_addr constant [24 x i8] c"Direct write to stdout\0A\00", align 1
@74 = private unnamed_addr constant [13 x i8] c"--- Time ---\00", align 1
@75 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@76 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@77 = private unnamed_addr constant [13 x i8] c"Start time: \00", align 1
@78 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@79 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@80 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@81 = private unnamed_addr constant [11 x i8] c"End time: \00", align 1
@82 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@83 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@84 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@85 = private unnamed_addr constant [15 x i8] c"--- System ---\00", align 1
@86 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@87 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@88 = private unnamed_addr constant [8 x i8] c"Arg 0: \00", align 1
@89 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@90 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@91 = private unnamed_addr constant [16 x i8] c"--- Process ---\00", align 1
@92 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@93 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@94 = private unnamed_addr constant [3 x i8] c"ls\00", align 1
@95 = private unnamed_addr constant [3 x i8] c"-l\00", align 1
@96 = private unnamed_addr constant [12 x i8] c"test_io.txt\00", align 1
@97 = private unnamed_addr constant [19 x i8] c"--- Env & Size ---\00", align 1
@98 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@99 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@100 = private unnamed_addr constant [31 x i8] c"sys.env evaluated successfully\00", align 1
@101 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@102 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@103 = private unnamed_addr constant [4 x i8] c"i32\00", align 1
@104 = private unnamed_addr constant [17 x i8] c"sys.size(i32) = \00", align 1
@105 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@106 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@107 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@108 = private unnamed_addr constant [7 x i8] c"string\00", align 1
@109 = private unnamed_addr constant [20 x i8] c"sys.size(string) = \00", align 1
@110 = private unnamed_addr constant [4 x i8] c"%ld\00", align 1
@111 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@112 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@113 = private unnamed_addr constant [19 x i8] c"System Tests Done.\00", align 1
@114 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@115 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() {
entry:
  ret void

entry1:                                           ; No predecessors!
}

define i32 @__aiml_user_main(i32 %0, ptr %1) personality ptr @__gxx_personality_v0 {
entry:
  %argc = alloca i32, align 4
  store i32 %0, ptr %argc, align 4
  %argv = alloca ptr, align 8
  store ptr %1, ptr %argv, align 8
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr @0)
  %print_call1 = call i32 (ptr, ...) @printf(ptr @5, ptr @3)
  %f = alloca ptr, align 8
  store ptr null, ptr %f, align 8
  %2 = call ptr @fopen(ptr @6, ptr @7)
  store ptr %2, ptr %f, align 8
  %f_val = load ptr, ptr %f, align 8
  %3 = icmp eq ptr %f_val, null
  br i1 %3, label %invalid_write_handle, label %valid_write_handle

valid_write_handle:                               ; preds = %entry
  %4 = call i64 @strlen(ptr @8)
  %5 = call i64 @fwrite(ptr @8, i64 1, i64 %4, ptr %f_val)
  br label %merge_write

invalid_write_handle:                             ; preds = %entry
  br label %merge_write

merge_write:                                      ; preds = %valid_write_handle, %invalid_write_handle
  %write_res = phi i64 [ 0, %invalid_write_handle ], [ %5, %valid_write_handle ]
  %6 = trunc i64 %write_res to i32
  %f_val2 = load ptr, ptr %f, align 8
  %7 = call i32 @fclose(ptr %f_val2)
  %8 = call ptr @fopen(ptr @9, ptr @10)
  store ptr %8, ptr %f, align 8
  %content = alloca ptr, align 8
  store ptr null, ptr %content, align 8
  %f_val3 = load ptr, ptr %f, align 8
  store i32 0, ptr %content, align 4
  %f_val4 = load ptr, ptr %f, align 8
  %9 = call i32 @fclose(ptr %f_val4)
  %content_val = load ptr, ptr %content, align 8
  %10 = call i64 @strlen(ptr @11)
  %11 = call i64 @strlen(ptr %content_val)
  %12 = add i64 %10, %11
  %13 = add i64 %12, 1
  %14 = call ptr @malloc(i64 %13)
  %15 = call ptr @strcpy(ptr %14, ptr @11)
  %16 = call ptr @strcat(ptr %14, ptr %content_val)
  %print_call5 = call i32 (ptr, ...) @printf(ptr @13, ptr %14)
  %17 = call ptr @fdopen(i32 1, ptr @14)
  %18 = icmp eq ptr %17, null
  br i1 %18, label %invalid_write_handle7, label %valid_write_handle6

valid_write_handle6:                              ; preds = %merge_write
  %19 = call i64 @strlen(ptr @15)
  %20 = call i64 @fwrite(ptr @15, i64 1, i64 %19, ptr %17)
  br label %merge_write8

invalid_write_handle7:                            ; preds = %merge_write
  br label %merge_write8

merge_write8:                                     ; preds = %valid_write_handle6, %invalid_write_handle7
  %write_res9 = phi i64 [ 0, %invalid_write_handle7 ], [ %20, %valid_write_handle6 ]
  %21 = trunc i64 %write_res9 to i32
  %print_call10 = call i32 (ptr, ...) @printf(ptr @18, ptr @16)
  %t1 = alloca i64, align 8
  store i64 0, ptr %t1, align 8
  %22 = call i64 @time_now_ms()
  store i64 %22, ptr %t1, align 8
  %t1_val = load i64, ptr %t1, align 8
  %23 = call ptr @malloc(i64 24)
  %24 = call i32 (ptr, ptr, ...) @sprintf(ptr %23, ptr @20, i64 %t1_val)
  %25 = call i64 @strlen(ptr @19)
  %26 = call i64 @strlen(ptr %23)
  %27 = add i64 %25, %26
  %28 = add i64 %27, 1
  %29 = call ptr @malloc(i64 %28)
  %30 = call ptr @strcpy(ptr %29, ptr @19)
  %31 = call ptr @strcat(ptr %29, ptr %23)
  %print_call11 = call i32 (ptr, ...) @printf(ptr @22, ptr %29)
  call void @time_sleep_ms(i32 500)
  %t2 = alloca i64, align 8
  store i64 0, ptr %t2, align 8
  %32 = call i64 @time_now_ms()
  store i64 %32, ptr %t2, align 8
  %t2_val = load i64, ptr %t2, align 8
  %33 = call ptr @malloc(i64 24)
  %34 = call i32 (ptr, ptr, ...) @sprintf(ptr %33, ptr @24, i64 %t2_val)
  %35 = call i64 @strlen(ptr @23)
  %36 = call i64 @strlen(ptr %33)
  %37 = add i64 %35, %36
  %38 = add i64 %37, 1
  %39 = call ptr @malloc(i64 %38)
  %40 = call ptr @strcpy(ptr %39, ptr @23)
  %41 = call ptr @strcat(ptr %39, ptr %33)
  %print_call12 = call i32 (ptr, ...) @printf(ptr @26, ptr %39)
  %diff = alloca i64, align 8
  store i64 0, ptr %diff, align 8
  %print_call13 = call i32 (ptr, ...) @printf(ptr @29, ptr @27)
  %arg0 = alloca ptr, align 8
  store ptr null, ptr %arg0, align 8
  store i32 0, ptr %arg0, align 4
  %arg0_val = load ptr, ptr %arg0, align 8
  %42 = call i64 @strlen(ptr @30)
  %43 = call i64 @strlen(ptr %arg0_val)
  %44 = add i64 %42, %43
  %45 = add i64 %44, 1
  %46 = call ptr @malloc(i64 %45)
  %47 = call ptr @strcpy(ptr %46, ptr @30)
  %48 = call ptr @strcat(ptr %46, ptr %arg0_val)
  %print_call14 = call i32 (ptr, ...) @printf(ptr @32, ptr %46)
  %print_call15 = call i32 (ptr, ...) @printf(ptr @35, ptr @33)
  %args = alloca %list_string, align 8
  store %list_string zeroinitializer, ptr %args, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %49 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  store ptr %49, ptr %dataPtr, align 8
  %sizePtr16 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr17 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size = load i32, ptr %sizePtr16, align 4
  %capacity = load i32, ptr %capPtr17, align 4
  %50 = icmp eq i32 %size, %capacity
  br i1 %50, label %grow, label %cont

grow:                                             ; preds = %merge_write8
  %51 = mul i32 %capacity, 2
  store i32 %51, ptr %capPtr17, align 4
  %dataPtr18 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %52 = load ptr, ptr %dataPtr18, align 8
  %53 = zext i32 %51 to i64
  %54 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %53
  %55 = call ptr @realloc(ptr %52, i64 %54)
  store ptr %55, ptr %dataPtr18, align 8
  br label %cont

cont:                                             ; preds = %grow, %merge_write8
  %dataPtr19 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr19, align 8
  %56 = getelementptr inbounds ptr, ptr %dataBase, i32 %size
  store ptr @36, ptr %56, align 8
  %57 = add i32 %size, 1
  store i32 %57, ptr %sizePtr16, align 4
  %sizePtr20 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr21 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size22 = load i32, ptr %sizePtr20, align 4
  %capacity23 = load i32, ptr %capPtr21, align 4
  %58 = icmp eq i32 %size22, %capacity23
  br i1 %58, label %grow24, label %cont25

grow24:                                           ; preds = %cont
  %59 = mul i32 %capacity23, 2
  store i32 %59, ptr %capPtr21, align 4
  %dataPtr26 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %60 = load ptr, ptr %dataPtr26, align 8
  %61 = zext i32 %59 to i64
  %62 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %61
  %63 = call ptr @realloc(ptr %60, i64 %62)
  store ptr %63, ptr %dataPtr26, align 8
  br label %cont25

cont25:                                           ; preds = %grow24, %cont
  %dataPtr27 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase28 = load ptr, ptr %dataPtr27, align 8
  %64 = getelementptr inbounds ptr, ptr %dataBase28, i32 %size22
  store ptr @37, ptr %64, align 8
  %65 = add i32 %size22, 1
  store i32 %65, ptr %sizePtr20, align 4
  %sizePtr29 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr30 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size31 = load i32, ptr %sizePtr29, align 4
  %capacity32 = load i32, ptr %capPtr30, align 4
  %66 = icmp eq i32 %size31, %capacity32
  br i1 %66, label %grow33, label %cont34

grow33:                                           ; preds = %cont25
  %67 = mul i32 %capacity32, 2
  store i32 %67, ptr %capPtr30, align 4
  %dataPtr35 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %68 = load ptr, ptr %dataPtr35, align 8
  %69 = zext i32 %67 to i64
  %70 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %69
  %71 = call ptr @realloc(ptr %68, i64 %70)
  store ptr %71, ptr %dataPtr35, align 8
  br label %cont34

cont34:                                           ; preds = %grow33, %cont25
  %dataPtr36 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase37 = load ptr, ptr %dataPtr36, align 8
  %72 = getelementptr inbounds ptr, ptr %dataBase37, i32 %size31
  store ptr @38, ptr %72, align 8
  %73 = add i32 %size31, 1
  store i32 %73, ptr %sizePtr29, align 4
  %proc = alloca ptr, align 8
  store ptr null, ptr %proc, align 8
  %args_val = load %list_string, ptr %args, align 8
  store i32 0, ptr %proc, align 4
  %74 = load ptr, ptr %proc, align 8
  %75 = call i32 @process_wait(ptr %74)
  %print_call38 = call i32 (ptr, ...) @printf(ptr @41, ptr @39)
  %env_map = alloca %map_string_string, align 8
  store %map_string_string zeroinitializer, ptr %env_map, align 8
  %sizePtr39 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 0
  store i32 0, ptr %sizePtr39, align 4
  %capPtr40 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 1
  store i32 4, ptr %capPtr40, align 4
  %76 = call ptr @calloc(i64 4, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 2
  store ptr %76, ptr %occPtr, align 8
  %77 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 3
  store ptr %77, ptr %keysPtr, align 8
  %78 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 4
  store ptr %78, ptr %valsPtr, align 8
  store ptr null, ptr %env_map, align 8
  %print_call41 = call i32 (ptr, ...) @printf(ptr @44, ptr @42)
  %size_i32 = alloca i64, align 8
  store i64 0, ptr %size_i32, align 8
  store i32 0, ptr %size_i32, align 4
  %size_i32_val = load i64, ptr %size_i32, align 8
  %79 = call ptr @malloc(i64 24)
  %80 = call i32 (ptr, ptr, ...) @sprintf(ptr %79, ptr @47, i64 %size_i32_val)
  %81 = call i64 @strlen(ptr @46)
  %82 = call i64 @strlen(ptr %79)
  %83 = add i64 %81, %82
  %84 = add i64 %83, 1
  %85 = call ptr @malloc(i64 %84)
  %86 = call ptr @strcpy(ptr %85, ptr @46)
  %87 = call ptr @strcat(ptr %85, ptr %79)
  %print_call42 = call i32 (ptr, ...) @printf(ptr @49, ptr %85)
  %size_str = alloca i64, align 8
  store i64 0, ptr %size_str, align 8
  store i32 0, ptr %size_str, align 4
  %size_str_val = load i64, ptr %size_str, align 8
  %88 = call ptr @malloc(i64 24)
  %89 = call i32 (ptr, ptr, ...) @sprintf(ptr %88, ptr @52, i64 %size_str_val)
  %90 = call i64 @strlen(ptr @51)
  %91 = call i64 @strlen(ptr %88)
  %92 = add i64 %90, %91
  %93 = add i64 %92, 1
  %94 = call ptr @malloc(i64 %93)
  %95 = call ptr @strcpy(ptr %94, ptr @51)
  %96 = call ptr @strcat(ptr %94, ptr %88)
  %print_call43 = call i32 (ptr, ...) @printf(ptr @54, ptr %94)
  %print_call44 = call i32 (ptr, ...) @printf(ptr @57, ptr @55)
  ret i32 0
}

declare i32 @__gxx_personality_v0(...)

declare i32 @printf(ptr, ...)

declare ptr @fopen(ptr, ptr)

declare i64 @strlen(ptr)

declare i64 @fwrite(ptr, i64, i64, ptr)

declare i32 @fclose(ptr)

declare ptr @malloc(i64)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare ptr @fdopen(i32, ptr)

declare i64 @time_now_ms()

declare i32 @sprintf(ptr, ptr, ...)

declare void @time_sleep_ms(i32)

declare ptr @realloc(ptr, i64)

declare i32 @process_wait(ptr)

declare ptr @calloc(i64, i64)

define i32 @main(i32 %argc, ptr %argv) {
entry:
  store i32 %argc, ptr @__argc, align 4
  store ptr %argv, ptr @__argv, align 8
  call void @__aiml_init()
  %0 = call i32 @__aiml_user_main(i32 %argc, ptr %argv)
  ret i32 %0
}

define void @__aiml_init.1() {
entry:
  ret void

entry1:                                           ; No predecessors!
}

define i32 @main.2(i32 %0, ptr %1) personality ptr @__gxx_personality_v0 {
entry:
  %argc = alloca i32, align 4
  store i32 %0, ptr %argc, align 4
  %argv = alloca ptr, align 8
  store ptr %1, ptr %argv, align 8
  %print_call = call i32 (ptr, ...) @printf(ptr @60, ptr @58)
  %print_call1 = call i32 (ptr, ...) @printf(ptr @63, ptr @61)
  %f = alloca ptr, align 8
  store ptr null, ptr %f, align 8
  %2 = call ptr @fopen(ptr @64, ptr @65)
  store ptr %2, ptr %f, align 8
  %f_val = load ptr, ptr %f, align 8
  %3 = icmp eq ptr %f_val, null
  br i1 %3, label %invalid_write_handle, label %valid_write_handle

valid_write_handle:                               ; preds = %entry
  %4 = call i64 @strlen(ptr @66)
  %5 = call i64 @fwrite(ptr @66, i64 1, i64 %4, ptr %f_val)
  br label %merge_write

invalid_write_handle:                             ; preds = %entry
  br label %merge_write

merge_write:                                      ; preds = %valid_write_handle, %invalid_write_handle
  %write_res = phi i64 [ 0, %invalid_write_handle ], [ %5, %valid_write_handle ]
  %6 = trunc i64 %write_res to i32
  %f_val2 = load ptr, ptr %f, align 8
  %7 = call i32 @fclose(ptr %f_val2)
  %8 = call ptr @fopen(ptr @67, ptr @68)
  store ptr %8, ptr %f, align 8
  %content = alloca ptr, align 8
  store ptr null, ptr %content, align 8
  %f_val3 = load ptr, ptr %f, align 8
  store i32 0, ptr %content, align 4
  %f_val4 = load ptr, ptr %f, align 8
  %9 = call i32 @fclose(ptr %f_val4)
  %content_val = load ptr, ptr %content, align 8
  %10 = call i64 @strlen(ptr @69)
  %11 = call i64 @strlen(ptr %content_val)
  %12 = add i64 %10, %11
  %13 = add i64 %12, 1
  %14 = call ptr @malloc(i64 %13)
  %15 = call ptr @strcpy(ptr %14, ptr @69)
  %16 = call ptr @strcat(ptr %14, ptr %content_val)
  %print_call5 = call i32 (ptr, ...) @printf(ptr @71, ptr %14)
  %17 = call ptr @fdopen(i32 1, ptr @72)
  %18 = icmp eq ptr %17, null
  br i1 %18, label %invalid_write_handle7, label %valid_write_handle6

valid_write_handle6:                              ; preds = %merge_write
  %19 = call i64 @strlen(ptr @73)
  %20 = call i64 @fwrite(ptr @73, i64 1, i64 %19, ptr %17)
  br label %merge_write8

invalid_write_handle7:                            ; preds = %merge_write
  br label %merge_write8

merge_write8:                                     ; preds = %valid_write_handle6, %invalid_write_handle7
  %write_res9 = phi i64 [ 0, %invalid_write_handle7 ], [ %20, %valid_write_handle6 ]
  %21 = trunc i64 %write_res9 to i32
  %print_call10 = call i32 (ptr, ...) @printf(ptr @76, ptr @74)
  %t1 = alloca i64, align 8
  store i64 0, ptr %t1, align 8
  %22 = call i64 @time_now_ms()
  store i64 %22, ptr %t1, align 8
  %t1_val = load i64, ptr %t1, align 8
  %23 = call ptr @malloc(i64 24)
  %24 = call i32 (ptr, ptr, ...) @sprintf(ptr %23, ptr @78, i64 %t1_val)
  %25 = call i64 @strlen(ptr @77)
  %26 = call i64 @strlen(ptr %23)
  %27 = add i64 %25, %26
  %28 = add i64 %27, 1
  %29 = call ptr @malloc(i64 %28)
  %30 = call ptr @strcpy(ptr %29, ptr @77)
  %31 = call ptr @strcat(ptr %29, ptr %23)
  %print_call11 = call i32 (ptr, ...) @printf(ptr @80, ptr %29)
  call void @time_sleep_ms(i32 500)
  %t2 = alloca i64, align 8
  store i64 0, ptr %t2, align 8
  %32 = call i64 @time_now_ms()
  store i64 %32, ptr %t2, align 8
  %t2_val = load i64, ptr %t2, align 8
  %33 = call ptr @malloc(i64 24)
  %34 = call i32 (ptr, ptr, ...) @sprintf(ptr %33, ptr @82, i64 %t2_val)
  %35 = call i64 @strlen(ptr @81)
  %36 = call i64 @strlen(ptr %33)
  %37 = add i64 %35, %36
  %38 = add i64 %37, 1
  %39 = call ptr @malloc(i64 %38)
  %40 = call ptr @strcpy(ptr %39, ptr @81)
  %41 = call ptr @strcat(ptr %39, ptr %33)
  %print_call12 = call i32 (ptr, ...) @printf(ptr @84, ptr %39)
  %diff = alloca i64, align 8
  store i64 0, ptr %diff, align 8
  %print_call13 = call i32 (ptr, ...) @printf(ptr @87, ptr @85)
  %arg0 = alloca ptr, align 8
  store ptr null, ptr %arg0, align 8
  store i32 0, ptr %arg0, align 4
  %arg0_val = load ptr, ptr %arg0, align 8
  %42 = call i64 @strlen(ptr @88)
  %43 = call i64 @strlen(ptr %arg0_val)
  %44 = add i64 %42, %43
  %45 = add i64 %44, 1
  %46 = call ptr @malloc(i64 %45)
  %47 = call ptr @strcpy(ptr %46, ptr @88)
  %48 = call ptr @strcat(ptr %46, ptr %arg0_val)
  %print_call14 = call i32 (ptr, ...) @printf(ptr @90, ptr %46)
  %print_call15 = call i32 (ptr, ...) @printf(ptr @93, ptr @91)
  %args = alloca %list_string, align 8
  store %list_string zeroinitializer, ptr %args, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %49 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  store ptr %49, ptr %dataPtr, align 8
  %sizePtr16 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr17 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size = load i32, ptr %sizePtr16, align 4
  %capacity = load i32, ptr %capPtr17, align 4
  %50 = icmp eq i32 %size, %capacity
  br i1 %50, label %grow, label %cont

grow:                                             ; preds = %merge_write8
  %51 = mul i32 %capacity, 2
  store i32 %51, ptr %capPtr17, align 4
  %dataPtr18 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %52 = load ptr, ptr %dataPtr18, align 8
  %53 = zext i32 %51 to i64
  %54 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %53
  %55 = call ptr @realloc(ptr %52, i64 %54)
  store ptr %55, ptr %dataPtr18, align 8
  br label %cont

cont:                                             ; preds = %grow, %merge_write8
  %dataPtr19 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr19, align 8
  %56 = getelementptr inbounds ptr, ptr %dataBase, i32 %size
  store ptr @94, ptr %56, align 8
  %57 = add i32 %size, 1
  store i32 %57, ptr %sizePtr16, align 4
  %sizePtr20 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr21 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size22 = load i32, ptr %sizePtr20, align 4
  %capacity23 = load i32, ptr %capPtr21, align 4
  %58 = icmp eq i32 %size22, %capacity23
  br i1 %58, label %grow24, label %cont25

grow24:                                           ; preds = %cont
  %59 = mul i32 %capacity23, 2
  store i32 %59, ptr %capPtr21, align 4
  %dataPtr26 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %60 = load ptr, ptr %dataPtr26, align 8
  %61 = zext i32 %59 to i64
  %62 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %61
  %63 = call ptr @realloc(ptr %60, i64 %62)
  store ptr %63, ptr %dataPtr26, align 8
  br label %cont25

cont25:                                           ; preds = %grow24, %cont
  %dataPtr27 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase28 = load ptr, ptr %dataPtr27, align 8
  %64 = getelementptr inbounds ptr, ptr %dataBase28, i32 %size22
  store ptr @95, ptr %64, align 8
  %65 = add i32 %size22, 1
  store i32 %65, ptr %sizePtr20, align 4
  %sizePtr29 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 0
  %capPtr30 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 1
  %size31 = load i32, ptr %sizePtr29, align 4
  %capacity32 = load i32, ptr %capPtr30, align 4
  %66 = icmp eq i32 %size31, %capacity32
  br i1 %66, label %grow33, label %cont34

grow33:                                           ; preds = %cont25
  %67 = mul i32 %capacity32, 2
  store i32 %67, ptr %capPtr30, align 4
  %dataPtr35 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %68 = load ptr, ptr %dataPtr35, align 8
  %69 = zext i32 %67 to i64
  %70 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %69
  %71 = call ptr @realloc(ptr %68, i64 %70)
  store ptr %71, ptr %dataPtr35, align 8
  br label %cont34

cont34:                                           ; preds = %grow33, %cont25
  %dataPtr36 = getelementptr inbounds %GenericList, ptr %args, i32 0, i32 2
  %dataBase37 = load ptr, ptr %dataPtr36, align 8
  %72 = getelementptr inbounds ptr, ptr %dataBase37, i32 %size31
  store ptr @96, ptr %72, align 8
  %73 = add i32 %size31, 1
  store i32 %73, ptr %sizePtr29, align 4
  %proc = alloca ptr, align 8
  store ptr null, ptr %proc, align 8
  %args_val = load %list_string, ptr %args, align 8
  store i32 0, ptr %proc, align 4
  %74 = load ptr, ptr %proc, align 8
  %75 = call i32 @process_wait(ptr %74)
  %print_call38 = call i32 (ptr, ...) @printf(ptr @99, ptr @97)
  %env_map = alloca %map_string_string, align 8
  store %map_string_string zeroinitializer, ptr %env_map, align 8
  %sizePtr39 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 0
  store i32 0, ptr %sizePtr39, align 4
  %capPtr40 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 1
  store i32 4, ptr %capPtr40, align 4
  %76 = call ptr @calloc(i64 4, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 2
  store ptr %76, ptr %occPtr, align 8
  %77 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 3
  store ptr %77, ptr %keysPtr, align 8
  %78 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %env_map, i32 0, i32 4
  store ptr %78, ptr %valsPtr, align 8
  store ptr null, ptr %env_map, align 8
  %print_call41 = call i32 (ptr, ...) @printf(ptr @102, ptr @100)
  %size_i32 = alloca i64, align 8
  store i64 0, ptr %size_i32, align 8
  store i32 0, ptr %size_i32, align 4
  %size_i32_val = load i64, ptr %size_i32, align 8
  %79 = call ptr @malloc(i64 24)
  %80 = call i32 (ptr, ptr, ...) @sprintf(ptr %79, ptr @105, i64 %size_i32_val)
  %81 = call i64 @strlen(ptr @104)
  %82 = call i64 @strlen(ptr %79)
  %83 = add i64 %81, %82
  %84 = add i64 %83, 1
  %85 = call ptr @malloc(i64 %84)
  %86 = call ptr @strcpy(ptr %85, ptr @104)
  %87 = call ptr @strcat(ptr %85, ptr %79)
  %print_call42 = call i32 (ptr, ...) @printf(ptr @107, ptr %85)
  %size_str = alloca i64, align 8
  store i64 0, ptr %size_str, align 8
  store i32 0, ptr %size_str, align 4
  %size_str_val = load i64, ptr %size_str, align 8
  %88 = call ptr @malloc(i64 24)
  %89 = call i32 (ptr, ptr, ...) @sprintf(ptr %88, ptr @110, i64 %size_str_val)
  %90 = call i64 @strlen(ptr @109)
  %91 = call i64 @strlen(ptr %88)
  %92 = add i64 %90, %91
  %93 = add i64 %92, 1
  %94 = call ptr @malloc(i64 %93)
  %95 = call ptr @strcpy(ptr %94, ptr @109)
  %96 = call ptr @strcat(ptr %94, ptr %88)
  %print_call43 = call i32 (ptr, ...) @printf(ptr @112, ptr %94)
  %print_call44 = call i32 (ptr, ...) @printf(ptr @115, ptr @113)
  ret i32 0
}
