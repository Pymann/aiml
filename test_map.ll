; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"

%map_string_i32 = type { i32, i32, ptr, ptr, ptr }

@0 = private unnamed_addr constant [16 x i8] c"Testing Maps...\00", align 1
@fmt_str = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@1 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@2 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@3 = private unnamed_addr constant [4 x i8] c"Bob\00", align 1
@4 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@5 = private unnamed_addr constant [14 x i8] c"Alice Score: \00", align 1
@6 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@7 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.1 = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@8 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@9 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@10 = private unnamed_addr constant [12 x i8] c"Bob Score: \00", align 1
@11 = private unnamed_addr constant [4 x i8] c"Bob\00", align 1
@12 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.2 = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@13 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@14 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@15 = private unnamed_addr constant [21 x i8] c"Size before remove: \00", align 1
@16 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.3 = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@17 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@18 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@19 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@20 = private unnamed_addr constant [20 x i8] c"Size after remove: \00", align 1
@21 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.4 = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@22 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@23 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@24 = private unnamed_addr constant [12 x i8] c"Has Alice? \00", align 1
@25 = private unnamed_addr constant [6 x i8] c"Alice\00", align 1
@26 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.5 = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@27 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@28 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@29 = private unnamed_addr constant [10 x i8] c"Has Bob? \00", align 1
@30 = private unnamed_addr constant [4 x i8] c"Bob\00", align 1
@31 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.6 = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@32 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@33 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@34 = private unnamed_addr constant [22 x i8] c"Math Test: 10 + 20 = \00", align 1
@35 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.7 = private unnamed_addr constant [3 x i8] c"%s\00", align 1
@36 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define i32 @main() {
entry:
  %scores = alloca %map_string_i32, align 8
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
  %3 = call i32 (ptr, ...) @printf(ptr @fmt_str, ptr @0)
  %4 = call i32 (ptr, ...) @printf(ptr @1)
  %sizePtr1 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %capPtr2 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 1
  %size = load i32, ptr %sizePtr1, align 4
  %capacity = load i32, ptr %capPtr2, align 4
  %5 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %keysBase = load ptr, ptr %5, align 8
  %6 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %valsBase = load ptr, ptr %6, align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

put_loop:                                         ; preds = %put_next, %entry
  %7 = load i32, ptr %i_put, align 4
  %8 = icmp slt i32 %7, %size
  br i1 %8, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %9 = getelementptr inbounds ptr, ptr %keysBase, i32 %7
  %10 = load ptr, ptr %9, align 8
  %11 = call i32 @strcmp(ptr @2, ptr %10)
  %str_eq = icmp eq i32 %11, 0
  br i1 %str_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %12 = getelementptr inbounds i32, ptr %valsBase, i32 %7
  store i32 100, ptr %12, align 4
  br label %put_end

put_next:                                         ; preds = %put_check
  %13 = add i32 %7, 1
  store i32 %13, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %14 = icmp eq i32 %size, %capacity
  br i1 %14, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %sizePtr3 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %capPtr4 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 1
  %size5 = load i32, ptr %sizePtr3, align 4
  %capacity6 = load i32, ptr %capPtr4, align 4
  %15 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %keysBase7 = load ptr, ptr %15, align 8
  %16 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %valsBase8 = load ptr, ptr %16, align 8
  %i_put15 = alloca i32, align 4
  store i32 0, ptr %i_put15, align 4
  br label %put_loop9

put_grow:                                         ; preds = %put_not_found
  %17 = mul i32 %capacity, 2
  store i32 %17, ptr %capPtr2, align 4
  %18 = zext i32 %17 to i64
  %19 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %18
  %20 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %21 = load ptr, ptr %20, align 8
  %22 = call ptr @realloc(ptr %21, i64 %19)
  store ptr %22, ptr %20, align 8
  %23 = zext i32 %17 to i64
  %24 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %23
  %25 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %26 = load ptr, ptr %25, align 8
  %27 = call ptr @realloc(ptr %26, i64 %24)
  store ptr %27, ptr %25, align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %28 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %29 = load ptr, ptr %28, align 8
  %30 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %31 = load ptr, ptr %30, align 8
  %32 = getelementptr inbounds ptr, ptr %29, i32 %size
  store ptr @2, ptr %32, align 8
  %33 = getelementptr inbounds i32, ptr %31, i32 %size
  store i32 100, ptr %33, align 4
  %34 = add i32 %size, 1
  store i32 %34, ptr %sizePtr1, align 4
  br label %put_end

put_loop9:                                        ; preds = %put_next12, %put_end
  %35 = load i32, ptr %i_put15, align 4
  %36 = icmp slt i32 %35, %size5
  br i1 %36, label %put_check10, label %put_not_found13

put_check10:                                      ; preds = %put_loop9
  %37 = getelementptr inbounds ptr, ptr %keysBase7, i32 %35
  %38 = load ptr, ptr %37, align 8
  %39 = call i32 @strcmp(ptr @3, ptr %38)
  %str_eq16 = icmp eq i32 %39, 0
  br i1 %str_eq16, label %put_found11, label %put_next12

put_found11:                                      ; preds = %put_check10
  %40 = getelementptr inbounds i32, ptr %valsBase8, i32 %35
  store i32 200, ptr %40, align 4
  br label %put_end14

put_next12:                                       ; preds = %put_check10
  %41 = add i32 %35, 1
  store i32 %41, ptr %i_put15, align 4
  br label %put_loop9

put_not_found13:                                  ; preds = %put_loop9
  %42 = icmp eq i32 %size5, %capacity6
  br i1 %42, label %put_grow17, label %put_append18

put_end14:                                        ; preds = %put_append18, %put_found11
  %43 = call i64 @strlen(ptr @4)
  %44 = call i64 @strlen(ptr @5)
  %45 = add i64 %43, %44
  %46 = add i64 %45, 1
  %47 = call ptr @malloc(i64 %46)
  %48 = call ptr @strcpy(ptr %47, ptr @4)
  %49 = call ptr @strcat(ptr %47, ptr @5)
  %50 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %51 = load i32, ptr %50, align 4
  %52 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %53 = load ptr, ptr %52, align 8
  %54 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %55 = load ptr, ptr %54, align 8
  %i_get = alloca i32, align 4
  store i32 0, ptr %i_get, align 4
  %res_get = alloca i32, align 4
  store i32 0, ptr %res_get, align 4
  br label %get_loop

put_grow17:                                       ; preds = %put_not_found13
  %56 = mul i32 %capacity6, 2
  store i32 %56, ptr %capPtr4, align 4
  %57 = zext i32 %56 to i64
  %58 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %57
  %59 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %60 = load ptr, ptr %59, align 8
  %61 = call ptr @realloc(ptr %60, i64 %58)
  store ptr %61, ptr %59, align 8
  %62 = zext i32 %56 to i64
  %63 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %62
  %64 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %65 = load ptr, ptr %64, align 8
  %66 = call ptr @realloc(ptr %65, i64 %63)
  store ptr %66, ptr %64, align 8
  br label %put_append18

put_append18:                                     ; preds = %put_grow17, %put_not_found13
  %67 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %68 = load ptr, ptr %67, align 8
  %69 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %70 = load ptr, ptr %69, align 8
  %71 = getelementptr inbounds ptr, ptr %68, i32 %size5
  store ptr @3, ptr %71, align 8
  %72 = getelementptr inbounds i32, ptr %70, i32 %size5
  store i32 200, ptr %72, align 4
  %73 = add i32 %size5, 1
  store i32 %73, ptr %sizePtr3, align 4
  br label %put_end14

get_loop:                                         ; preds = %get_next, %put_end14
  %74 = load i32, ptr %i_get, align 4
  %75 = icmp slt i32 %74, %51
  br i1 %75, label %get_check, label %get_end

get_check:                                        ; preds = %get_loop
  %76 = getelementptr inbounds ptr, ptr %53, i32 %74
  %77 = load ptr, ptr %76, align 8
  %78 = call i32 @strcmp(ptr @6, ptr %77)
  %str_eq19 = icmp eq i32 %78, 0
  br i1 %str_eq19, label %get_found, label %get_next

get_found:                                        ; preds = %get_check
  %79 = getelementptr inbounds i32, ptr %55, i32 %74
  %80 = load i32, ptr %79, align 4
  store i32 %80, ptr %res_get, align 4
  br label %get_end

get_end:                                          ; preds = %get_found, %get_loop
  %get_res = load i32, ptr %res_get, align 4
  %81 = call ptr @malloc(i64 12)
  %82 = call i32 (ptr, ptr, ...) @sprintf(ptr %81, ptr @7, i32 %get_res)
  %83 = call i64 @strlen(ptr %47)
  %84 = call i64 @strlen(ptr %81)
  %85 = add i64 %83, %84
  %86 = add i64 %85, 1
  %87 = call ptr @malloc(i64 %86)
  %88 = call ptr @strcpy(ptr %87, ptr %47)
  %89 = call ptr @strcat(ptr %87, ptr %81)
  %90 = call i32 (ptr, ...) @printf(ptr @fmt_str.1, ptr %87)
  %91 = call i32 (ptr, ...) @printf(ptr @8)
  %92 = call i64 @strlen(ptr @9)
  %93 = call i64 @strlen(ptr @10)
  %94 = add i64 %92, %93
  %95 = add i64 %94, 1
  %96 = call ptr @malloc(i64 %95)
  %97 = call ptr @strcpy(ptr %96, ptr @9)
  %98 = call ptr @strcat(ptr %96, ptr @10)
  %99 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %100 = load i32, ptr %99, align 4
  %101 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %102 = load ptr, ptr %101, align 8
  %103 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %104 = load ptr, ptr %103, align 8
  %i_get24 = alloca i32, align 4
  store i32 0, ptr %i_get24, align 4
  %res_get25 = alloca i32, align 4
  store i32 0, ptr %res_get25, align 4
  br label %get_loop20

get_next:                                         ; preds = %get_check
  %105 = add i32 %74, 1
  store i32 %105, ptr %i_get, align 4
  br label %get_loop

get_loop20:                                       ; preds = %get_next27, %get_end
  %106 = load i32, ptr %i_get24, align 4
  %107 = icmp slt i32 %106, %100
  br i1 %107, label %get_check21, label %get_end23

get_check21:                                      ; preds = %get_loop20
  %108 = getelementptr inbounds ptr, ptr %102, i32 %106
  %109 = load ptr, ptr %108, align 8
  %110 = call i32 @strcmp(ptr @11, ptr %109)
  %str_eq26 = icmp eq i32 %110, 0
  br i1 %str_eq26, label %get_found22, label %get_next27

get_found22:                                      ; preds = %get_check21
  %111 = getelementptr inbounds i32, ptr %104, i32 %106
  %112 = load i32, ptr %111, align 4
  store i32 %112, ptr %res_get25, align 4
  br label %get_end23

get_end23:                                        ; preds = %get_found22, %get_loop20
  %get_res28 = load i32, ptr %res_get25, align 4
  %113 = call ptr @malloc(i64 12)
  %114 = call i32 (ptr, ptr, ...) @sprintf(ptr %113, ptr @12, i32 %get_res28)
  %115 = call i64 @strlen(ptr %96)
  %116 = call i64 @strlen(ptr %113)
  %117 = add i64 %115, %116
  %118 = add i64 %117, 1
  %119 = call ptr @malloc(i64 %118)
  %120 = call ptr @strcpy(ptr %119, ptr %96)
  %121 = call ptr @strcat(ptr %119, ptr %113)
  %122 = call i32 (ptr, ...) @printf(ptr @fmt_str.2, ptr %119)
  %123 = call i32 (ptr, ...) @printf(ptr @13)
  %124 = call i64 @strlen(ptr @14)
  %125 = call i64 @strlen(ptr @15)
  %126 = add i64 %124, %125
  %127 = add i64 %126, 1
  %128 = call ptr @malloc(i64 %127)
  %129 = call ptr @strcpy(ptr %128, ptr @14)
  %130 = call ptr @strcat(ptr %128, ptr @15)
  %sizePtr29 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %size_val = load i32, ptr %sizePtr29, align 4
  %131 = call ptr @malloc(i64 12)
  %132 = call i32 (ptr, ptr, ...) @sprintf(ptr %131, ptr @16, i32 %size_val)
  %133 = call i64 @strlen(ptr %128)
  %134 = call i64 @strlen(ptr %131)
  %135 = add i64 %133, %134
  %136 = add i64 %135, 1
  %137 = call ptr @malloc(i64 %136)
  %138 = call ptr @strcpy(ptr %137, ptr %128)
  %139 = call ptr @strcat(ptr %137, ptr %131)
  %140 = call i32 (ptr, ...) @printf(ptr @fmt_str.3, ptr %137)
  %141 = call i32 (ptr, ...) @printf(ptr @17)
  %sizePtr30 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %142 = load i32, ptr %sizePtr30, align 4
  %143 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %144 = load ptr, ptr %143, align 8
  %145 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 4
  %146 = load ptr, ptr %145, align 8
  %i_rm = alloca i32, align 4
  store i32 0, ptr %i_rm, align 4
  br label %rm_loop

get_next27:                                       ; preds = %get_check21
  %147 = add i32 %106, 1
  store i32 %147, ptr %i_get24, align 4
  br label %get_loop20

rm_loop:                                          ; preds = %rm_next, %get_end23
  %148 = load i32, ptr %i_rm, align 4
  %149 = icmp slt i32 %148, %142
  br i1 %149, label %rm_check, label %rm_end

rm_check:                                         ; preds = %rm_loop
  %150 = getelementptr inbounds ptr, ptr %144, i32 %148
  %151 = load ptr, ptr %150, align 8
  %152 = call i32 @strcmp(ptr @18, ptr %151)
  %str_eq31 = icmp eq i32 %152, 0
  br i1 %str_eq31, label %rm_found, label %rm_next

rm_found:                                         ; preds = %rm_check
  %153 = add i32 %148, 1
  %154 = sub i32 %142, %153
  %155 = getelementptr inbounds ptr, ptr %144, i32 %148
  %156 = getelementptr inbounds ptr, ptr %144, i32 %153
  %157 = zext i32 %154 to i64
  %158 = mul i64 %157, ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64)
  %159 = call ptr @memmove(ptr %155, ptr %156, i64 %158)
  %160 = getelementptr inbounds i32, ptr %146, i32 %148
  %161 = getelementptr inbounds i32, ptr %146, i32 %153
  %162 = zext i32 %154 to i64
  %163 = mul i64 %162, ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64)
  %164 = call ptr @memmove(ptr %160, ptr %161, i64 %163)
  %165 = sub i32 %142, 1
  store i32 %165, ptr %sizePtr30, align 4
  br label %rm_end

rm_end:                                           ; preds = %rm_found, %rm_loop
  %166 = call i64 @strlen(ptr @19)
  %167 = call i64 @strlen(ptr @20)
  %168 = add i64 %166, %167
  %169 = add i64 %168, 1
  %170 = call ptr @malloc(i64 %169)
  %171 = call ptr @strcpy(ptr %170, ptr @19)
  %172 = call ptr @strcat(ptr %170, ptr @20)
  %sizePtr32 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %size_val33 = load i32, ptr %sizePtr32, align 4
  %173 = call ptr @malloc(i64 12)
  %174 = call i32 (ptr, ptr, ...) @sprintf(ptr %173, ptr @21, i32 %size_val33)
  %175 = call i64 @strlen(ptr %170)
  %176 = call i64 @strlen(ptr %173)
  %177 = add i64 %175, %176
  %178 = add i64 %177, 1
  %179 = call ptr @malloc(i64 %178)
  %180 = call ptr @strcpy(ptr %179, ptr %170)
  %181 = call ptr @strcat(ptr %179, ptr %173)
  %182 = call i32 (ptr, ...) @printf(ptr @fmt_str.4, ptr %179)
  %183 = call i32 (ptr, ...) @printf(ptr @22)
  %184 = call i64 @strlen(ptr @23)
  %185 = call i64 @strlen(ptr @24)
  %186 = add i64 %184, %185
  %187 = add i64 %186, 1
  %188 = call ptr @malloc(i64 %187)
  %189 = call ptr @strcpy(ptr %188, ptr @23)
  %190 = call ptr @strcat(ptr %188, ptr @24)
  %191 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %192 = load i32, ptr %191, align 4
  %193 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %194 = load ptr, ptr %193, align 8
  %i_cont = alloca i32, align 4
  store i32 0, ptr %i_cont, align 4
  %res_cont = alloca i1, align 1
  store i1 false, ptr %res_cont, align 1
  br label %cont_loop

rm_next:                                          ; preds = %rm_check
  store i32 %153, ptr %i_rm, align 4
  br label %rm_loop

cont_loop:                                        ; preds = %cont_next, %rm_end
  %195 = load i32, ptr %i_cont, align 4
  %196 = icmp slt i32 %195, %192
  br i1 %196, label %cont_check, label %cont_end

cont_check:                                       ; preds = %cont_loop
  %197 = getelementptr inbounds ptr, ptr %194, i32 %195
  %198 = load ptr, ptr %197, align 8
  %199 = call i32 @strcmp(ptr @25, ptr %198)
  %str_eq34 = icmp eq i32 %199, 0
  br i1 %str_eq34, label %cont_found, label %cont_next

cont_found:                                       ; preds = %cont_check
  store i1 true, ptr %res_cont, align 1
  br label %cont_end

cont_end:                                         ; preds = %cont_found, %cont_loop
  %200 = load i1, ptr %res_cont, align 1
  %201 = zext i1 %200 to i32
  %202 = call ptr @malloc(i64 12)
  %203 = call i32 (ptr, ptr, ...) @sprintf(ptr %202, ptr @26, i32 %201)
  %204 = call i64 @strlen(ptr %188)
  %205 = call i64 @strlen(ptr %202)
  %206 = add i64 %204, %205
  %207 = add i64 %206, 1
  %208 = call ptr @malloc(i64 %207)
  %209 = call ptr @strcpy(ptr %208, ptr %188)
  %210 = call ptr @strcat(ptr %208, ptr %202)
  %211 = call i32 (ptr, ...) @printf(ptr @fmt_str.5, ptr %208)
  %212 = call i32 (ptr, ...) @printf(ptr @27)
  %213 = call i64 @strlen(ptr @28)
  %214 = call i64 @strlen(ptr @29)
  %215 = add i64 %213, %214
  %216 = add i64 %215, 1
  %217 = call ptr @malloc(i64 %216)
  %218 = call ptr @strcpy(ptr %217, ptr @28)
  %219 = call ptr @strcat(ptr %217, ptr @29)
  %220 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 0
  %221 = load i32, ptr %220, align 4
  %222 = getelementptr inbounds %map_string_i32, ptr %scores, i32 0, i32 3
  %223 = load ptr, ptr %222, align 8
  %i_cont39 = alloca i32, align 4
  store i32 0, ptr %i_cont39, align 4
  %res_cont40 = alloca i1, align 1
  store i1 false, ptr %res_cont40, align 1
  br label %cont_loop35

cont_next:                                        ; preds = %cont_check
  %224 = add i32 %195, 1
  store i32 %224, ptr %i_cont, align 4
  br label %cont_loop

cont_loop35:                                      ; preds = %cont_next42, %cont_end
  %225 = load i32, ptr %i_cont39, align 4
  %226 = icmp slt i32 %225, %221
  br i1 %226, label %cont_check36, label %cont_end38

cont_check36:                                     ; preds = %cont_loop35
  %227 = getelementptr inbounds ptr, ptr %223, i32 %225
  %228 = load ptr, ptr %227, align 8
  %229 = call i32 @strcmp(ptr @30, ptr %228)
  %str_eq41 = icmp eq i32 %229, 0
  br i1 %str_eq41, label %cont_found37, label %cont_next42

cont_found37:                                     ; preds = %cont_check36
  store i1 true, ptr %res_cont40, align 1
  br label %cont_end38

cont_end38:                                       ; preds = %cont_found37, %cont_loop35
  %230 = load i1, ptr %res_cont40, align 1
  %231 = zext i1 %230 to i32
  %232 = call ptr @malloc(i64 12)
  %233 = call i32 (ptr, ptr, ...) @sprintf(ptr %232, ptr @31, i32 %231)
  %234 = call i64 @strlen(ptr %217)
  %235 = call i64 @strlen(ptr %232)
  %236 = add i64 %234, %235
  %237 = add i64 %236, 1
  %238 = call ptr @malloc(i64 %237)
  %239 = call ptr @strcpy(ptr %238, ptr %217)
  %240 = call ptr @strcat(ptr %238, ptr %232)
  %241 = call i32 (ptr, ...) @printf(ptr @fmt_str.6, ptr %238)
  %242 = call i32 (ptr, ...) @printf(ptr @32)
  %243 = call i64 @strlen(ptr @33)
  %244 = call i64 @strlen(ptr @34)
  %245 = add i64 %243, %244
  %246 = add i64 %245, 1
  %247 = call ptr @malloc(i64 %246)
  %248 = call ptr @strcpy(ptr %247, ptr @33)
  %249 = call ptr @strcat(ptr %247, ptr @34)
  %250 = call ptr @malloc(i64 12)
  %251 = call i32 (ptr, ptr, ...) @sprintf(ptr %250, ptr @35, i32 30)
  %252 = call i64 @strlen(ptr %247)
  %253 = call i64 @strlen(ptr %250)
  %254 = add i64 %252, %253
  %255 = add i64 %254, 1
  %256 = call ptr @malloc(i64 %255)
  %257 = call ptr @strcpy(ptr %256, ptr %247)
  %258 = call ptr @strcat(ptr %256, ptr %250)
  %259 = call i32 (ptr, ...) @printf(ptr @fmt_str.7, ptr %256)
  %260 = call i32 (ptr, ...) @printf(ptr @36)
  call void @exit(i32 0)
  ret i32 0

cont_next42:                                      ; preds = %cont_check36
  %261 = add i32 %225, 1
  store i32 %261, ptr %i_cont39, align 4
  br label %cont_loop35
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
