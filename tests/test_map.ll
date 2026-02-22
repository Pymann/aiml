; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%map_string_i32 = type { i32, i32, ptr, ptr, ptr }

@__argc = internal global i32 0
@__argv = internal global ptr null
@0 = private unnamed_addr constant [16 x i8] c"Testing Maps...\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"Bob\00", align 1
@5 = private unnamed_addr constant [14 x i8] c"Alice Score: \00", align 1
@6 = private unnamed_addr constant [35 x i8] c"${ { call: [scores.get, \22Alice\22] }\00", align 1
@7 = private unnamed_addr constant [3 x i8] c" }\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [12 x i8] c"Bob Score: \00", align 1
@11 = private unnamed_addr constant [33 x i8] c"${ { call: [scores.get, \22Bob\22] }\00", align 1
@12 = private unnamed_addr constant [3 x i8] c" }\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [21 x i8] c"Size before remove: \00", align 1
@16 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@20 = private unnamed_addr constant [20 x i8] c"Size after remove: \00", align 1
@21 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@24 = private unnamed_addr constant [12 x i8] c"Has Alice? \00", align 1
@25 = private unnamed_addr constant [40 x i8] c"${ { call: [scores.contains, \22Alice\22] }\00", align 1
@26 = private unnamed_addr constant [3 x i8] c" }\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@28 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@29 = private unnamed_addr constant [10 x i8] c"Has Bob? \00", align 1
@30 = private unnamed_addr constant [38 x i8] c"${ { call: [scores.contains, \22Bob\22] }\00", align 1
@31 = private unnamed_addr constant [3 x i8] c" }\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@33 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@34 = private unnamed_addr constant [22 x i8] c"Math Test: 10 + 20 = \00", align 1
@35 = private unnamed_addr constant [19 x i8] c"${ { +: [10, 20] }\00", align 1
@36 = private unnamed_addr constant [3 x i8] c" }\00", align 1
@37 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@38 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@39 = private unnamed_addr constant [16 x i8] c"Testing Maps...\00", align 1
@40 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@42 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@43 = private unnamed_addr constant [4 x i8] c"Bob\00", align 1
@44 = private unnamed_addr constant [14 x i8] c"Alice Score: \00", align 1
@45 = private unnamed_addr constant [35 x i8] c"${ { call: [scores.get, \22Alice\22] }\00", align 1
@46 = private unnamed_addr constant [3 x i8] c" }\00", align 1
@47 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@48 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@49 = private unnamed_addr constant [12 x i8] c"Bob Score: \00", align 1
@50 = private unnamed_addr constant [33 x i8] c"${ { call: [scores.get, \22Bob\22] }\00", align 1
@51 = private unnamed_addr constant [3 x i8] c" }\00", align 1
@52 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@53 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@54 = private unnamed_addr constant [21 x i8] c"Size before remove: \00", align 1
@55 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@56 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@57 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@58 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@59 = private unnamed_addr constant [20 x i8] c"Size after remove: \00", align 1
@60 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@61 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@62 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@63 = private unnamed_addr constant [12 x i8] c"Has Alice? \00", align 1
@64 = private unnamed_addr constant [40 x i8] c"${ { call: [scores.contains, \22Alice\22] }\00", align 1
@65 = private unnamed_addr constant [3 x i8] c" }\00", align 1
@66 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@67 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@68 = private unnamed_addr constant [10 x i8] c"Has Bob? \00", align 1
@69 = private unnamed_addr constant [38 x i8] c"${ { call: [scores.contains, \22Bob\22] }\00", align 1
@70 = private unnamed_addr constant [3 x i8] c" }\00", align 1
@71 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@72 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@73 = private unnamed_addr constant [22 x i8] c"Math Test: 10 + 20 = \00", align 1
@74 = private unnamed_addr constant [19 x i8] c"${ { +: [10, 20] }\00", align 1
@75 = private unnamed_addr constant [3 x i8] c" }\00", align 1
@76 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@77 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() {
entry:
  %scores = alloca %map_string_i32, align 8
  store %map_string_i32 zeroinitializer, ptr %scores, align 8
  %sizePtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %scores, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %scores, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %0 = call ptr @calloc(i64 4, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %scores, i32 0, i32 2
  store ptr %0, ptr %occPtr, align 8
  %1 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %scores, i32 0, i32 3
  store ptr %1, ptr %keysPtr, align 8
  %2 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %scores, i32 0, i32 4
  store ptr %2, ptr %valsPtr, align 8
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr @0)
  %sizePtr1 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %capPtr2 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 1
  %size = load i32, ptr %sizePtr1, align 4
  %capacity = load i32, ptr %capPtr2, align 4
  %3 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %keysBase = load ptr, ptr %3, align 8
  %4 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %valsBase = load ptr, ptr %4, align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

put_loop:                                         ; preds = %put_next, %entry
  %5 = load i32, ptr %i_put, align 4
  %6 = icmp slt i32 %5, %size
  br i1 %6, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %7 = getelementptr inbounds ptr, ptr %keysBase, i32 %5
  %8 = load ptr, ptr %7, align 8
  %9 = call i32 @strcmp(ptr @3, ptr %8)
  %str_eq = icmp eq i32 %9, 0
  br i1 %str_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %10 = getelementptr inbounds i32, ptr %valsBase, i32 %5
  store i32 100, ptr %10, align 4
  br label %put_end

put_next:                                         ; preds = %put_check
  %11 = add i32 %5, 1
  store i32 %11, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %12 = icmp eq i32 %size, %capacity
  br i1 %12, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %sizePtr3 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %capPtr4 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 1
  %size5 = load i32, ptr %sizePtr3, align 4
  %capacity6 = load i32, ptr %capPtr4, align 4
  %13 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %keysBase7 = load ptr, ptr %13, align 8
  %14 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %valsBase8 = load ptr, ptr %14, align 8
  %i_put15 = alloca i32, align 4
  store i32 0, ptr %i_put15, align 4
  br label %put_loop9

put_grow:                                         ; preds = %put_not_found
  %15 = mul i32 %capacity, 2
  store i32 %15, ptr %capPtr2, align 4
  %16 = zext i32 %15 to i64
  %17 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %16
  %18 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %19 = load ptr, ptr %18, align 8
  %20 = call ptr @realloc(ptr %19, i64 %17)
  store ptr %20, ptr %18, align 8
  %21 = zext i32 %15 to i64
  %22 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %21
  %23 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %24 = load ptr, ptr %23, align 8
  %25 = call ptr @realloc(ptr %24, i64 %22)
  store ptr %25, ptr %23, align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %26 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %27 = load ptr, ptr %26, align 8
  %28 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %29 = load ptr, ptr %28, align 8
  %30 = getelementptr inbounds ptr, ptr %27, i32 %size
  store ptr @3, ptr %30, align 8
  %31 = getelementptr inbounds i32, ptr %29, i32 %size
  store i32 100, ptr %31, align 4
  %32 = add i32 %size, 1
  store i32 %32, ptr %sizePtr1, align 4
  br label %put_end

put_loop9:                                        ; preds = %put_next12, %put_end
  %33 = load i32, ptr %i_put15, align 4
  %34 = icmp slt i32 %33, %size5
  br i1 %34, label %put_check10, label %put_not_found13

put_check10:                                      ; preds = %put_loop9
  %35 = getelementptr inbounds ptr, ptr %keysBase7, i32 %33
  %36 = load ptr, ptr %35, align 8
  %37 = call i32 @strcmp(ptr @4, ptr %36)
  %str_eq16 = icmp eq i32 %37, 0
  br i1 %str_eq16, label %put_found11, label %put_next12

put_found11:                                      ; preds = %put_check10
  %38 = getelementptr inbounds i32, ptr %valsBase8, i32 %33
  store i32 200, ptr %38, align 4
  br label %put_end14

put_next12:                                       ; preds = %put_check10
  %39 = add i32 %33, 1
  store i32 %39, ptr %i_put15, align 4
  br label %put_loop9

put_not_found13:                                  ; preds = %put_loop9
  %40 = icmp eq i32 %size5, %capacity6
  br i1 %40, label %put_grow17, label %put_append18

put_end14:                                        ; preds = %put_append18, %put_found11
  %41 = call i64 @strlen(ptr @5)
  %42 = call i64 @strlen(ptr @6)
  %43 = add i64 %41, %42
  %44 = add i64 %43, 1
  %45 = call ptr @malloc(i64 %44)
  %46 = call ptr @strcpy(ptr %45, ptr @5)
  %47 = call ptr @strcat(ptr %45, ptr @6)
  %48 = call i64 @strlen(ptr %45)
  %49 = call i64 @strlen(ptr @7)
  %50 = add i64 %48, %49
  %51 = add i64 %50, 1
  %52 = call ptr @malloc(i64 %51)
  %53 = call ptr @strcpy(ptr %52, ptr %45)
  %54 = call ptr @strcat(ptr %52, ptr @7)
  %print_call19 = call i32 (ptr, ...) @printf(ptr @9, ptr %52)
  %55 = call i64 @strlen(ptr @10)
  %56 = call i64 @strlen(ptr @11)
  %57 = add i64 %55, %56
  %58 = add i64 %57, 1
  %59 = call ptr @malloc(i64 %58)
  %60 = call ptr @strcpy(ptr %59, ptr @10)
  %61 = call ptr @strcat(ptr %59, ptr @11)
  %62 = call i64 @strlen(ptr %59)
  %63 = call i64 @strlen(ptr @12)
  %64 = add i64 %62, %63
  %65 = add i64 %64, 1
  %66 = call ptr @malloc(i64 %65)
  %67 = call ptr @strcpy(ptr %66, ptr %59)
  %68 = call ptr @strcat(ptr %66, ptr @12)
  %print_call20 = call i32 (ptr, ...) @printf(ptr @14, ptr %66)
  %sizePtr21 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %size_val = load i32, ptr %sizePtr21, align 4
  %69 = call ptr @malloc(i64 12)
  %70 = call i32 (ptr, ptr, ...) @sprintf(ptr %69, ptr @16, i32 %size_val)
  %71 = call i64 @strlen(ptr @15)
  %72 = call i64 @strlen(ptr %69)
  %73 = add i64 %71, %72
  %74 = add i64 %73, 1
  %75 = call ptr @malloc(i64 %74)
  %76 = call ptr @strcpy(ptr %75, ptr @15)
  %77 = call ptr @strcat(ptr %75, ptr %69)
  %print_call22 = call i32 (ptr, ...) @printf(ptr @18, ptr %75)
  %sizePtr23 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %78 = load i32, ptr %sizePtr23, align 4
  %79 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %80 = load ptr, ptr %79, align 8
  %81 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %82 = load ptr, ptr %81, align 8
  %i_rm = alloca i32, align 4
  store i32 0, ptr %i_rm, align 4
  br label %rm_loop

put_grow17:                                       ; preds = %put_not_found13
  %83 = mul i32 %capacity6, 2
  store i32 %83, ptr %capPtr4, align 4
  %84 = zext i32 %83 to i64
  %85 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %84
  %86 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %87 = load ptr, ptr %86, align 8
  %88 = call ptr @realloc(ptr %87, i64 %85)
  store ptr %88, ptr %86, align 8
  %89 = zext i32 %83 to i64
  %90 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %89
  %91 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %92 = load ptr, ptr %91, align 8
  %93 = call ptr @realloc(ptr %92, i64 %90)
  store ptr %93, ptr %91, align 8
  br label %put_append18

put_append18:                                     ; preds = %put_grow17, %put_not_found13
  %94 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %95 = load ptr, ptr %94, align 8
  %96 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %97 = load ptr, ptr %96, align 8
  %98 = getelementptr inbounds ptr, ptr %95, i32 %size5
  store ptr @4, ptr %98, align 8
  %99 = getelementptr inbounds i32, ptr %97, i32 %size5
  store i32 200, ptr %99, align 4
  %100 = add i32 %size5, 1
  store i32 %100, ptr %sizePtr3, align 4
  br label %put_end14

rm_loop:                                          ; preds = %rm_next, %put_end14
  %101 = load i32, ptr %i_rm, align 4
  %102 = icmp slt i32 %101, %78
  br i1 %102, label %rm_check, label %rm_end

rm_check:                                         ; preds = %rm_loop
  %103 = getelementptr inbounds ptr, ptr %80, i32 %101
  %104 = load ptr, ptr %103, align 8
  %105 = call i32 @strcmp(ptr @19, ptr %104)
  %str_eq24 = icmp eq i32 %105, 0
  br i1 %str_eq24, label %rm_found, label %rm_next

rm_found:                                         ; preds = %rm_check
  %106 = add i32 %101, 1
  %107 = sub i32 %78, %106
  %108 = getelementptr inbounds ptr, ptr %80, i32 %101
  %109 = getelementptr inbounds ptr, ptr %80, i32 %106
  %110 = zext i32 %107 to i64
  %111 = mul i64 %110, ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64)
  %112 = call ptr @memmove(ptr %108, ptr %109, i64 %111)
  %113 = getelementptr inbounds %map_string_i32, ptr %82, i32 %101
  %114 = getelementptr inbounds %map_string_i32, ptr %82, i32 %106
  %115 = zext i32 %107 to i64
  %116 = mul i64 %115, ptrtoint (ptr getelementptr inbounds (%map_string_i32, ptr null, i32 1) to i64)
  %117 = call ptr @memmove(ptr %113, ptr %114, i64 %116)
  %118 = sub i32 %78, 1
  store i32 %118, ptr %sizePtr23, align 4
  br label %rm_end

rm_end:                                           ; preds = %rm_found, %rm_loop
  %sizePtr25 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %size_val26 = load i32, ptr %sizePtr25, align 4
  %119 = call ptr @malloc(i64 12)
  %120 = call i32 (ptr, ptr, ...) @sprintf(ptr %119, ptr @21, i32 %size_val26)
  %121 = call i64 @strlen(ptr @20)
  %122 = call i64 @strlen(ptr %119)
  %123 = add i64 %121, %122
  %124 = add i64 %123, 1
  %125 = call ptr @malloc(i64 %124)
  %126 = call ptr @strcpy(ptr %125, ptr @20)
  %127 = call ptr @strcat(ptr %125, ptr %119)
  %print_call27 = call i32 (ptr, ...) @printf(ptr @23, ptr %125)
  %128 = call i64 @strlen(ptr @24)
  %129 = call i64 @strlen(ptr @25)
  %130 = add i64 %128, %129
  %131 = add i64 %130, 1
  %132 = call ptr @malloc(i64 %131)
  %133 = call ptr @strcpy(ptr %132, ptr @24)
  %134 = call ptr @strcat(ptr %132, ptr @25)
  %135 = call i64 @strlen(ptr %132)
  %136 = call i64 @strlen(ptr @26)
  %137 = add i64 %135, %136
  %138 = add i64 %137, 1
  %139 = call ptr @malloc(i64 %138)
  %140 = call ptr @strcpy(ptr %139, ptr %132)
  %141 = call ptr @strcat(ptr %139, ptr @26)
  %print_call28 = call i32 (ptr, ...) @printf(ptr @28, ptr %139)
  %142 = call i64 @strlen(ptr @29)
  %143 = call i64 @strlen(ptr @30)
  %144 = add i64 %142, %143
  %145 = add i64 %144, 1
  %146 = call ptr @malloc(i64 %145)
  %147 = call ptr @strcpy(ptr %146, ptr @29)
  %148 = call ptr @strcat(ptr %146, ptr @30)
  %149 = call i64 @strlen(ptr %146)
  %150 = call i64 @strlen(ptr @31)
  %151 = add i64 %149, %150
  %152 = add i64 %151, 1
  %153 = call ptr @malloc(i64 %152)
  %154 = call ptr @strcpy(ptr %153, ptr %146)
  %155 = call ptr @strcat(ptr %153, ptr @31)
  %print_call29 = call i32 (ptr, ...) @printf(ptr @33, ptr %153)
  %156 = call i64 @strlen(ptr @34)
  %157 = call i64 @strlen(ptr @35)
  %158 = add i64 %156, %157
  %159 = add i64 %158, 1
  %160 = call ptr @malloc(i64 %159)
  %161 = call ptr @strcpy(ptr %160, ptr @34)
  %162 = call ptr @strcat(ptr %160, ptr @35)
  %163 = call i64 @strlen(ptr %160)
  %164 = call i64 @strlen(ptr @36)
  %165 = add i64 %163, %164
  %166 = add i64 %165, 1
  %167 = call ptr @malloc(i64 %166)
  %168 = call ptr @strcpy(ptr %167, ptr %160)
  %169 = call ptr @strcat(ptr %167, ptr @36)
  %print_call30 = call i32 (ptr, ...) @printf(ptr @38, ptr %167)
  call void @exit(i32 0)
  ret void

rm_next:                                          ; preds = %rm_check
  %170 = add i32 %101, 1
  store i32 %170, ptr %i_rm, align 4
  br label %rm_loop
}

declare ptr @calloc(i64, i64)

declare ptr @malloc(i64)

declare i32 @printf(ptr, ...)

declare i32 @strcmp(ptr, ptr)

declare ptr @realloc(ptr, i64)

declare i64 @strlen(ptr)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare i32 @sprintf(ptr, ptr, ...)

declare ptr @memmove(ptr, ptr, i64)

declare void @exit(i32)

define i32 @main(i32 %argc, ptr %argv) {
entry:
  store i32 %argc, ptr @__argc, align 4
  store ptr %argv, ptr @__argv, align 8
  call void @__aiml_init()
  ret i32 0
}

define void @__aiml_init.1() {
entry:
  %scores = alloca %map_string_i32, align 8
  store %map_string_i32 zeroinitializer, ptr %scores, align 8
  %sizePtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %scores, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %scores, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %0 = call ptr @calloc(i64 4, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %scores, i32 0, i32 2
  store ptr %0, ptr %occPtr, align 8
  %1 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %scores, i32 0, i32 3
  store ptr %1, ptr %keysPtr, align 8
  %2 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %scores, i32 0, i32 4
  store ptr %2, ptr %valsPtr, align 8
  %print_call = call i32 (ptr, ...) @printf(ptr @41, ptr @39)
  %sizePtr1 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %capPtr2 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 1
  %size = load i32, ptr %sizePtr1, align 4
  %capacity = load i32, ptr %capPtr2, align 4
  %3 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %keysBase = load ptr, ptr %3, align 8
  %4 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %valsBase = load ptr, ptr %4, align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

put_loop:                                         ; preds = %put_next, %entry
  %5 = load i32, ptr %i_put, align 4
  %6 = icmp slt i32 %5, %size
  br i1 %6, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %7 = getelementptr inbounds ptr, ptr %keysBase, i32 %5
  %8 = load ptr, ptr %7, align 8
  %9 = call i32 @strcmp(ptr @42, ptr %8)
  %str_eq = icmp eq i32 %9, 0
  br i1 %str_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %10 = getelementptr inbounds i32, ptr %valsBase, i32 %5
  store i32 100, ptr %10, align 4
  br label %put_end

put_next:                                         ; preds = %put_check
  %11 = add i32 %5, 1
  store i32 %11, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %12 = icmp eq i32 %size, %capacity
  br i1 %12, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %sizePtr3 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %capPtr4 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 1
  %size5 = load i32, ptr %sizePtr3, align 4
  %capacity6 = load i32, ptr %capPtr4, align 4
  %13 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %keysBase7 = load ptr, ptr %13, align 8
  %14 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %valsBase8 = load ptr, ptr %14, align 8
  %i_put15 = alloca i32, align 4
  store i32 0, ptr %i_put15, align 4
  br label %put_loop9

put_grow:                                         ; preds = %put_not_found
  %15 = mul i32 %capacity, 2
  store i32 %15, ptr %capPtr2, align 4
  %16 = zext i32 %15 to i64
  %17 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %16
  %18 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %19 = load ptr, ptr %18, align 8
  %20 = call ptr @realloc(ptr %19, i64 %17)
  store ptr %20, ptr %18, align 8
  %21 = zext i32 %15 to i64
  %22 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %21
  %23 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %24 = load ptr, ptr %23, align 8
  %25 = call ptr @realloc(ptr %24, i64 %22)
  store ptr %25, ptr %23, align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %26 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %27 = load ptr, ptr %26, align 8
  %28 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %29 = load ptr, ptr %28, align 8
  %30 = getelementptr inbounds ptr, ptr %27, i32 %size
  store ptr @42, ptr %30, align 8
  %31 = getelementptr inbounds i32, ptr %29, i32 %size
  store i32 100, ptr %31, align 4
  %32 = add i32 %size, 1
  store i32 %32, ptr %sizePtr1, align 4
  br label %put_end

put_loop9:                                        ; preds = %put_next12, %put_end
  %33 = load i32, ptr %i_put15, align 4
  %34 = icmp slt i32 %33, %size5
  br i1 %34, label %put_check10, label %put_not_found13

put_check10:                                      ; preds = %put_loop9
  %35 = getelementptr inbounds ptr, ptr %keysBase7, i32 %33
  %36 = load ptr, ptr %35, align 8
  %37 = call i32 @strcmp(ptr @43, ptr %36)
  %str_eq16 = icmp eq i32 %37, 0
  br i1 %str_eq16, label %put_found11, label %put_next12

put_found11:                                      ; preds = %put_check10
  %38 = getelementptr inbounds i32, ptr %valsBase8, i32 %33
  store i32 200, ptr %38, align 4
  br label %put_end14

put_next12:                                       ; preds = %put_check10
  %39 = add i32 %33, 1
  store i32 %39, ptr %i_put15, align 4
  br label %put_loop9

put_not_found13:                                  ; preds = %put_loop9
  %40 = icmp eq i32 %size5, %capacity6
  br i1 %40, label %put_grow17, label %put_append18

put_end14:                                        ; preds = %put_append18, %put_found11
  %41 = call i64 @strlen(ptr @44)
  %42 = call i64 @strlen(ptr @45)
  %43 = add i64 %41, %42
  %44 = add i64 %43, 1
  %45 = call ptr @malloc(i64 %44)
  %46 = call ptr @strcpy(ptr %45, ptr @44)
  %47 = call ptr @strcat(ptr %45, ptr @45)
  %48 = call i64 @strlen(ptr %45)
  %49 = call i64 @strlen(ptr @46)
  %50 = add i64 %48, %49
  %51 = add i64 %50, 1
  %52 = call ptr @malloc(i64 %51)
  %53 = call ptr @strcpy(ptr %52, ptr %45)
  %54 = call ptr @strcat(ptr %52, ptr @46)
  %print_call19 = call i32 (ptr, ...) @printf(ptr @48, ptr %52)
  %55 = call i64 @strlen(ptr @49)
  %56 = call i64 @strlen(ptr @50)
  %57 = add i64 %55, %56
  %58 = add i64 %57, 1
  %59 = call ptr @malloc(i64 %58)
  %60 = call ptr @strcpy(ptr %59, ptr @49)
  %61 = call ptr @strcat(ptr %59, ptr @50)
  %62 = call i64 @strlen(ptr %59)
  %63 = call i64 @strlen(ptr @51)
  %64 = add i64 %62, %63
  %65 = add i64 %64, 1
  %66 = call ptr @malloc(i64 %65)
  %67 = call ptr @strcpy(ptr %66, ptr %59)
  %68 = call ptr @strcat(ptr %66, ptr @51)
  %print_call20 = call i32 (ptr, ...) @printf(ptr @53, ptr %66)
  %sizePtr21 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %size_val = load i32, ptr %sizePtr21, align 4
  %69 = call ptr @malloc(i64 12)
  %70 = call i32 (ptr, ptr, ...) @sprintf(ptr %69, ptr @55, i32 %size_val)
  %71 = call i64 @strlen(ptr @54)
  %72 = call i64 @strlen(ptr %69)
  %73 = add i64 %71, %72
  %74 = add i64 %73, 1
  %75 = call ptr @malloc(i64 %74)
  %76 = call ptr @strcpy(ptr %75, ptr @54)
  %77 = call ptr @strcat(ptr %75, ptr %69)
  %print_call22 = call i32 (ptr, ...) @printf(ptr @57, ptr %75)
  %sizePtr23 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %78 = load i32, ptr %sizePtr23, align 4
  %79 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %80 = load ptr, ptr %79, align 8
  %81 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %82 = load ptr, ptr %81, align 8
  %i_rm = alloca i32, align 4
  store i32 0, ptr %i_rm, align 4
  br label %rm_loop

put_grow17:                                       ; preds = %put_not_found13
  %83 = mul i32 %capacity6, 2
  store i32 %83, ptr %capPtr4, align 4
  %84 = zext i32 %83 to i64
  %85 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %84
  %86 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %87 = load ptr, ptr %86, align 8
  %88 = call ptr @realloc(ptr %87, i64 %85)
  store ptr %88, ptr %86, align 8
  %89 = zext i32 %83 to i64
  %90 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %89
  %91 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %92 = load ptr, ptr %91, align 8
  %93 = call ptr @realloc(ptr %92, i64 %90)
  store ptr %93, ptr %91, align 8
  br label %put_append18

put_append18:                                     ; preds = %put_grow17, %put_not_found13
  %94 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %95 = load ptr, ptr %94, align 8
  %96 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %97 = load ptr, ptr %96, align 8
  %98 = getelementptr inbounds ptr, ptr %95, i32 %size5
  store ptr @43, ptr %98, align 8
  %99 = getelementptr inbounds i32, ptr %97, i32 %size5
  store i32 200, ptr %99, align 4
  %100 = add i32 %size5, 1
  store i32 %100, ptr %sizePtr3, align 4
  br label %put_end14

rm_loop:                                          ; preds = %rm_next, %put_end14
  %101 = load i32, ptr %i_rm, align 4
  %102 = icmp slt i32 %101, %78
  br i1 %102, label %rm_check, label %rm_end

rm_check:                                         ; preds = %rm_loop
  %103 = getelementptr inbounds ptr, ptr %80, i32 %101
  %104 = load ptr, ptr %103, align 8
  %105 = call i32 @strcmp(ptr @58, ptr %104)
  %str_eq24 = icmp eq i32 %105, 0
  br i1 %str_eq24, label %rm_found, label %rm_next

rm_found:                                         ; preds = %rm_check
  %106 = add i32 %101, 1
  %107 = sub i32 %78, %106
  %108 = getelementptr inbounds ptr, ptr %80, i32 %101
  %109 = getelementptr inbounds ptr, ptr %80, i32 %106
  %110 = zext i32 %107 to i64
  %111 = mul i64 %110, ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64)
  %112 = call ptr @memmove(ptr %108, ptr %109, i64 %111)
  %113 = getelementptr inbounds %map_string_i32, ptr %82, i32 %101
  %114 = getelementptr inbounds %map_string_i32, ptr %82, i32 %106
  %115 = zext i32 %107 to i64
  %116 = mul i64 %115, ptrtoint (ptr getelementptr inbounds (%map_string_i32, ptr null, i32 1) to i64)
  %117 = call ptr @memmove(ptr %113, ptr %114, i64 %116)
  %118 = sub i32 %78, 1
  store i32 %118, ptr %sizePtr23, align 4
  br label %rm_end

rm_end:                                           ; preds = %rm_found, %rm_loop
  %sizePtr25 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %size_val26 = load i32, ptr %sizePtr25, align 4
  %119 = call ptr @malloc(i64 12)
  %120 = call i32 (ptr, ptr, ...) @sprintf(ptr %119, ptr @60, i32 %size_val26)
  %121 = call i64 @strlen(ptr @59)
  %122 = call i64 @strlen(ptr %119)
  %123 = add i64 %121, %122
  %124 = add i64 %123, 1
  %125 = call ptr @malloc(i64 %124)
  %126 = call ptr @strcpy(ptr %125, ptr @59)
  %127 = call ptr @strcat(ptr %125, ptr %119)
  %print_call27 = call i32 (ptr, ...) @printf(ptr @62, ptr %125)
  %128 = call i64 @strlen(ptr @63)
  %129 = call i64 @strlen(ptr @64)
  %130 = add i64 %128, %129
  %131 = add i64 %130, 1
  %132 = call ptr @malloc(i64 %131)
  %133 = call ptr @strcpy(ptr %132, ptr @63)
  %134 = call ptr @strcat(ptr %132, ptr @64)
  %135 = call i64 @strlen(ptr %132)
  %136 = call i64 @strlen(ptr @65)
  %137 = add i64 %135, %136
  %138 = add i64 %137, 1
  %139 = call ptr @malloc(i64 %138)
  %140 = call ptr @strcpy(ptr %139, ptr %132)
  %141 = call ptr @strcat(ptr %139, ptr @65)
  %print_call28 = call i32 (ptr, ...) @printf(ptr @67, ptr %139)
  %142 = call i64 @strlen(ptr @68)
  %143 = call i64 @strlen(ptr @69)
  %144 = add i64 %142, %143
  %145 = add i64 %144, 1
  %146 = call ptr @malloc(i64 %145)
  %147 = call ptr @strcpy(ptr %146, ptr @68)
  %148 = call ptr @strcat(ptr %146, ptr @69)
  %149 = call i64 @strlen(ptr %146)
  %150 = call i64 @strlen(ptr @70)
  %151 = add i64 %149, %150
  %152 = add i64 %151, 1
  %153 = call ptr @malloc(i64 %152)
  %154 = call ptr @strcpy(ptr %153, ptr %146)
  %155 = call ptr @strcat(ptr %153, ptr @70)
  %print_call29 = call i32 (ptr, ...) @printf(ptr @72, ptr %153)
  %156 = call i64 @strlen(ptr @73)
  %157 = call i64 @strlen(ptr @74)
  %158 = add i64 %156, %157
  %159 = add i64 %158, 1
  %160 = call ptr @malloc(i64 %159)
  %161 = call ptr @strcpy(ptr %160, ptr @73)
  %162 = call ptr @strcat(ptr %160, ptr @74)
  %163 = call i64 @strlen(ptr %160)
  %164 = call i64 @strlen(ptr @75)
  %165 = add i64 %163, %164
  %166 = add i64 %165, 1
  %167 = call ptr @malloc(i64 %166)
  %168 = call ptr @strcpy(ptr %167, ptr %160)
  %169 = call ptr @strcat(ptr %167, ptr @75)
  %print_call30 = call i32 (ptr, ...) @printf(ptr @77, ptr %167)
  call void @exit(i32 0)
  ret void

rm_next:                                          ; preds = %rm_check
  %170 = add i32 %101, 1
  store i32 %170, ptr %i_rm, align 4
  br label %rm_loop
}
