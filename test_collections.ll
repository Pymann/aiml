; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"

@0 = private unnamed_addr constant [37 x i8] c"Testing Arrays with Hyphen Syntax...\00", align 1
@fmt_str = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@1 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@2 = private unnamed_addr constant [9 x i8] c"arr-2 = \00", align 1
@3 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define i32 @main() {
entry:
  %0 = call i32 (ptr, ...) @printf(ptr @fmt_str, ptr @0)
  %arr = alloca [3 x i32], align 4
  store [3 x i32] zeroinitializer, ptr %arr, align 4
  %arr_idx_0 = getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 0
  store i32 10, ptr %arr_idx_0, align 4
  %arr_idx_1 = getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 1
  store i32 20, ptr %arr_idx_1, align 4
  %arr_idx_01 = getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 0
  %arr_elem_load = load i32, ptr %arr_idx_01, align 4
  %arr_idx_12 = getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 1
  %arr_elem_load3 = load i32, ptr %arr_idx_12, align 4
  %add_tmp = add i32 %arr_elem_load, %arr_elem_load3
  %arr_idx_2 = getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 2
  store i32 %add_tmp, ptr %arr_idx_2, align 4
  %1 = call i64 @strlen(ptr @1)
  %2 = call i64 @strlen(ptr @2)
  %3 = add i64 %1, %2
  %4 = add i64 %3, 1
  %5 = call ptr @malloc(i64 %4)
  %6 = call ptr @strcpy(ptr %5, ptr @1)
  %7 = call ptr @strcat(ptr %5, ptr @2)
  %arr_idx_24 = getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 2
  %arr_elem_load5 = load i32, ptr %arr_idx_24, align 4
  %8 = call ptr @malloc(i64 12)
  %9 = call i32 (ptr, ptr, ...) @sprintf(ptr %8, ptr @3, i32 %arr_elem_load5)
  %10 = call i64 @strlen(ptr %5)
  %11 = call i64 @strlen(ptr %8)
  %12 = add i64 %10, %11
  %13 = add i64 %12, 1
  %14 = call ptr @malloc(i64 %13)
  %15 = call ptr @strcpy(ptr %14, ptr %5)
  %16 = call ptr @strcat(ptr %14, ptr %8)
  %17 = call i32 (ptr, ...) @printf(ptr @fmt_str.1, ptr %14)
  %arr_idx_26 = getelementptr inbounds [3 x i32], ptr %arr, i32 0, i32 2
  %arr_elem_load7 = load i32, ptr %arr_idx_26, align 4
  call void @exit(i32 %arr_elem_load7)
  ret i32 0
}

declare i32 @printf(ptr, ...)

declare i64 @strlen(ptr)

declare ptr @malloc(i64)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare i32 @sprintf(ptr, ptr, ...)

declare void @exit(i32)
