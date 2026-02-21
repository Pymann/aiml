; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%map_Color_string = type { i32, i32, ptr, ptr, ptr }

@__argc = internal global i32 0
@__argv = internal global ptr null
@0 = private unnamed_addr constant [15 x i8] c"Color Red is: \00", align 1
@fmt_str = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_int = private unnamed_addr constant [4 x i8] c"%d \00", align 1
@1 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@2 = private unnamed_addr constant [16 x i8] c"Color Blue is: \00", align 1
@fmt_str.1 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_int.2 = private unnamed_addr constant [4 x i8] c"%d \00", align 1
@3 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@4 = private unnamed_addr constant [27 x i8] c"Color Yellow (Blue+1) is: \00", align 1
@fmt_str.3 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_int.4 = private unnamed_addr constant [4 x i8] c"%d \00", align 1
@5 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@6 = private unnamed_addr constant [20 x i8] c"Confirmed: c is Red\00", align 1
@fmt_str.5 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@7 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@8 = private unnamed_addr constant [26 x i8] c"Confirmed: c is NOT Green\00", align 1
@fmt_str.6 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@9 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@10 = private unnamed_addr constant [12 x i8] c"This is Red\00", align 1
@11 = private unnamed_addr constant [13 x i8] c"This is Blue\00", align 1
@12 = private unnamed_addr constant [11 x i8] c"Map[Red]: \00", align 1
@fmt_str.7 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_str.8 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@13 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@14 = private unnamed_addr constant [12 x i8] c"Map[Blue]: \00", align 1
@fmt_str.9 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_str.10 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@15 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@16 = private unnamed_addr constant [15 x i8] c"Color Red is: \00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [16 x i8] c"Color Blue is: \00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@22 = private unnamed_addr constant [27 x i8] c"Color Yellow (Blue+1) is: \00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@25 = private unnamed_addr constant [12 x i8] c"This is Red\00", align 1
@26 = private unnamed_addr constant [13 x i8] c"This is Blue\00", align 1
@27 = private unnamed_addr constant [11 x i8] c"Map[Red]: \00", align 1
@28 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@30 = private unnamed_addr constant [12 x i8] c"Map[Blue]: \00", align 1
@31 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@33 = private unnamed_addr constant [15 x i8] c"Color Red is: \00", align 1
@fmt_str.12 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_int.13 = private unnamed_addr constant [4 x i8] c"%d \00", align 1
@34 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@35 = private unnamed_addr constant [16 x i8] c"Color Blue is: \00", align 1
@fmt_str.14 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_int.15 = private unnamed_addr constant [4 x i8] c"%d \00", align 1
@36 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@37 = private unnamed_addr constant [27 x i8] c"Color Yellow (Blue+1) is: \00", align 1
@fmt_str.16 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_int.17 = private unnamed_addr constant [4 x i8] c"%d \00", align 1
@38 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@39 = private unnamed_addr constant [20 x i8] c"Confirmed: c is Red\00", align 1
@fmt_str.18 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@40 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@41 = private unnamed_addr constant [26 x i8] c"Confirmed: c is NOT Green\00", align 1
@fmt_str.19 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@42 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@43 = private unnamed_addr constant [12 x i8] c"This is Red\00", align 1
@44 = private unnamed_addr constant [13 x i8] c"This is Blue\00", align 1
@45 = private unnamed_addr constant [11 x i8] c"Map[Red]: \00", align 1
@fmt_str.20 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_str.21 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@46 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@47 = private unnamed_addr constant [12 x i8] c"Map[Blue]: \00", align 1
@fmt_str.22 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@fmt_str.23 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@48 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@49 = private unnamed_addr constant [15 x i8] c"Color Red is: \00", align 1
@50 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@51 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@52 = private unnamed_addr constant [16 x i8] c"Color Blue is: \00", align 1
@53 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@54 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@55 = private unnamed_addr constant [27 x i8] c"Color Yellow (Blue+1) is: \00", align 1
@56 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@57 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@58 = private unnamed_addr constant [12 x i8] c"This is Red\00", align 1
@59 = private unnamed_addr constant [13 x i8] c"This is Blue\00", align 1
@60 = private unnamed_addr constant [11 x i8] c"Map[Red]: \00", align 1
@61 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@62 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@63 = private unnamed_addr constant [12 x i8] c"Map[Blue]: \00", align 1
@64 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@65 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define internal void @__aiml_init() {
entry:
  %c = alloca i32, align 4
  store i32 0, ptr %c, align 4
  %c_val = load i32, ptr %c, align 4
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str, ptr @0)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_int, i32 %c_val)
  %2 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @1)
  %c2 = alloca i32, align 4
  store i32 10, ptr %c2, align 4
  %c2_val = load i32, ptr %c2, align 4
  %3 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.1, ptr @2)
  %4 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_int.2, i32 %c2_val)
  %5 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @3)
  %c3 = alloca i32, align 4
  store i32 11, ptr %c3, align 4
  %c3_val = load i32, ptr %c3, align 4
  %6 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.3, ptr @4)
  %7 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_int.4, i32 %c3_val)
  %8 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @5)
  %c_val1 = load i32, ptr %c, align 4
  %eq_tmp = icmp eq i32 %c_val1, 0
  br i1 %eq_tmp, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  %9 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.5, ptr @6)
  %10 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @7)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %entry
  %c_val2 = load i32, ptr %c, align 4
  %eq_tmp3 = icmp eq i32 %c_val2, 1
  br i1 %eq_tmp3, label %if_then4, label %if_else

if_then4:                                         ; preds = %if_merge
  br label %if_merge5

if_else:                                          ; preds = %if_merge
  %11 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.6, ptr @8)
  %12 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @9)
  br label %if_merge5

if_merge5:                                        ; preds = %if_else, %if_then4
  %colorMap = alloca %map_Color_string, align 8
  %sizePtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %colorMap, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %colorMap, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %13 = call ptr @calloc(i64 4, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %colorMap, i32 0, i32 2
  store ptr %13, ptr %occPtr, align 8
  %14 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %colorMap, i32 0, i32 3
  store ptr %14, ptr %keysPtr, align 8
  %15 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %colorMap, i32 0, i32 4
  store ptr %15, ptr %valsPtr, align 8
  %sizePtr6 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 0
  %capPtr7 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 1
  %size = load i32, ptr %sizePtr6, align 4
  %capacity = load i32, ptr %capPtr7, align 4
  %16 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %keysBase = load ptr, ptr %16, align 8
  %17 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %valsBase = load ptr, ptr %17, align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

put_loop:                                         ; preds = %put_next, %if_merge5
  %18 = load i32, ptr %i_put, align 4
  %19 = icmp slt i32 %18, %size
  br i1 %19, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %20 = getelementptr inbounds i32, ptr %keysBase, i32 %18
  %21 = load i32, ptr %20, align 4
  %key_eq = icmp eq i32 0, %21
  br i1 %key_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %22 = getelementptr inbounds ptr, ptr %valsBase, i32 %18
  store ptr @10, ptr %22, align 8
  br label %put_end

put_next:                                         ; preds = %put_check
  %23 = add i32 %18, 1
  store i32 %23, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %24 = icmp eq i32 %size, %capacity
  br i1 %24, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %sizePtr8 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 0
  %capPtr9 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 1
  %size10 = load i32, ptr %sizePtr8, align 4
  %capacity11 = load i32, ptr %capPtr9, align 4
  %25 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %keysBase12 = load ptr, ptr %25, align 8
  %26 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %valsBase13 = load ptr, ptr %26, align 8
  %i_put20 = alloca i32, align 4
  store i32 0, ptr %i_put20, align 4
  br label %put_loop14

put_grow:                                         ; preds = %put_not_found
  %27 = mul i32 %capacity, 2
  store i32 %27, ptr %capPtr7, align 4
  %28 = zext i32 %27 to i64
  %29 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %28
  %30 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %31 = load ptr, ptr %30, align 8
  %32 = call ptr @realloc(ptr %31, i64 %29)
  store ptr %32, ptr %30, align 8
  %33 = zext i32 %27 to i64
  %34 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %33
  %35 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %36 = load ptr, ptr %35, align 8
  %37 = call ptr @realloc(ptr %36, i64 %34)
  store ptr %37, ptr %35, align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %38 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %39 = load ptr, ptr %38, align 8
  %40 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %41 = load ptr, ptr %40, align 8
  %42 = getelementptr inbounds i32, ptr %39, i32 %size
  store i32 0, ptr %42, align 4
  %43 = getelementptr inbounds ptr, ptr %41, i32 %size
  store ptr @10, ptr %43, align 8
  %44 = add i32 %size, 1
  store i32 %44, ptr %sizePtr6, align 4
  br label %put_end

put_loop14:                                       ; preds = %put_next17, %put_end
  %45 = load i32, ptr %i_put20, align 4
  %46 = icmp slt i32 %45, %size10
  br i1 %46, label %put_check15, label %put_not_found18

put_check15:                                      ; preds = %put_loop14
  %47 = getelementptr inbounds i32, ptr %keysBase12, i32 %45
  %48 = load i32, ptr %47, align 4
  %key_eq21 = icmp eq i32 10, %48
  br i1 %key_eq21, label %put_found16, label %put_next17

put_found16:                                      ; preds = %put_check15
  %49 = getelementptr inbounds ptr, ptr %valsBase13, i32 %45
  store ptr @11, ptr %49, align 8
  br label %put_end19

put_next17:                                       ; preds = %put_check15
  %50 = add i32 %45, 1
  store i32 %50, ptr %i_put20, align 4
  br label %put_loop14

put_not_found18:                                  ; preds = %put_loop14
  %51 = icmp eq i32 %size10, %capacity11
  br i1 %51, label %put_grow22, label %put_append23

put_end19:                                        ; preds = %put_append23, %put_found16
  %52 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 0
  %53 = load i32, ptr %52, align 4
  %54 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %55 = load ptr, ptr %54, align 8
  %56 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %57 = load ptr, ptr %56, align 8
  %i_get = alloca i32, align 4
  store i32 0, ptr %i_get, align 4
  %res_get = alloca ptr, align 8
  store ptr null, ptr %res_get, align 8
  br label %get_loop

put_grow22:                                       ; preds = %put_not_found18
  %58 = mul i32 %capacity11, 2
  store i32 %58, ptr %capPtr9, align 4
  %59 = zext i32 %58 to i64
  %60 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %59
  %61 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %62 = load ptr, ptr %61, align 8
  %63 = call ptr @realloc(ptr %62, i64 %60)
  store ptr %63, ptr %61, align 8
  %64 = zext i32 %58 to i64
  %65 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %64
  %66 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %67 = load ptr, ptr %66, align 8
  %68 = call ptr @realloc(ptr %67, i64 %65)
  store ptr %68, ptr %66, align 8
  br label %put_append23

put_append23:                                     ; preds = %put_grow22, %put_not_found18
  %69 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %70 = load ptr, ptr %69, align 8
  %71 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %72 = load ptr, ptr %71, align 8
  %73 = getelementptr inbounds i32, ptr %70, i32 %size10
  store i32 10, ptr %73, align 4
  %74 = getelementptr inbounds ptr, ptr %72, i32 %size10
  store ptr @11, ptr %74, align 8
  %75 = add i32 %size10, 1
  store i32 %75, ptr %sizePtr8, align 4
  br label %put_end19

get_loop:                                         ; preds = %get_next, %put_end19
  %76 = load i32, ptr %i_get, align 4
  %77 = icmp slt i32 %76, %53
  br i1 %77, label %get_check, label %get_end

get_check:                                        ; preds = %get_loop
  %78 = getelementptr inbounds i32, ptr %55, i32 %76
  %79 = load i32, ptr %78, align 4
  %key_eq24 = icmp eq i32 0, %79
  br i1 %key_eq24, label %get_found, label %get_next

get_found:                                        ; preds = %get_check
  %80 = getelementptr inbounds ptr, ptr %57, i32 %76
  %81 = load ptr, ptr %80, align 8
  store ptr %81, ptr %res_get, align 8
  br label %get_end

get_end:                                          ; preds = %get_found, %get_loop
  %get_res = load ptr, ptr %res_get, align 8
  %82 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.7, ptr @12)
  %83 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.8, ptr %get_res)
  %84 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @13)
  %85 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 0
  %86 = load i32, ptr %85, align 4
  %87 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %88 = load ptr, ptr %87, align 8
  %89 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %90 = load ptr, ptr %89, align 8
  %i_get29 = alloca i32, align 4
  store i32 0, ptr %i_get29, align 4
  %res_get30 = alloca ptr, align 8
  store ptr null, ptr %res_get30, align 8
  br label %get_loop25

get_next:                                         ; preds = %get_check
  %91 = add i32 %76, 1
  store i32 %91, ptr %i_get, align 4
  br label %get_loop

get_loop25:                                       ; preds = %get_next32, %get_end
  %92 = load i32, ptr %i_get29, align 4
  %93 = icmp slt i32 %92, %86
  br i1 %93, label %get_check26, label %get_end28

get_check26:                                      ; preds = %get_loop25
  %94 = getelementptr inbounds i32, ptr %88, i32 %92
  %95 = load i32, ptr %94, align 4
  %key_eq31 = icmp eq i32 10, %95
  br i1 %key_eq31, label %get_found27, label %get_next32

get_found27:                                      ; preds = %get_check26
  %96 = getelementptr inbounds ptr, ptr %90, i32 %92
  %97 = load ptr, ptr %96, align 8
  store ptr %97, ptr %res_get30, align 8
  br label %get_end28

get_end28:                                        ; preds = %get_found27, %get_loop25
  %get_res33 = load ptr, ptr %res_get30, align 8
  %98 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.9, ptr @14)
  %99 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.10, ptr %get_res33)
  %100 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @15)
  %c34 = alloca i32, align 4
  store i32 0, ptr %c34, align 4
  %print_call = call i32 (ptr, ...) @printf(ptr @18, ptr @16)
  %c235 = alloca i32, align 4
  store i32 10, ptr %c235, align 4
  %print_call36 = call i32 (ptr, ...) @printf(ptr @21, ptr @19)
  %c337 = alloca i32, align 4
  store i32 11, ptr %c337, align 4
  %print_call38 = call i32 (ptr, ...) @printf(ptr @24, ptr @22)
  %colorMap39 = alloca %map_Color_string, align 8
  %sizePtr40 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 0
  %capPtr41 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 1
  %size42 = load i32, ptr %sizePtr40, align 4
  %capacity43 = load i32, ptr %capPtr41, align 4
  %101 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %keysBase44 = load ptr, ptr %101, align 8
  %102 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %valsBase45 = load ptr, ptr %102, align 8
  %i_put52 = alloca i32, align 4
  store i32 0, ptr %i_put52, align 4
  br label %put_loop46

get_next32:                                       ; preds = %get_check26
  %103 = add i32 %92, 1
  store i32 %103, ptr %i_get29, align 4
  br label %get_loop25

put_loop46:                                       ; preds = %put_next49, %get_end28
  %104 = load i32, ptr %i_put52, align 4
  %105 = icmp slt i32 %104, %size42
  br i1 %105, label %put_check47, label %put_not_found50

put_check47:                                      ; preds = %put_loop46
  %106 = getelementptr inbounds i32, ptr %keysBase44, i32 %104
  %107 = load i32, ptr %106, align 4
  %key_eq53 = icmp eq i32 0, %107
  br i1 %key_eq53, label %put_found48, label %put_next49

put_found48:                                      ; preds = %put_check47
  %108 = getelementptr inbounds ptr, ptr %valsBase45, i32 %104
  store ptr @25, ptr %108, align 8
  br label %put_end51

put_next49:                                       ; preds = %put_check47
  %109 = add i32 %104, 1
  store i32 %109, ptr %i_put52, align 4
  br label %put_loop46

put_not_found50:                                  ; preds = %put_loop46
  %110 = icmp eq i32 %size42, %capacity43
  br i1 %110, label %put_grow54, label %put_append55

put_end51:                                        ; preds = %put_append55, %put_found48
  %sizePtr56 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 0
  %capPtr57 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 1
  %size58 = load i32, ptr %sizePtr56, align 4
  %capacity59 = load i32, ptr %capPtr57, align 4
  %111 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %keysBase60 = load ptr, ptr %111, align 8
  %112 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %valsBase61 = load ptr, ptr %112, align 8
  %i_put68 = alloca i32, align 4
  store i32 0, ptr %i_put68, align 4
  br label %put_loop62

put_grow54:                                       ; preds = %put_not_found50
  %113 = mul i32 %capacity43, 2
  store i32 %113, ptr %capPtr41, align 4
  %114 = zext i32 %113 to i64
  %115 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %114
  %116 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %117 = load ptr, ptr %116, align 8
  %118 = call ptr @realloc(ptr %117, i64 %115)
  store ptr %118, ptr %116, align 8
  %119 = zext i32 %113 to i64
  %120 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %119
  %121 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %122 = load ptr, ptr %121, align 8
  %123 = call ptr @realloc(ptr %122, i64 %120)
  store ptr %123, ptr %121, align 8
  br label %put_append55

put_append55:                                     ; preds = %put_grow54, %put_not_found50
  %124 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %125 = load ptr, ptr %124, align 8
  %126 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %127 = load ptr, ptr %126, align 8
  %128 = getelementptr inbounds i32, ptr %125, i32 %size42
  store i32 0, ptr %128, align 4
  %129 = getelementptr inbounds ptr, ptr %127, i32 %size42
  store ptr @25, ptr %129, align 8
  %130 = add i32 %size42, 1
  store i32 %130, ptr %sizePtr40, align 4
  br label %put_end51

put_loop62:                                       ; preds = %put_next65, %put_end51
  %131 = load i32, ptr %i_put68, align 4
  %132 = icmp slt i32 %131, %size58
  br i1 %132, label %put_check63, label %put_not_found66

put_check63:                                      ; preds = %put_loop62
  %133 = getelementptr inbounds i32, ptr %keysBase60, i32 %131
  %134 = load i32, ptr %133, align 4
  %key_eq69 = icmp eq i32 10, %134
  br i1 %key_eq69, label %put_found64, label %put_next65

put_found64:                                      ; preds = %put_check63
  %135 = getelementptr inbounds ptr, ptr %valsBase61, i32 %131
  store ptr @26, ptr %135, align 8
  br label %put_end67

put_next65:                                       ; preds = %put_check63
  %136 = add i32 %131, 1
  store i32 %136, ptr %i_put68, align 4
  br label %put_loop62

put_not_found66:                                  ; preds = %put_loop62
  %137 = icmp eq i32 %size58, %capacity59
  br i1 %137, label %put_grow70, label %put_append71

put_end67:                                        ; preds = %put_append71, %put_found64
  %print_call72 = call i32 (ptr, ...) @printf(ptr @29, ptr @27)
  %print_call73 = call i32 (ptr, ...) @printf(ptr @32, ptr @30)
  ret void

put_grow70:                                       ; preds = %put_not_found66
  %138 = mul i32 %capacity59, 2
  store i32 %138, ptr %capPtr57, align 4
  %139 = zext i32 %138 to i64
  %140 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %139
  %141 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %142 = load ptr, ptr %141, align 8
  %143 = call ptr @realloc(ptr %142, i64 %140)
  store ptr %143, ptr %141, align 8
  %144 = zext i32 %138 to i64
  %145 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %144
  %146 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %147 = load ptr, ptr %146, align 8
  %148 = call ptr @realloc(ptr %147, i64 %145)
  store ptr %148, ptr %146, align 8
  br label %put_append71

put_append71:                                     ; preds = %put_grow70, %put_not_found66
  %149 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %150 = load ptr, ptr %149, align 8
  %151 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %152 = load ptr, ptr %151, align 8
  %153 = getelementptr inbounds i32, ptr %150, i32 %size58
  store i32 10, ptr %153, align 4
  %154 = getelementptr inbounds ptr, ptr %152, i32 %size58
  store ptr @26, ptr %154, align 8
  %155 = add i32 %size58, 1
  store i32 %155, ptr %sizePtr56, align 4
  br label %put_end67
}

declare i32 @dprintf(i32, ptr, ...)

declare ptr @calloc(i64, i64)

declare ptr @malloc(i64)

declare ptr @realloc(ptr, i64)

declare i32 @printf(ptr, ...)

define i32 @main(i32 %argc, ptr %argv) {
entry:
  store i32 %argc, ptr @__argc, align 4
  store ptr %argv, ptr @__argv, align 8
  call void @__aiml_init()
  ret i32 0
}

define internal void @__aiml_init.11() {
entry:
  %c = alloca i32, align 4
  store i32 0, ptr %c, align 4
  %c_val = load i32, ptr %c, align 4
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.12, ptr @33)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_int.13, i32 %c_val)
  %2 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @34)
  %c2 = alloca i32, align 4
  store i32 10, ptr %c2, align 4
  %c2_val = load i32, ptr %c2, align 4
  %3 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.14, ptr @35)
  %4 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_int.15, i32 %c2_val)
  %5 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @36)
  %c3 = alloca i32, align 4
  store i32 11, ptr %c3, align 4
  %c3_val = load i32, ptr %c3, align 4
  %6 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.16, ptr @37)
  %7 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_int.17, i32 %c3_val)
  %8 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @38)
  %c_val1 = load i32, ptr %c, align 4
  %eq_tmp = icmp eq i32 %c_val1, 0
  br i1 %eq_tmp, label %if_then, label %if_merge

if_then:                                          ; preds = %entry
  %9 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.18, ptr @39)
  %10 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @40)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %entry
  %c_val2 = load i32, ptr %c, align 4
  %eq_tmp3 = icmp eq i32 %c_val2, 1
  br i1 %eq_tmp3, label %if_then4, label %if_else

if_then4:                                         ; preds = %if_merge
  br label %if_merge5

if_else:                                          ; preds = %if_merge
  %11 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.19, ptr @41)
  %12 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @42)
  br label %if_merge5

if_merge5:                                        ; preds = %if_else, %if_then4
  %colorMap = alloca %map_Color_string, align 8
  %sizePtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %colorMap, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %colorMap, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %13 = call ptr @calloc(i64 4, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %colorMap, i32 0, i32 2
  store ptr %13, ptr %occPtr, align 8
  %14 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %colorMap, i32 0, i32 3
  store ptr %14, ptr %keysPtr, align 8
  %15 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %colorMap, i32 0, i32 4
  store ptr %15, ptr %valsPtr, align 8
  %sizePtr6 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 0
  %capPtr7 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 1
  %size = load i32, ptr %sizePtr6, align 4
  %capacity = load i32, ptr %capPtr7, align 4
  %16 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %keysBase = load ptr, ptr %16, align 8
  %17 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %valsBase = load ptr, ptr %17, align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

put_loop:                                         ; preds = %put_next, %if_merge5
  %18 = load i32, ptr %i_put, align 4
  %19 = icmp slt i32 %18, %size
  br i1 %19, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %20 = getelementptr inbounds i32, ptr %keysBase, i32 %18
  %21 = load i32, ptr %20, align 4
  %key_eq = icmp eq i32 0, %21
  br i1 %key_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %22 = getelementptr inbounds ptr, ptr %valsBase, i32 %18
  store ptr @43, ptr %22, align 8
  br label %put_end

put_next:                                         ; preds = %put_check
  %23 = add i32 %18, 1
  store i32 %23, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %24 = icmp eq i32 %size, %capacity
  br i1 %24, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %sizePtr8 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 0
  %capPtr9 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 1
  %size10 = load i32, ptr %sizePtr8, align 4
  %capacity11 = load i32, ptr %capPtr9, align 4
  %25 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %keysBase12 = load ptr, ptr %25, align 8
  %26 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %valsBase13 = load ptr, ptr %26, align 8
  %i_put20 = alloca i32, align 4
  store i32 0, ptr %i_put20, align 4
  br label %put_loop14

put_grow:                                         ; preds = %put_not_found
  %27 = mul i32 %capacity, 2
  store i32 %27, ptr %capPtr7, align 4
  %28 = zext i32 %27 to i64
  %29 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %28
  %30 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %31 = load ptr, ptr %30, align 8
  %32 = call ptr @realloc(ptr %31, i64 %29)
  store ptr %32, ptr %30, align 8
  %33 = zext i32 %27 to i64
  %34 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %33
  %35 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %36 = load ptr, ptr %35, align 8
  %37 = call ptr @realloc(ptr %36, i64 %34)
  store ptr %37, ptr %35, align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %38 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %39 = load ptr, ptr %38, align 8
  %40 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %41 = load ptr, ptr %40, align 8
  %42 = getelementptr inbounds i32, ptr %39, i32 %size
  store i32 0, ptr %42, align 4
  %43 = getelementptr inbounds ptr, ptr %41, i32 %size
  store ptr @43, ptr %43, align 8
  %44 = add i32 %size, 1
  store i32 %44, ptr %sizePtr6, align 4
  br label %put_end

put_loop14:                                       ; preds = %put_next17, %put_end
  %45 = load i32, ptr %i_put20, align 4
  %46 = icmp slt i32 %45, %size10
  br i1 %46, label %put_check15, label %put_not_found18

put_check15:                                      ; preds = %put_loop14
  %47 = getelementptr inbounds i32, ptr %keysBase12, i32 %45
  %48 = load i32, ptr %47, align 4
  %key_eq21 = icmp eq i32 10, %48
  br i1 %key_eq21, label %put_found16, label %put_next17

put_found16:                                      ; preds = %put_check15
  %49 = getelementptr inbounds ptr, ptr %valsBase13, i32 %45
  store ptr @44, ptr %49, align 8
  br label %put_end19

put_next17:                                       ; preds = %put_check15
  %50 = add i32 %45, 1
  store i32 %50, ptr %i_put20, align 4
  br label %put_loop14

put_not_found18:                                  ; preds = %put_loop14
  %51 = icmp eq i32 %size10, %capacity11
  br i1 %51, label %put_grow22, label %put_append23

put_end19:                                        ; preds = %put_append23, %put_found16
  %52 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 0
  %53 = load i32, ptr %52, align 4
  %54 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %55 = load ptr, ptr %54, align 8
  %56 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %57 = load ptr, ptr %56, align 8
  %i_get = alloca i32, align 4
  store i32 0, ptr %i_get, align 4
  %res_get = alloca ptr, align 8
  store ptr null, ptr %res_get, align 8
  br label %get_loop

put_grow22:                                       ; preds = %put_not_found18
  %58 = mul i32 %capacity11, 2
  store i32 %58, ptr %capPtr9, align 4
  %59 = zext i32 %58 to i64
  %60 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %59
  %61 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %62 = load ptr, ptr %61, align 8
  %63 = call ptr @realloc(ptr %62, i64 %60)
  store ptr %63, ptr %61, align 8
  %64 = zext i32 %58 to i64
  %65 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %64
  %66 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %67 = load ptr, ptr %66, align 8
  %68 = call ptr @realloc(ptr %67, i64 %65)
  store ptr %68, ptr %66, align 8
  br label %put_append23

put_append23:                                     ; preds = %put_grow22, %put_not_found18
  %69 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %70 = load ptr, ptr %69, align 8
  %71 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %72 = load ptr, ptr %71, align 8
  %73 = getelementptr inbounds i32, ptr %70, i32 %size10
  store i32 10, ptr %73, align 4
  %74 = getelementptr inbounds ptr, ptr %72, i32 %size10
  store ptr @44, ptr %74, align 8
  %75 = add i32 %size10, 1
  store i32 %75, ptr %sizePtr8, align 4
  br label %put_end19

get_loop:                                         ; preds = %get_next, %put_end19
  %76 = load i32, ptr %i_get, align 4
  %77 = icmp slt i32 %76, %53
  br i1 %77, label %get_check, label %get_end

get_check:                                        ; preds = %get_loop
  %78 = getelementptr inbounds i32, ptr %55, i32 %76
  %79 = load i32, ptr %78, align 4
  %key_eq24 = icmp eq i32 0, %79
  br i1 %key_eq24, label %get_found, label %get_next

get_found:                                        ; preds = %get_check
  %80 = getelementptr inbounds ptr, ptr %57, i32 %76
  %81 = load ptr, ptr %80, align 8
  store ptr %81, ptr %res_get, align 8
  br label %get_end

get_end:                                          ; preds = %get_found, %get_loop
  %get_res = load ptr, ptr %res_get, align 8
  %82 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.20, ptr @45)
  %83 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.21, ptr %get_res)
  %84 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @46)
  %85 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 0
  %86 = load i32, ptr %85, align 4
  %87 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 3
  %88 = load ptr, ptr %87, align 8
  %89 = getelementptr inbounds %map_Color_string, ptr %colorMap, i32 0, i32 4
  %90 = load ptr, ptr %89, align 8
  %i_get29 = alloca i32, align 4
  store i32 0, ptr %i_get29, align 4
  %res_get30 = alloca ptr, align 8
  store ptr null, ptr %res_get30, align 8
  br label %get_loop25

get_next:                                         ; preds = %get_check
  %91 = add i32 %76, 1
  store i32 %91, ptr %i_get, align 4
  br label %get_loop

get_loop25:                                       ; preds = %get_next32, %get_end
  %92 = load i32, ptr %i_get29, align 4
  %93 = icmp slt i32 %92, %86
  br i1 %93, label %get_check26, label %get_end28

get_check26:                                      ; preds = %get_loop25
  %94 = getelementptr inbounds i32, ptr %88, i32 %92
  %95 = load i32, ptr %94, align 4
  %key_eq31 = icmp eq i32 10, %95
  br i1 %key_eq31, label %get_found27, label %get_next32

get_found27:                                      ; preds = %get_check26
  %96 = getelementptr inbounds ptr, ptr %90, i32 %92
  %97 = load ptr, ptr %96, align 8
  store ptr %97, ptr %res_get30, align 8
  br label %get_end28

get_end28:                                        ; preds = %get_found27, %get_loop25
  %get_res33 = load ptr, ptr %res_get30, align 8
  %98 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.22, ptr @47)
  %99 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.23, ptr %get_res33)
  %100 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @48)
  %c34 = alloca i32, align 4
  store i32 0, ptr %c34, align 4
  %print_call = call i32 (ptr, ...) @printf(ptr @51, ptr @49)
  %c235 = alloca i32, align 4
  store i32 10, ptr %c235, align 4
  %print_call36 = call i32 (ptr, ...) @printf(ptr @54, ptr @52)
  %c337 = alloca i32, align 4
  store i32 11, ptr %c337, align 4
  %print_call38 = call i32 (ptr, ...) @printf(ptr @57, ptr @55)
  %colorMap39 = alloca %map_Color_string, align 8
  %sizePtr40 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 0
  %capPtr41 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 1
  %size42 = load i32, ptr %sizePtr40, align 4
  %capacity43 = load i32, ptr %capPtr41, align 4
  %101 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %keysBase44 = load ptr, ptr %101, align 8
  %102 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %valsBase45 = load ptr, ptr %102, align 8
  %i_put52 = alloca i32, align 4
  store i32 0, ptr %i_put52, align 4
  br label %put_loop46

get_next32:                                       ; preds = %get_check26
  %103 = add i32 %92, 1
  store i32 %103, ptr %i_get29, align 4
  br label %get_loop25

put_loop46:                                       ; preds = %put_next49, %get_end28
  %104 = load i32, ptr %i_put52, align 4
  %105 = icmp slt i32 %104, %size42
  br i1 %105, label %put_check47, label %put_not_found50

put_check47:                                      ; preds = %put_loop46
  %106 = getelementptr inbounds i32, ptr %keysBase44, i32 %104
  %107 = load i32, ptr %106, align 4
  %key_eq53 = icmp eq i32 0, %107
  br i1 %key_eq53, label %put_found48, label %put_next49

put_found48:                                      ; preds = %put_check47
  %108 = getelementptr inbounds ptr, ptr %valsBase45, i32 %104
  store ptr @58, ptr %108, align 8
  br label %put_end51

put_next49:                                       ; preds = %put_check47
  %109 = add i32 %104, 1
  store i32 %109, ptr %i_put52, align 4
  br label %put_loop46

put_not_found50:                                  ; preds = %put_loop46
  %110 = icmp eq i32 %size42, %capacity43
  br i1 %110, label %put_grow54, label %put_append55

put_end51:                                        ; preds = %put_append55, %put_found48
  %sizePtr56 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 0
  %capPtr57 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 1
  %size58 = load i32, ptr %sizePtr56, align 4
  %capacity59 = load i32, ptr %capPtr57, align 4
  %111 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %keysBase60 = load ptr, ptr %111, align 8
  %112 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %valsBase61 = load ptr, ptr %112, align 8
  %i_put68 = alloca i32, align 4
  store i32 0, ptr %i_put68, align 4
  br label %put_loop62

put_grow54:                                       ; preds = %put_not_found50
  %113 = mul i32 %capacity43, 2
  store i32 %113, ptr %capPtr41, align 4
  %114 = zext i32 %113 to i64
  %115 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %114
  %116 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %117 = load ptr, ptr %116, align 8
  %118 = call ptr @realloc(ptr %117, i64 %115)
  store ptr %118, ptr %116, align 8
  %119 = zext i32 %113 to i64
  %120 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %119
  %121 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %122 = load ptr, ptr %121, align 8
  %123 = call ptr @realloc(ptr %122, i64 %120)
  store ptr %123, ptr %121, align 8
  br label %put_append55

put_append55:                                     ; preds = %put_grow54, %put_not_found50
  %124 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %125 = load ptr, ptr %124, align 8
  %126 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %127 = load ptr, ptr %126, align 8
  %128 = getelementptr inbounds i32, ptr %125, i32 %size42
  store i32 0, ptr %128, align 4
  %129 = getelementptr inbounds ptr, ptr %127, i32 %size42
  store ptr @58, ptr %129, align 8
  %130 = add i32 %size42, 1
  store i32 %130, ptr %sizePtr40, align 4
  br label %put_end51

put_loop62:                                       ; preds = %put_next65, %put_end51
  %131 = load i32, ptr %i_put68, align 4
  %132 = icmp slt i32 %131, %size58
  br i1 %132, label %put_check63, label %put_not_found66

put_check63:                                      ; preds = %put_loop62
  %133 = getelementptr inbounds i32, ptr %keysBase60, i32 %131
  %134 = load i32, ptr %133, align 4
  %key_eq69 = icmp eq i32 10, %134
  br i1 %key_eq69, label %put_found64, label %put_next65

put_found64:                                      ; preds = %put_check63
  %135 = getelementptr inbounds ptr, ptr %valsBase61, i32 %131
  store ptr @59, ptr %135, align 8
  br label %put_end67

put_next65:                                       ; preds = %put_check63
  %136 = add i32 %131, 1
  store i32 %136, ptr %i_put68, align 4
  br label %put_loop62

put_not_found66:                                  ; preds = %put_loop62
  %137 = icmp eq i32 %size58, %capacity59
  br i1 %137, label %put_grow70, label %put_append71

put_end67:                                        ; preds = %put_append71, %put_found64
  %print_call72 = call i32 (ptr, ...) @printf(ptr @62, ptr @60)
  %print_call73 = call i32 (ptr, ...) @printf(ptr @65, ptr @63)
  ret void

put_grow70:                                       ; preds = %put_not_found66
  %138 = mul i32 %capacity59, 2
  store i32 %138, ptr %capPtr57, align 4
  %139 = zext i32 %138 to i64
  %140 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %139
  %141 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %142 = load ptr, ptr %141, align 8
  %143 = call ptr @realloc(ptr %142, i64 %140)
  store ptr %143, ptr %141, align 8
  %144 = zext i32 %138 to i64
  %145 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %144
  %146 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %147 = load ptr, ptr %146, align 8
  %148 = call ptr @realloc(ptr %147, i64 %145)
  store ptr %148, ptr %146, align 8
  br label %put_append71

put_append71:                                     ; preds = %put_grow70, %put_not_found66
  %149 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 3
  %150 = load ptr, ptr %149, align 8
  %151 = getelementptr inbounds %map_Color_string, ptr %colorMap39, i32 0, i32 4
  %152 = load ptr, ptr %151, align 8
  %153 = getelementptr inbounds i32, ptr %150, i32 %size58
  store i32 10, ptr %153, align 4
  %154 = getelementptr inbounds ptr, ptr %152, i32 %size58
  store ptr @59, ptr %154, align 8
  %155 = add i32 %size58, 1
  store i32 %155, ptr %sizePtr56, align 4
  br label %put_end67
}
