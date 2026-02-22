; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%list_i32 = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@0 = private unnamed_addr constant [25 x i8] c"Testing Dynamic Lists...\00", align 1
@1 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@3 = private unnamed_addr constant [9 x i8] c"lst-0 = \00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [9 x i8] c"lst-1 = \00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@9 = private unnamed_addr constant [18 x i8] c"Size before pop: \00", align 1
@10 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@12 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@13 = private unnamed_addr constant [27 x i8] c"Popped element. New size: \00", align 1
@14 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [18 x i8] c"lst-0 after pop: \00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [18 x i8] c"Testing Insert...\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@23 = private unnamed_addr constant [26 x i8] c"Inserted 999 at 0. Size: \00", align 1
@24 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@27 = private unnamed_addr constant [9 x i8] c"lst-0 = \00", align 1
@28 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@29 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@30 = private unnamed_addr constant [9 x i8] c"lst-1 = \00", align 1
@31 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@33 = private unnamed_addr constant [18 x i8] c"Testing Remove...\00", align 1
@34 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@35 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@36 = private unnamed_addr constant [21 x i8] c"Removed at 0. Size: \00", align 1
@37 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@38 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@39 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@40 = private unnamed_addr constant [9 x i8] c"lst-0 = \00", align 1
@41 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@42 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@43 = private unnamed_addr constant [23 x i8] c"Testing Clear/Empty...\00", align 1
@44 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@45 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@46 = private unnamed_addr constant [11 x i8] c"Is Empty? \00", align 1
@47 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@48 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@49 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@50 = private unnamed_addr constant [16 x i8] c"Cleared. Size: \00", align 1
@51 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@52 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@53 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@54 = private unnamed_addr constant [11 x i8] c"Is Empty? \00", align 1
@55 = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@56 = private unnamed_addr constant [4 x i8] c"%d\0A\00", align 1
@57 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1

define void @__aiml_init() {
entry:
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr @0)
  %lst = alloca %list_i32, align 8
  store %list_i32 zeroinitializer, ptr %lst, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %0 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  store ptr %0, ptr %dataPtr, align 8
  %sizePtr1 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %capPtr2 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 1
  %size = load i32, ptr %sizePtr1, align 4
  %capacity = load i32, ptr %capPtr2, align 4
  %1 = icmp eq i32 %size, %capacity
  br i1 %1, label %grow, label %cont

grow:                                             ; preds = %entry
  %2 = mul i32 %capacity, 2
  store i32 %2, ptr %capPtr2, align 4
  %dataPtr3 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %3 = load ptr, ptr %dataPtr3, align 8
  %4 = zext i32 %2 to i64
  %5 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %4
  %6 = call ptr @realloc(ptr %3, i64 %5)
  store ptr %6, ptr %dataPtr3, align 8
  br label %cont

cont:                                             ; preds = %grow, %entry
  %dataPtr4 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr4, align 8
  %7 = getelementptr inbounds i32, ptr %dataBase, i32 %size
  store i32 100, ptr %7, align 4
  %8 = add i32 %size, 1
  store i32 %8, ptr %sizePtr1, align 4
  %sizePtr5 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %capPtr6 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 1
  %size7 = load i32, ptr %sizePtr5, align 4
  %capacity8 = load i32, ptr %capPtr6, align 4
  %9 = icmp eq i32 %size7, %capacity8
  br i1 %9, label %grow9, label %cont10

grow9:                                            ; preds = %cont
  %10 = mul i32 %capacity8, 2
  store i32 %10, ptr %capPtr6, align 4
  %dataPtr11 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %11 = load ptr, ptr %dataPtr11, align 8
  %12 = zext i32 %10 to i64
  %13 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %12
  %14 = call ptr @realloc(ptr %11, i64 %13)
  store ptr %14, ptr %dataPtr11, align 8
  br label %cont10

cont10:                                           ; preds = %grow9, %cont
  %dataPtr12 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase13 = load ptr, ptr %dataPtr12, align 8
  %15 = getelementptr inbounds i32, ptr %dataBase13, i32 %size7
  store i32 200, ptr %15, align 4
  %16 = add i32 %size7, 1
  store i32 %16, ptr %sizePtr5, align 4
  %dataPtr14 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase15 = load ptr, ptr %dataPtr14, align 8
  %17 = getelementptr inbounds %list_i32, ptr %dataBase15, i32 0
  store i32 150, ptr %17, align 4
  %dataPtr16 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase17 = load ptr, ptr %dataPtr16, align 8
  %18 = getelementptr inbounds %list_i32, ptr %dataBase17, i32 0
  %lst_elem = load %list_i32, ptr %18, align 8
  %19 = call i64 @strlen(ptr @3)
  %20 = call i64 @strlen(%list_i32 %lst_elem)
  %21 = add i64 %19, %20
  %22 = add i64 %21, 1
  %23 = call ptr @malloc(i64 %22)
  %24 = call ptr @strcpy(ptr %23, ptr @3)
  %25 = call ptr @strcat(ptr %23, %list_i32 %lst_elem)
  %print_call18 = call i32 (ptr, ...) @printf(ptr @5, ptr %23)
  %dataPtr19 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase20 = load ptr, ptr %dataPtr19, align 8
  %26 = getelementptr inbounds %list_i32, ptr %dataBase20, i32 1
  %lst_elem21 = load %list_i32, ptr %26, align 8
  %27 = call i64 @strlen(ptr @6)
  %28 = call i64 @strlen(%list_i32 %lst_elem21)
  %29 = add i64 %27, %28
  %30 = add i64 %29, 1
  %31 = call ptr @malloc(i64 %30)
  %32 = call ptr @strcpy(ptr %31, ptr @6)
  %33 = call ptr @strcat(ptr %31, %list_i32 %lst_elem21)
  %print_call22 = call i32 (ptr, ...) @printf(ptr @8, ptr %31)
  %sizePtr23 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr23, align 4
  %34 = call ptr @malloc(i64 12)
  %35 = call i32 (ptr, ptr, ...) @sprintf(ptr %34, ptr @10, i32 %sizeVal)
  %36 = call i64 @strlen(ptr @9)
  %37 = call i64 @strlen(ptr %34)
  %38 = add i64 %36, %37
  %39 = add i64 %38, 1
  %40 = call ptr @malloc(i64 %39)
  %41 = call ptr @strcpy(ptr %40, ptr @9)
  %42 = call ptr @strcat(ptr %40, ptr %34)
  %print_call24 = call i32 (ptr, ...) @printf(ptr @12, ptr %40)
  %sizePtr25 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %size26 = load i32, ptr %sizePtr25, align 4
  %43 = sub i32 %size26, 1
  store i32 %43, ptr %sizePtr25, align 4
  %dataPtr27 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase28 = load ptr, ptr %dataPtr27, align 8
  %44 = getelementptr inbounds %list_i32, ptr %dataBase28, i32 %43
  %poppedVal = load %list_i32, ptr %44, align 8
  %sizePtr29 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal30 = load i32, ptr %sizePtr29, align 4
  %45 = call ptr @malloc(i64 12)
  %46 = call i32 (ptr, ptr, ...) @sprintf(ptr %45, ptr @14, i32 %sizeVal30)
  %47 = call i64 @strlen(ptr @13)
  %48 = call i64 @strlen(ptr %45)
  %49 = add i64 %47, %48
  %50 = add i64 %49, 1
  %51 = call ptr @malloc(i64 %50)
  %52 = call ptr @strcpy(ptr %51, ptr @13)
  %53 = call ptr @strcat(ptr %51, ptr %45)
  %print_call31 = call i32 (ptr, ...) @printf(ptr @16, ptr %51)
  %dataPtr32 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase33 = load ptr, ptr %dataPtr32, align 8
  %54 = getelementptr inbounds %list_i32, ptr %dataBase33, i32 0
  %lst_elem34 = load %list_i32, ptr %54, align 8
  %55 = call i64 @strlen(ptr @17)
  %56 = call i64 @strlen(%list_i32 %lst_elem34)
  %57 = add i64 %55, %56
  %58 = add i64 %57, 1
  %59 = call ptr @malloc(i64 %58)
  %60 = call ptr @strcpy(ptr %59, ptr @17)
  %61 = call ptr @strcat(ptr %59, %list_i32 %lst_elem34)
  %print_call35 = call i32 (ptr, ...) @printf(ptr @19, ptr %59)
  %print_call36 = call i32 (ptr, ...) @printf(ptr @22, ptr @20)
  %sizePtr37 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %capPtr38 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 1
  %62 = load i32, ptr %sizePtr37, align 4
  %63 = load i32, ptr %capPtr38, align 4
  %64 = icmp eq i32 %62, %63
  br i1 %64, label %insert_grow, label %insert_move

insert_grow:                                      ; preds = %cont10
  %65 = mul i32 %63, 2
  store i32 %65, ptr %capPtr38, align 4
  %dataPtr39 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %66 = load ptr, ptr %dataPtr39, align 8
  %67 = zext i32 %65 to i64
  %68 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %67
  %69 = call ptr @realloc(ptr %66, i64 %68)
  store ptr %69, ptr %dataPtr39, align 8
  br label %insert_move

insert_move:                                      ; preds = %insert_grow, %cont10
  %dataPtr40 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase41 = load ptr, ptr %dataPtr40, align 8
  %70 = getelementptr inbounds i32, ptr %dataBase41, i32 0
  %71 = getelementptr inbounds i32, ptr %dataBase41, i32 1
  %72 = sub i32 %62, 0
  %73 = zext i32 %72 to i64
  %74 = mul i64 %73, ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64)
  %75 = icmp sgt i32 %72, 0
  br i1 %75, label %do_move, label %insert_store

do_move:                                          ; preds = %insert_move
  %76 = call ptr @memmove(ptr %71, ptr %70, i64 %74)
  br label %insert_store

insert_store:                                     ; preds = %do_move, %insert_move
  store i32 999, ptr %70, align 4
  %77 = add i32 %62, 1
  store i32 %77, ptr %sizePtr37, align 4
  %sizePtr42 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal43 = load i32, ptr %sizePtr42, align 4
  %78 = call ptr @malloc(i64 12)
  %79 = call i32 (ptr, ptr, ...) @sprintf(ptr %78, ptr @24, i32 %sizeVal43)
  %80 = call i64 @strlen(ptr @23)
  %81 = call i64 @strlen(ptr %78)
  %82 = add i64 %80, %81
  %83 = add i64 %82, 1
  %84 = call ptr @malloc(i64 %83)
  %85 = call ptr @strcpy(ptr %84, ptr @23)
  %86 = call ptr @strcat(ptr %84, ptr %78)
  %print_call44 = call i32 (ptr, ...) @printf(ptr @26, ptr %84)
  %dataPtr45 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase46 = load ptr, ptr %dataPtr45, align 8
  %87 = getelementptr inbounds %list_i32, ptr %dataBase46, i32 0
  %lst_elem47 = load %list_i32, ptr %87, align 8
  %88 = call i64 @strlen(ptr @27)
  %89 = call i64 @strlen(%list_i32 %lst_elem47)
  %90 = add i64 %88, %89
  %91 = add i64 %90, 1
  %92 = call ptr @malloc(i64 %91)
  %93 = call ptr @strcpy(ptr %92, ptr @27)
  %94 = call ptr @strcat(ptr %92, %list_i32 %lst_elem47)
  %print_call48 = call i32 (ptr, ...) @printf(ptr @29, ptr %92)
  %dataPtr49 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase50 = load ptr, ptr %dataPtr49, align 8
  %95 = getelementptr inbounds %list_i32, ptr %dataBase50, i32 1
  %lst_elem51 = load %list_i32, ptr %95, align 8
  %96 = call i64 @strlen(ptr @30)
  %97 = call i64 @strlen(%list_i32 %lst_elem51)
  %98 = add i64 %96, %97
  %99 = add i64 %98, 1
  %100 = call ptr @malloc(i64 %99)
  %101 = call ptr @strcpy(ptr %100, ptr @30)
  %102 = call ptr @strcat(ptr %100, %list_i32 %lst_elem51)
  %print_call52 = call i32 (ptr, ...) @printf(ptr @32, ptr %100)
  %print_call53 = call i32 (ptr, ...) @printf(ptr @35, ptr @33)
  %sizePtr54 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %103 = load i32, ptr %sizePtr54, align 4
  %dataPtr55 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase56 = load ptr, ptr %dataPtr55, align 8
  %104 = getelementptr inbounds %list_i32, ptr %dataBase56, i32 0
  %105 = getelementptr inbounds %list_i32, ptr %dataBase56, i32 1
  %106 = sub i32 %103, 1
  %107 = sub i32 %106, 0
  %108 = zext i32 %107 to i64
  %109 = mul i64 %108, ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64)
  %110 = icmp sgt i32 %107, 0
  br i1 %110, label %rm_do_move, label %rm_finish

rm_do_move:                                       ; preds = %insert_store
  %111 = call ptr @memmove(ptr %104, ptr %105, i64 %109)
  br label %rm_finish

rm_finish:                                        ; preds = %rm_do_move, %insert_store
  store i32 %106, ptr %sizePtr54, align 4
  %sizePtr57 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal58 = load i32, ptr %sizePtr57, align 4
  %112 = call ptr @malloc(i64 12)
  %113 = call i32 (ptr, ptr, ...) @sprintf(ptr %112, ptr @37, i32 %sizeVal58)
  %114 = call i64 @strlen(ptr @36)
  %115 = call i64 @strlen(ptr %112)
  %116 = add i64 %114, %115
  %117 = add i64 %116, 1
  %118 = call ptr @malloc(i64 %117)
  %119 = call ptr @strcpy(ptr %118, ptr @36)
  %120 = call ptr @strcat(ptr %118, ptr %112)
  %print_call59 = call i32 (ptr, ...) @printf(ptr @39, ptr %118)
  %dataPtr60 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 2
  %dataBase61 = load ptr, ptr %dataPtr60, align 8
  %121 = getelementptr inbounds %list_i32, ptr %dataBase61, i32 0
  %lst_elem62 = load %list_i32, ptr %121, align 8
  %122 = call i64 @strlen(ptr @40)
  %123 = call i64 @strlen(%list_i32 %lst_elem62)
  %124 = add i64 %122, %123
  %125 = add i64 %124, 1
  %126 = call ptr @malloc(i64 %125)
  %127 = call ptr @strcpy(ptr %126, ptr @40)
  %128 = call ptr @strcat(ptr %126, %list_i32 %lst_elem62)
  %print_call63 = call i32 (ptr, ...) @printf(ptr @42, ptr %126)
  %print_call64 = call i32 (ptr, ...) @printf(ptr @45, ptr @43)
  %sizePtr65 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal66 = load i32, ptr %sizePtr65, align 4
  %129 = icmp eq i32 %sizeVal66, 0
  %empty_zext = zext i1 %129 to i32
  %130 = call ptr @malloc(i64 12)
  %131 = call i32 (ptr, ptr, ...) @sprintf(ptr %130, ptr @47, i32 %empty_zext)
  %132 = call i64 @strlen(ptr @46)
  %133 = call i64 @strlen(ptr %130)
  %134 = add i64 %132, %133
  %135 = add i64 %134, 1
  %136 = call ptr @malloc(i64 %135)
  %137 = call ptr @strcpy(ptr %136, ptr @46)
  %138 = call ptr @strcat(ptr %136, ptr %130)
  %print_call67 = call i32 (ptr, ...) @printf(ptr @49, ptr %136)
  %sizePtr68 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  store i32 0, ptr %sizePtr68, align 4
  %sizePtr69 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal70 = load i32, ptr %sizePtr69, align 4
  %139 = call ptr @malloc(i64 12)
  %140 = call i32 (ptr, ptr, ...) @sprintf(ptr %139, ptr @51, i32 %sizeVal70)
  %141 = call i64 @strlen(ptr @50)
  %142 = call i64 @strlen(ptr %139)
  %143 = add i64 %141, %142
  %144 = add i64 %143, 1
  %145 = call ptr @malloc(i64 %144)
  %146 = call ptr @strcpy(ptr %145, ptr @50)
  %147 = call ptr @strcat(ptr %145, ptr %139)
  %print_call71 = call i32 (ptr, ...) @printf(ptr @53, ptr %145)
  %sizePtr72 = getelementptr inbounds %GenericList, ptr %lst, i32 0, i32 0
  %sizeVal73 = load i32, ptr %sizePtr72, align 4
  %148 = icmp eq i32 %sizeVal73, 0
  %empty_zext74 = zext i1 %148 to i32
  %149 = call ptr @malloc(i64 12)
  %150 = call i32 (ptr, ptr, ...) @sprintf(ptr %149, ptr @55, i32 %empty_zext74)
  %151 = call i64 @strlen(ptr @54)
  %152 = call i64 @strlen(ptr %149)
  %153 = add i64 %151, %152
  %154 = add i64 %153, 1
  %155 = call ptr @malloc(i64 %154)
  %156 = call ptr @strcpy(ptr %155, ptr @54)
  %157 = call ptr @strcat(ptr %155, ptr %149)
  %print_call75 = call i32 (ptr, ...) @printf(ptr @57, ptr %155)
  call void @exit(i32 0)
  ret void
}

declare i32 @printf(ptr, ...)

declare ptr @malloc(i64)

declare ptr @realloc(ptr, i64)

declare i64 @strlen(ptr)

declare ptr @strcpy(ptr, ptr)

declare ptr @strcat(ptr, ptr)

declare i32 @sprintf(ptr, ptr, ...)

declare ptr @memmove(ptr, ptr, i64)

declare void @exit(i32)
