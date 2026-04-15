; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%AimlClassSchema = type { ptr, i32, i32, i32, i32, [256 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%Card = type { ptr, i32, i32, %map_ManaColor_i32, ptr, %list_PassiveAbility, %list_ActiveAbility, i32, i32, i32, i32, i32, i32, i32, i32, i64 }
%map_ManaColor_i32 = type { i32, i32, ptr, ptr, ptr }
%list_PassiveAbility = type { i32, i32, ptr }
%list_ActiveAbility = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }
%ActiveAbility = type { ptr, %list_i32, %"list_list<i32>", %"list_list<i32>" }
%list_i32 = type { i32, i32, ptr }
%"list_list<i32>" = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_ActiveAbility = private unnamed_addr constant [10 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 1, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 5, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 6, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 8, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 7, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 9, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__aiml_sernodes_BaseCard = private unnamed_addr constant [21 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 7, i32 1, i32 0, i32 88, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 12, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 8, i32 9, i32 16, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 48, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 10, i32 0, i32 56, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 11, i32 0, i32 72, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 12, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 16, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 17, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 19, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 18, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 20, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__aiml_sernodes_Card = private unnamed_addr constant [30 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 16, i32 1, i32 0, i32 128, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 12, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 17, i32 18, i32 16, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 48, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 19, i32 0, i32 56, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 20, i32 0, i32 72, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 88, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 92, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 96, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 100, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 104, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 108, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 112, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 116, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 3, i32 0, i32 0, i32 120, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 21, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 25, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 26, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 28, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 27, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 29, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@0 = private unnamed_addr constant [383 x i8] c"{\22__type\22:\22Card\22,\22name\22:\22Red Pyromancer IV\22,\22card_type\22:1,\22mkp\22:5,\22mana_cost\22:{\221\22:2,\225\22:1},\22description\22:\22A red creature.\22,\22passive_abilities\22:[10,30,12],\22active_abilities\22:[{\22cost_text\22:\222 Fire\22,\22activation_cost\22:[1,2],\22trigger\22:[[1,7]],\22effects\22:[[10,3],[20,5,1]]}],\22lp\22:4,\22attack\22:5,\22defense\22:1,\22initiative\22:2,\22moral\22:3,\22movement\22:2,\22spell_type\22:2,\22equip_cost\22:0,\22created_at\22:0}\00", align 1
@1 = private unnamed_addr constant [34 x i8] c"/tmp/aiml_test_card_heap_persist/\00", align 1
@2 = private unnamed_addr constant [18 x i8] c"card_heap_persist\00", align 1
@3 = private unnamed_addr constant [3 x i8] c"c1\00", align 1
@4 = private unnamed_addr constant [3 x i8] c"c1\00", align 1
@5 = private unnamed_addr constant [18 x i8] c"Red Pyromancer IV\00", align 1
@6 = private unnamed_addr constant [15 x i8] c"FAIL got.name=\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@7 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@8 = private unnamed_addr constant [32 x i8] c"FAIL got.passive_abilities.len=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [31 x i8] c"FAIL got.active_abilities.len=\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [7 x i8] c"2 Fire\00", align 1
@13 = private unnamed_addr constant [19 x i8] c"FAIL ab.cost_text=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@14 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@15 = private unnamed_addr constant [21 x i8] c"FAIL ab.effects.len=\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@16 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@17 = private unnamed_addr constant [3 x i8] c"c1\00", align 1
@18 = private unnamed_addr constant [3 x i8] c"c1\00", align 1
@19 = private unnamed_addr constant [23 x i8] c"FAIL has_after_delete=\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@20 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@21 = private unnamed_addr constant [23 x i8] c"Card heap persist PASS\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@__aiml_fptable = global [1 x [16 x ptr]] [[16 x ptr] [ptr @main, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null, ptr null]]
@__aiml_fptable_current = global [1 x ptr] [ptr @main]
@__aiml_fptable_versions = global [1 x i32] [i32 1]
@__aiml_fname_main = private constant [5 x i8] c"main\00"
@__aiml_func_names = constant [1 x ptr] [ptr @__aiml_fname_main]
@__aiml_func_count = constant i32 1
@__aiml_sname_ActiveAbility = private constant [14 x i8] c"ActiveAbility\00"
@__aiml_sfield_ActiveAbility_cost_text = private constant [10 x i8] c"cost_text\00"
@__aiml_sfield_ActiveAbility_activation_cost = private constant [16 x i8] c"activation_cost\00"
@__aiml_sfield_ActiveAbility_trigger = private constant [8 x i8] c"trigger\00"
@__aiml_sfield_ActiveAbility_effects = private constant [8 x i8] c"effects\00"
@__aiml_sname_BaseCard = private constant [9 x i8] c"BaseCard\00"
@__aiml_sfield_BaseCard_name = private constant [5 x i8] c"name\00"
@__aiml_sfield_BaseCard_card_type = private constant [10 x i8] c"card_type\00"
@__aiml_sfield_BaseCard_mkp = private constant [4 x i8] c"mkp\00"
@__aiml_sfield_BaseCard_mana_cost = private constant [10 x i8] c"mana_cost\00"
@__aiml_sfield_BaseCard_description = private constant [12 x i8] c"description\00"
@__aiml_sfield_BaseCard_passive_abilities = private constant [18 x i8] c"passive_abilities\00"
@__aiml_sfield_BaseCard_active_abilities = private constant [17 x i8] c"active_abilities\00"
@__aiml_sname_Card = private constant [5 x i8] c"Card\00"
@__aiml_sfield_Card_name = private constant [5 x i8] c"name\00"
@__aiml_sfield_Card_card_type = private constant [10 x i8] c"card_type\00"
@__aiml_sfield_Card_mkp = private constant [4 x i8] c"mkp\00"
@__aiml_sfield_Card_mana_cost = private constant [10 x i8] c"mana_cost\00"
@__aiml_sfield_Card_description = private constant [12 x i8] c"description\00"
@__aiml_sfield_Card_passive_abilities = private constant [18 x i8] c"passive_abilities\00"
@__aiml_sfield_Card_active_abilities = private constant [17 x i8] c"active_abilities\00"
@__aiml_sfield_Card_lp = private constant [3 x i8] c"lp\00"
@__aiml_sfield_Card_attack = private constant [7 x i8] c"attack\00"
@__aiml_sfield_Card_defense = private constant [8 x i8] c"defense\00"
@__aiml_sfield_Card_initiative = private constant [11 x i8] c"initiative\00"
@__aiml_sfield_Card_moral = private constant [6 x i8] c"moral\00"
@__aiml_sfield_Card_movement = private constant [9 x i8] c"movement\00"
@__aiml_sfield_Card_spell_type = private constant [11 x i8] c"spell_type\00"
@__aiml_sfield_Card_equip_cost = private constant [11 x i8] c"equip_cost\00"
@__aiml_sfield_Card_created_at = private constant [11 x i8] c"created_at\00"
@__aiml_schemas = global [3 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_ActiveAbility, i32 1, i32 4, i32 56, i32 64, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_cost_text }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_activation_cost }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_trigger }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_effects }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_BaseCard, i32 1, i32 7, i32 88, i32 128, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_BaseCard_name }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_card_type }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_mkp }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_mana_cost }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_description }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_passive_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_active_abilities }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Card, i32 1, i32 16, i32 128, i32 128, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Card_name }, %AimlFieldInfo { ptr @__aiml_sfield_Card_card_type }, %AimlFieldInfo { ptr @__aiml_sfield_Card_mkp }, %AimlFieldInfo { ptr @__aiml_sfield_Card_mana_cost }, %AimlFieldInfo { ptr @__aiml_sfield_Card_description }, %AimlFieldInfo { ptr @__aiml_sfield_Card_passive_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_Card_active_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_Card_lp }, %AimlFieldInfo { ptr @__aiml_sfield_Card_attack }, %AimlFieldInfo { ptr @__aiml_sfield_Card_defense }, %AimlFieldInfo { ptr @__aiml_sfield_Card_initiative }, %AimlFieldInfo { ptr @__aiml_sfield_Card_moral }, %AimlFieldInfo { ptr @__aiml_sfield_Card_movement }, %AimlFieldInfo { ptr @__aiml_sfield_Card_spell_type }, %AimlFieldInfo { ptr @__aiml_sfield_Card_equip_cost }, %AimlFieldInfo { ptr @__aiml_sfield_Card_created_at }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 3

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %j1 = alloca ptr, align 8
  store ptr null, ptr %j1, align 8
  store ptr @0, ptr %j1, align 8
  %c1 = alloca %Card, align 8
  store %Card zeroinitializer, ptr %c1, align 8
  %j1_val = load ptr, ptr %j1, align 8
  store i32 0, ptr %c1, align 4
  %kv = alloca ptr, align 8
  store ptr null, ptr %kv, align 8
  %0 = call ptr @aiml_db_kv_open_typed_persist(ptr @2, i64 128, ptr @1, ptr @__aiml_sernodes_Card, i32 30)
  store ptr %0, ptr %kv, align 8
  %c1_val = load %Card, ptr %c1, align 8
  %kv_ptr = load ptr, ptr %kv, align 8
  %kv_val_tmp = alloca %Card, align 8
  store %Card %c1_val, ptr %kv_val_tmp, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr, ptr @3, ptr %kv_val_tmp, i64 128)
  %got = alloca ptr, align 8
  store ptr null, ptr %got, align 8
  %kv_ptr1 = load ptr, ptr %kv, align 8
  %1 = call ptr @aiml_db_kv_get(ptr %kv_ptr1, ptr @4)
  store ptr %1, ptr %got, align 8
  %prop_ptr_base = load ptr, ptr %got, align 8
  %prop_ptr = getelementptr inbounds %Card, ptr %prop_ptr_base, i32 0, i32 0
  %name_val = load ptr, ptr %prop_ptr, align 8
  %2 = icmp eq ptr %name_val, null
  %3 = icmp eq ptr %name_val, @5
  %4 = or i1 %2, false
  br i1 %4, label %cmp_check_null, label %cmp_do_strcmp

cmp_check_null:                                   ; preds = %entry
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %entry
  %5 = call i32 @strcmp(ptr %name_val, ptr @5)
  %6 = icmp eq i32 %5, 0
  br label %cmp_merge

cmp_merge:                                        ; preds = %cmp_do_strcmp, %cmp_check_null
  %strcmp_res = phi i1 [ %3, %cmp_check_null ], [ %6, %cmp_do_strcmp ]
  %7 = sext i1 %strcmp_res to i32
  %8 = icmp eq i32 %7, 0
  br i1 %8, label %if_then, label %if_merge

if_then:                                          ; preds = %cmp_merge
  %prop_ptr_base2 = load ptr, ptr %got, align 8
  %prop_ptr3 = getelementptr inbounds %Card, ptr %prop_ptr_base2, i32 0, i32 0
  %name_val4 = load ptr, ptr %prop_ptr3, align 8
  %9 = call ptr @__aiml_str_concat(ptr @6, ptr %name_val4)
  %print_null_check = icmp eq ptr %9, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %9
  %print_call = call i32 (ptr, ...) @printf(ptr @7, ptr %print_safe_str)
  call void @exit(i32 1)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %cmp_merge
  %chain_base = load ptr, ptr %got, align 8
  %chain_gep = getelementptr inbounds %Card, ptr %chain_base, i32 0, i32 5
  %sizePtr = getelementptr inbounds %GenericList, ptr %chain_gep, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr, align 4
  %10 = icmp ne i32 %sizeVal, 3
  br i1 %10, label %if_then5, label %if_merge6

if_then5:                                         ; preds = %if_merge
  %chain_base7 = load ptr, ptr %got, align 8
  %chain_gep8 = getelementptr inbounds %Card, ptr %chain_base7, i32 0, i32 5
  %sizePtr9 = getelementptr inbounds %GenericList, ptr %chain_gep8, i32 0, i32 0
  %sizeVal10 = load i32, ptr %sizePtr9, align 4
  %11 = call ptr @__aiml_int_to_string(i32 %sizeVal10)
  %12 = call ptr @__aiml_str_concat(ptr @8, ptr %11)
  %print_null_check11 = icmp eq ptr %12, null
  %print_safe_str12 = select i1 %print_null_check11, ptr @print_null_str.1, ptr %12
  %print_call13 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str12)
  call void @exit(i32 1)
  br label %if_merge6

if_merge6:                                        ; preds = %if_then5, %if_merge
  %chain_base14 = load ptr, ptr %got, align 8
  %chain_gep15 = getelementptr inbounds %Card, ptr %chain_base14, i32 0, i32 6
  %sizePtr16 = getelementptr inbounds %GenericList, ptr %chain_gep15, i32 0, i32 0
  %sizeVal17 = load i32, ptr %sizePtr16, align 4
  %13 = icmp ne i32 %sizeVal17, 1
  br i1 %13, label %if_then18, label %if_merge19

if_then18:                                        ; preds = %if_merge6
  %chain_base20 = load ptr, ptr %got, align 8
  %chain_gep21 = getelementptr inbounds %Card, ptr %chain_base20, i32 0, i32 6
  %sizePtr22 = getelementptr inbounds %GenericList, ptr %chain_gep21, i32 0, i32 0
  %sizeVal23 = load i32, ptr %sizePtr22, align 4
  %14 = call ptr @__aiml_int_to_string(i32 %sizeVal23)
  %15 = call ptr @__aiml_str_concat(ptr @10, ptr %14)
  %print_null_check24 = icmp eq ptr %15, null
  %print_safe_str25 = select i1 %print_null_check24, ptr @print_null_str.2, ptr %15
  %print_call26 = call i32 (ptr, ...) @printf(ptr @11, ptr %print_safe_str25)
  call void @exit(i32 1)
  br label %if_merge19

if_merge19:                                       ; preds = %if_then18, %if_merge6
  %ab = alloca %ActiveAbility, align 8
  store %ActiveAbility zeroinitializer, ptr %ab, align 8
  %chain_base27 = load ptr, ptr %got, align 8
  %chain_gep28 = getelementptr inbounds %Card, ptr %chain_base27, i32 0, i32 6
  %dataPtr = getelementptr inbounds %GenericList, ptr %chain_gep28, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr, align 8
  %16 = getelementptr inbounds %ActiveAbility, ptr %dataBase, i32 0
  %chain_elem = load %ActiveAbility, ptr %16, align 8
  store %ActiveAbility %chain_elem, ptr %ab, align 8
  %prop_ptr29 = getelementptr inbounds %ActiveAbility, ptr %ab, i32 0, i32 0
  %cost_text_val = load ptr, ptr %prop_ptr29, align 8
  %17 = icmp eq ptr %cost_text_val, null
  %18 = icmp eq ptr %cost_text_val, @12
  %19 = or i1 %17, false
  br i1 %19, label %cmp_check_null30, label %cmp_do_strcmp31

cmp_check_null30:                                 ; preds = %if_merge19
  br label %cmp_merge32

cmp_do_strcmp31:                                  ; preds = %if_merge19
  %20 = call i32 @strcmp(ptr %cost_text_val, ptr @12)
  %21 = icmp eq i32 %20, 0
  br label %cmp_merge32

cmp_merge32:                                      ; preds = %cmp_do_strcmp31, %cmp_check_null30
  %strcmp_res33 = phi i1 [ %18, %cmp_check_null30 ], [ %21, %cmp_do_strcmp31 ]
  %22 = sext i1 %strcmp_res33 to i32
  %23 = icmp eq i32 %22, 0
  br i1 %23, label %if_then34, label %if_merge35

if_then34:                                        ; preds = %cmp_merge32
  %prop_ptr36 = getelementptr inbounds %ActiveAbility, ptr %ab, i32 0, i32 0
  %cost_text_val37 = load ptr, ptr %prop_ptr36, align 8
  %24 = call ptr @__aiml_str_concat(ptr @13, ptr %cost_text_val37)
  %print_null_check38 = icmp eq ptr %24, null
  %print_safe_str39 = select i1 %print_null_check38, ptr @print_null_str.3, ptr %24
  %print_call40 = call i32 (ptr, ...) @printf(ptr @14, ptr %print_safe_str39)
  call void @exit(i32 1)
  br label %if_merge35

if_merge35:                                       ; preds = %if_then34, %cmp_merge32
  %chain_gep41 = getelementptr inbounds %ActiveAbility, ptr %ab, i32 0, i32 3
  %sizePtr42 = getelementptr inbounds %GenericList, ptr %chain_gep41, i32 0, i32 0
  %sizeVal43 = load i32, ptr %sizePtr42, align 4
  %25 = icmp ne i32 %sizeVal43, 2
  br i1 %25, label %if_then44, label %if_merge45

if_then44:                                        ; preds = %if_merge35
  %chain_gep46 = getelementptr inbounds %ActiveAbility, ptr %ab, i32 0, i32 3
  %sizePtr47 = getelementptr inbounds %GenericList, ptr %chain_gep46, i32 0, i32 0
  %sizeVal48 = load i32, ptr %sizePtr47, align 4
  %26 = call ptr @__aiml_int_to_string(i32 %sizeVal48)
  %27 = call ptr @__aiml_str_concat(ptr @15, ptr %26)
  %print_null_check49 = icmp eq ptr %27, null
  %print_safe_str50 = select i1 %print_null_check49, ptr @print_null_str.4, ptr %27
  %print_call51 = call i32 (ptr, ...) @printf(ptr @16, ptr %print_safe_str50)
  call void @exit(i32 1)
  br label %if_merge45

if_merge45:                                       ; preds = %if_then44, %if_merge35
  %kv_ptr52 = load ptr, ptr %kv, align 8
  call void @aiml_db_kv_delete(ptr %kv_ptr52, ptr @17)
  %has = alloca i32, align 4
  store i32 0, ptr %has, align 4
  %kv_ptr53 = load ptr, ptr %kv, align 8
  %28 = call i32 @aiml_db_kv_has(ptr %kv_ptr53, ptr @18)
  store i32 %28, ptr %has, align 4
  %has_val = load i32, ptr %has, align 4
  %29 = icmp ne i32 %has_val, 0
  br i1 %29, label %if_then54, label %if_merge55

if_then54:                                        ; preds = %if_merge45
  %has_val56 = load i32, ptr %has, align 4
  %30 = call ptr @__aiml_int_to_string(i32 %has_val56)
  %31 = call ptr @__aiml_str_concat(ptr @19, ptr %30)
  %print_null_check57 = icmp eq ptr %31, null
  %print_safe_str58 = select i1 %print_null_check57, ptr @print_null_str.5, ptr %31
  %print_call59 = call i32 (ptr, ...) @printf(ptr @20, ptr %print_safe_str58)
  call void @exit(i32 1)
  br label %if_merge55

if_merge55:                                       ; preds = %if_then54, %if_merge45
  %32 = call i32 @aiml_db_checkpoint_all()
  %print_call60 = call i32 (ptr, ...) @printf(ptr @22, ptr @21)
  ret i32 0

ret_dead:                                         ; No predecessors!
  ret i32 0
}

define void @__aiml_init() {
entry:
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare ptr @memcpy(ptr, ptr, i64)

declare ptr @aiml_db_kv_open_typed_persist(ptr, i64, ptr, ptr, i32)

declare void @aiml_db_kv_set(ptr, ptr, ptr, i64)

declare ptr @aiml_db_kv_get(ptr, ptr)

declare i32 @strcmp(ptr, ptr)

declare i64 @strlen(ptr)

declare ptr @malloc(i64)

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

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare void @aiml_db_kv_delete(ptr, ptr)

declare i32 @aiml_db_kv_has(ptr, ptr)

declare i32 @aiml_db_checkpoint_all()

attributes #0 = { noinline }
