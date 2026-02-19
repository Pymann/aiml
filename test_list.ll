; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%list_i32 = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }

@__argc = internal global i32 0
@__argv = internal global ptr null
@0 = private unnamed_addr constant [25 x i8] c"Testing Dynamic Lists...\00", align 1
@fmt_str = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@1 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@2 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@3 = private unnamed_addr constant [9 x i8] c"lst-0 = \00", align 1
@4 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.1 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@5 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@6 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@7 = private unnamed_addr constant [9 x i8] c"lst-1 = \00", align 1
@8 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.2 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@9 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@10 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@11 = private unnamed_addr constant [18 x i8] c"Size before pop: \00", align 1
@12 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.3 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@13 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@14 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@15 = private unnamed_addr constant [27 x i8] c"Popped element. New size: \00", align 1
@16 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.4 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@17 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@18 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@19 = private unnamed_addr constant [18 x i8] c"lst-0 after pop: \00", align 1
@20 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.5 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@21 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@22 = private unnamed_addr constant [18 x i8] c"Testing Insert...\00", align 1
@fmt_str.6 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@23 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@24 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@25 = private unnamed_addr constant [26 x i8] c"Inserted 999 at 0. Size: \00", align 1
@26 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.7 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@27 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@28 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@29 = private unnamed_addr constant [9 x i8] c"lst-0 = \00", align 1
@30 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.8 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@31 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@32 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@33 = private unnamed_addr constant [9 x i8] c"lst-1 = \00", align 1
@34 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.9 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@35 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@36 = private unnamed_addr constant [18 x i8] c"Testing Remove...\00", align 1
@fmt_str.10 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@37 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@38 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@39 = private unnamed_addr constant [21 x i8] c"Removed at 0. Size: \00", align 1
@40 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.11 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@41 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@42 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@43 = private unnamed_addr constant [9 x i8] c"lst-0 = \00", align 1
@44 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.12 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@45 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@46 = private unnamed_addr constant [23 x i8] c"Testing Clear/Empty...\00", align 1
@fmt_str.13 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@47 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@48 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@49 = private unnamed_addr constant [11 x i8] c"Is Empty? \00", align 1
@50 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.14 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@51 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@52 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@53 = private unnamed_addr constant [16 x i8] c"Cleared. Size: \00", align 1
@54 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.15 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@55 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1
@56 = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@57 = private unnamed_addr constant [11 x i8] c"Is Empty? \00", align 1
@58 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@fmt_str.16 = private unnamed_addr constant [4 x i8] c"%s \00", align 1
@59 = private unnamed_addr constant [2 x i8] c"\0A\00", align 1

define i32 @main(i32 %argc, ptr %argv) personality ptr @__gxx_personality_v0 {
entry:
  store i32 %argc, ptr @__argc, align 4
  store ptr %argv, ptr @__argv, align 8
  %0 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str, ptr @0)
  %1 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @1)
  %lst = alloca %list_i32, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %2 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  store ptr %2, ptr %dataPtr, align 8
  %sizePtr1 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %capPtr2 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 1
  %size = load i32, ptr %sizePtr1, align 4
  %capacity = load i32, ptr %capPtr2, align 4
  %3 = icmp eq i32 %size, %capacity
  br i1 %3, label %grow, label %cont

grow:                                             ; preds = %entry
  %4 = mul i32 %capacity, 2
  store i32 %4, ptr %capPtr2, align 4
  %dataPtr3 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %5 = load ptr, ptr %dataPtr3, align 8
  %6 = zext i32 %4 to i64
  %7 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %6
  %8 = call ptr @realloc(ptr %5, i64 %7)
  store ptr %8, ptr %dataPtr3, align 8
  br label %cont

cont:                                             ; preds = %grow, %entry
  %dataPtr4 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr4, align 8
  %9 = getelementptr inbounds i32, ptr %dataBase, i32 %size
  store i32 100, ptr %9, align 4
  %10 = add i32 %size, 1
  store i32 %10, ptr %sizePtr1, align 4
  %sizePtr5 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %capPtr6 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 1
  %size7 = load i32, ptr %sizePtr5, align 4
  %capacity8 = load i32, ptr %capPtr6, align 4
  %11 = icmp eq i32 %size7, %capacity8
  br i1 %11, label %grow9, label %cont10

grow9:                                            ; preds = %cont
  %12 = mul i32 %capacity8, 2
  store i32 %12, ptr %capPtr6, align 4
  %dataPtr11 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %13 = load ptr, ptr %dataPtr11, align 8
  %14 = zext i32 %12 to i64
  %15 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %14
  %16 = call ptr @realloc(ptr %13, i64 %15)
  store ptr %16, ptr %dataPtr11, align 8
  br label %cont10

cont10:                                           ; preds = %grow9, %cont
  %dataPtr12 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase13 = load ptr, ptr %dataPtr12, align 8
  %17 = getelementptr inbounds i32, ptr %dataBase13, i32 %size7
  store i32 200, ptr %17, align 4
  %18 = add i32 %size7, 1
  store i32 %18, ptr %sizePtr5, align 4
  %dataPtr14 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase15 = load ptr, ptr %dataPtr14, align 8
  %19 = getelementptr inbounds i32, ptr %dataBase15, i32 0
  store i32 150, ptr %19, align 4
  %20 = call i64 @strlen(ptr @2)
  %21 = call i64 @strlen(ptr @3)
  %22 = add i64 %20, %21
  %23 = add i64 %22, 1
  %24 = call ptr @malloc(i64 %23)
  %25 = call ptr @strcpy(ptr %24, ptr @2)
  %26 = call ptr @strcat(ptr %24, ptr @3)
  %dataPtr16 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase17 = load ptr, ptr %dataPtr16, align 8
  %27 = getelementptr inbounds i32, ptr %dataBase17, i32 0
  %list_elem_load = load i32, ptr %27, align 4
  %28 = call ptr @malloc(i64 12)
  %29 = call i32 (ptr, ptr, ...) @sprintf(ptr %28, ptr @4, i32 %list_elem_load)
  %30 = call i64 @strlen(ptr %24)
  %31 = call i64 @strlen(ptr %28)
  %32 = add i64 %30, %31
  %33 = add i64 %32, 1
  %34 = call ptr @malloc(i64 %33)
  %35 = call ptr @strcpy(ptr %34, ptr %24)
  %36 = call ptr @strcat(ptr %34, ptr %28)
  %37 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.1, ptr %34)
  %38 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @5)
  %39 = call i64 @strlen(ptr @6)
  %40 = call i64 @strlen(ptr @7)
  %41 = add i64 %39, %40
  %42 = add i64 %41, 1
  %43 = call ptr @malloc(i64 %42)
  %44 = call ptr @strcpy(ptr %43, ptr @6)
  %45 = call ptr @strcat(ptr %43, ptr @7)
  %dataPtr18 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase19 = load ptr, ptr %dataPtr18, align 8
  %46 = getelementptr inbounds i32, ptr %dataBase19, i32 1
  %list_elem_load20 = load i32, ptr %46, align 4
  %47 = call ptr @malloc(i64 12)
  %48 = call i32 (ptr, ptr, ...) @sprintf(ptr %47, ptr @8, i32 %list_elem_load20)
  %49 = call i64 @strlen(ptr %43)
  %50 = call i64 @strlen(ptr %47)
  %51 = add i64 %49, %50
  %52 = add i64 %51, 1
  %53 = call ptr @malloc(i64 %52)
  %54 = call ptr @strcpy(ptr %53, ptr %43)
  %55 = call ptr @strcat(ptr %53, ptr %47)
  %56 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.2, ptr %53)
  %57 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @9)
  %58 = call i64 @strlen(ptr @10)
  %59 = call i64 @strlen(ptr @11)
  %60 = add i64 %58, %59
  %61 = add i64 %60, 1
  %62 = call ptr @malloc(i64 %61)
  %63 = call ptr @strcpy(ptr %62, ptr @10)
  %64 = call ptr @strcat(ptr %62, ptr @11)
  %sizePtr21 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr21, align 4
  %65 = call ptr @malloc(i64 12)
  %66 = call i32 (ptr, ptr, ...) @sprintf(ptr %65, ptr @12, i32 %sizeVal)
  %67 = call i64 @strlen(ptr %62)
  %68 = call i64 @strlen(ptr %65)
  %69 = add i64 %67, %68
  %70 = add i64 %69, 1
  %71 = call ptr @malloc(i64 %70)
  %72 = call ptr @strcpy(ptr %71, ptr %62)
  %73 = call ptr @strcat(ptr %71, ptr %65)
  %74 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.3, ptr %71)
  %75 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @13)
  %sizePtr22 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %size23 = load i32, ptr %sizePtr22, align 4
  %76 = sub i32 %size23, 1
  store i32 %76, ptr %sizePtr22, align 4
  %dataPtr24 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase25 = load ptr, ptr %dataPtr24, align 8
  %77 = getelementptr inbounds i32, ptr %dataBase25, i32 %76
  %poppedVal = load i32, ptr %77, align 4
  %78 = call i64 @strlen(ptr @14)
  %79 = call i64 @strlen(ptr @15)
  %80 = add i64 %78, %79
  %81 = add i64 %80, 1
  %82 = call ptr @malloc(i64 %81)
  %83 = call ptr @strcpy(ptr %82, ptr @14)
  %84 = call ptr @strcat(ptr %82, ptr @15)
  %sizePtr26 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal27 = load i32, ptr %sizePtr26, align 4
  %85 = call ptr @malloc(i64 12)
  %86 = call i32 (ptr, ptr, ...) @sprintf(ptr %85, ptr @16, i32 %sizeVal27)
  %87 = call i64 @strlen(ptr %82)
  %88 = call i64 @strlen(ptr %85)
  %89 = add i64 %87, %88
  %90 = add i64 %89, 1
  %91 = call ptr @malloc(i64 %90)
  %92 = call ptr @strcpy(ptr %91, ptr %82)
  %93 = call ptr @strcat(ptr %91, ptr %85)
  %94 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.4, ptr %91)
  %95 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @17)
  %96 = call i64 @strlen(ptr @18)
  %97 = call i64 @strlen(ptr @19)
  %98 = add i64 %96, %97
  %99 = add i64 %98, 1
  %100 = call ptr @malloc(i64 %99)
  %101 = call ptr @strcpy(ptr %100, ptr @18)
  %102 = call ptr @strcat(ptr %100, ptr @19)
  %dataPtr28 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase29 = load ptr, ptr %dataPtr28, align 8
  %103 = getelementptr inbounds i32, ptr %dataBase29, i32 0
  %list_elem_load30 = load i32, ptr %103, align 4
  %104 = call ptr @malloc(i64 12)
  %105 = call i32 (ptr, ptr, ...) @sprintf(ptr %104, ptr @20, i32 %list_elem_load30)
  %106 = call i64 @strlen(ptr %100)
  %107 = call i64 @strlen(ptr %104)
  %108 = add i64 %106, %107
  %109 = add i64 %108, 1
  %110 = call ptr @malloc(i64 %109)
  %111 = call ptr @strcpy(ptr %110, ptr %100)
  %112 = call ptr @strcat(ptr %110, ptr %104)
  %113 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.5, ptr %110)
  %114 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @21)
  %115 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.6, ptr @22)
  %116 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @23)
  %sizePtr31 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %capPtr32 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 1
  %117 = load i32, ptr %sizePtr31, align 4
  %118 = load i32, ptr %capPtr32, align 4
  %119 = icmp eq i32 %117, %118
  br i1 %119, label %insert_grow, label %insert_move

insert_grow:                                      ; preds = %cont10
  %120 = mul i32 %118, 2
  store i32 %120, ptr %capPtr32, align 4
  %dataPtr33 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %121 = load ptr, ptr %dataPtr33, align 8
  %122 = zext i32 %120 to i64
  %123 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %122
  %124 = call ptr @realloc(ptr %121, i64 %123)
  store ptr %124, ptr %dataPtr33, align 8
  br label %insert_move

insert_move:                                      ; preds = %insert_grow, %cont10
  %dataPtr34 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase35 = load ptr, ptr %dataPtr34, align 8
  %125 = getelementptr inbounds i32, ptr %dataBase35, i32 0
  %126 = getelementptr inbounds i32, ptr %dataBase35, i32 1
  %127 = sub i32 %117, 0
  %128 = zext i32 %127 to i64
  %129 = mul i64 %128, ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64)
  %130 = icmp sgt i32 %127, 0
  br i1 %130, label %do_move, label %insert_store

do_move:                                          ; preds = %insert_move
  %131 = call ptr @memmove(ptr %126, ptr %125, i64 %129)
  br label %insert_store

insert_store:                                     ; preds = %do_move, %insert_move
  store i32 999, ptr %125, align 4
  %132 = add i32 %117, 1
  store i32 %132, ptr %sizePtr31, align 4
  %133 = call i64 @strlen(ptr @24)
  %134 = call i64 @strlen(ptr @25)
  %135 = add i64 %133, %134
  %136 = add i64 %135, 1
  %137 = call ptr @malloc(i64 %136)
  %138 = call ptr @strcpy(ptr %137, ptr @24)
  %139 = call ptr @strcat(ptr %137, ptr @25)
  %sizePtr36 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal37 = load i32, ptr %sizePtr36, align 4
  %140 = call ptr @malloc(i64 12)
  %141 = call i32 (ptr, ptr, ...) @sprintf(ptr %140, ptr @26, i32 %sizeVal37)
  %142 = call i64 @strlen(ptr %137)
  %143 = call i64 @strlen(ptr %140)
  %144 = add i64 %142, %143
  %145 = add i64 %144, 1
  %146 = call ptr @malloc(i64 %145)
  %147 = call ptr @strcpy(ptr %146, ptr %137)
  %148 = call ptr @strcat(ptr %146, ptr %140)
  %149 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.7, ptr %146)
  %150 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @27)
  %151 = call i64 @strlen(ptr @28)
  %152 = call i64 @strlen(ptr @29)
  %153 = add i64 %151, %152
  %154 = add i64 %153, 1
  %155 = call ptr @malloc(i64 %154)
  %156 = call ptr @strcpy(ptr %155, ptr @28)
  %157 = call ptr @strcat(ptr %155, ptr @29)
  %dataPtr38 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase39 = load ptr, ptr %dataPtr38, align 8
  %158 = getelementptr inbounds i32, ptr %dataBase39, i32 0
  %list_elem_load40 = load i32, ptr %158, align 4
  %159 = call ptr @malloc(i64 12)
  %160 = call i32 (ptr, ptr, ...) @sprintf(ptr %159, ptr @30, i32 %list_elem_load40)
  %161 = call i64 @strlen(ptr %155)
  %162 = call i64 @strlen(ptr %159)
  %163 = add i64 %161, %162
  %164 = add i64 %163, 1
  %165 = call ptr @malloc(i64 %164)
  %166 = call ptr @strcpy(ptr %165, ptr %155)
  %167 = call ptr @strcat(ptr %165, ptr %159)
  %168 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.8, ptr %165)
  %169 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @31)
  %170 = call i64 @strlen(ptr @32)
  %171 = call i64 @strlen(ptr @33)
  %172 = add i64 %170, %171
  %173 = add i64 %172, 1
  %174 = call ptr @malloc(i64 %173)
  %175 = call ptr @strcpy(ptr %174, ptr @32)
  %176 = call ptr @strcat(ptr %174, ptr @33)
  %dataPtr41 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase42 = load ptr, ptr %dataPtr41, align 8
  %177 = getelementptr inbounds i32, ptr %dataBase42, i32 1
  %list_elem_load43 = load i32, ptr %177, align 4
  %178 = call ptr @malloc(i64 12)
  %179 = call i32 (ptr, ptr, ...) @sprintf(ptr %178, ptr @34, i32 %list_elem_load43)
  %180 = call i64 @strlen(ptr %174)
  %181 = call i64 @strlen(ptr %178)
  %182 = add i64 %180, %181
  %183 = add i64 %182, 1
  %184 = call ptr @malloc(i64 %183)
  %185 = call ptr @strcpy(ptr %184, ptr %174)
  %186 = call ptr @strcat(ptr %184, ptr %178)
  %187 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.9, ptr %184)
  %188 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @35)
  %189 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.10, ptr @36)
  %190 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @37)
  %sizePtr44 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %191 = load i32, ptr %sizePtr44, align 4
  %dataPtr45 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase46 = load ptr, ptr %dataPtr45, align 8
  %192 = getelementptr inbounds i32, ptr %dataBase46, i32 0
  %193 = getelementptr inbounds i32, ptr %dataBase46, i32 1
  %194 = sub i32 %191, 1
  %195 = sub i32 %194, 0
  %196 = zext i32 %195 to i64
  %197 = mul i64 %196, ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64)
  %198 = icmp sgt i32 %195, 0
  br i1 %198, label %rm_do_move, label %rm_finish

rm_do_move:                                       ; preds = %insert_store
  %199 = call ptr @memmove(ptr %192, ptr %193, i64 %197)
  br label %rm_finish

rm_finish:                                        ; preds = %rm_do_move, %insert_store
  store i32 %194, ptr %sizePtr44, align 4
  %200 = call i64 @strlen(ptr @38)
  %201 = call i64 @strlen(ptr @39)
  %202 = add i64 %200, %201
  %203 = add i64 %202, 1
  %204 = call ptr @malloc(i64 %203)
  %205 = call ptr @strcpy(ptr %204, ptr @38)
  %206 = call ptr @strcat(ptr %204, ptr @39)
  %sizePtr47 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal48 = load i32, ptr %sizePtr47, align 4
  %207 = call ptr @malloc(i64 12)
  %208 = call i32 (ptr, ptr, ...) @sprintf(ptr %207, ptr @40, i32 %sizeVal48)
  %209 = call i64 @strlen(ptr %204)
  %210 = call i64 @strlen(ptr %207)
  %211 = add i64 %209, %210
  %212 = add i64 %211, 1
  %213 = call ptr @malloc(i64 %212)
  %214 = call ptr @strcpy(ptr %213, ptr %204)
  %215 = call ptr @strcat(ptr %213, ptr %207)
  %216 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.11, ptr %213)
  %217 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @41)
  %218 = call i64 @strlen(ptr @42)
  %219 = call i64 @strlen(ptr @43)
  %220 = add i64 %218, %219
  %221 = add i64 %220, 1
  %222 = call ptr @malloc(i64 %221)
  %223 = call ptr @strcpy(ptr %222, ptr @42)
  %224 = call ptr @strcat(ptr %222, ptr @43)
  %dataPtr49 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase50 = load ptr, ptr %dataPtr49, align 8
  %225 = getelementptr inbounds i32, ptr %dataBase50, i32 0
  %list_elem_load51 = load i32, ptr %225, align 4
  %226 = call ptr @malloc(i64 12)
  %227 = call i32 (ptr, ptr, ...) @sprintf(ptr %226, ptr @44, i32 %list_elem_load51)
  %228 = call i64 @strlen(ptr %222)
  %229 = call i64 @strlen(ptr %226)
  %230 = add i64 %228, %229
  %231 = add i64 %230, 1
  %232 = call ptr @malloc(i64 %231)
  %233 = call ptr @strcpy(ptr %232, ptr %222)
  %234 = call ptr @strcat(ptr %232, ptr %226)
  %235 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.12, ptr %232)
  %236 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @45)
  %237 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.13, ptr @46)
  %238 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @47)
  %239 = call i64 @strlen(ptr @48)
  %240 = call i64 @strlen(ptr @49)
  %241 = add i64 %239, %240
  %242 = add i64 %241, 1
  %243 = call ptr @malloc(i64 %242)
  %244 = call ptr @strcpy(ptr %243, ptr @48)
  %245 = call ptr @strcat(ptr %243, ptr @49)
  %sizePtr52 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal53 = load i32, ptr %sizePtr52, align 4
  %isEmpty = icmp eq i32 %sizeVal53, 0
  %246 = zext i1 %isEmpty to i32
  %247 = call ptr @malloc(i64 12)
  %248 = call i32 (ptr, ptr, ...) @sprintf(ptr %247, ptr @50, i32 %246)
  %249 = call i64 @strlen(ptr %243)
  %250 = call i64 @strlen(ptr %247)
  %251 = add i64 %249, %250
  %252 = add i64 %251, 1
  %253 = call ptr @malloc(i64 %252)
  %254 = call ptr @strcpy(ptr %253, ptr %243)
  %255 = call ptr @strcat(ptr %253, ptr %247)
  %256 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.14, ptr %253)
  %257 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @51)
  %sizePtr54 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  store i32 0, ptr %sizePtr54, align 4
  %258 = call i64 @strlen(ptr @52)
  %259 = call i64 @strlen(ptr @53)
  %260 = add i64 %258, %259
  %261 = add i64 %260, 1
  %262 = call ptr @malloc(i64 %261)
  %263 = call ptr @strcpy(ptr %262, ptr @52)
  %264 = call ptr @strcat(ptr %262, ptr @53)
  %sizePtr55 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal56 = load i32, ptr %sizePtr55, align 4
  %265 = call ptr @malloc(i64 12)
  %266 = call i32 (ptr, ptr, ...) @sprintf(ptr %265, ptr @54, i32 %sizeVal56)
  %267 = call i64 @strlen(ptr %262)
  %268 = call i64 @strlen(ptr %265)
  %269 = add i64 %267, %268
  %270 = add i64 %269, 1
  %271 = call ptr @malloc(i64 %270)
  %272 = call ptr @strcpy(ptr %271, ptr %262)
  %273 = call ptr @strcat(ptr %271, ptr %265)
  %274 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.15, ptr %271)
  %275 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @55)
  %276 = call i64 @strlen(ptr @56)
  %277 = call i64 @strlen(ptr @57)
  %278 = add i64 %276, %277
  %279 = add i64 %278, 1
  %280 = call ptr @malloc(i64 %279)
  %281 = call ptr @strcpy(ptr %280, ptr @56)
  %282 = call ptr @strcat(ptr %280, ptr @57)
  %sizePtr57 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal58 = load i32, ptr %sizePtr57, align 4
  %isEmpty59 = icmp eq i32 %sizeVal58, 0
  %283 = zext i1 %isEmpty59 to i32
  %284 = call ptr @malloc(i64 12)
  %285 = call i32 (ptr, ptr, ...) @sprintf(ptr %284, ptr @58, i32 %283)
  %286 = call i64 @strlen(ptr %280)
  %287 = call i64 @strlen(ptr %284)
  %288 = add i64 %286, %287
  %289 = add i64 %288, 1
  %290 = call ptr @malloc(i64 %289)
  %291 = call ptr @strcpy(ptr %290, ptr %280)
  %292 = call ptr @strcat(ptr %290, ptr %284)
  %293 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @fmt_str.16, ptr %290)
  %294 = call i32 (i32, ptr, ...) @dprintf(i32 1, ptr @59)
  ret i32 0
}

declare i32 @__gxx_personality_v0(...)

declare i32 @dprintf(i32, ptr, ...)

declare ptr @malloc(i64)

declare ptr @realloc(ptr, i64)

declare i64 @strlen(ptr)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare i32 @sprintf(ptr, ptr, ...)

declare ptr @memmove(ptr, ptr, i64)
