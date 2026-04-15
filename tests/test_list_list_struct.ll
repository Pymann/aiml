; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%AimlClassSchema = type { ptr, i32, i32, i32, i32, [256 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%Shape = type { ptr, %"list_list<Point>" }
%"list_list<Point>" = type { i32, i32, ptr }
%list_Point = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }
%Point = type { i32, i32 }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_Shape = private unnamed_addr constant [7 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 2, i32 1, i32 0, i32 24, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 3, i32 0, i32 8, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 4, i32 0, i32 0, i32 8, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 5, i32 2, i32 5, i32 0, i32 8, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 4, i32 0, i32 0 }]
@0 = private unnamed_addr constant [5 x i8] c"poly\00", align 1
@tn = private unnamed_addr constant [6 x i8] c"Shape\00", align 1
@fk_name = private unnamed_addr constant [5 x i8] c"name\00", align 1
@fk_segments = private unnamed_addr constant [9 x i8] c"segments\00", align 1
@fk_x = private unnamed_addr constant [2 x i8] c"x\00", align 1
@fk_y = private unnamed_addr constant [2 x i8] c"y\00", align 1
@1 = private unnamed_addr constant [12 x i8] c"serialized=\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@2 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@fk_name.1 = private unnamed_addr constant [5 x i8] c"name\00", align 1
@fk_segments.2 = private unnamed_addr constant [9 x i8] c"segments\00", align 1
@fk_x.3 = private unnamed_addr constant [2 x i8] c"x\00", align 1
@fk_y.4 = private unnamed_addr constant [2 x i8] c"y\00", align 1
@3 = private unnamed_addr constant [5 x i8] c"poly\00", align 1
@4 = private unnamed_addr constant [11 x i8] c"FAIL name=\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@5 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@6 = private unnamed_addr constant [19 x i8] c"FAIL segments.len=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [14 x i8] c"FAIL gs0.len=\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [12 x i8] c"FAIL gp0.x=\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [12 x i8] c"FAIL gp0.y=\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [12 x i8] c"FAIL gp1.x=\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [14 x i8] c"FAIL gs1.len=\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [12 x i8] c"FAIL gp2.x=\00", align 1
@print_null_str.12 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [24 x i8] c"list<list<Struct>> PASS\00", align 1
@print_null_str.13 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @main, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @main]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_main = private constant [5 x i8] c"main\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_main]
@__aiml_func_count = constant i32 1
@__aiml_sname_Point = private constant [6 x i8] c"Point\00"
@__aiml_sfield_Point_x = private constant [2 x i8] c"x\00"
@__aiml_sfield_Point_y = private constant [2 x i8] c"y\00"
@__aiml_sname_Shape = private constant [6 x i8] c"Shape\00"
@__aiml_sfield_Shape_name = private constant [5 x i8] c"name\00"
@__aiml_sfield_Shape_segments = private constant [9 x i8] c"segments\00"
@__aiml_schemas = global [2 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_Point, i32 1, i32 2, i32 8, i32 8, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Point_x }, %AimlFieldInfo { ptr @__aiml_sfield_Point_y }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Shape, i32 1, i32 2, i32 24, i32 32, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Shape_name }, %AimlFieldInfo { ptr @__aiml_sfield_Shape_segments }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 2

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %sh = alloca %Shape, align 8
  store %Shape zeroinitializer, ptr %sh, align 8
  %set_prop_ptr_w = getelementptr inbounds %Shape, ptr %sh, i32 0, i32 0
  store ptr @0, ptr %set_prop_ptr_w, align 8
  %seg0 = alloca %list_Point, align 8
  store %list_Point zeroinitializer, ptr %seg0, align 8
  %sizePtr = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %0 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%Point, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 2
  store ptr %0, ptr %dataPtr, align 8
  %p0 = alloca %Point, align 8
  store %Point zeroinitializer, ptr %p0, align 4
  %set_prop_ptr_w1 = getelementptr inbounds %Point, ptr %p0, i32 0, i32 0
  store i32 1, ptr %set_prop_ptr_w1, align 4
  %set_prop_ptr_w2 = getelementptr inbounds %Point, ptr %p0, i32 0, i32 1
  store i32 2, ptr %set_prop_ptr_w2, align 4
  %p0_val = load %Point, ptr %p0, align 4
  %sizePtr3 = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 0
  %capPtr4 = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 1
  %size = load i32, ptr %sizePtr3, align 4
  %capacity = load i32, ptr %capPtr4, align 4
  %1 = icmp eq i32 %size, %capacity
  br i1 %1, label %grow, label %cont

grow:                                             ; preds = %entry
  %2 = mul i32 %capacity, 2
  %3 = icmp sgt i32 %2, 0
  %4 = select i1 %3, i32 %2, i32 4
  store i32 %4, ptr %capPtr4, align 4
  %dataPtr5 = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 2
  %5 = load ptr, ptr %dataPtr5, align 8
  %6 = zext i32 %4 to i64
  %7 = mul i64 ptrtoint (ptr getelementptr inbounds (%Point, ptr null, i32 1) to i64), %6
  %8 = call ptr @realloc(ptr %5, i64 %7)
  store ptr %8, ptr %dataPtr5, align 8
  br label %cont

cont:                                             ; preds = %grow, %entry
  %dataPtr6 = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr6, align 8
  %9 = getelementptr inbounds %Point, ptr %dataBase, i32 %size
  store %Point %p0_val, ptr %9, align 4
  %10 = add i32 %size, 1
  store i32 %10, ptr %sizePtr3, align 4
  %p1 = alloca %Point, align 8
  store %Point zeroinitializer, ptr %p1, align 4
  %set_prop_ptr_w7 = getelementptr inbounds %Point, ptr %p1, i32 0, i32 0
  store i32 3, ptr %set_prop_ptr_w7, align 4
  %set_prop_ptr_w8 = getelementptr inbounds %Point, ptr %p1, i32 0, i32 1
  store i32 4, ptr %set_prop_ptr_w8, align 4
  %p1_val = load %Point, ptr %p1, align 4
  %sizePtr9 = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 0
  %capPtr10 = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 1
  %size11 = load i32, ptr %sizePtr9, align 4
  %capacity12 = load i32, ptr %capPtr10, align 4
  %11 = icmp eq i32 %size11, %capacity12
  br i1 %11, label %grow13, label %cont14

grow13:                                           ; preds = %cont
  %12 = mul i32 %capacity12, 2
  %13 = icmp sgt i32 %12, 0
  %14 = select i1 %13, i32 %12, i32 4
  store i32 %14, ptr %capPtr10, align 4
  %dataPtr15 = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 2
  %15 = load ptr, ptr %dataPtr15, align 8
  %16 = zext i32 %14 to i64
  %17 = mul i64 ptrtoint (ptr getelementptr inbounds (%Point, ptr null, i32 1) to i64), %16
  %18 = call ptr @realloc(ptr %15, i64 %17)
  store ptr %18, ptr %dataPtr15, align 8
  br label %cont14

cont14:                                           ; preds = %grow13, %cont
  %dataPtr16 = getelementptr inbounds %GenericList, ptr %seg0, i32 0, i32 2
  %dataBase17 = load ptr, ptr %dataPtr16, align 8
  %19 = getelementptr inbounds %Point, ptr %dataBase17, i32 %size11
  store %Point %p1_val, ptr %19, align 4
  %20 = add i32 %size11, 1
  store i32 %20, ptr %sizePtr9, align 4
  %set_prop_ptr_w18 = getelementptr inbounds %Shape, ptr %sh, i32 0, i32 1
  %seg0_val = load %list_Point, ptr %seg0, align 8
  %sizePtr19 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w18, i32 0, i32 0
  %capPtr20 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w18, i32 0, i32 1
  %size21 = load i32, ptr %sizePtr19, align 4
  %capacity22 = load i32, ptr %capPtr20, align 4
  %21 = icmp eq i32 %size21, %capacity22
  br i1 %21, label %grow23, label %cont24

grow23:                                           ; preds = %cont14
  %22 = mul i32 %capacity22, 2
  %23 = icmp sgt i32 %22, 0
  %24 = select i1 %23, i32 %22, i32 4
  store i32 %24, ptr %capPtr20, align 4
  %dataPtr25 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w18, i32 0, i32 2
  %25 = load ptr, ptr %dataPtr25, align 8
  %26 = zext i32 %24 to i64
  %27 = mul i64 ptrtoint (ptr getelementptr inbounds (%list_Point, ptr null, i32 1) to i64), %26
  %28 = call ptr @realloc(ptr %25, i64 %27)
  store ptr %28, ptr %dataPtr25, align 8
  br label %cont24

cont24:                                           ; preds = %grow23, %cont14
  %dataPtr26 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w18, i32 0, i32 2
  %dataBase27 = load ptr, ptr %dataPtr26, align 8
  %29 = getelementptr inbounds %list_Point, ptr %dataBase27, i32 %size21
  store %list_Point %seg0_val, ptr %29, align 8
  %30 = add i32 %size21, 1
  store i32 %30, ptr %sizePtr19, align 4
  %seg1 = alloca %list_Point, align 8
  store %list_Point zeroinitializer, ptr %seg1, align 8
  %sizePtr28 = getelementptr inbounds %GenericList, ptr %seg1, i32 0, i32 0
  store i32 0, ptr %sizePtr28, align 4
  %capPtr29 = getelementptr inbounds %GenericList, ptr %seg1, i32 0, i32 1
  store i32 4, ptr %capPtr29, align 4
  %31 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%Point, ptr null, i32 1) to i64), i64 4))
  %dataPtr30 = getelementptr inbounds %GenericList, ptr %seg1, i32 0, i32 2
  store ptr %31, ptr %dataPtr30, align 8
  %p2 = alloca %Point, align 8
  store %Point zeroinitializer, ptr %p2, align 4
  %set_prop_ptr_w31 = getelementptr inbounds %Point, ptr %p2, i32 0, i32 0
  store i32 5, ptr %set_prop_ptr_w31, align 4
  %set_prop_ptr_w32 = getelementptr inbounds %Point, ptr %p2, i32 0, i32 1
  store i32 6, ptr %set_prop_ptr_w32, align 4
  %p2_val = load %Point, ptr %p2, align 4
  %sizePtr33 = getelementptr inbounds %GenericList, ptr %seg1, i32 0, i32 0
  %capPtr34 = getelementptr inbounds %GenericList, ptr %seg1, i32 0, i32 1
  %size35 = load i32, ptr %sizePtr33, align 4
  %capacity36 = load i32, ptr %capPtr34, align 4
  %32 = icmp eq i32 %size35, %capacity36
  br i1 %32, label %grow37, label %cont38

grow37:                                           ; preds = %cont24
  %33 = mul i32 %capacity36, 2
  %34 = icmp sgt i32 %33, 0
  %35 = select i1 %34, i32 %33, i32 4
  store i32 %35, ptr %capPtr34, align 4
  %dataPtr39 = getelementptr inbounds %GenericList, ptr %seg1, i32 0, i32 2
  %36 = load ptr, ptr %dataPtr39, align 8
  %37 = zext i32 %35 to i64
  %38 = mul i64 ptrtoint (ptr getelementptr inbounds (%Point, ptr null, i32 1) to i64), %37
  %39 = call ptr @realloc(ptr %36, i64 %38)
  store ptr %39, ptr %dataPtr39, align 8
  br label %cont38

cont38:                                           ; preds = %grow37, %cont24
  %dataPtr40 = getelementptr inbounds %GenericList, ptr %seg1, i32 0, i32 2
  %dataBase41 = load ptr, ptr %dataPtr40, align 8
  %40 = getelementptr inbounds %Point, ptr %dataBase41, i32 %size35
  store %Point %p2_val, ptr %40, align 4
  %41 = add i32 %size35, 1
  store i32 %41, ptr %sizePtr33, align 4
  %set_prop_ptr_w42 = getelementptr inbounds %Shape, ptr %sh, i32 0, i32 1
  %seg1_val = load %list_Point, ptr %seg1, align 8
  %sizePtr43 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w42, i32 0, i32 0
  %capPtr44 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w42, i32 0, i32 1
  %size45 = load i32, ptr %sizePtr43, align 4
  %capacity46 = load i32, ptr %capPtr44, align 4
  %42 = icmp eq i32 %size45, %capacity46
  br i1 %42, label %grow47, label %cont48

grow47:                                           ; preds = %cont38
  %43 = mul i32 %capacity46, 2
  %44 = icmp sgt i32 %43, 0
  %45 = select i1 %44, i32 %43, i32 4
  store i32 %45, ptr %capPtr44, align 4
  %dataPtr49 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w42, i32 0, i32 2
  %46 = load ptr, ptr %dataPtr49, align 8
  %47 = zext i32 %45 to i64
  %48 = mul i64 ptrtoint (ptr getelementptr inbounds (%list_Point, ptr null, i32 1) to i64), %47
  %49 = call ptr @realloc(ptr %46, i64 %48)
  store ptr %49, ptr %dataPtr49, align 8
  br label %cont48

cont48:                                           ; preds = %grow47, %cont38
  %dataPtr50 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w42, i32 0, i32 2
  %dataBase51 = load ptr, ptr %dataPtr50, align 8
  %50 = getelementptr inbounds %list_Point, ptr %dataBase51, i32 %size45
  store %list_Point %seg1_val, ptr %50, align 8
  %51 = add i32 %size45, 1
  store i32 %51, ptr %sizePtr43, align 4
  %json = alloca ptr, align 8
  store ptr null, ptr %json, align 8
  %ser_r = call ptr @__aiml_ser_Shape(ptr %sh)
  store ptr %ser_r, ptr %json, align 8
  %json_val = load ptr, ptr %json, align 8
  %52 = call ptr @__aiml_str_concat(ptr @1, ptr %json_val)
  %print_null_check = icmp eq ptr %52, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %52
  %print_call = call i32 (ptr, ...) @printf(ptr @2, ptr %print_safe_str)
  %sh2 = alloca %Shape, align 8
  store %Shape zeroinitializer, ptr %sh2, align 8
  %json_val52 = load ptr, ptr %json, align 8
  %deser_r = call ptr @__aiml_deser_Shape(ptr %json_val52)
  %53 = call ptr @memcpy(ptr %sh2, ptr %deser_r, i64 24)
  %prop_ptr = getelementptr inbounds %Shape, ptr %sh2, i32 0, i32 0
  %name_val = load ptr, ptr %prop_ptr, align 8
  %54 = icmp eq ptr %name_val, null
  %55 = icmp eq ptr %name_val, @3
  %56 = xor i1 %55, true
  %57 = or i1 %54, false
  br i1 %57, label %cmp_check_null_ne, label %cmp_do_strcmp_ne

cmp_check_null_ne:                                ; preds = %cont48
  br label %cmp_merge_ne

cmp_do_strcmp_ne:                                 ; preds = %cont48
  %58 = call i32 @strcmp(ptr %name_val, ptr @3)
  %59 = icmp ne i32 %58, 0
  br label %cmp_merge_ne

cmp_merge_ne:                                     ; preds = %cmp_do_strcmp_ne, %cmp_check_null_ne
  %strcmp_res_ne = phi i1 [ %56, %cmp_check_null_ne ], [ %59, %cmp_do_strcmp_ne ]
  br i1 %strcmp_res_ne, label %if_then, label %if_merge

if_then:                                          ; preds = %cmp_merge_ne
  %prop_ptr53 = getelementptr inbounds %Shape, ptr %sh2, i32 0, i32 0
  %name_val54 = load ptr, ptr %prop_ptr53, align 8
  %60 = call ptr @__aiml_str_concat(ptr @4, ptr %name_val54)
  %print_null_check55 = icmp eq ptr %60, null
  %print_safe_str56 = select i1 %print_null_check55, ptr @print_null_str.5, ptr %60
  %print_call57 = call i32 (ptr, ...) @printf(ptr @5, ptr %print_safe_str56)
  call void @exit(i32 1)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %cmp_merge_ne
  %chain_gep = getelementptr inbounds %Shape, ptr %sh2, i32 0, i32 1
  %sizePtr58 = getelementptr inbounds %GenericList, ptr %chain_gep, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr58, align 4
  %61 = icmp ne i32 %sizeVal, 2
  br i1 %61, label %if_then59, label %if_merge60

if_then59:                                        ; preds = %if_merge
  %chain_gep61 = getelementptr inbounds %Shape, ptr %sh2, i32 0, i32 1
  %sizePtr62 = getelementptr inbounds %GenericList, ptr %chain_gep61, i32 0, i32 0
  %sizeVal63 = load i32, ptr %sizePtr62, align 4
  %62 = call ptr @__aiml_int_to_string(i32 %sizeVal63)
  %63 = call ptr @__aiml_str_concat(ptr @6, ptr %62)
  %print_null_check64 = icmp eq ptr %63, null
  %print_safe_str65 = select i1 %print_null_check64, ptr @print_null_str.6, ptr %63
  %print_call66 = call i32 (ptr, ...) @printf(ptr @7, ptr %print_safe_str65)
  call void @exit(i32 1)
  br label %if_merge60

if_merge60:                                       ; preds = %if_then59, %if_merge
  %gs0 = alloca %list_Point, align 8
  store %list_Point zeroinitializer, ptr %gs0, align 8
  %sizePtr67 = getelementptr inbounds %GenericList, ptr %gs0, i32 0, i32 0
  store i32 0, ptr %sizePtr67, align 4
  %capPtr68 = getelementptr inbounds %GenericList, ptr %gs0, i32 0, i32 1
  store i32 4, ptr %capPtr68, align 4
  %64 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%Point, ptr null, i32 1) to i64), i64 4))
  %dataPtr69 = getelementptr inbounds %GenericList, ptr %gs0, i32 0, i32 2
  store ptr %64, ptr %dataPtr69, align 8
  %set_prop_ptr_w70 = getelementptr inbounds %Shape, ptr %sh2, i32 0, i32 1
  %dataPtr71 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w70, i32 0, i32 2
  %dataBase72 = load ptr, ptr %dataPtr71, align 8
  %65 = getelementptr inbounds %list_Point, ptr %dataBase72, i32 0
  %66 = call ptr @memcpy(ptr %gs0, ptr %65, i64 16)
  %sizePtr73 = getelementptr inbounds %GenericList, ptr %gs0, i32 0, i32 0
  %sizeVal74 = load i32, ptr %sizePtr73, align 4
  %67 = icmp ne i32 %sizeVal74, 2
  br i1 %67, label %if_then75, label %if_merge76

if_then75:                                        ; preds = %if_merge60
  %sizePtr77 = getelementptr inbounds %GenericList, ptr %gs0, i32 0, i32 0
  %sizeVal78 = load i32, ptr %sizePtr77, align 4
  %68 = call ptr @__aiml_int_to_string(i32 %sizeVal78)
  %69 = call ptr @__aiml_str_concat(ptr @8, ptr %68)
  %print_null_check79 = icmp eq ptr %69, null
  %print_safe_str80 = select i1 %print_null_check79, ptr @print_null_str.7, ptr %69
  %print_call81 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str80)
  call void @exit(i32 1)
  br label %if_merge76

if_merge76:                                       ; preds = %if_then75, %if_merge60
  %gp0 = alloca ptr, align 8
  store ptr null, ptr %gp0, align 8
  %dataPtr82 = getelementptr inbounds %GenericList, ptr %gs0, i32 0, i32 2
  %dataBase83 = load ptr, ptr %dataPtr82, align 8
  %70 = getelementptr inbounds %Point, ptr %dataBase83, i32 0
  store ptr %70, ptr %gp0, align 8
  %prop_ptr_base = load ptr, ptr %gp0, align 8
  %prop_ptr84 = getelementptr inbounds %Point, ptr %prop_ptr_base, i32 0, i32 0
  %x_val = load i32, ptr %prop_ptr84, align 4
  %71 = icmp ne i32 %x_val, 1
  br i1 %71, label %if_then85, label %if_merge86

if_then85:                                        ; preds = %if_merge76
  %prop_ptr_base87 = load ptr, ptr %gp0, align 8
  %prop_ptr88 = getelementptr inbounds %Point, ptr %prop_ptr_base87, i32 0, i32 0
  %x_val89 = load i32, ptr %prop_ptr88, align 4
  %72 = call ptr @__aiml_int_to_string(i32 %x_val89)
  %73 = call ptr @__aiml_str_concat(ptr @10, ptr %72)
  %print_null_check90 = icmp eq ptr %73, null
  %print_safe_str91 = select i1 %print_null_check90, ptr @print_null_str.8, ptr %73
  %print_call92 = call i32 (ptr, ...) @printf(ptr @11, ptr %print_safe_str91)
  call void @exit(i32 1)
  br label %if_merge86

if_merge86:                                       ; preds = %if_then85, %if_merge76
  %prop_ptr_base93 = load ptr, ptr %gp0, align 8
  %prop_ptr94 = getelementptr inbounds %Point, ptr %prop_ptr_base93, i32 0, i32 1
  %y_val = load i32, ptr %prop_ptr94, align 4
  %74 = icmp ne i32 %y_val, 2
  br i1 %74, label %if_then95, label %if_merge96

if_then95:                                        ; preds = %if_merge86
  %prop_ptr_base97 = load ptr, ptr %gp0, align 8
  %prop_ptr98 = getelementptr inbounds %Point, ptr %prop_ptr_base97, i32 0, i32 1
  %y_val99 = load i32, ptr %prop_ptr98, align 4
  %75 = call ptr @__aiml_int_to_string(i32 %y_val99)
  %76 = call ptr @__aiml_str_concat(ptr @12, ptr %75)
  %print_null_check100 = icmp eq ptr %76, null
  %print_safe_str101 = select i1 %print_null_check100, ptr @print_null_str.9, ptr %76
  %print_call102 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str101)
  call void @exit(i32 1)
  br label %if_merge96

if_merge96:                                       ; preds = %if_then95, %if_merge86
  %gp1 = alloca ptr, align 8
  store ptr null, ptr %gp1, align 8
  %dataPtr103 = getelementptr inbounds %GenericList, ptr %gs0, i32 0, i32 2
  %dataBase104 = load ptr, ptr %dataPtr103, align 8
  %77 = getelementptr inbounds %Point, ptr %dataBase104, i32 1
  store ptr %77, ptr %gp1, align 8
  %prop_ptr_base105 = load ptr, ptr %gp1, align 8
  %prop_ptr106 = getelementptr inbounds %Point, ptr %prop_ptr_base105, i32 0, i32 0
  %x_val107 = load i32, ptr %prop_ptr106, align 4
  %78 = icmp ne i32 %x_val107, 3
  br i1 %78, label %if_then108, label %if_merge109

if_then108:                                       ; preds = %if_merge96
  %prop_ptr_base110 = load ptr, ptr %gp1, align 8
  %prop_ptr111 = getelementptr inbounds %Point, ptr %prop_ptr_base110, i32 0, i32 0
  %x_val112 = load i32, ptr %prop_ptr111, align 4
  %79 = call ptr @__aiml_int_to_string(i32 %x_val112)
  %80 = call ptr @__aiml_str_concat(ptr @14, ptr %79)
  %print_null_check113 = icmp eq ptr %80, null
  %print_safe_str114 = select i1 %print_null_check113, ptr @print_null_str.10, ptr %80
  %print_call115 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str114)
  call void @exit(i32 1)
  br label %if_merge109

if_merge109:                                      ; preds = %if_then108, %if_merge96
  %gs1 = alloca %list_Point, align 8
  store %list_Point zeroinitializer, ptr %gs1, align 8
  %sizePtr116 = getelementptr inbounds %GenericList, ptr %gs1, i32 0, i32 0
  store i32 0, ptr %sizePtr116, align 4
  %capPtr117 = getelementptr inbounds %GenericList, ptr %gs1, i32 0, i32 1
  store i32 4, ptr %capPtr117, align 4
  %81 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%Point, ptr null, i32 1) to i64), i64 4))
  %dataPtr118 = getelementptr inbounds %GenericList, ptr %gs1, i32 0, i32 2
  store ptr %81, ptr %dataPtr118, align 8
  %set_prop_ptr_w119 = getelementptr inbounds %Shape, ptr %sh2, i32 0, i32 1
  %dataPtr120 = getelementptr inbounds %GenericList, ptr %set_prop_ptr_w119, i32 0, i32 2
  %dataBase121 = load ptr, ptr %dataPtr120, align 8
  %82 = getelementptr inbounds %list_Point, ptr %dataBase121, i32 1
  %83 = call ptr @memcpy(ptr %gs1, ptr %82, i64 16)
  %sizePtr122 = getelementptr inbounds %GenericList, ptr %gs1, i32 0, i32 0
  %sizeVal123 = load i32, ptr %sizePtr122, align 4
  %84 = icmp ne i32 %sizeVal123, 1
  br i1 %84, label %if_then124, label %if_merge125

if_then124:                                       ; preds = %if_merge109
  %sizePtr126 = getelementptr inbounds %GenericList, ptr %gs1, i32 0, i32 0
  %sizeVal127 = load i32, ptr %sizePtr126, align 4
  %85 = call ptr @__aiml_int_to_string(i32 %sizeVal127)
  %86 = call ptr @__aiml_str_concat(ptr @16, ptr %85)
  %print_null_check128 = icmp eq ptr %86, null
  %print_safe_str129 = select i1 %print_null_check128, ptr @print_null_str.11, ptr %86
  %print_call130 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str129)
  call void @exit(i32 1)
  br label %if_merge125

if_merge125:                                      ; preds = %if_then124, %if_merge109
  %gp2 = alloca ptr, align 8
  store ptr null, ptr %gp2, align 8
  %dataPtr131 = getelementptr inbounds %GenericList, ptr %gs1, i32 0, i32 2
  %dataBase132 = load ptr, ptr %dataPtr131, align 8
  %87 = getelementptr inbounds %Point, ptr %dataBase132, i32 0
  store ptr %87, ptr %gp2, align 8
  %prop_ptr_base133 = load ptr, ptr %gp2, align 8
  %prop_ptr134 = getelementptr inbounds %Point, ptr %prop_ptr_base133, i32 0, i32 0
  %x_val135 = load i32, ptr %prop_ptr134, align 4
  %88 = icmp ne i32 %x_val135, 5
  br i1 %88, label %if_then136, label %if_merge137

if_then136:                                       ; preds = %if_merge125
  %prop_ptr_base138 = load ptr, ptr %gp2, align 8
  %prop_ptr139 = getelementptr inbounds %Point, ptr %prop_ptr_base138, i32 0, i32 0
  %x_val140 = load i32, ptr %prop_ptr139, align 4
  %89 = call ptr @__aiml_int_to_string(i32 %x_val140)
  %90 = call ptr @__aiml_str_concat(ptr @18, ptr %89)
  %print_null_check141 = icmp eq ptr %90, null
  %print_safe_str142 = select i1 %print_null_check141, ptr @print_null_str.12, ptr %90
  %print_call143 = call i32 (ptr, ...) @printf(ptr @19, ptr %print_safe_str142)
  call void @exit(i32 1)
  br label %if_merge137

if_merge137:                                      ; preds = %if_then136, %if_merge125
  %print_call144 = call i32 (ptr, ...) @printf(ptr @21, ptr @20)
  ret i32 0

ret_dead:                                         ; No predecessors!
  ret i32 0
}

define void @__aiml_init() {
entry:
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare ptr @malloc(i64)

declare ptr @realloc(ptr, i64)

; Function Attrs: noinline
define internal ptr @__aiml_ser_Shape(ptr %sp) #0 {
entry:
  %w = call ptr @aiml_jw_new()
  call void @aiml_jw_set_type(ptr %w, ptr @tn)
  %fp = getelementptr inbounds %Shape, ptr %sp, i32 0, i32 0
  %sv = load ptr, ptr %fp, align 8
  call void @aiml_jw_set_str(ptr %w, ptr @fk_name, ptr %sv)
  %lfp = getelementptr inbounds %Shape, ptr %sp, i32 0, i32 1
  %lsg = getelementptr inbounds %"list_list<Point>", ptr %lfp, i32 0, i32 0
  %lsz = load i32, ptr %lsg, align 4
  %ldg = getelementptr inbounds %"list_list<Point>", ptr %lfp, i32 0, i32 2
  %ldp = load ptr, ptr %ldg, align 8
  call void @aiml_jw_arr_begin(ptr %w, ptr @fk_segments)
  br label %segments_el_lc

segments_el_lc:                                   ; preds = %segments_eli_le, %entry
  %segments_el_i = phi i32 [ 0, %entry ], [ %ni4, %segments_eli_le ]
  %0 = icmp slt i32 %segments_el_i, %lsz
  br i1 %0, label %segments_el_lb, label %segments_el_le

segments_el_lb:                                   ; preds = %segments_el_lc
  %segments_el_ep = getelementptr %list_Point, ptr %ldp, i32 %segments_el_i
  %isg = getelementptr inbounds %list_Point, ptr %segments_el_ep, i32 0, i32 0
  %isz = load i32, ptr %isg, align 4
  %idg = getelementptr inbounds %list_Point, ptr %segments_el_ep, i32 0, i32 2
  %idp = load ptr, ptr %idg, align 8
  call void @aiml_jw_arr_arr_begin(ptr %w)
  br label %segments_eli_lc

segments_el_le:                                   ; preds = %segments_el_lc
  call void @aiml_jw_arr_end(ptr %w)
  %json = call ptr @aiml_jw_done(ptr %w)
  ret ptr %json

segments_eli_lc:                                  ; preds = %segments_eli_lb, %segments_el_lb
  %segments_eli_i = phi i32 [ 0, %segments_el_lb ], [ %ni, %segments_eli_lb ]
  %1 = icmp slt i32 %segments_eli_i, %isz
  br i1 %1, label %segments_eli_lb, label %segments_eli_le

segments_eli_lb:                                  ; preds = %segments_eli_lc
  %segments_eli_ep = getelementptr %Point, ptr %idp, i32 %segments_eli_i
  call void @aiml_jw_arr_obj_begin(ptr %w)
  %fp1 = getelementptr inbounds %Point, ptr %segments_eli_ep, i32 0, i32 0
  %v = load i32, ptr %fp1, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_x, i32 %v)
  %fp2 = getelementptr inbounds %Point, ptr %segments_eli_ep, i32 0, i32 1
  %v3 = load i32, ptr %fp2, align 4
  call void @aiml_jw_set_i32(ptr %w, ptr @fk_y, i32 %v3)
  call void @aiml_jw_arr_obj_end(ptr %w)
  %ni = add i32 %segments_eli_i, 1
  br label %segments_eli_lc

segments_eli_le:                                  ; preds = %segments_eli_lc
  call void @aiml_jw_arr_arr_end(ptr %w)
  %ni4 = add i32 %segments_el_i, 1
  br label %segments_el_lc
}

declare ptr @aiml_jw_new()

declare void @aiml_jw_set_type(ptr, ptr)

declare void @aiml_jw_set_str(ptr, ptr, ptr)

declare void @aiml_jw_arr_begin(ptr, ptr)

declare void @aiml_jw_arr_arr_begin(ptr)

declare void @aiml_jw_arr_obj_begin(ptr)

declare void @aiml_jw_set_i32(ptr, ptr, i32)

declare void @aiml_jw_arr_obj_end(ptr)

declare void @aiml_jw_arr_arr_end(ptr)

declare void @aiml_jw_arr_end(ptr)

declare ptr @aiml_jw_done(ptr)

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

declare i32 @printf(ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_deser_Shape(ptr %json) #0 {
entry:
  %r = call ptr @aiml_jr_parse(ptr %json)
  %hp = call ptr @malloc(i64 24)
  call void @llvm.memset.p0.i64(ptr align 8 %hp, i8 0, i64 24, i1 false)
  %fp_name = getelementptr inbounds %Shape, ptr %hp, i32 0, i32 0
  %sv = call ptr @aiml_jr_str(ptr %r, ptr @fk_name.1)
  store ptr %sv, ptr %fp_name, align 8
  %fp_segments = getelementptr inbounds %Shape, ptr %hp, i32 0, i32 1
  %sizePtr = getelementptr inbounds %GenericList, ptr %fp_segments, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %fp_segments, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %0 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%list_Point, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %fp_segments, i32 0, i32 2
  store ptr %0, ptr %dataPtr, align 8
  %cnt = call i32 @aiml_jr_arr_len(ptr %r, ptr @fk_segments.2)
  br label %segments_dlc

segments_dlc:                                     ; preds = %cont16, %entry
  %dli = phi i32 [ 0, %entry ], [ %ni20, %cont16 ]
  %1 = icmp slt i32 %dli, %cnt
  br i1 %1, label %segments_dlb, label %segments_dle

segments_dlb:                                     ; preds = %segments_dlc
  %ila = alloca %list_Point, align 8
  %sizePtr1 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 0
  store i32 0, ptr %sizePtr1, align 4
  %capPtr2 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 1
  store i32 4, ptr %capPtr2, align 4
  %2 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%Point, ptr null, i32 1) to i64), i64 4))
  %dataPtr3 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  store ptr %2, ptr %dataPtr3, align 8
  %isr = call ptr @aiml_jr_arr_sub(ptr %r, ptr @fk_segments.2, i32 %dli)
  %3 = icmp ne ptr %isr, null
  br i1 %3, label %segments_isrok, label %segments_isrdone

segments_dle:                                     ; preds = %segments_dlc
  call void @aiml_jr_free(ptr %r)
  ret ptr %hp

segments_isrok:                                   ; preds = %segments_dlb
  %cnt4 = call i32 @aiml_jr_val_len(ptr %isr)
  br label %segments_il_lc

segments_isrdone:                                 ; preds = %segments_il_le, %segments_dlb
  %ilval = load %list_Point, ptr %ila, align 8
  %sizePtr11 = getelementptr inbounds %GenericList, ptr %fp_segments, i32 0, i32 0
  %capPtr12 = getelementptr inbounds %GenericList, ptr %fp_segments, i32 0, i32 1
  %size13 = load i32, ptr %sizePtr11, align 4
  %capacity14 = load i32, ptr %capPtr12, align 4
  %4 = icmp eq i32 %size13, %capacity14
  br i1 %4, label %grow15, label %cont16

segments_il_lc:                                   ; preds = %segments_il_srdone, %segments_isrok
  %segments_il_i = phi i32 [ 0, %segments_isrok ], [ %ni, %segments_il_srdone ]
  %5 = icmp slt i32 %segments_il_i, %cnt4
  br i1 %5, label %segments_il_lb, label %segments_il_le

segments_il_lb:                                   ; preds = %segments_il_lc
  %sr = call ptr @aiml_jr_val_sub(ptr %isr, i32 %segments_il_i)
  %6 = icmp ne ptr %sr, null
  br i1 %6, label %segments_il_srok, label %segments_il_srdone

segments_il_le:                                   ; preds = %segments_il_lc
  call void @aiml_jr_free(ptr %isr)
  br label %segments_isrdone

segments_il_srok:                                 ; preds = %segments_il_lb
  %stmp = alloca %Point, align 8
  call void @llvm.memset.p0.i64(ptr align 8 %stmp, i8 0, i64 8, i1 false)
  %fp_x = getelementptr inbounds %Point, ptr %stmp, i32 0, i32 0
  %sv5 = call i32 @aiml_jr_i32(ptr %sr, ptr @fk_x.3)
  store i32 %sv5, ptr %fp_x, align 4
  %fp_y = getelementptr inbounds %Point, ptr %stmp, i32 0, i32 1
  %sv6 = call i32 @aiml_jr_i32(ptr %sr, ptr @fk_y.4)
  store i32 %sv6, ptr %fp_y, align 4
  %sval = load %Point, ptr %stmp, align 4
  %sizePtr7 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 0
  %capPtr8 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 1
  %size = load i32, ptr %sizePtr7, align 4
  %capacity = load i32, ptr %capPtr8, align 4
  %7 = icmp eq i32 %size, %capacity
  br i1 %7, label %grow, label %cont

segments_il_srdone:                               ; preds = %cont, %segments_il_lb
  %ni = add i32 %segments_il_i, 1
  br label %segments_il_lc

grow:                                             ; preds = %segments_il_srok
  %8 = mul i32 %capacity, 2
  %9 = icmp sgt i32 %8, 0
  %10 = select i1 %9, i32 %8, i32 4
  store i32 %10, ptr %capPtr8, align 4
  %dataPtr9 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  %11 = load ptr, ptr %dataPtr9, align 8
  %12 = zext i32 %10 to i64
  %13 = mul i64 ptrtoint (ptr getelementptr inbounds (%Point, ptr null, i32 1) to i64), %12
  %14 = call ptr @realloc(ptr %11, i64 %13)
  store ptr %14, ptr %dataPtr9, align 8
  br label %cont

cont:                                             ; preds = %grow, %segments_il_srok
  %dataPtr10 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr10, align 8
  %15 = getelementptr inbounds %Point, ptr %dataBase, i32 %size
  store %Point %sval, ptr %15, align 4
  %16 = add i32 %size, 1
  store i32 %16, ptr %sizePtr7, align 4
  call void @aiml_jr_free(ptr %sr)
  br label %segments_il_srdone

grow15:                                           ; preds = %segments_isrdone
  %17 = mul i32 %capacity14, 2
  %18 = icmp sgt i32 %17, 0
  %19 = select i1 %18, i32 %17, i32 4
  store i32 %19, ptr %capPtr12, align 4
  %dataPtr17 = getelementptr inbounds %GenericList, ptr %fp_segments, i32 0, i32 2
  %20 = load ptr, ptr %dataPtr17, align 8
  %21 = zext i32 %19 to i64
  %22 = mul i64 ptrtoint (ptr getelementptr inbounds (%list_Point, ptr null, i32 1) to i64), %21
  %23 = call ptr @realloc(ptr %20, i64 %22)
  store ptr %23, ptr %dataPtr17, align 8
  br label %cont16

cont16:                                           ; preds = %grow15, %segments_isrdone
  %dataPtr18 = getelementptr inbounds %GenericList, ptr %fp_segments, i32 0, i32 2
  %dataBase19 = load ptr, ptr %dataPtr18, align 8
  %24 = getelementptr inbounds %list_Point, ptr %dataBase19, i32 %size13
  store %list_Point %ilval, ptr %24, align 8
  %25 = add i32 %size13, 1
  store i32 %25, ptr %sizePtr11, align 4
  %ni20 = add i32 %dli, 1
  br label %segments_dlc
}

declare ptr @aiml_jr_parse(ptr)

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #1

declare ptr @aiml_jr_str(ptr, ptr)

declare i32 @aiml_jr_arr_len(ptr, ptr)

declare ptr @aiml_jr_arr_sub(ptr, ptr, i32)

declare i32 @aiml_jr_val_len(ptr)

declare ptr @aiml_jr_val_sub(ptr, i32)

declare i32 @aiml_jr_i32(ptr, ptr)

declare void @aiml_jr_free(ptr)

declare ptr @memcpy(ptr, ptr, i64)

declare i32 @strcmp(ptr, ptr)

declare void @exit(i32)

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

attributes #0 = { noinline }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: write) }
