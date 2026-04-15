; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%AimlClassSchema = type { ptr, i32, i32, i32, i32, [32 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%ManaBag = type { ptr, %map_ManaColor_i32 }
%map_ManaColor_i32 = type { i32, i32, ptr, ptr, ptr }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_ManaBag = private unnamed_addr constant [5 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 2, i32 1, i32 0, i32 40, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 3, i32 4, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@0 = private unnamed_addr constant [37 x i8] c"/tmp/aiml_test_map_enum_key_persist/\00", align 1
@1 = private unnamed_addr constant [21 x i8] c"map_enum_key_persist\00", align 1
@2 = private unnamed_addr constant [9 x i8] c"enum-map\00", align 1
@3 = private unnamed_addr constant [15 x i8] c"FAIL pre_fire=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [16 x i8] c"FAIL pre_earth=\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"bag\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"bag\00", align 1
@9 = private unnamed_addr constant [14 x i8] c"FAIL got=null\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@10 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@11 = private unnamed_addr constant [9 x i8] c"enum-map\00", align 1
@12 = private unnamed_addr constant [15 x i8] c"FAIL got.name=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [15 x i8] c"FAIL fire_amt=\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [16 x i8] c"FAIL earth_amt=\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [26 x i8] c"map enum key persist PASS\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @main, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @main]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_main = private constant [5 x i8] c"main\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_main]
@__aiml_func_count = constant i32 1
@__aiml_sname_ManaBag = private constant [8 x i8] c"ManaBag\00"
@__aiml_sfield_ManaBag_name = private constant [5 x i8] c"name\00"
@__aiml_sfield_ManaBag_mana_cost = private constant [10 x i8] c"mana_cost\00"
@__aiml_schemas = global [1 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_ManaBag, i32 1, i32 2, i32 40, i32 64, [32 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_ManaBag_name }, %AimlFieldInfo { ptr @__aiml_sfield_ManaBag_mana_cost }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 1

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %kv = alloca ptr, align 8
  store ptr null, ptr %kv, align 8
  %0 = call ptr @aiml_db_kv_open_typed_persist(ptr @1, i64 40, ptr @0, ptr @__aiml_sernodes_ManaBag, i32 5)
  store ptr %0, ptr %kv, align 8
  %bag = alloca %ManaBag, align 8
  store %ManaBag zeroinitializer, ptr %bag, align 8
  %set_prop_ptr_w = getelementptr inbounds %ManaBag, ptr %bag, i32 0, i32 0
  store ptr @2, ptr %set_prop_ptr_w, align 8
  %mana_cost_ptr = getelementptr inbounds %ManaBag, ptr %bag, i32 0, i32 1
  %sizePtr = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr, i32 0, i32 0
  %capPtr = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr, i32 0, i32 1
  %size = load i32, ptr %sizePtr, align 4
  %capacity = load i32, ptr %capPtr, align 4
  %1 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr, i32 0, i32 3
  %keysBase = load ptr, ptr %1, align 8
  %2 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr, i32 0, i32 4
  %valsBase = load ptr, ptr %2, align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

put_loop:                                         ; preds = %put_next, %entry
  %3 = load i32, ptr %i_put, align 4
  %4 = icmp slt i32 %3, %size
  br i1 %4, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %5 = getelementptr inbounds i32, ptr %keysBase, i32 %3
  %6 = load i32, ptr %5, align 4
  %key_eq = icmp eq i32 1, %6
  br i1 %key_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %7 = getelementptr inbounds i32, ptr %valsBase, i32 %3
  store i32 2, ptr %7, align 4
  br label %put_end

put_next:                                         ; preds = %put_check
  %8 = add i32 %3, 1
  store i32 %8, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %9 = icmp eq i32 %size, %capacity
  br i1 %9, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %mana_cost_ptr1 = getelementptr inbounds %ManaBag, ptr %bag, i32 0, i32 1
  %sizePtr2 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr1, i32 0, i32 0
  %capPtr3 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr1, i32 0, i32 1
  %size4 = load i32, ptr %sizePtr2, align 4
  %capacity5 = load i32, ptr %capPtr3, align 4
  %10 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr1, i32 0, i32 3
  %keysBase6 = load ptr, ptr %10, align 8
  %11 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr1, i32 0, i32 4
  %valsBase7 = load ptr, ptr %11, align 8
  %i_put14 = alloca i32, align 4
  store i32 0, ptr %i_put14, align 4
  br label %put_loop8

put_grow:                                         ; preds = %put_not_found
  %12 = mul i32 %capacity, 2
  store i32 %12, ptr %capPtr, align 4
  %13 = zext i32 %12 to i64
  %14 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %13
  %15 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr, i32 0, i32 3
  %16 = load ptr, ptr %15, align 8
  %17 = call ptr @realloc(ptr %16, i64 %14)
  store ptr %17, ptr %15, align 8
  %18 = zext i32 %12 to i64
  %19 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %18
  %20 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr, i32 0, i32 4
  %21 = load ptr, ptr %20, align 8
  %22 = call ptr @realloc(ptr %21, i64 %19)
  store ptr %22, ptr %20, align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %23 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr, i32 0, i32 3
  %24 = load ptr, ptr %23, align 8
  %25 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr, i32 0, i32 4
  %26 = load ptr, ptr %25, align 8
  %27 = getelementptr inbounds i32, ptr %24, i32 %size
  store i32 1, ptr %27, align 4
  %28 = getelementptr inbounds i32, ptr %26, i32 %size
  store i32 2, ptr %28, align 4
  %occMem = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr, i32 0, i32 2
  %occBase = load ptr, ptr %occMem, align 8
  %occSlot = getelementptr inbounds i8, ptr %occBase, i32 %size
  store i8 1, ptr %occSlot, align 1
  %29 = add i32 %size, 1
  store i32 %29, ptr %sizePtr, align 4
  br label %put_end

put_loop8:                                        ; preds = %put_next11, %put_end
  %30 = load i32, ptr %i_put14, align 4
  %31 = icmp slt i32 %30, %size4
  br i1 %31, label %put_check9, label %put_not_found12

put_check9:                                       ; preds = %put_loop8
  %32 = getelementptr inbounds i32, ptr %keysBase6, i32 %30
  %33 = load i32, ptr %32, align 4
  %key_eq15 = icmp eq i32 5, %33
  br i1 %key_eq15, label %put_found10, label %put_next11

put_found10:                                      ; preds = %put_check9
  %34 = getelementptr inbounds i32, ptr %valsBase7, i32 %30
  store i32 7, ptr %34, align 4
  br label %put_end13

put_next11:                                       ; preds = %put_check9
  %35 = add i32 %30, 1
  store i32 %35, ptr %i_put14, align 4
  br label %put_loop8

put_not_found12:                                  ; preds = %put_loop8
  %36 = icmp eq i32 %size4, %capacity5
  br i1 %36, label %put_grow16, label %put_append17

put_end13:                                        ; preds = %put_append17, %put_found10
  %pre_fire = alloca i32, align 4
  store i32 0, ptr %pre_fire, align 4
  %mana_cost_ptr21 = getelementptr inbounds %ManaBag, ptr %bag, i32 0, i32 1
  %37 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr21, i32 0, i32 0
  %38 = load i32, ptr %37, align 4
  %39 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr21, i32 0, i32 3
  %40 = load ptr, ptr %39, align 8
  %41 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr21, i32 0, i32 4
  %42 = load ptr, ptr %41, align 8
  %i_get = alloca i32, align 4
  store i32 0, ptr %i_get, align 4
  %res_get = alloca i32, align 4
  store i32 0, ptr %res_get, align 4
  br label %get_loop

put_grow16:                                       ; preds = %put_not_found12
  %43 = mul i32 %capacity5, 2
  store i32 %43, ptr %capPtr3, align 4
  %44 = zext i32 %43 to i64
  %45 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %44
  %46 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr1, i32 0, i32 3
  %47 = load ptr, ptr %46, align 8
  %48 = call ptr @realloc(ptr %47, i64 %45)
  store ptr %48, ptr %46, align 8
  %49 = zext i32 %43 to i64
  %50 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %49
  %51 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr1, i32 0, i32 4
  %52 = load ptr, ptr %51, align 8
  %53 = call ptr @realloc(ptr %52, i64 %50)
  store ptr %53, ptr %51, align 8
  br label %put_append17

put_append17:                                     ; preds = %put_grow16, %put_not_found12
  %54 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr1, i32 0, i32 3
  %55 = load ptr, ptr %54, align 8
  %56 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr1, i32 0, i32 4
  %57 = load ptr, ptr %56, align 8
  %58 = getelementptr inbounds i32, ptr %55, i32 %size4
  store i32 5, ptr %58, align 4
  %59 = getelementptr inbounds i32, ptr %57, i32 %size4
  store i32 7, ptr %59, align 4
  %occMem18 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr1, i32 0, i32 2
  %occBase19 = load ptr, ptr %occMem18, align 8
  %occSlot20 = getelementptr inbounds i8, ptr %occBase19, i32 %size4
  store i8 1, ptr %occSlot20, align 1
  %60 = add i32 %size4, 1
  store i32 %60, ptr %sizePtr2, align 4
  br label %put_end13

get_loop:                                         ; preds = %get_next, %put_end13
  %61 = load i32, ptr %i_get, align 4
  %62 = icmp slt i32 %61, %38
  br i1 %62, label %get_check, label %get_end

get_check:                                        ; preds = %get_loop
  %63 = getelementptr inbounds i32, ptr %40, i32 %61
  %64 = load i32, ptr %63, align 4
  %key_eq22 = icmp eq i32 1, %64
  br i1 %key_eq22, label %get_found, label %get_next

get_found:                                        ; preds = %get_check
  %65 = getelementptr inbounds i32, ptr %42, i32 %61
  %66 = load i32, ptr %65, align 4
  store i32 %66, ptr %res_get, align 4
  br label %get_end

get_end:                                          ; preds = %get_found, %get_loop
  %get_res = load i32, ptr %res_get, align 4
  store i32 %get_res, ptr %pre_fire, align 4
  %pre_earth = alloca i32, align 4
  store i32 0, ptr %pre_earth, align 4
  %mana_cost_ptr23 = getelementptr inbounds %ManaBag, ptr %bag, i32 0, i32 1
  %67 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr23, i32 0, i32 0
  %68 = load i32, ptr %67, align 4
  %69 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr23, i32 0, i32 3
  %70 = load ptr, ptr %69, align 8
  %71 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr23, i32 0, i32 4
  %72 = load ptr, ptr %71, align 8
  %i_get28 = alloca i32, align 4
  store i32 0, ptr %i_get28, align 4
  %res_get29 = alloca i32, align 4
  store i32 0, ptr %res_get29, align 4
  br label %get_loop24

get_next:                                         ; preds = %get_check
  %73 = add i32 %61, 1
  store i32 %73, ptr %i_get, align 4
  br label %get_loop

get_loop24:                                       ; preds = %get_next31, %get_end
  %74 = load i32, ptr %i_get28, align 4
  %75 = icmp slt i32 %74, %68
  br i1 %75, label %get_check25, label %get_end27

get_check25:                                      ; preds = %get_loop24
  %76 = getelementptr inbounds i32, ptr %70, i32 %74
  %77 = load i32, ptr %76, align 4
  %key_eq30 = icmp eq i32 5, %77
  br i1 %key_eq30, label %get_found26, label %get_next31

get_found26:                                      ; preds = %get_check25
  %78 = getelementptr inbounds i32, ptr %72, i32 %74
  %79 = load i32, ptr %78, align 4
  store i32 %79, ptr %res_get29, align 4
  br label %get_end27

get_end27:                                        ; preds = %get_found26, %get_loop24
  %get_res32 = load i32, ptr %res_get29, align 4
  store i32 %get_res32, ptr %pre_earth, align 4
  %pre_fire_val = load i32, ptr %pre_fire, align 4
  %80 = icmp ne i32 %pre_fire_val, 2
  br i1 %80, label %if_then, label %if_merge

get_next31:                                       ; preds = %get_check25
  %81 = add i32 %74, 1
  store i32 %81, ptr %i_get28, align 4
  br label %get_loop24

if_then:                                          ; preds = %get_end27
  %pre_fire_val33 = load i32, ptr %pre_fire, align 4
  %82 = call ptr @__aiml_int_to_string(i32 %pre_fire_val33)
  %83 = call ptr @__aiml_str_concat(ptr @3, ptr %82)
  %print_null_check = icmp eq ptr %83, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %83
  %print_call = call i32 (ptr, ...) @printf(ptr @4, ptr %print_safe_str)
  call void @exit(i32 1)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %get_end27
  %pre_earth_val = load i32, ptr %pre_earth, align 4
  %84 = icmp ne i32 %pre_earth_val, 7
  br i1 %84, label %if_then34, label %if_merge35

if_then34:                                        ; preds = %if_merge
  %pre_earth_val36 = load i32, ptr %pre_earth, align 4
  %85 = call ptr @__aiml_int_to_string(i32 %pre_earth_val36)
  %86 = call ptr @__aiml_str_concat(ptr @5, ptr %85)
  %print_null_check37 = icmp eq ptr %86, null
  %print_safe_str38 = select i1 %print_null_check37, ptr @print_null_str.1, ptr %86
  %print_call39 = call i32 (ptr, ...) @printf(ptr @6, ptr %print_safe_str38)
  call void @exit(i32 1)
  br label %if_merge35

if_merge35:                                       ; preds = %if_then34, %if_merge
  %bag_val = load %ManaBag, ptr %bag, align 8
  %kv_ptr = load ptr, ptr %kv, align 8
  %kv_val_tmp = alloca %ManaBag, align 8
  store %ManaBag %bag_val, ptr %kv_val_tmp, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr, ptr @7, ptr %kv_val_tmp, i64 40)
  %got = alloca ptr, align 8
  store ptr null, ptr %got, align 8
  %kv_ptr40 = load ptr, ptr %kv, align 8
  %87 = call ptr @aiml_db_kv_get(ptr %kv_ptr40, ptr @8)
  store ptr %87, ptr %got, align 8
  %got_val = load ptr, ptr %got, align 8
  %88 = icmp eq ptr %got_val, null
  br i1 %88, label %if_then41, label %if_merge42

if_then41:                                        ; preds = %if_merge35
  %print_call43 = call i32 (ptr, ...) @printf(ptr @10, ptr @9)
  call void @exit(i32 1)
  br label %if_merge42

if_merge42:                                       ; preds = %if_then41, %if_merge35
  %prop_ptr_base = load ptr, ptr %got, align 8
  %prop_ptr = getelementptr inbounds %ManaBag, ptr %prop_ptr_base, i32 0, i32 0
  %name_val = load ptr, ptr %prop_ptr, align 8
  %89 = icmp eq ptr %name_val, null
  %90 = icmp eq ptr %name_val, @11
  %91 = or i1 %89, false
  br i1 %91, label %cmp_check_null, label %cmp_do_strcmp

cmp_check_null:                                   ; preds = %if_merge42
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %if_merge42
  %92 = call i32 @strcmp(ptr %name_val, ptr @11)
  %93 = icmp eq i32 %92, 0
  br label %cmp_merge

cmp_merge:                                        ; preds = %cmp_do_strcmp, %cmp_check_null
  %strcmp_res = phi i1 [ %90, %cmp_check_null ], [ %93, %cmp_do_strcmp ]
  %94 = sext i1 %strcmp_res to i32
  %95 = icmp eq i32 %94, 0
  br i1 %95, label %if_then44, label %if_merge45

if_then44:                                        ; preds = %cmp_merge
  %prop_ptr_base46 = load ptr, ptr %got, align 8
  %prop_ptr47 = getelementptr inbounds %ManaBag, ptr %prop_ptr_base46, i32 0, i32 0
  %name_val48 = load ptr, ptr %prop_ptr47, align 8
  %96 = call ptr @__aiml_str_concat(ptr @12, ptr %name_val48)
  %print_null_check49 = icmp eq ptr %96, null
  %print_safe_str50 = select i1 %print_null_check49, ptr @print_null_str.3, ptr %96
  %print_call51 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str50)
  call void @exit(i32 1)
  br label %if_merge45

if_merge45:                                       ; preds = %if_then44, %cmp_merge
  %fire_amt = alloca i32, align 4
  store i32 0, ptr %fire_amt, align 4
  %deref_this = load ptr, ptr %got, align 8
  %mana_cost_ptr52 = getelementptr inbounds %ManaBag, ptr %deref_this, i32 0, i32 1
  %97 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr52, i32 0, i32 0
  %98 = load i32, ptr %97, align 4
  %99 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr52, i32 0, i32 3
  %100 = load ptr, ptr %99, align 8
  %101 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr52, i32 0, i32 4
  %102 = load ptr, ptr %101, align 8
  %i_get57 = alloca i32, align 4
  store i32 0, ptr %i_get57, align 4
  %res_get58 = alloca i32, align 4
  store i32 0, ptr %res_get58, align 4
  br label %get_loop53

get_loop53:                                       ; preds = %get_next60, %if_merge45
  %103 = load i32, ptr %i_get57, align 4
  %104 = icmp slt i32 %103, %98
  br i1 %104, label %get_check54, label %get_end56

get_check54:                                      ; preds = %get_loop53
  %105 = getelementptr inbounds i32, ptr %100, i32 %103
  %106 = load i32, ptr %105, align 4
  %key_eq59 = icmp eq i32 1, %106
  br i1 %key_eq59, label %get_found55, label %get_next60

get_found55:                                      ; preds = %get_check54
  %107 = getelementptr inbounds i32, ptr %102, i32 %103
  %108 = load i32, ptr %107, align 4
  store i32 %108, ptr %res_get58, align 4
  br label %get_end56

get_end56:                                        ; preds = %get_found55, %get_loop53
  %get_res61 = load i32, ptr %res_get58, align 4
  store i32 %get_res61, ptr %fire_amt, align 4
  %earth_amt = alloca i32, align 4
  store i32 0, ptr %earth_amt, align 4
  %deref_this62 = load ptr, ptr %got, align 8
  %mana_cost_ptr63 = getelementptr inbounds %ManaBag, ptr %deref_this62, i32 0, i32 1
  %109 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr63, i32 0, i32 0
  %110 = load i32, ptr %109, align 4
  %111 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr63, i32 0, i32 3
  %112 = load ptr, ptr %111, align 8
  %113 = getelementptr inbounds %map_ManaColor_i32, ptr %mana_cost_ptr63, i32 0, i32 4
  %114 = load ptr, ptr %113, align 8
  %i_get68 = alloca i32, align 4
  store i32 0, ptr %i_get68, align 4
  %res_get69 = alloca i32, align 4
  store i32 0, ptr %res_get69, align 4
  br label %get_loop64

get_next60:                                       ; preds = %get_check54
  %115 = add i32 %103, 1
  store i32 %115, ptr %i_get57, align 4
  br label %get_loop53

get_loop64:                                       ; preds = %get_next71, %get_end56
  %116 = load i32, ptr %i_get68, align 4
  %117 = icmp slt i32 %116, %110
  br i1 %117, label %get_check65, label %get_end67

get_check65:                                      ; preds = %get_loop64
  %118 = getelementptr inbounds i32, ptr %112, i32 %116
  %119 = load i32, ptr %118, align 4
  %key_eq70 = icmp eq i32 5, %119
  br i1 %key_eq70, label %get_found66, label %get_next71

get_found66:                                      ; preds = %get_check65
  %120 = getelementptr inbounds i32, ptr %114, i32 %116
  %121 = load i32, ptr %120, align 4
  store i32 %121, ptr %res_get69, align 4
  br label %get_end67

get_end67:                                        ; preds = %get_found66, %get_loop64
  %get_res72 = load i32, ptr %res_get69, align 4
  store i32 %get_res72, ptr %earth_amt, align 4
  %fire_amt_val = load i32, ptr %fire_amt, align 4
  %122 = icmp ne i32 %fire_amt_val, 2
  br i1 %122, label %if_then73, label %if_merge74

get_next71:                                       ; preds = %get_check65
  %123 = add i32 %116, 1
  store i32 %123, ptr %i_get68, align 4
  br label %get_loop64

if_then73:                                        ; preds = %get_end67
  %fire_amt_val75 = load i32, ptr %fire_amt, align 4
  %124 = call ptr @__aiml_int_to_string(i32 %fire_amt_val75)
  %125 = call ptr @__aiml_str_concat(ptr @14, ptr %124)
  %print_null_check76 = icmp eq ptr %125, null
  %print_safe_str77 = select i1 %print_null_check76, ptr @print_null_str.4, ptr %125
  %print_call78 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str77)
  call void @exit(i32 1)
  br label %if_merge74

if_merge74:                                       ; preds = %if_then73, %get_end67
  %earth_amt_val = load i32, ptr %earth_amt, align 4
  %126 = icmp ne i32 %earth_amt_val, 7
  br i1 %126, label %if_then79, label %if_merge80

if_then79:                                        ; preds = %if_merge74
  %earth_amt_val81 = load i32, ptr %earth_amt, align 4
  %127 = call ptr @__aiml_int_to_string(i32 %earth_amt_val81)
  %128 = call ptr @__aiml_str_concat(ptr @16, ptr %127)
  %print_null_check82 = icmp eq ptr %128, null
  %print_safe_str83 = select i1 %print_null_check82, ptr @print_null_str.5, ptr %128
  %print_call84 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str83)
  call void @exit(i32 1)
  br label %if_merge80

if_merge80:                                       ; preds = %if_then79, %if_merge74
  %129 = call i32 @aiml_db_checkpoint_all()
  %print_call85 = call i32 (ptr, ...) @printf(ptr @19, ptr @18)
  ret i32 0
}

define void @__aiml_init() {
entry:
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare ptr @aiml_db_kv_open_typed_persist(ptr, i64, ptr, ptr, i32)

declare ptr @realloc(ptr, i64)

declare ptr @malloc(i64)

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

declare i32 @printf(ptr, ...)

declare void @exit(i32)

declare void @aiml_db_kv_set(ptr, ptr, ptr, i64)

declare ptr @aiml_db_kv_get(ptr, ptr)

declare i32 @strcmp(ptr, ptr)

declare i32 @aiml_db_checkpoint_all()

attributes #0 = { noinline }
