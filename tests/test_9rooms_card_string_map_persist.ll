; ModuleID = 'AIML_Module'
source_filename = "AIML_Module"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-linux-gnu"

%AimlClassSchema = type { ptr, i32, i32, i32, i32, [256 x %AimlFieldInfo] }
%AimlFieldInfo = type { ptr }
%Card = type { ptr, i32, i32, %map_string_i32, ptr, %list_PassiveAbility, %list_ActiveAbility, i32, i32, i32, i32, i32, i32, i32, i32, %map_string_i32, i32, i32, i32, i64 }
%list_PassiveAbility = type { i32, i32, ptr }
%list_ActiveAbility = type { i32, i32, ptr }
%map_string_i32 = type { i32, i32, ptr, ptr, ptr }
%GenericList = type { i32, i32, ptr }
%ActiveAbility = type { ptr, %list_i32, %"list_list<i32>", %"list_list<i32>" }
%list_i32 = type { i32, i32, ptr }
%"list_list<i32>" = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_ActiveAbility = private unnamed_addr constant [10 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 1, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 5, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 6, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 8, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 7, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 9, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__aiml_sernodes_BaseCard = private unnamed_addr constant [21 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 7, i32 1, i32 0, i32 88, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 12, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 8, i32 9, i32 16, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 48, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 10, i32 0, i32 56, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 11, i32 0, i32 72, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 12, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 16, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 17, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 19, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 18, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 20, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__aiml_sernodes_Card = private unnamed_addr constant [36 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 20, i32 1, i32 0, i32 176, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 12, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 21, i32 22, i32 16, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 48, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 23, i32 0, i32 56, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 24, i32 0, i32 72, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 88, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 92, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 96, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 100, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 104, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 108, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 112, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 116, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 34, i32 35, i32 120, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 152, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 156, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 160, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 3, i32 0, i32 0, i32 168, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 25, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 29, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 30, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 32, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 31, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 33, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@tr_fname = private unnamed_addr constant [5 x i8] c"main\00", align 1
@tr_args = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@tr_dir = private unnamed_addr constant [3 x i8] c"=>\00", align 1
@0 = private unnamed_addr constant [47 x i8] c"/tmp/aiml_test_9rooms_card_string_map_persist/\00", align 1
@1 = private unnamed_addr constant [401 x i8] c"{\22__type\22:\22Card\22,\22name\22:\22Fire Mana\22,\22card_type\22:3,\22mkp\22:2,\22mana_cost\22:{},\22description\22:\22mana source\22,\22passive_abilities\22:[18],\22active_abilities\22:[{\22cost_text\22:\22Tap\22,\22activation_cost\22:[1],\22trigger\22:[[1,7]],\22effects\22:[[10,3]]}],\22lp\22:0,\22attack\22:0,\22defense\22:0,\22initiative\22:0,\22moral\22:0,\22movement\22:0,\22spell_type\22:0,\22equip_cost\22:0,\22provides\22:{\221\22:1},\22skp_cost\22:0,\22duration\22:0,\22room_effect\22:0,\22created_at\22:0}\00", align 1
@fk_name = private unnamed_addr constant [5 x i8] c"name\00", align 1
@fk_card_type = private unnamed_addr constant [10 x i8] c"card_type\00", align 1
@fk_mkp = private unnamed_addr constant [4 x i8] c"mkp\00", align 1
@fk_mana_cost = private unnamed_addr constant [10 x i8] c"mana_cost\00", align 1
@fk_description = private unnamed_addr constant [12 x i8] c"description\00", align 1
@fk_passive_abilities = private unnamed_addr constant [18 x i8] c"passive_abilities\00", align 1
@fk_active_abilities = private unnamed_addr constant [17 x i8] c"active_abilities\00", align 1
@fk_cost_text = private unnamed_addr constant [10 x i8] c"cost_text\00", align 1
@fk_activation_cost = private unnamed_addr constant [16 x i8] c"activation_cost\00", align 1
@fk_trigger = private unnamed_addr constant [8 x i8] c"trigger\00", align 1
@fk_effects = private unnamed_addr constant [8 x i8] c"effects\00", align 1
@fk_lp = private unnamed_addr constant [3 x i8] c"lp\00", align 1
@fk_attack = private unnamed_addr constant [7 x i8] c"attack\00", align 1
@fk_defense = private unnamed_addr constant [8 x i8] c"defense\00", align 1
@fk_initiative = private unnamed_addr constant [11 x i8] c"initiative\00", align 1
@fk_moral = private unnamed_addr constant [6 x i8] c"moral\00", align 1
@fk_movement = private unnamed_addr constant [9 x i8] c"movement\00", align 1
@fk_spell_type = private unnamed_addr constant [11 x i8] c"spell_type\00", align 1
@fk_equip_cost = private unnamed_addr constant [11 x i8] c"equip_cost\00", align 1
@fk_provides = private unnamed_addr constant [9 x i8] c"provides\00", align 1
@fk_skp_cost = private unnamed_addr constant [9 x i8] c"skp_cost\00", align 1
@fk_duration = private unnamed_addr constant [9 x i8] c"duration\00", align 1
@fk_room_effect = private unnamed_addr constant [12 x i8] c"room_effect\00", align 1
@fk_created_at = private unnamed_addr constant [11 x i8] c"created_at\00", align 1
@2 = private unnamed_addr constant [10 x i8] c"Fire Mana\00", align 1
@3 = private unnamed_addr constant [15 x i8] c"FAIL pre_name=\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@4 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@5 = private unnamed_addr constant [23 x i8] c"FAIL pre_provides_len=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@6 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@7 = private unnamed_addr constant [2 x i8] c"1\00", align 1
@8 = private unnamed_addr constant [14 x i8] c"FAIL pre_amt=\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@9 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@10 = private unnamed_addr constant [17 x i8] c"FAIL pre_pa_len=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@11 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@12 = private unnamed_addr constant [17 x i8] c"FAIL pre_aa_len=\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@13 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@14 = private unnamed_addr constant [24 x i8] c"card_string_map_persist\00", align 1
@15 = private unnamed_addr constant [5 x i8] c"mana\00", align 1
@16 = private unnamed_addr constant [5 x i8] c"mana\00", align 1
@17 = private unnamed_addr constant [14 x i8] c"FAIL got=null\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@18 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@19 = private unnamed_addr constant [10 x i8] c"Fire Mana\00", align 1
@20 = private unnamed_addr constant [15 x i8] c"FAIL got.name=\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@21 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@22 = private unnamed_addr constant [23 x i8] c"FAIL got_provides_len=\00", align 1
@print_null_str.7 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@23 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@24 = private unnamed_addr constant [2 x i8] c"1\00", align 1
@25 = private unnamed_addr constant [14 x i8] c"FAIL got_amt=\00", align 1
@print_null_str.8 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@26 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@27 = private unnamed_addr constant [17 x i8] c"FAIL got_pa_len=\00", align 1
@print_null_str.9 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@28 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@29 = private unnamed_addr constant [17 x i8] c"FAIL got_aa_len=\00", align 1
@print_null_str.10 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@30 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@31 = private unnamed_addr constant [36 x i8] c"9rooms card string map persist PASS\00", align 1
@print_null_str.11 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@32 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
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
@__aiml_sfield_Card_provides = private constant [9 x i8] c"provides\00"
@__aiml_sfield_Card_skp_cost = private constant [9 x i8] c"skp_cost\00"
@__aiml_sfield_Card_duration = private constant [9 x i8] c"duration\00"
@__aiml_sfield_Card_room_effect = private constant [12 x i8] c"room_effect\00"
@__aiml_sfield_Card_created_at = private constant [11 x i8] c"created_at\00"
@__aiml_schemas = global [3 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_ActiveAbility, i32 1, i32 4, i32 56, i32 64, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_cost_text }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_activation_cost }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_trigger }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_effects }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_BaseCard, i32 1, i32 7, i32 88, i32 128, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_BaseCard_name }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_card_type }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_mkp }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_mana_cost }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_description }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_passive_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_active_abilities }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Card, i32 1, i32 20, i32 176, i32 256, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Card_name }, %AimlFieldInfo { ptr @__aiml_sfield_Card_card_type }, %AimlFieldInfo { ptr @__aiml_sfield_Card_mkp }, %AimlFieldInfo { ptr @__aiml_sfield_Card_mana_cost }, %AimlFieldInfo { ptr @__aiml_sfield_Card_description }, %AimlFieldInfo { ptr @__aiml_sfield_Card_passive_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_Card_active_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_Card_lp }, %AimlFieldInfo { ptr @__aiml_sfield_Card_attack }, %AimlFieldInfo { ptr @__aiml_sfield_Card_defense }, %AimlFieldInfo { ptr @__aiml_sfield_Card_initiative }, %AimlFieldInfo { ptr @__aiml_sfield_Card_moral }, %AimlFieldInfo { ptr @__aiml_sfield_Card_movement }, %AimlFieldInfo { ptr @__aiml_sfield_Card_spell_type }, %AimlFieldInfo { ptr @__aiml_sfield_Card_equip_cost }, %AimlFieldInfo { ptr @__aiml_sfield_Card_provides }, %AimlFieldInfo { ptr @__aiml_sfield_Card_skp_cost }, %AimlFieldInfo { ptr @__aiml_sfield_Card_duration }, %AimlFieldInfo { ptr @__aiml_sfield_Card_room_effect }, %AimlFieldInfo { ptr @__aiml_sfield_Card_created_at }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 3

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %tr_active = call i32 @aiml_trace_active(ptr @tr_fname)
  %tr_cond = icmp ne i32 %tr_active, 0
  br i1 %tr_cond, label %tr_enter, label %tr_body

tr_enter:                                         ; preds = %entry
  call void @aiml_trace(ptr @tr_fname, ptr @tr_dir, ptr @tr_args)
  br label %tr_body

tr_body:                                          ; preds = %tr_enter, %entry
  %db_dir = alloca ptr, align 8
  store ptr null, ptr %db_dir, align 8
  %path_init = call ptr @aiml_path_intern(ptr @0)
  store ptr %path_init, ptr %db_dir, align 8
  %db_dir_val = load ptr, ptr %db_dir, align 8
  %0 = call i32 @aiml_io_remove_all(ptr %db_dir_val)
  %j1 = alloca ptr, align 8
  store ptr null, ptr %j1, align 8
  store ptr @1, ptr %j1, align 8
  %c1 = alloca %Card, align 8
  store %Card zeroinitializer, ptr %c1, align 8
  %j1_val = load ptr, ptr %j1, align 8
  %deser_r = call ptr @__aiml_deser_Card(ptr %j1_val)
  %1 = call ptr @memcpy(ptr %c1, ptr %deser_r, i64 176)
  %prop_ptr = getelementptr inbounds %Card, ptr %c1, i32 0, i32 0
  %name_val = load ptr, ptr %prop_ptr, align 8
  %2 = icmp eq ptr %name_val, null
  %3 = icmp eq ptr %name_val, @2
  %4 = or i1 %2, false
  br i1 %4, label %cmp_check_null, label %cmp_do_strcmp

cmp_check_null:                                   ; preds = %tr_body
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %tr_body
  %5 = call i32 @strcmp(ptr %name_val, ptr @2)
  %6 = icmp eq i32 %5, 0
  br label %cmp_merge

cmp_merge:                                        ; preds = %cmp_do_strcmp, %cmp_check_null
  %strcmp_res = phi i1 [ %3, %cmp_check_null ], [ %6, %cmp_do_strcmp ]
  %7 = sext i1 %strcmp_res to i32
  %8 = icmp eq i32 %7, 0
  br i1 %8, label %if_then, label %if_merge

if_then:                                          ; preds = %cmp_merge
  %prop_ptr1 = getelementptr inbounds %Card, ptr %c1, i32 0, i32 0
  %name_val2 = load ptr, ptr %prop_ptr1, align 8
  %9 = call ptr @__aiml_str_concat(ptr @3, ptr %name_val2)
  %print_null_check = icmp eq ptr %9, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %9
  %print_call = call i32 (ptr, ...) @printf(ptr @4, ptr %print_safe_str)
  call void @exit(i32 1)
  br label %if_merge

if_merge:                                         ; preds = %if_then, %cmp_merge
  %chain_gep = getelementptr inbounds %Card, ptr %c1, i32 0, i32 15
  %sizePtr = getelementptr inbounds %map_string_i32, ptr %chain_gep, i32 0, i32 0
  %size_val = load i32, ptr %sizePtr, align 4
  %10 = icmp ne i32 %size_val, 1
  br i1 %10, label %if_then3, label %if_merge4

if_then3:                                         ; preds = %if_merge
  %chain_gep5 = getelementptr inbounds %Card, ptr %c1, i32 0, i32 15
  %sizePtr6 = getelementptr inbounds %map_string_i32, ptr %chain_gep5, i32 0, i32 0
  %size_val7 = load i32, ptr %sizePtr6, align 4
  %11 = call ptr @__aiml_int_to_string(i32 %size_val7)
  %12 = call ptr @__aiml_str_concat(ptr @5, ptr %11)
  %print_null_check8 = icmp eq ptr %12, null
  %print_safe_str9 = select i1 %print_null_check8, ptr @print_null_str.1, ptr %12
  %print_call10 = call i32 (ptr, ...) @printf(ptr @6, ptr %print_safe_str9)
  call void @exit(i32 1)
  br label %if_merge4

if_merge4:                                        ; preds = %if_then3, %if_merge
  %pre_amt = alloca i32, align 4
  store i32 0, ptr %pre_amt, align 4
  %set_prop_ptr_w = getelementptr inbounds %Card, ptr %c1, i32 0, i32 15
  %13 = getelementptr inbounds %map_string_i32, ptr %set_prop_ptr_w, i32 0, i32 0
  %14 = load i32, ptr %13, align 4
  %15 = getelementptr inbounds %map_string_i32, ptr %set_prop_ptr_w, i32 0, i32 3
  %16 = load ptr, ptr %15, align 8
  %17 = getelementptr inbounds %map_string_i32, ptr %set_prop_ptr_w, i32 0, i32 4
  %18 = load ptr, ptr %17, align 8
  %i_get = alloca i32, align 4
  store i32 0, ptr %i_get, align 4
  %res_get = alloca i32, align 4
  store i32 0, ptr %res_get, align 4
  br label %get_loop

get_loop:                                         ; preds = %get_next, %if_merge4
  %19 = load i32, ptr %i_get, align 4
  %20 = icmp slt i32 %19, %14
  br i1 %20, label %get_check, label %get_end

get_check:                                        ; preds = %get_loop
  %21 = getelementptr inbounds ptr, ptr %16, i32 %19
  %22 = load ptr, ptr %21, align 8
  %23 = call i32 @strcmp(ptr @7, ptr %22)
  %str_eq = icmp eq i32 %23, 0
  br i1 %str_eq, label %get_found, label %get_next

get_found:                                        ; preds = %get_check
  %24 = getelementptr inbounds i32, ptr %18, i32 %19
  %25 = load i32, ptr %24, align 4
  store i32 %25, ptr %res_get, align 4
  br label %get_end

get_end:                                          ; preds = %get_found, %get_loop
  %get_res = load i32, ptr %res_get, align 4
  store i32 %get_res, ptr %pre_amt, align 4
  %pre_amt_val = load i32, ptr %pre_amt, align 4
  %26 = icmp ne i32 %pre_amt_val, 1
  br i1 %26, label %if_then11, label %if_merge12

get_next:                                         ; preds = %get_check
  %27 = add i32 %19, 1
  store i32 %27, ptr %i_get, align 4
  br label %get_loop

if_then11:                                        ; preds = %get_end
  %pre_amt_val13 = load i32, ptr %pre_amt, align 4
  %28 = call ptr @__aiml_int_to_string(i32 %pre_amt_val13)
  %29 = call ptr @__aiml_str_concat(ptr @8, ptr %28)
  %print_null_check14 = icmp eq ptr %29, null
  %print_safe_str15 = select i1 %print_null_check14, ptr @print_null_str.2, ptr %29
  %print_call16 = call i32 (ptr, ...) @printf(ptr @9, ptr %print_safe_str15)
  call void @exit(i32 1)
  br label %if_merge12

if_merge12:                                       ; preds = %if_then11, %get_end
  %chain_gep17 = getelementptr inbounds %Card, ptr %c1, i32 0, i32 5
  %sizePtr18 = getelementptr inbounds %GenericList, ptr %chain_gep17, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr18, align 4
  %30 = icmp ne i32 %sizeVal, 1
  br i1 %30, label %if_then19, label %if_merge20

if_then19:                                        ; preds = %if_merge12
  %chain_gep21 = getelementptr inbounds %Card, ptr %c1, i32 0, i32 5
  %sizePtr22 = getelementptr inbounds %GenericList, ptr %chain_gep21, i32 0, i32 0
  %sizeVal23 = load i32, ptr %sizePtr22, align 4
  %31 = call ptr @__aiml_int_to_string(i32 %sizeVal23)
  %32 = call ptr @__aiml_str_concat(ptr @10, ptr %31)
  %print_null_check24 = icmp eq ptr %32, null
  %print_safe_str25 = select i1 %print_null_check24, ptr @print_null_str.3, ptr %32
  %print_call26 = call i32 (ptr, ...) @printf(ptr @11, ptr %print_safe_str25)
  call void @exit(i32 1)
  br label %if_merge20

if_merge20:                                       ; preds = %if_then19, %if_merge12
  %chain_gep27 = getelementptr inbounds %Card, ptr %c1, i32 0, i32 6
  %sizePtr28 = getelementptr inbounds %GenericList, ptr %chain_gep27, i32 0, i32 0
  %sizeVal29 = load i32, ptr %sizePtr28, align 4
  %33 = icmp ne i32 %sizeVal29, 1
  br i1 %33, label %if_then30, label %if_merge31

if_then30:                                        ; preds = %if_merge20
  %chain_gep32 = getelementptr inbounds %Card, ptr %c1, i32 0, i32 6
  %sizePtr33 = getelementptr inbounds %GenericList, ptr %chain_gep32, i32 0, i32 0
  %sizeVal34 = load i32, ptr %sizePtr33, align 4
  %34 = call ptr @__aiml_int_to_string(i32 %sizeVal34)
  %35 = call ptr @__aiml_str_concat(ptr @12, ptr %34)
  %print_null_check35 = icmp eq ptr %35, null
  %print_safe_str36 = select i1 %print_null_check35, ptr @print_null_str.4, ptr %35
  %print_call37 = call i32 (ptr, ...) @printf(ptr @13, ptr %print_safe_str36)
  call void @exit(i32 1)
  br label %if_merge31

if_merge31:                                       ; preds = %if_then30, %if_merge20
  %kv = alloca ptr, align 8
  store ptr null, ptr %kv, align 8
  %db_dir_val38 = load ptr, ptr %db_dir, align 8
  %36 = call ptr @aiml_db_kv_open_typed_persist(ptr @14, i64 176, ptr %db_dir_val38, ptr @__aiml_sernodes_Card, i32 36)
  store ptr %36, ptr %kv, align 8
  %c1_val = load %Card, ptr %c1, align 8
  %kv_ptr = load ptr, ptr %kv, align 8
  %kv_val_tmp = alloca %Card, align 8
  store %Card %c1_val, ptr %kv_val_tmp, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr, ptr @15, ptr %kv_val_tmp, i64 176)
  %got = alloca ptr, align 8
  store ptr null, ptr %got, align 8
  %kv_ptr39 = load ptr, ptr %kv, align 8
  %37 = call ptr @aiml_db_kv_get(ptr %kv_ptr39, ptr @16)
  store ptr %37, ptr %got, align 8
  %got_val = load ptr, ptr %got, align 8
  %38 = icmp eq ptr %got_val, null
  %39 = icmp eq ptr %got_val, null
  %40 = or i1 %38, true
  br i1 %40, label %cmp_check_null40, label %cmp_do_strcmp41

cmp_check_null40:                                 ; preds = %if_merge31
  br label %cmp_merge42

cmp_do_strcmp41:                                  ; preds = %if_merge31
  %41 = call i32 @strcmp(ptr %got_val, ptr null)
  %42 = icmp eq i32 %41, 0
  br label %cmp_merge42

cmp_merge42:                                      ; preds = %cmp_do_strcmp41, %cmp_check_null40
  %strcmp_res43 = phi i1 [ %39, %cmp_check_null40 ], [ %42, %cmp_do_strcmp41 ]
  br i1 %strcmp_res43, label %if_then44, label %if_merge45

if_then44:                                        ; preds = %cmp_merge42
  %print_call46 = call i32 (ptr, ...) @printf(ptr @18, ptr @17)
  call void @exit(i32 1)
  br label %if_merge45

if_merge45:                                       ; preds = %if_then44, %cmp_merge42
  %prop_ptr_base = load ptr, ptr %got, align 8
  %prop_ptr47 = getelementptr inbounds %Card, ptr %prop_ptr_base, i32 0, i32 0
  %name_val48 = load ptr, ptr %prop_ptr47, align 8
  %43 = icmp eq ptr %name_val48, null
  %44 = icmp eq ptr %name_val48, @19
  %45 = or i1 %43, false
  br i1 %45, label %cmp_check_null49, label %cmp_do_strcmp50

cmp_check_null49:                                 ; preds = %if_merge45
  br label %cmp_merge51

cmp_do_strcmp50:                                  ; preds = %if_merge45
  %46 = call i32 @strcmp(ptr %name_val48, ptr @19)
  %47 = icmp eq i32 %46, 0
  br label %cmp_merge51

cmp_merge51:                                      ; preds = %cmp_do_strcmp50, %cmp_check_null49
  %strcmp_res52 = phi i1 [ %44, %cmp_check_null49 ], [ %47, %cmp_do_strcmp50 ]
  %48 = sext i1 %strcmp_res52 to i32
  %49 = icmp eq i32 %48, 0
  br i1 %49, label %if_then53, label %if_merge54

if_then53:                                        ; preds = %cmp_merge51
  %prop_ptr_base55 = load ptr, ptr %got, align 8
  %prop_ptr56 = getelementptr inbounds %Card, ptr %prop_ptr_base55, i32 0, i32 0
  %name_val57 = load ptr, ptr %prop_ptr56, align 8
  %50 = call ptr @__aiml_str_concat(ptr @20, ptr %name_val57)
  %print_null_check58 = icmp eq ptr %50, null
  %print_safe_str59 = select i1 %print_null_check58, ptr @print_null_str.6, ptr %50
  %print_call60 = call i32 (ptr, ...) @printf(ptr @21, ptr %print_safe_str59)
  call void @exit(i32 1)
  br label %if_merge54

if_merge54:                                       ; preds = %if_then53, %cmp_merge51
  %chain_base = load ptr, ptr %got, align 8
  %chain_gep61 = getelementptr inbounds %Card, ptr %chain_base, i32 0, i32 15
  %sizePtr62 = getelementptr inbounds %map_string_i32, ptr %chain_gep61, i32 0, i32 0
  %size_val63 = load i32, ptr %sizePtr62, align 4
  %51 = icmp ne i32 %size_val63, 1
  br i1 %51, label %if_then64, label %if_merge65

if_then64:                                        ; preds = %if_merge54
  %chain_base66 = load ptr, ptr %got, align 8
  %chain_gep67 = getelementptr inbounds %Card, ptr %chain_base66, i32 0, i32 15
  %sizePtr68 = getelementptr inbounds %map_string_i32, ptr %chain_gep67, i32 0, i32 0
  %size_val69 = load i32, ptr %sizePtr68, align 4
  %52 = call ptr @__aiml_int_to_string(i32 %size_val69)
  %53 = call ptr @__aiml_str_concat(ptr @22, ptr %52)
  %print_null_check70 = icmp eq ptr %53, null
  %print_safe_str71 = select i1 %print_null_check70, ptr @print_null_str.7, ptr %53
  %print_call72 = call i32 (ptr, ...) @printf(ptr @23, ptr %print_safe_str71)
  call void @exit(i32 1)
  br label %if_merge65

if_merge65:                                       ; preds = %if_then64, %if_merge54
  %got_amt = alloca i32, align 4
  store i32 0, ptr %got_amt, align 4
  %prop_ptr_base_w = load ptr, ptr %got, align 8
  %set_prop_ptr_w73 = getelementptr inbounds %Card, ptr %prop_ptr_base_w, i32 0, i32 15
  %54 = getelementptr inbounds %map_string_i32, ptr %set_prop_ptr_w73, i32 0, i32 0
  %55 = load i32, ptr %54, align 4
  %56 = getelementptr inbounds %map_string_i32, ptr %set_prop_ptr_w73, i32 0, i32 3
  %57 = load ptr, ptr %56, align 8
  %58 = getelementptr inbounds %map_string_i32, ptr %set_prop_ptr_w73, i32 0, i32 4
  %59 = load ptr, ptr %58, align 8
  %i_get78 = alloca i32, align 4
  store i32 0, ptr %i_get78, align 4
  %res_get79 = alloca i32, align 4
  store i32 0, ptr %res_get79, align 4
  br label %get_loop74

get_loop74:                                       ; preds = %get_next81, %if_merge65
  %60 = load i32, ptr %i_get78, align 4
  %61 = icmp slt i32 %60, %55
  br i1 %61, label %get_check75, label %get_end77

get_check75:                                      ; preds = %get_loop74
  %62 = getelementptr inbounds ptr, ptr %57, i32 %60
  %63 = load ptr, ptr %62, align 8
  %64 = call i32 @strcmp(ptr @24, ptr %63)
  %str_eq80 = icmp eq i32 %64, 0
  br i1 %str_eq80, label %get_found76, label %get_next81

get_found76:                                      ; preds = %get_check75
  %65 = getelementptr inbounds i32, ptr %59, i32 %60
  %66 = load i32, ptr %65, align 4
  store i32 %66, ptr %res_get79, align 4
  br label %get_end77

get_end77:                                        ; preds = %get_found76, %get_loop74
  %get_res82 = load i32, ptr %res_get79, align 4
  store i32 %get_res82, ptr %got_amt, align 4
  %got_amt_val = load i32, ptr %got_amt, align 4
  %67 = icmp ne i32 %got_amt_val, 1
  br i1 %67, label %if_then83, label %if_merge84

get_next81:                                       ; preds = %get_check75
  %68 = add i32 %60, 1
  store i32 %68, ptr %i_get78, align 4
  br label %get_loop74

if_then83:                                        ; preds = %get_end77
  %got_amt_val85 = load i32, ptr %got_amt, align 4
  %69 = call ptr @__aiml_int_to_string(i32 %got_amt_val85)
  %70 = call ptr @__aiml_str_concat(ptr @25, ptr %69)
  %print_null_check86 = icmp eq ptr %70, null
  %print_safe_str87 = select i1 %print_null_check86, ptr @print_null_str.8, ptr %70
  %print_call88 = call i32 (ptr, ...) @printf(ptr @26, ptr %print_safe_str87)
  call void @exit(i32 1)
  br label %if_merge84

if_merge84:                                       ; preds = %if_then83, %get_end77
  %chain_base89 = load ptr, ptr %got, align 8
  %chain_gep90 = getelementptr inbounds %Card, ptr %chain_base89, i32 0, i32 5
  %sizePtr91 = getelementptr inbounds %GenericList, ptr %chain_gep90, i32 0, i32 0
  %sizeVal92 = load i32, ptr %sizePtr91, align 4
  %71 = icmp ne i32 %sizeVal92, 1
  br i1 %71, label %if_then93, label %if_merge94

if_then93:                                        ; preds = %if_merge84
  %chain_base95 = load ptr, ptr %got, align 8
  %chain_gep96 = getelementptr inbounds %Card, ptr %chain_base95, i32 0, i32 5
  %sizePtr97 = getelementptr inbounds %GenericList, ptr %chain_gep96, i32 0, i32 0
  %sizeVal98 = load i32, ptr %sizePtr97, align 4
  %72 = call ptr @__aiml_int_to_string(i32 %sizeVal98)
  %73 = call ptr @__aiml_str_concat(ptr @27, ptr %72)
  %print_null_check99 = icmp eq ptr %73, null
  %print_safe_str100 = select i1 %print_null_check99, ptr @print_null_str.9, ptr %73
  %print_call101 = call i32 (ptr, ...) @printf(ptr @28, ptr %print_safe_str100)
  call void @exit(i32 1)
  br label %if_merge94

if_merge94:                                       ; preds = %if_then93, %if_merge84
  %chain_base102 = load ptr, ptr %got, align 8
  %chain_gep103 = getelementptr inbounds %Card, ptr %chain_base102, i32 0, i32 6
  %sizePtr104 = getelementptr inbounds %GenericList, ptr %chain_gep103, i32 0, i32 0
  %sizeVal105 = load i32, ptr %sizePtr104, align 4
  %74 = icmp ne i32 %sizeVal105, 1
  br i1 %74, label %if_then106, label %if_merge107

if_then106:                                       ; preds = %if_merge94
  %chain_base108 = load ptr, ptr %got, align 8
  %chain_gep109 = getelementptr inbounds %Card, ptr %chain_base108, i32 0, i32 6
  %sizePtr110 = getelementptr inbounds %GenericList, ptr %chain_gep109, i32 0, i32 0
  %sizeVal111 = load i32, ptr %sizePtr110, align 4
  %75 = call ptr @__aiml_int_to_string(i32 %sizeVal111)
  %76 = call ptr @__aiml_str_concat(ptr @29, ptr %75)
  %print_null_check112 = icmp eq ptr %76, null
  %print_safe_str113 = select i1 %print_null_check112, ptr @print_null_str.10, ptr %76
  %print_call114 = call i32 (ptr, ...) @printf(ptr @30, ptr %print_safe_str113)
  call void @exit(i32 1)
  br label %if_merge107

if_merge107:                                      ; preds = %if_then106, %if_merge94
  %77 = call i32 @aiml_db_checkpoint_all()
  %print_call115 = call i32 (ptr, ...) @printf(ptr @32, ptr @31)
  ret i32 0

ret_dead:                                         ; No predecessors!
  ret i32 0
}

define void @__aiml_init() {
entry:
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare i32 @aiml_trace_active(ptr)

declare void @aiml_trace(ptr, ptr, ptr)

declare ptr @aiml_path_intern(ptr)

declare i32 @aiml_io_remove_all(ptr)

; Function Attrs: noinline
define internal ptr @__aiml_deser_Card(ptr %json) #0 {
entry:
  %r = call ptr @aiml_jr_parse(ptr %json)
  %hp = call ptr @malloc(i64 176)
  call void @llvm.memset.p0.i64(ptr align 8 %hp, i8 0, i64 176, i1 false)
  %fp_name = getelementptr inbounds %Card, ptr %hp, i32 0, i32 0
  %sv = call ptr @aiml_jr_str(ptr %r, ptr @fk_name)
  store ptr %sv, ptr %fp_name, align 8
  %fp_card_type = getelementptr inbounds %Card, ptr %hp, i32 0, i32 1
  %sv1 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_card_type)
  store i32 %sv1, ptr %fp_card_type, align 4
  %fp_mkp = getelementptr inbounds %Card, ptr %hp, i32 0, i32 2
  %sv2 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_mkp)
  store i32 %sv2, ptr %fp_mkp, align 4
  %fp_mana_cost = getelementptr inbounds %Card, ptr %hp, i32 0, i32 3
  %sizePtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 1
  store i32 8, ptr %capPtr, align 4
  %0 = call ptr @calloc(i64 8, i64 1)
  %occPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 2
  store ptr %0, ptr %occPtr, align 8
  %1 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %keysPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 3
  store ptr %1, ptr %keysPtr, align 8
  %2 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 8))
  %valsPtr = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 4
  store ptr %2, ptr %valsPtr, align 8
  %cnt = call i32 @aiml_jr_map_count(ptr %r, ptr @fk_mana_cost)
  br label %mana_cost_dmc

mana_cost_dmc:                                    ; preds = %put_end, %entry
  %dmi = phi i32 [ 0, %entry ], [ %ni, %put_end ]
  %3 = icmp slt i32 %dmi, %cnt
  br i1 %3, label %mana_cost_dmb, label %mana_cost_dme

mana_cost_dmb:                                    ; preds = %mana_cost_dmc
  %k = call ptr @aiml_jr_map_key(ptr %r, ptr @fk_mana_cost, i32 %dmi)
  %v = call i32 @aiml_jr_map_val_i32(ptr %r, ptr @fk_mana_cost, i32 %dmi)
  %sizePtr3 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 0
  %capPtr4 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 1
  %size = load i32, ptr %sizePtr3, align 4
  %capacity = load i32, ptr %capPtr4, align 4
  %4 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 3
  %keysBase = load ptr, ptr %4, align 8
  %5 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 4
  %valsBase = load ptr, ptr %5, align 8
  %i_put = alloca i32, align 4
  store i32 0, ptr %i_put, align 4
  br label %put_loop

mana_cost_dme:                                    ; preds = %mana_cost_dmc
  %fp_description = getelementptr inbounds %Card, ptr %hp, i32 0, i32 4
  %sv5 = call ptr @aiml_jr_str(ptr %r, ptr @fk_description)
  store ptr %sv5, ptr %fp_description, align 8
  %fp_passive_abilities = getelementptr inbounds %Card, ptr %hp, i32 0, i32 5
  %sizePtr6 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 0
  store i32 0, ptr %sizePtr6, align 4
  %capPtr7 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 1
  store i32 4, ptr %capPtr7, align 4
  %6 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 2
  store ptr %6, ptr %dataPtr, align 8
  %cnt8 = call i32 @aiml_jr_arr_len(ptr %r, ptr @fk_passive_abilities)
  br label %passive_abilities_dlc

put_loop:                                         ; preds = %put_next, %mana_cost_dmb
  %7 = load i32, ptr %i_put, align 4
  %8 = icmp slt i32 %7, %size
  br i1 %8, label %put_check, label %put_not_found

put_check:                                        ; preds = %put_loop
  %9 = getelementptr inbounds ptr, ptr %keysBase, i32 %7
  %10 = load ptr, ptr %9, align 8
  %11 = call i32 @strcmp(ptr %k, ptr %10)
  %str_eq = icmp eq i32 %11, 0
  br i1 %str_eq, label %put_found, label %put_next

put_found:                                        ; preds = %put_check
  %12 = getelementptr inbounds i32, ptr %valsBase, i32 %7
  store i32 %v, ptr %12, align 4
  br label %put_end

put_next:                                         ; preds = %put_check
  %13 = add i32 %7, 1
  store i32 %13, ptr %i_put, align 4
  br label %put_loop

put_not_found:                                    ; preds = %put_loop
  %14 = icmp eq i32 %size, %capacity
  br i1 %14, label %put_grow, label %put_append

put_end:                                          ; preds = %put_append, %put_found
  %ni = add i32 %dmi, 1
  br label %mana_cost_dmc

put_grow:                                         ; preds = %put_not_found
  %15 = mul i32 %capacity, 2
  %16 = icmp sgt i32 %15, 3
  %growCap = select i1 %16, i32 %15, i32 4
  store i32 %growCap, ptr %capPtr4, align 4
  %17 = zext i32 %growCap to i64
  %18 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %17
  %19 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 3
  %20 = load ptr, ptr %19, align 8
  %21 = call ptr @realloc(ptr %20, i64 %18)
  store ptr %21, ptr %19, align 8
  %22 = zext i32 %growCap to i64
  %23 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %22
  %24 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 4
  %25 = load ptr, ptr %24, align 8
  %26 = call ptr @realloc(ptr %25, i64 %23)
  store ptr %26, ptr %24, align 8
  br label %put_append

put_append:                                       ; preds = %put_grow, %put_not_found
  %27 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 3
  %28 = load ptr, ptr %27, align 8
  %29 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_mana_cost, i32 0, i32 4
  %30 = load ptr, ptr %29, align 8
  %31 = getelementptr inbounds ptr, ptr %28, i32 %size
  store ptr %k, ptr %31, align 8
  %32 = getelementptr inbounds i32, ptr %30, i32 %size
  store i32 %v, ptr %32, align 4
  %33 = add i32 %size, 1
  store i32 %33, ptr %sizePtr3, align 4
  br label %put_end

passive_abilities_dlc:                            ; preds = %cont, %mana_cost_dme
  %dli = phi i32 [ 0, %mana_cost_dme ], [ %ni15, %cont ]
  %34 = icmp slt i32 %dli, %cnt8
  br i1 %34, label %passive_abilities_dlb, label %passive_abilities_dle

passive_abilities_dlb:                            ; preds = %passive_abilities_dlc
  %ae = call i32 @aiml_jr_arr_i32(ptr %r, ptr @fk_passive_abilities, i32 %dli)
  %sizePtr9 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 0
  %capPtr10 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 1
  %size11 = load i32, ptr %sizePtr9, align 4
  %capacity12 = load i32, ptr %capPtr10, align 4
  %35 = icmp eq i32 %size11, %capacity12
  br i1 %35, label %grow, label %cont

passive_abilities_dle:                            ; preds = %passive_abilities_dlc
  %fp_active_abilities = getelementptr inbounds %Card, ptr %hp, i32 0, i32 6
  %sizePtr16 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 0
  store i32 0, ptr %sizePtr16, align 4
  %capPtr17 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 1
  store i32 4, ptr %capPtr17, align 4
  %36 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%ActiveAbility, ptr null, i32 1) to i64), i64 4))
  %dataPtr18 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 2
  store ptr %36, ptr %dataPtr18, align 8
  %cnt19 = call i32 @aiml_jr_arr_len(ptr %r, ptr @fk_active_abilities)
  br label %active_abilities_dlc

grow:                                             ; preds = %passive_abilities_dlb
  %37 = mul i32 %capacity12, 2
  %38 = icmp sgt i32 %37, 0
  %39 = select i1 %38, i32 %37, i32 4
  store i32 %39, ptr %capPtr10, align 4
  %dataPtr13 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 2
  %40 = load ptr, ptr %dataPtr13, align 8
  %41 = zext i32 %39 to i64
  %42 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %41
  %43 = call ptr @realloc(ptr %40, i64 %42)
  store ptr %43, ptr %dataPtr13, align 8
  br label %cont

cont:                                             ; preds = %grow, %passive_abilities_dlb
  %dataPtr14 = getelementptr inbounds %GenericList, ptr %fp_passive_abilities, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr14, align 8
  %44 = getelementptr inbounds i32, ptr %dataBase, i32 %size11
  store i32 %ae, ptr %44, align 4
  %45 = add i32 %size11, 1
  store i32 %45, ptr %sizePtr9, align 4
  %ni15 = add i32 %dli, 1
  br label %passive_abilities_dlc

active_abilities_dlc:                             ; preds = %active_abilities_srdone, %passive_abilities_dle
  %dli20 = phi i32 [ 0, %passive_abilities_dle ], [ %ni109, %active_abilities_srdone ]
  %46 = icmp slt i32 %dli20, %cnt19
  br i1 %46, label %active_abilities_dlb, label %active_abilities_dle

active_abilities_dlb:                             ; preds = %active_abilities_dlc
  %sr = call ptr @aiml_jr_arr_sub(ptr %r, ptr @fk_active_abilities, i32 %dli20)
  %47 = icmp ne ptr %sr, null
  br i1 %47, label %active_abilities_srok, label %active_abilities_srdone

active_abilities_dle:                             ; preds = %active_abilities_dlc
  %fp_lp = getelementptr inbounds %Card, ptr %hp, i32 0, i32 7
  %sv110 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_lp)
  store i32 %sv110, ptr %fp_lp, align 4
  %fp_attack = getelementptr inbounds %Card, ptr %hp, i32 0, i32 8
  %sv111 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_attack)
  store i32 %sv111, ptr %fp_attack, align 4
  %fp_defense = getelementptr inbounds %Card, ptr %hp, i32 0, i32 9
  %sv112 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_defense)
  store i32 %sv112, ptr %fp_defense, align 4
  %fp_initiative = getelementptr inbounds %Card, ptr %hp, i32 0, i32 10
  %sv113 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_initiative)
  store i32 %sv113, ptr %fp_initiative, align 4
  %fp_moral = getelementptr inbounds %Card, ptr %hp, i32 0, i32 11
  %sv114 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_moral)
  store i32 %sv114, ptr %fp_moral, align 4
  %fp_movement = getelementptr inbounds %Card, ptr %hp, i32 0, i32 12
  %sv115 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_movement)
  store i32 %sv115, ptr %fp_movement, align 4
  %fp_spell_type = getelementptr inbounds %Card, ptr %hp, i32 0, i32 13
  %sv116 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_spell_type)
  store i32 %sv116, ptr %fp_spell_type, align 4
  %fp_equip_cost = getelementptr inbounds %Card, ptr %hp, i32 0, i32 14
  %sv117 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_equip_cost)
  store i32 %sv117, ptr %fp_equip_cost, align 4
  %fp_provides = getelementptr inbounds %Card, ptr %hp, i32 0, i32 15
  %sizePtr118 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 0
  store i32 0, ptr %sizePtr118, align 4
  %capPtr119 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 1
  store i32 8, ptr %capPtr119, align 4
  %48 = call ptr @calloc(i64 8, i64 1)
  %occPtr120 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 2
  store ptr %48, ptr %occPtr120, align 8
  %49 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 8))
  %keysPtr121 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 3
  store ptr %49, ptr %keysPtr121, align 8
  %50 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 8))
  %valsPtr122 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 4
  store ptr %50, ptr %valsPtr122, align 8
  %cnt123 = call i32 @aiml_jr_map_count(ptr %r, ptr @fk_provides)
  br label %provides_dmc

active_abilities_srok:                            ; preds = %active_abilities_dlb
  %stmp = alloca %ActiveAbility, align 8
  call void @llvm.memset.p0.i64(ptr align 8 %stmp, i8 0, i64 56, i1 false)
  %fp_cost_text = getelementptr inbounds %ActiveAbility, ptr %stmp, i32 0, i32 0
  %sv21 = call ptr @aiml_jr_str(ptr %sr, ptr @fk_cost_text)
  store ptr %sv21, ptr %fp_cost_text, align 8
  %fp_activation_cost = getelementptr inbounds %ActiveAbility, ptr %stmp, i32 0, i32 1
  %sizePtr22 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 0
  store i32 0, ptr %sizePtr22, align 4
  %capPtr23 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 1
  store i32 4, ptr %capPtr23, align 4
  %51 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr24 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 2
  store ptr %51, ptr %dataPtr24, align 8
  %cnt25 = call i32 @aiml_jr_arr_len(ptr %sr, ptr @fk_activation_cost)
  br label %activation_cost_dlc

active_abilities_srdone:                          ; preds = %cont105, %active_abilities_dlb
  %ni109 = add i32 %dli20, 1
  br label %active_abilities_dlc

activation_cost_dlc:                              ; preds = %cont33, %active_abilities_srok
  %dli26 = phi i32 [ 0, %active_abilities_srok ], [ %ni37, %cont33 ]
  %52 = icmp slt i32 %dli26, %cnt25
  br i1 %52, label %activation_cost_dlb, label %activation_cost_dle

activation_cost_dlb:                              ; preds = %activation_cost_dlc
  %ae27 = call i32 @aiml_jr_arr_i32(ptr %sr, ptr @fk_activation_cost, i32 %dli26)
  %sizePtr28 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 0
  %capPtr29 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 1
  %size30 = load i32, ptr %sizePtr28, align 4
  %capacity31 = load i32, ptr %capPtr29, align 4
  %53 = icmp eq i32 %size30, %capacity31
  br i1 %53, label %grow32, label %cont33

activation_cost_dle:                              ; preds = %activation_cost_dlc
  %fp_trigger = getelementptr inbounds %ActiveAbility, ptr %stmp, i32 0, i32 2
  %sizePtr38 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 0
  store i32 0, ptr %sizePtr38, align 4
  %capPtr39 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 1
  store i32 4, ptr %capPtr39, align 4
  %54 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr40 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 2
  store ptr %54, ptr %dataPtr40, align 8
  %cnt41 = call i32 @aiml_jr_arr_len(ptr %sr, ptr @fk_trigger)
  br label %trigger_dlc

grow32:                                           ; preds = %activation_cost_dlb
  %55 = mul i32 %capacity31, 2
  %56 = icmp sgt i32 %55, 0
  %57 = select i1 %56, i32 %55, i32 4
  store i32 %57, ptr %capPtr29, align 4
  %dataPtr34 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 2
  %58 = load ptr, ptr %dataPtr34, align 8
  %59 = zext i32 %57 to i64
  %60 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %59
  %61 = call ptr @realloc(ptr %58, i64 %60)
  store ptr %61, ptr %dataPtr34, align 8
  br label %cont33

cont33:                                           ; preds = %grow32, %activation_cost_dlb
  %dataPtr35 = getelementptr inbounds %GenericList, ptr %fp_activation_cost, i32 0, i32 2
  %dataBase36 = load ptr, ptr %dataPtr35, align 8
  %62 = getelementptr inbounds i32, ptr %dataBase36, i32 %size30
  store i32 %ae27, ptr %62, align 4
  %63 = add i32 %size30, 1
  store i32 %63, ptr %sizePtr28, align 4
  %ni37 = add i32 %dli26, 1
  br label %activation_cost_dlc

trigger_dlc:                                      ; preds = %cont62, %activation_cost_dle
  %dli42 = phi i32 [ 0, %activation_cost_dle ], [ %ni66, %cont62 ]
  %64 = icmp slt i32 %dli42, %cnt41
  br i1 %64, label %trigger_dlb, label %trigger_dle

trigger_dlb:                                      ; preds = %trigger_dlc
  %ila = alloca %list_i32, align 8
  %sizePtr43 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 0
  store i32 0, ptr %sizePtr43, align 4
  %capPtr44 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 1
  store i32 4, ptr %capPtr44, align 4
  %65 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr45 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  store ptr %65, ptr %dataPtr45, align 8
  %isr = call ptr @aiml_jr_arr_sub(ptr %sr, ptr @fk_trigger, i32 %dli42)
  %66 = icmp ne ptr %isr, null
  br i1 %66, label %trigger_isrok, label %trigger_isrdone

trigger_dle:                                      ; preds = %trigger_dlc
  %fp_effects = getelementptr inbounds %ActiveAbility, ptr %stmp, i32 0, i32 3
  %sizePtr67 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 0
  store i32 0, ptr %sizePtr67, align 4
  %capPtr68 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 1
  store i32 4, ptr %capPtr68, align 4
  %67 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr69 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 2
  store ptr %67, ptr %dataPtr69, align 8
  %cnt70 = call i32 @aiml_jr_arr_len(ptr %sr, ptr @fk_effects)
  br label %effects_dlc

trigger_isrok:                                    ; preds = %trigger_dlb
  %cnt46 = call i32 @aiml_jr_val_len(ptr %isr)
  br label %trigger_il_lc

trigger_isrdone:                                  ; preds = %trigger_il_le, %trigger_dlb
  %ilval = load %list_i32, ptr %ila, align 8
  %sizePtr57 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 0
  %capPtr58 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 1
  %size59 = load i32, ptr %sizePtr57, align 4
  %capacity60 = load i32, ptr %capPtr58, align 4
  %68 = icmp eq i32 %size59, %capacity60
  br i1 %68, label %grow61, label %cont62

trigger_il_lc:                                    ; preds = %cont52, %trigger_isrok
  %trigger_il_i = phi i32 [ 0, %trigger_isrok ], [ %ni56, %cont52 ]
  %69 = icmp slt i32 %trigger_il_i, %cnt46
  br i1 %69, label %trigger_il_lb, label %trigger_il_le

trigger_il_lb:                                    ; preds = %trigger_il_lc
  %ve = call i32 @aiml_jr_val_i32(ptr %isr, i32 %trigger_il_i)
  %sizePtr47 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 0
  %capPtr48 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 1
  %size49 = load i32, ptr %sizePtr47, align 4
  %capacity50 = load i32, ptr %capPtr48, align 4
  %70 = icmp eq i32 %size49, %capacity50
  br i1 %70, label %grow51, label %cont52

trigger_il_le:                                    ; preds = %trigger_il_lc
  call void @aiml_jr_free(ptr %isr)
  br label %trigger_isrdone

grow51:                                           ; preds = %trigger_il_lb
  %71 = mul i32 %capacity50, 2
  %72 = icmp sgt i32 %71, 0
  %73 = select i1 %72, i32 %71, i32 4
  store i32 %73, ptr %capPtr48, align 4
  %dataPtr53 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  %74 = load ptr, ptr %dataPtr53, align 8
  %75 = zext i32 %73 to i64
  %76 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %75
  %77 = call ptr @realloc(ptr %74, i64 %76)
  store ptr %77, ptr %dataPtr53, align 8
  br label %cont52

cont52:                                           ; preds = %grow51, %trigger_il_lb
  %dataPtr54 = getelementptr inbounds %GenericList, ptr %ila, i32 0, i32 2
  %dataBase55 = load ptr, ptr %dataPtr54, align 8
  %78 = getelementptr inbounds i32, ptr %dataBase55, i32 %size49
  store i32 %ve, ptr %78, align 4
  %79 = add i32 %size49, 1
  store i32 %79, ptr %sizePtr47, align 4
  %ni56 = add i32 %trigger_il_i, 1
  br label %trigger_il_lc

grow61:                                           ; preds = %trigger_isrdone
  %80 = mul i32 %capacity60, 2
  %81 = icmp sgt i32 %80, 0
  %82 = select i1 %81, i32 %80, i32 4
  store i32 %82, ptr %capPtr58, align 4
  %dataPtr63 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 2
  %83 = load ptr, ptr %dataPtr63, align 8
  %84 = zext i32 %82 to i64
  %85 = mul i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), %84
  %86 = call ptr @realloc(ptr %83, i64 %85)
  store ptr %86, ptr %dataPtr63, align 8
  br label %cont62

cont62:                                           ; preds = %grow61, %trigger_isrdone
  %dataPtr64 = getelementptr inbounds %GenericList, ptr %fp_trigger, i32 0, i32 2
  %dataBase65 = load ptr, ptr %dataPtr64, align 8
  %87 = getelementptr inbounds %list_i32, ptr %dataBase65, i32 %size59
  store %list_i32 %ilval, ptr %87, align 8
  %88 = add i32 %size59, 1
  store i32 %88, ptr %sizePtr57, align 4
  %ni66 = add i32 %dli42, 1
  br label %trigger_dlc

effects_dlc:                                      ; preds = %cont95, %trigger_dle
  %dli71 = phi i32 [ 0, %trigger_dle ], [ %ni99, %cont95 ]
  %89 = icmp slt i32 %dli71, %cnt70
  br i1 %89, label %effects_dlb, label %effects_dle

effects_dlb:                                      ; preds = %effects_dlc
  %ila72 = alloca %list_i32, align 8
  %sizePtr73 = getelementptr inbounds %GenericList, ptr %ila72, i32 0, i32 0
  store i32 0, ptr %sizePtr73, align 4
  %capPtr74 = getelementptr inbounds %GenericList, ptr %ila72, i32 0, i32 1
  store i32 4, ptr %capPtr74, align 4
  %90 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), i64 4))
  %dataPtr75 = getelementptr inbounds %GenericList, ptr %ila72, i32 0, i32 2
  store ptr %90, ptr %dataPtr75, align 8
  %isr76 = call ptr @aiml_jr_arr_sub(ptr %sr, ptr @fk_effects, i32 %dli71)
  %91 = icmp ne ptr %isr76, null
  br i1 %91, label %effects_isrok, label %effects_isrdone

effects_dle:                                      ; preds = %effects_dlc
  %sval = load %ActiveAbility, ptr %stmp, align 8
  %sizePtr100 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 0
  %capPtr101 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 1
  %size102 = load i32, ptr %sizePtr100, align 4
  %capacity103 = load i32, ptr %capPtr101, align 4
  %92 = icmp eq i32 %size102, %capacity103
  br i1 %92, label %grow104, label %cont105

effects_isrok:                                    ; preds = %effects_dlb
  %cnt77 = call i32 @aiml_jr_val_len(ptr %isr76)
  br label %effects_il_lc

effects_isrdone:                                  ; preds = %effects_il_le, %effects_dlb
  %ilval89 = load %list_i32, ptr %ila72, align 8
  %sizePtr90 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 0
  %capPtr91 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 1
  %size92 = load i32, ptr %sizePtr90, align 4
  %capacity93 = load i32, ptr %capPtr91, align 4
  %93 = icmp eq i32 %size92, %capacity93
  br i1 %93, label %grow94, label %cont95

effects_il_lc:                                    ; preds = %cont84, %effects_isrok
  %effects_il_i = phi i32 [ 0, %effects_isrok ], [ %ni88, %cont84 ]
  %94 = icmp slt i32 %effects_il_i, %cnt77
  br i1 %94, label %effects_il_lb, label %effects_il_le

effects_il_lb:                                    ; preds = %effects_il_lc
  %ve78 = call i32 @aiml_jr_val_i32(ptr %isr76, i32 %effects_il_i)
  %sizePtr79 = getelementptr inbounds %GenericList, ptr %ila72, i32 0, i32 0
  %capPtr80 = getelementptr inbounds %GenericList, ptr %ila72, i32 0, i32 1
  %size81 = load i32, ptr %sizePtr79, align 4
  %capacity82 = load i32, ptr %capPtr80, align 4
  %95 = icmp eq i32 %size81, %capacity82
  br i1 %95, label %grow83, label %cont84

effects_il_le:                                    ; preds = %effects_il_lc
  call void @aiml_jr_free(ptr %isr76)
  br label %effects_isrdone

grow83:                                           ; preds = %effects_il_lb
  %96 = mul i32 %capacity82, 2
  %97 = icmp sgt i32 %96, 0
  %98 = select i1 %97, i32 %96, i32 4
  store i32 %98, ptr %capPtr80, align 4
  %dataPtr85 = getelementptr inbounds %GenericList, ptr %ila72, i32 0, i32 2
  %99 = load ptr, ptr %dataPtr85, align 8
  %100 = zext i32 %98 to i64
  %101 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %100
  %102 = call ptr @realloc(ptr %99, i64 %101)
  store ptr %102, ptr %dataPtr85, align 8
  br label %cont84

cont84:                                           ; preds = %grow83, %effects_il_lb
  %dataPtr86 = getelementptr inbounds %GenericList, ptr %ila72, i32 0, i32 2
  %dataBase87 = load ptr, ptr %dataPtr86, align 8
  %103 = getelementptr inbounds i32, ptr %dataBase87, i32 %size81
  store i32 %ve78, ptr %103, align 4
  %104 = add i32 %size81, 1
  store i32 %104, ptr %sizePtr79, align 4
  %ni88 = add i32 %effects_il_i, 1
  br label %effects_il_lc

grow94:                                           ; preds = %effects_isrdone
  %105 = mul i32 %capacity93, 2
  %106 = icmp sgt i32 %105, 0
  %107 = select i1 %106, i32 %105, i32 4
  store i32 %107, ptr %capPtr91, align 4
  %dataPtr96 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 2
  %108 = load ptr, ptr %dataPtr96, align 8
  %109 = zext i32 %107 to i64
  %110 = mul i64 ptrtoint (ptr getelementptr inbounds (%list_i32, ptr null, i32 1) to i64), %109
  %111 = call ptr @realloc(ptr %108, i64 %110)
  store ptr %111, ptr %dataPtr96, align 8
  br label %cont95

cont95:                                           ; preds = %grow94, %effects_isrdone
  %dataPtr97 = getelementptr inbounds %GenericList, ptr %fp_effects, i32 0, i32 2
  %dataBase98 = load ptr, ptr %dataPtr97, align 8
  %112 = getelementptr inbounds %list_i32, ptr %dataBase98, i32 %size92
  store %list_i32 %ilval89, ptr %112, align 8
  %113 = add i32 %size92, 1
  store i32 %113, ptr %sizePtr90, align 4
  %ni99 = add i32 %dli71, 1
  br label %effects_dlc

grow104:                                          ; preds = %effects_dle
  %114 = mul i32 %capacity103, 2
  %115 = icmp sgt i32 %114, 0
  %116 = select i1 %115, i32 %114, i32 4
  store i32 %116, ptr %capPtr101, align 4
  %dataPtr106 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 2
  %117 = load ptr, ptr %dataPtr106, align 8
  %118 = zext i32 %116 to i64
  %119 = mul i64 ptrtoint (ptr getelementptr inbounds (%ActiveAbility, ptr null, i32 1) to i64), %118
  %120 = call ptr @realloc(ptr %117, i64 %119)
  store ptr %120, ptr %dataPtr106, align 8
  br label %cont105

cont105:                                          ; preds = %grow104, %effects_dle
  %dataPtr107 = getelementptr inbounds %GenericList, ptr %fp_active_abilities, i32 0, i32 2
  %dataBase108 = load ptr, ptr %dataPtr107, align 8
  %121 = getelementptr inbounds %ActiveAbility, ptr %dataBase108, i32 %size102
  store %ActiveAbility %sval, ptr %121, align 8
  %122 = add i32 %size102, 1
  store i32 %122, ptr %sizePtr100, align 4
  call void @aiml_jr_free(ptr %sr)
  br label %active_abilities_srdone

provides_dmc:                                     ; preds = %put_end138, %active_abilities_dle
  %dmi124 = phi i32 [ 0, %active_abilities_dle ], [ %ni144, %put_end138 ]
  %123 = icmp slt i32 %dmi124, %cnt123
  br i1 %123, label %provides_dmb, label %provides_dme

provides_dmb:                                     ; preds = %provides_dmc
  %k125 = call ptr @aiml_jr_map_key(ptr %r, ptr @fk_provides, i32 %dmi124)
  %v126 = call i32 @aiml_jr_map_val_i32(ptr %r, ptr @fk_provides, i32 %dmi124)
  %sizePtr127 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 0
  %capPtr128 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 1
  %size129 = load i32, ptr %sizePtr127, align 4
  %capacity130 = load i32, ptr %capPtr128, align 4
  %124 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 3
  %keysBase131 = load ptr, ptr %124, align 8
  %125 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 4
  %valsBase132 = load ptr, ptr %125, align 8
  %i_put139 = alloca i32, align 4
  store i32 0, ptr %i_put139, align 4
  br label %put_loop133

provides_dme:                                     ; preds = %provides_dmc
  %fp_skp_cost = getelementptr inbounds %Card, ptr %hp, i32 0, i32 16
  %sv145 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_skp_cost)
  store i32 %sv145, ptr %fp_skp_cost, align 4
  %fp_duration = getelementptr inbounds %Card, ptr %hp, i32 0, i32 17
  %sv146 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_duration)
  store i32 %sv146, ptr %fp_duration, align 4
  %fp_room_effect = getelementptr inbounds %Card, ptr %hp, i32 0, i32 18
  %sv147 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_room_effect)
  store i32 %sv147, ptr %fp_room_effect, align 4
  %fp_created_at = getelementptr inbounds %Card, ptr %hp, i32 0, i32 19
  %sv148 = call i64 @aiml_jr_i64(ptr %r, ptr @fk_created_at)
  store i64 %sv148, ptr %fp_created_at, align 8
  call void @aiml_jr_free(ptr %r)
  ret ptr %hp

put_loop133:                                      ; preds = %put_next136, %provides_dmb
  %126 = load i32, ptr %i_put139, align 4
  %127 = icmp slt i32 %126, %size129
  br i1 %127, label %put_check134, label %put_not_found137

put_check134:                                     ; preds = %put_loop133
  %128 = getelementptr inbounds ptr, ptr %keysBase131, i32 %126
  %129 = load ptr, ptr %128, align 8
  %130 = call i32 @strcmp(ptr %k125, ptr %129)
  %str_eq140 = icmp eq i32 %130, 0
  br i1 %str_eq140, label %put_found135, label %put_next136

put_found135:                                     ; preds = %put_check134
  %131 = getelementptr inbounds i32, ptr %valsBase132, i32 %126
  store i32 %v126, ptr %131, align 4
  br label %put_end138

put_next136:                                      ; preds = %put_check134
  %132 = add i32 %126, 1
  store i32 %132, ptr %i_put139, align 4
  br label %put_loop133

put_not_found137:                                 ; preds = %put_loop133
  %133 = icmp eq i32 %size129, %capacity130
  br i1 %133, label %put_grow141, label %put_append142

put_end138:                                       ; preds = %put_append142, %put_found135
  %ni144 = add i32 %dmi124, 1
  br label %provides_dmc

put_grow141:                                      ; preds = %put_not_found137
  %134 = mul i32 %capacity130, 2
  %135 = icmp sgt i32 %134, 3
  %growCap143 = select i1 %135, i32 %134, i32 4
  store i32 %growCap143, ptr %capPtr128, align 4
  %136 = zext i32 %growCap143 to i64
  %137 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %136
  %138 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 3
  %139 = load ptr, ptr %138, align 8
  %140 = call ptr @realloc(ptr %139, i64 %137)
  store ptr %140, ptr %138, align 8
  %141 = zext i32 %growCap143 to i64
  %142 = mul i64 ptrtoint (ptr getelementptr inbounds (i32, ptr null, i32 1) to i64), %141
  %143 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 4
  %144 = load ptr, ptr %143, align 8
  %145 = call ptr @realloc(ptr %144, i64 %142)
  store ptr %145, ptr %143, align 8
  br label %put_append142

put_append142:                                    ; preds = %put_grow141, %put_not_found137
  %146 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 3
  %147 = load ptr, ptr %146, align 8
  %148 = getelementptr inbounds { i32, i32, ptr, ptr, ptr }, ptr %fp_provides, i32 0, i32 4
  %149 = load ptr, ptr %148, align 8
  %150 = getelementptr inbounds ptr, ptr %147, i32 %size129
  store ptr %k125, ptr %150, align 8
  %151 = getelementptr inbounds i32, ptr %149, i32 %size129
  store i32 %v126, ptr %151, align 4
  %152 = add i32 %size129, 1
  store i32 %152, ptr %sizePtr127, align 4
  br label %put_end138
}

declare ptr @aiml_jr_parse(ptr)

declare ptr @malloc(i64)

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #1

declare ptr @aiml_jr_str(ptr, ptr)

declare i32 @aiml_jr_i32(ptr, ptr)

declare ptr @calloc(i64, i64)

declare i32 @aiml_jr_map_count(ptr, ptr)

declare ptr @aiml_jr_map_key(ptr, ptr, i32)

declare i32 @aiml_jr_map_val_i32(ptr, ptr, i32)

declare i32 @strcmp(ptr, ptr)

declare ptr @realloc(ptr, i64)

declare i32 @aiml_jr_arr_len(ptr, ptr)

declare i32 @aiml_jr_arr_i32(ptr, ptr, i32)

declare ptr @aiml_jr_arr_sub(ptr, ptr, i32)

declare i32 @aiml_jr_val_len(ptr)

declare i32 @aiml_jr_val_i32(ptr, i32)

declare void @aiml_jr_free(ptr)

declare i64 @aiml_jr_i64(ptr, ptr)

declare ptr @memcpy(ptr, ptr, i64)

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

declare i32 @sprintf(ptr, ptr, ...)

; Function Attrs: noinline
define internal ptr @__aiml_int_to_string(i32 %0) #0 {
entry:
  %1 = call ptr @malloc(i64 12)
  %2 = call i32 (ptr, ptr, ...) @sprintf(ptr %1, ptr @fmt_d, i32 %0)
  ret ptr %1
}

declare ptr @aiml_db_kv_open_typed_persist(ptr, i64, ptr, ptr, i32)

declare void @aiml_db_kv_set(ptr, ptr, ptr, i64)

declare ptr @aiml_db_kv_get(ptr, ptr)

declare i32 @aiml_db_checkpoint_all()

attributes #0 = { noinline }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: write) }
