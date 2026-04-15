; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%map_string_i32 = type { i32, i32, ptr, ptr, ptr }

@__argc = external global i32
@__argv = external global ptr
@__global_scores = global %map_string_i32 zeroinitializer
@0 = private unnamed_addr constant [16 x i8] c"Testing Maps...\00", align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@2 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"Bob\00", align 1
@4 = private unnamed_addr constant [14 x i8] c"Alice Score: \00", align 1
@5 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [12 x i8] c"Bob Score: \00", align 1
@8 = private unnamed_addr constant [4 x i8] c"Bob\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [21 x i8] c"Size before remove: \00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@13 = private unnamed_addr constant [20 x i8] c"Size after remove: \00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [12 x i8] c"Has Alice? \00", align 1
@16 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@17 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@18 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [10 x i8] c"Has Bob? \00", align 1
@21 = private unnamed_addr constant [4 x i8] c"Bob\00", align 1
@22 = private unnamed_addr constant [5 x i8] c"true\00", align 1
@23 = private unnamed_addr constant [6 x i8] c"false\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@24 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@25 = private unnamed_addr constant [22 x i8] c"Math Test: 10 + 20 = \00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_schema_count = constant i32 0

define void @__aiml_init() {
entry:
  store i32 0, ptr @__global_scores, align 4
  store i32 4, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_scores, i32 0, i32 1), align 4
  %0 = call ptr @calloc(i64 4, i64 1)
  store ptr %0, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_scores, i32 0, i32 2), align 8
  %1 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  store ptr %1, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_scores, i32 0, i32 3), align 8
  %2 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  store ptr %2, ptr getelementptr inbounds ({ i32, i32, ptr, ptr, ptr }, ptr @__global_scores, i32 0, i32 4), align 8
  %print_call = call i32 (ptr, ...) @printf(ptr @1, ptr @0)
  %size = load i32, ptr @__global_scores, align 4
  %capacity = load i32, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 1), align 4
  %keysBase = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %valsBase = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
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
  %7 = call i32 @strcmp(ptr @2, ptr %6)
  %str_eq = icmp eq i32 %7, 0
  br i1 %str_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %8 = getelementptr inbounds i32, ptr %valsBase, i32 %3
  store i32 100, ptr %8, align 4
  br label %put_end

put_next:                                         ; preds = %put_check
  %9 = add i32 %3, 1
  store i32 %9, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %10 = icmp eq i32 %size, %capacity
  br i1 %10, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %size1 = load i32, ptr @__global_scores, align 4
  %capacity2 = load i32, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 1), align 4
  %keysBase3 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %valsBase4 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
  %i_put11 = alloca i32, align 4
  store i32 0, ptr %i_put11, align 4
  br label %put_loop5

put_grow:                                         ; preds = %put_not_found
  %11 = mul i32 %capacity, 2
  %12 = icmp sgt i32 %11, 3
  %growCap = select i1 %12, i32 %11, i32 4
  store i32 %growCap, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 1), align 4
  %13 = zext i32 %growCap to i64
  %14 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %13
  %15 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %16 = call ptr @realloc(ptr %15, i64 %14)
  store ptr %16, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %17 = zext i32 %growCap to i64
  %18 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %17
  %19 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
  %20 = call ptr @realloc(ptr %19, i64 %18)
  store ptr %20, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %21 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %22 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
  %23 = getelementptr inbounds ptr, ptr %21, i32 %size
  store ptr @2, ptr %23, align 8
  %24 = getelementptr inbounds i32, ptr %22, i32 %size
  store i32 100, ptr %24, align 4
  %25 = add i32 %size, 1
  store i32 %25, ptr @__global_scores, align 4
  br label %put_end

put_loop5:                                        ; preds = %put_next8, %put_end
  %26 = load i32, ptr %i_put11, align 4
  %27 = icmp slt i32 %26, %size1
  br i1 %27, label %put_check6, label %put_not_found9

put_check6:                                       ; preds = %put_loop5
  %28 = getelementptr inbounds ptr, ptr %keysBase3, i32 %26
  %29 = load ptr, ptr %28, align 8
  %30 = call i32 @strcmp(ptr @3, ptr %29)
  %str_eq12 = icmp eq i32 %30, 0
  br i1 %str_eq12, label %put_found7, label %put_next8

put_found7:                                       ; preds = %put_check6
  %31 = getelementptr inbounds i32, ptr %valsBase4, i32 %26
  store i32 200, ptr %31, align 4
  br label %put_end10

put_next8:                                        ; preds = %put_check6
  %32 = add i32 %26, 1
  store i32 %32, ptr %i_put11, align 4
  br label %put_loop5

put_not_found9:                                   ; preds = %put_loop5
  %33 = icmp eq i32 %size1, %capacity2
  br i1 %33, label %put_grow13, label %put_append14

put_end10:                                        ; preds = %put_append14, %put_found7
  %34 = load i32, ptr @__global_scores, align 4
  %35 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %36 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
  %i_get = alloca i32, align 4
  store i32 0, ptr %i_get, align 4
  %res_get = alloca i32, align 4
  store i32 0, ptr %res_get, align 4
  br label %get_loop

put_grow13:                                       ; preds = %put_not_found9
  %37 = mul i32 %capacity2, 2
  %38 = icmp sgt i32 %37, 3
  %growCap15 = select i1 %38, i32 %37, i32 4
  store i32 %growCap15, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 1), align 4
  %39 = zext i32 %growCap15 to i64
  %40 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %39
  %41 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %42 = call ptr @realloc(ptr %41, i64 %40)
  store ptr %42, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %43 = zext i32 %growCap15 to i64
  %44 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %43
  %45 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
  %46 = call ptr @realloc(ptr %45, i64 %44)
  store ptr %46, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
  br label %put_append14

put_append14:                                     ; preds = %put_grow13, %put_not_found9
  %47 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %48 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
  %49 = getelementptr inbounds ptr, ptr %47, i32 %size1
  store ptr @3, ptr %49, align 8
  %50 = getelementptr inbounds i32, ptr %48, i32 %size1
  store i32 200, ptr %50, align 4
  %51 = add i32 %size1, 1
  store i32 %51, ptr @__global_scores, align 4
  br label %put_end10

get_loop:                                         ; preds = %get_next, %put_end10
  %52 = load i32, ptr %i_get, align 4
  %53 = icmp slt i32 %52, %34
  br i1 %53, label %get_check, label %get_end

get_check:                                        ; preds = %get_loop
  %54 = getelementptr inbounds ptr, ptr %35, i32 %52
  %55 = load ptr, ptr %54, align 8
  %56 = call i32 @strcmp(ptr @5, ptr %55)
  %str_eq16 = icmp eq i32 %56, 0
  br i1 %str_eq16, label %get_found, label %get_next

get_found:                                        ; preds = %get_check
  %57 = getelementptr inbounds i32, ptr %36, i32 %52
  %58 = load i32, ptr %57, align 4
  store i32 %58, ptr %res_get, align 4
  br label %get_end

get_end:                                          ; preds = %get_found, %get_loop
  %get_res = load i32, ptr %res_get, align 4
  %59 = call ptr @__aiml_int_to_string(i32 %get_res)
  %60 = call ptr @__aiml_str_concat(ptr @4, ptr %59)
  %print_null_check = icmp eq ptr %60, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str.1, ptr %60
  %print_call17 = call i32 (ptr, ...) @printf(ptr @6, ptr %print_safe_str)
  %61 = load i32, ptr @__global_scores, align 4
  %62 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %63 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
  %i_get22 = alloca i32, align 4
  store i32 0, ptr %i_get22, align 4
  %res_get23 = alloca i32, align 4
  store i32 0, ptr %res_get23, align 4
  br label %get_loop18

get_next:                                         ; preds = %get_check
  %64 = add i32 %52, 1
  store i32 %64, ptr %i_get, align 4
  br label %get_loop

get_loop18:                                       ; preds = %get_next25, %get_end
  %65 = load i32, ptr %i_get22, align 4
  %66 = icmp slt i32 %65, %61
  br i1 %66, label %get_check19, label %get_end21

get_check19:                                      ; preds = %get_loop18
  %67 = getelementptr inbounds ptr, ptr %62, i32 %65
  %68 = load ptr, ptr %67, align 8
  %69 = call i32 @strcmp(ptr @8, ptr %68)
  %str_eq24 = icmp eq i32 %69, 0
  br i1 %str_eq24, label %get_found20, label %get_next25

get_found20:                                      ; preds = %get_check19
  %70 = getelementptr inbounds i32, ptr %63, i32 %65
  %71 = load i32, ptr %70, align 4
  store i32 %71, ptr %res_get23, align 4
  br label %get_end21

get_end21:                                        ; preds = %get_found20, %get_loop18
  %get_res26 = load i32, ptr %res_get23, align 4
  %72 = call ptr @__aiml_int_to_string(i32 %get_res26)
  %73 = call ptr @__aiml_str_concat(ptr @7, ptr %72)
  %print_null_check27 = icmp eq ptr %73, null
  %print_safe_str28 = select i1 %print_null_check27, ptr @print_null_str.2, ptr %73
  %print_call29 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str28)
  %size_val = load i32, ptr @__global_scores, align 4
  %74 = call ptr @__aiml_int_to_string(i32 %size_val)
  %75 = call ptr @__aiml_str_concat(ptr @10, ptr %74)
  %print_null_check30 = icmp eq ptr %75, null
  %print_safe_str31 = select i1 %print_null_check30, ptr @print_null_str.3, ptr %75
  %print_call32 = call i32 (ptr, ...) @printf(ptr @11, ptr %print_safe_str31)
  %76 = load i32, ptr @__global_scores, align 4
  %77 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %78 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 4), align 8
  %i_rm = alloca i32, align 4
  store i32 0, ptr %i_rm, align 4
  br label %rm_loop

get_next25:                                       ; preds = %get_check19
  %79 = add i32 %65, 1
  store i32 %79, ptr %i_get22, align 4
  br label %get_loop18

rm_loop:                                          ; preds = %rm_next, %get_end21
  %80 = load i32, ptr %i_rm, align 4
  %81 = icmp slt i32 %80, %76
  br i1 %81, label %rm_check, label %rm_end

rm_check:                                         ; preds = %rm_loop
  %82 = getelementptr inbounds ptr, ptr %77, i32 %80
  %83 = load ptr, ptr %82, align 8
  %84 = call i32 @strcmp(ptr @12, ptr %83)
  %str_eq33 = icmp eq i32 %84, 0
  br i1 %str_eq33, label %rm_found, label %rm_next

rm_found:                                         ; preds = %rm_check
  %85 = add i32 %80, 1
  %86 = sub i32 %76, %85
  %87 = getelementptr inbounds ptr, ptr %77, i32 %80
  %88 = getelementptr inbounds ptr, ptr %77, i32 %85
  %89 = zext i32 %86 to i64
  %90 = mul i64 %89, ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64)
  %91 = call ptr @memmove(ptr %87, ptr %88, i64 %90)
  %92 = getelementptr inbounds i32, ptr %78, i32 %80
  %93 = getelementptr inbounds i32, ptr %78, i32 %85
  %94 = zext i32 %86 to i64
  %95 = mul i64 %94, ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64)
  %96 = call ptr @memmove(ptr %92, ptr %93, i64 %95)
  %97 = sub i32 %76, 1
  store i32 %97, ptr @__global_scores, align 4
  br label %rm_end

rm_end:                                           ; preds = %rm_found, %rm_loop
  %size_val34 = load i32, ptr @__global_scores, align 4
  %98 = call ptr @__aiml_int_to_string(i32 %size_val34)
  %99 = call ptr @__aiml_str_concat(ptr @13, ptr %98)
  %print_null_check35 = icmp eq ptr %99, null
  %print_safe_str36 = select i1 %print_null_check35, ptr @print_null_str.4, ptr %99
  %print_call37 = call i32 (ptr, ...) @printf(ptr @14, ptr %print_safe_str36)
  %100 = load i32, ptr @__global_scores, align 4
  %101 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %i_cont = alloca i32, align 4
  store i32 0, ptr %i_cont, align 4
  %res_cont = alloca i1, align 1
  store i1 false, ptr %res_cont, align 1
  br label %cont_loop

rm_next:                                          ; preds = %rm_check
  %102 = add i32 %80, 1
  store i32 %102, ptr %i_rm, align 4
  br label %rm_loop

cont_loop:                                        ; preds = %cont_next, %rm_end
  %103 = load i32, ptr %i_cont, align 4
  %104 = icmp slt i32 %103, %100
  br i1 %104, label %cont_check, label %cont_end

cont_check:                                       ; preds = %cont_loop
  %105 = getelementptr inbounds ptr, ptr %101, i32 %103
  %106 = load ptr, ptr %105, align 8
  %107 = call i32 @strcmp(ptr @16, ptr %106)
  %str_eq38 = icmp eq i32 %107, 0
  br i1 %str_eq38, label %cont_found, label %cont_next

cont_found:                                       ; preds = %cont_check
  store i1 true, ptr %res_cont, align 1
  br label %cont_end

cont_end:                                         ; preds = %cont_found, %cont_loop
  %108 = load i1, ptr %res_cont, align 1
  %109 = select i1 %108, ptr @17, ptr @18
  %110 = call ptr @__aiml_str_concat(ptr @15, ptr %109)
  %print_null_check39 = icmp eq ptr %110, null
  %print_safe_str40 = select i1 %print_null_check39, ptr @print_null_str.5, ptr %110
  %print_call41 = call i32 (ptr, ...) @printf(ptr @19, ptr %print_safe_str40)
  %111 = load i32, ptr @__global_scores, align 4
  %112 = load ptr, ptr getelementptr inbounds (%map_string_i32, ptr @__global_scores, i32 0, i32 3), align 8
  %i_cont46 = alloca i32, align 4
  store i32 0, ptr %i_cont46, align 4
  %res_cont47 = alloca i1, align 1
  store i1 false, ptr %res_cont47, align 1
  br label %cont_loop42

cont_next:                                        ; preds = %cont_check
  %113 = add i32 %103, 1
  store i32 %113, ptr %i_cont, align 4
  br label %cont_loop

cont_loop42:                                      ; preds = %cont_next49, %cont_end
  %114 = load i32, ptr %i_cont46, align 4
  %115 = icmp slt i32 %114, %111
  br i1 %115, label %cont_check43, label %cont_end45

cont_check43:                                     ; preds = %cont_loop42
  %116 = getelementptr inbounds ptr, ptr %112, i32 %114
  %117 = load ptr, ptr %116, align 8
  %118 = call i32 @strcmp(ptr @21, ptr %117)
  %str_eq48 = icmp eq i32 %118, 0
  br i1 %str_eq48, label %cont_found44, label %cont_next49

cont_found44:                                     ; preds = %cont_check43
  store i1 true, ptr %res_cont47, align 1
  br label %cont_end45

cont_end45:                                       ; preds = %cont_found44, %cont_loop42
  %119 = load i1, ptr %res_cont47, align 1
  %120 = select i1 %119, ptr @22, ptr @23
  %121 = call ptr @__aiml_str_concat(ptr @20, ptr %120)
  %print_null_check50 = icmp eq ptr %121, null
  %print_safe_str51 = select i1 %print_null_check50, ptr @print_null_str.6, ptr %121
  %print_call52 = call i32 (ptr, ...) @printf(ptr @24, ptr %print_safe_str51)
  %122 = call ptr @__aiml_int_to_string(i32 30)
  %123 = call ptr @__aiml_str_concat(ptr @25, ptr %122)
  %print_null_check53 = icmp eq ptr %123, null
  %print_safe_str54 = select i1 %print_null_check53, ptr @print_null_str.7, ptr %123
  %print_call55 = call i32 (ptr, ...) @printf(ptr @26, ptr %print_safe_str54)
  call void @exit(i32 0)
  ret void

cont_next49:                                      ; preds = %cont_check43
  %124 = add i32 %114, 1
  store i32 %124, ptr %i_cont46, align 4
  br label %cont_loop42
}

declare ptr @calloc(i64, i64)

declare ptr @malloc(i64)

declare i32 @printf(ptr, ...)

declare i32 @strcmp(ptr, ptr)

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

declare ptr @memmove(ptr, ptr, i64)

declare void @exit(i32)

attributes #0 = { noinline }
