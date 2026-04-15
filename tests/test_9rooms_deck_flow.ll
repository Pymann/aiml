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
%Deck = type { ptr, ptr, ptr, %list_string, i32, i64 }
%list_string = type { i32, i32, ptr }
%GenericList = type { i32, i32, ptr }

@__argc = external global i32
@__argv = external global ptr
@__aiml_sernodes_ActiveAbility = private unnamed_addr constant [10 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 1, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 5, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 6, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 8, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 7, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 9, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__aiml_sernodes_BaseCard = private unnamed_addr constant [21 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 7, i32 1, i32 0, i32 88, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 12, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 8, i32 9, i32 16, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 48, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 10, i32 0, i32 56, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 11, i32 0, i32 72, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 12, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 16, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 17, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 19, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 18, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 20, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__aiml_sernodes_Card = private unnamed_addr constant [30 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 16, i32 1, i32 0, i32 128, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 12, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 7, i32 17, i32 18, i32 16, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 48, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 19, i32 0, i32 56, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 20, i32 0, i32 72, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 88, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 92, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 96, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 100, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 104, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 108, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 112, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 116, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 3, i32 0, i32 0, i32 120, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 9, i32 4, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 5, i32 4, i32 21, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 25, i32 0, i32 8, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 26, i32 0, i32 24, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 28, i32 0, i32 40, i32 16, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 27, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 29, i32 0, i32 0, i32 4, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@__aiml_sernodes_Deck = private unnamed_addr constant [8 x { i32, i32, i32, i32, i32, i32 }] [{ i32, i32, i32, i32, i32, i32 } { i32 5, i32 6, i32 1, i32 0, i32 56, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 8, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 16, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 6, i32 7, i32 0, i32 24, i32 8, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 2, i32 0, i32 0, i32 40, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 3, i32 0, i32 0, i32 48, i32 0, i32 0 }, { i32, i32, i32, i32, i32, i32 } { i32 4, i32 0, i32 0, i32 0, i32 0, i32 0 }]
@0 = private unnamed_addr constant [33 x i8] c"/tmp/aiml_test_9rooms_deck_flow/\00", align 1
@1 = private unnamed_addr constant [14 x i8] c"9r_cards_test\00", align 1
@2 = private unnamed_addr constant [256 x i8] c"{\22__type\22:\22Card\22,\22name\22:\22Spark\22,\22card_type\22:3,\22mkp\22:2,\22mana_cost\22:{},\22description\22:\22mana source\22,\22passive_abilities\22:[],\22active_abilities\22:[],\22lp\22:0,\22attack\22:0,\22defense\22:0,\22initiative\22:0,\22moral\22:0,\22movement\22:0,\22spell_type\22:2,\22equip_cost\22:0,\22created_at\22:0}\00", align 1
@3 = private unnamed_addr constant [263 x i8] c"{\22__type\22:\22Card\22,\22name\22:\22Blaze\22,\22card_type\22:3,\22mkp\22:5,\22mana_cost\22:{\221\22:1},\22description\22:\22bigger mana\22,\22passive_abilities\22:[18],\22active_abilities\22:[],\22lp\22:0,\22attack\22:0,\22defense\22:0,\22initiative\22:0,\22moral\22:0,\22movement\22:0,\22spell_type\22:2,\22equip_cost\22:0,\22created_at\22:0}\00", align 1
@4 = private unnamed_addr constant [3 x i8] c"c1\00", align 1
@5 = private unnamed_addr constant [3 x i8] c"c2\00", align 1
@6 = private unnamed_addr constant [140 x i8] c"{\22__type\22:\22Deck\22,\22name\22:\22Mana Starter\22,\22deck_type\22:\22mana_deck\22,\22description\22:\22starter\22,\22card_ids\22:[\22c1\22,\22c2\22],\22total_mkp\22:0,\22created_at\22:0}\00", align 1
@fk_name = private unnamed_addr constant [5 x i8] c"name\00", align 1
@fk_deck_type = private unnamed_addr constant [10 x i8] c"deck_type\00", align 1
@fk_description = private unnamed_addr constant [12 x i8] c"description\00", align 1
@fk_card_ids = private unnamed_addr constant [9 x i8] c"card_ids\00", align 1
@fk_total_mkp = private unnamed_addr constant [10 x i8] c"total_mkp\00", align 1
@fk_created_at = private unnamed_addr constant [11 x i8] c"created_at\00", align 1
@7 = private unnamed_addr constant [14 x i8] c"FAIL mkp_sum=\00", align 1
@fmt_d = private unnamed_addr constant [3 x i8] c"%d\00", align 1
@empty = private unnamed_addr constant [1 x i8] zeroinitializer, align 1
@print_null_str = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@8 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@9 = private unnamed_addr constant [33 x i8] c"/tmp/aiml_test_9rooms_deck_flow/\00", align 1
@10 = private unnamed_addr constant [14 x i8] c"9r_decks_test\00", align 1
@11 = private unnamed_addr constant [7 x i8] c"deck_a\00", align 1
@12 = private unnamed_addr constant [7 x i8] c"deck_a\00", align 1
@13 = private unnamed_addr constant [13 x i8] c"Mana Starter\00", align 1
@14 = private unnamed_addr constant [15 x i8] c"FAIL got.name=\00", align 1
@print_null_str.1 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@15 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@16 = private unnamed_addr constant [20 x i8] c"FAIL got.total_mkp=\00", align 1
@print_null_str.2 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@17 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@18 = private unnamed_addr constant [23 x i8] c"FAIL got.card_ids.len=\00", align 1
@print_null_str.3 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@19 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@20 = private unnamed_addr constant [3 x i8] c"c1\00", align 1
@21 = private unnamed_addr constant [21 x i8] c"FAIL got.card_ids-0=\00", align 1
@print_null_str.4 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@22 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@23 = private unnamed_addr constant [3 x i8] c"c2\00", align 1
@24 = private unnamed_addr constant [21 x i8] c"FAIL got.card_ids-1=\00", align 1
@print_null_str.5 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@25 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
@26 = private unnamed_addr constant [22 x i8] c"9rooms deck flow PASS\00", align 1
@print_null_str.6 = private unnamed_addr constant [7 x i8] c"(null)\00", align 1
@27 = private unnamed_addr constant [4 x i8] c"%s\0A\00", align 1
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
@__aiml_sname_Deck = private constant [5 x i8] c"Deck\00"
@__aiml_sfield_Deck_name = private constant [5 x i8] c"name\00"
@__aiml_sfield_Deck_deck_type = private constant [10 x i8] c"deck_type\00"
@__aiml_sfield_Deck_description = private constant [12 x i8] c"description\00"
@__aiml_sfield_Deck_card_ids = private constant [9 x i8] c"card_ids\00"
@__aiml_sfield_Deck_total_mkp = private constant [10 x i8] c"total_mkp\00"
@__aiml_sfield_Deck_created_at = private constant [11 x i8] c"created_at\00"
@__aiml_schemas = global [4 x %AimlClassSchema] [%AimlClassSchema { ptr @__aiml_sname_ActiveAbility, i32 1, i32 4, i32 56, i32 64, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_cost_text }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_activation_cost }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_trigger }, %AimlFieldInfo { ptr @__aiml_sfield_ActiveAbility_effects }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_BaseCard, i32 1, i32 7, i32 88, i32 128, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_BaseCard_name }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_card_type }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_mkp }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_mana_cost }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_description }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_passive_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_BaseCard_active_abilities }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Card, i32 1, i32 16, i32 128, i32 128, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Card_name }, %AimlFieldInfo { ptr @__aiml_sfield_Card_card_type }, %AimlFieldInfo { ptr @__aiml_sfield_Card_mkp }, %AimlFieldInfo { ptr @__aiml_sfield_Card_mana_cost }, %AimlFieldInfo { ptr @__aiml_sfield_Card_description }, %AimlFieldInfo { ptr @__aiml_sfield_Card_passive_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_Card_active_abilities }, %AimlFieldInfo { ptr @__aiml_sfield_Card_lp }, %AimlFieldInfo { ptr @__aiml_sfield_Card_attack }, %AimlFieldInfo { ptr @__aiml_sfield_Card_defense }, %AimlFieldInfo { ptr @__aiml_sfield_Card_initiative }, %AimlFieldInfo { ptr @__aiml_sfield_Card_moral }, %AimlFieldInfo { ptr @__aiml_sfield_Card_movement }, %AimlFieldInfo { ptr @__aiml_sfield_Card_spell_type }, %AimlFieldInfo { ptr @__aiml_sfield_Card_equip_cost }, %AimlFieldInfo { ptr @__aiml_sfield_Card_created_at }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }, %AimlClassSchema { ptr @__aiml_sname_Deck, i32 1, i32 6, i32 56, i32 64, [256 x %AimlFieldInfo] [%AimlFieldInfo { ptr @__aiml_sfield_Deck_name }, %AimlFieldInfo { ptr @__aiml_sfield_Deck_deck_type }, %AimlFieldInfo { ptr @__aiml_sfield_Deck_description }, %AimlFieldInfo { ptr @__aiml_sfield_Deck_card_ids }, %AimlFieldInfo { ptr @__aiml_sfield_Deck_total_mkp }, %AimlFieldInfo { ptr @__aiml_sfield_Deck_created_at }, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer, %AimlFieldInfo zeroinitializer] }]
@__aiml_schema_count = constant i32 4

define i32 @main() personality ptr @__gxx_personality_v0 {
entry:
  %cards_kv = alloca ptr, align 8
  store ptr null, ptr %cards_kv, align 8
  %0 = call ptr @aiml_db_kv_open_typed_persist(ptr @1, i64 128, ptr @0, ptr @__aiml_sernodes_Card, i32 30)
  store ptr %0, ptr %cards_kv, align 8
  %cjson1 = alloca ptr, align 8
  store ptr null, ptr %cjson1, align 8
  store ptr @2, ptr %cjson1, align 8
  %cjson2 = alloca ptr, align 8
  store ptr null, ptr %cjson2, align 8
  store ptr @3, ptr %cjson2, align 8
  %c1 = alloca %Card, align 8
  store %Card zeroinitializer, ptr %c1, align 8
  %c2 = alloca %Card, align 8
  store %Card zeroinitializer, ptr %c2, align 8
  %cjson1_val = load ptr, ptr %cjson1, align 8
  store i32 0, ptr %c1, align 4
  %cjson2_val = load ptr, ptr %cjson2, align 8
  store i32 0, ptr %c2, align 4
  %c1_val = load %Card, ptr %c1, align 8
  %kv_ptr = load ptr, ptr %cards_kv, align 8
  %kv_val_tmp = alloca %Card, align 8
  store %Card %c1_val, ptr %kv_val_tmp, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr, ptr @4, ptr %kv_val_tmp, i64 128)
  %c2_val = load %Card, ptr %c2, align 8
  %kv_ptr1 = load ptr, ptr %cards_kv, align 8
  %kv_val_tmp2 = alloca %Card, align 8
  store %Card %c2_val, ptr %kv_val_tmp2, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr1, ptr @5, ptr %kv_val_tmp2, i64 128)
  %djson = alloca ptr, align 8
  store ptr null, ptr %djson, align 8
  store ptr @6, ptr %djson, align 8
  %d = alloca %Deck, align 8
  store %Deck zeroinitializer, ptr %d, align 8
  %djson_val = load ptr, ptr %djson, align 8
  %deser_r = call ptr @__aiml_deser_Deck(ptr %djson_val)
  %1 = call ptr @memcpy(ptr %d, ptr %deser_r, i64 56)
  %mkp_sum = alloca i32, align 4
  store i32 0, ptr %mkp_sum, align 4
  store i32 0, ptr %mkp_sum, align 4
  %mk_i = alloca i32, align 4
  store i32 0, ptr %mk_i, align 4
  store i32 0, ptr %mk_i, align 4
  %mk_n = alloca i32, align 4
  store i32 0, ptr %mk_n, align 4
  %chain_gep = getelementptr inbounds %Deck, ptr %d, i32 0, i32 3
  %sizePtr = getelementptr inbounds %GenericList, ptr %chain_gep, i32 0, i32 0
  %sizeVal = load i32, ptr %sizePtr, align 4
  store i32 %sizeVal, ptr %mk_n, align 4
  %mk_cid = alloca ptr, align 8
  store ptr null, ptr %mk_cid, align 8
  %mk_has = alloca i32, align 4
  store i32 0, ptr %mk_has, align 4
  %mk_cp = alloca ptr, align 8
  store ptr null, ptr %mk_cp, align 8
  br label %while_loop

while_loop:                                       ; preds = %if_merge, %entry
  %mk_i_val = load i32, ptr %mk_i, align 4
  %mk_n_val = load i32, ptr %mk_n, align 4
  %2 = icmp slt i32 %mk_i_val, %mk_n_val
  br i1 %2, label %while_body, label %while_end

while_body:                                       ; preds = %while_loop
  %chain_gep3 = getelementptr inbounds %Deck, ptr %d, i32 0, i32 3
  %3 = load i32, ptr %mk_i, align 4
  %dataPtr = getelementptr inbounds %GenericList, ptr %chain_gep3, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr, align 8
  %4 = getelementptr inbounds ptr, ptr %dataBase, i32 %3
  %chain_elem = load ptr, ptr %4, align 8
  store ptr %chain_elem, ptr %mk_cid, align 8
  %mk_cid_val = load ptr, ptr %mk_cid, align 8
  %kv_ptr4 = load ptr, ptr %cards_kv, align 8
  %5 = call i32 @aiml_db_kv_has(ptr %kv_ptr4, ptr %mk_cid_val)
  store i32 %5, ptr %mk_has, align 4
  %mk_has_val = load i32, ptr %mk_has, align 4
  %6 = icmp eq i32 %mk_has_val, 1
  br i1 %6, label %if_then, label %if_merge

while_end:                                        ; preds = %while_loop
  %mkp_sum_val8 = load i32, ptr %mkp_sum, align 4
  %7 = icmp ne i32 %mkp_sum_val8, 7
  br i1 %7, label %if_then9, label %if_merge10

if_then:                                          ; preds = %while_body
  %mk_cid_val5 = load ptr, ptr %mk_cid, align 8
  %kv_ptr6 = load ptr, ptr %cards_kv, align 8
  %8 = call ptr @aiml_db_kv_get(ptr %kv_ptr6, ptr %mk_cid_val5)
  store ptr %8, ptr %mk_cp, align 8
  %mkp_sum_val = load i32, ptr %mkp_sum, align 4
  %prop_ptr_base = load ptr, ptr %mk_cp, align 8
  %prop_ptr = getelementptr inbounds %Card, ptr %prop_ptr_base, i32 0, i32 2
  %mkp_val = load i32, ptr %prop_ptr, align 4
  %9 = add i32 %mkp_sum_val, %mkp_val
  store i32 %9, ptr %mkp_sum, align 4
  br label %if_merge

if_merge:                                         ; preds = %if_then, %while_body
  %mk_i_val7 = load i32, ptr %mk_i, align 4
  %10 = add i32 %mk_i_val7, 1
  store i32 %10, ptr %mk_i, align 4
  br label %while_loop

if_then9:                                         ; preds = %while_end
  %mkp_sum_val11 = load i32, ptr %mkp_sum, align 4
  %11 = call ptr @__aiml_int_to_string(i32 %mkp_sum_val11)
  %12 = call ptr @__aiml_str_concat(ptr @7, ptr %11)
  %print_null_check = icmp eq ptr %12, null
  %print_safe_str = select i1 %print_null_check, ptr @print_null_str, ptr %12
  %print_call = call i32 (ptr, ...) @printf(ptr @8, ptr %print_safe_str)
  call void @exit(i32 1)
  br label %if_merge10

if_merge10:                                       ; preds = %if_then9, %while_end
  %mkp_sum_val12 = load i32, ptr %mkp_sum, align 4
  %set_prop_ptr_w = getelementptr inbounds %Deck, ptr %d, i32 0, i32 4
  store i32 %mkp_sum_val12, ptr %set_prop_ptr_w, align 4
  %decks_kv = alloca ptr, align 8
  store ptr null, ptr %decks_kv, align 8
  %13 = call ptr @aiml_db_kv_open_typed_persist(ptr @10, i64 56, ptr @9, ptr @__aiml_sernodes_Deck, i32 8)
  store ptr %13, ptr %decks_kv, align 8
  %d_val = load %Deck, ptr %d, align 8
  %kv_ptr13 = load ptr, ptr %decks_kv, align 8
  %kv_val_tmp14 = alloca %Deck, align 8
  store %Deck %d_val, ptr %kv_val_tmp14, align 8
  call void @aiml_db_kv_set(ptr %kv_ptr13, ptr @11, ptr %kv_val_tmp14, i64 56)
  %got = alloca ptr, align 8
  store ptr null, ptr %got, align 8
  %kv_ptr15 = load ptr, ptr %decks_kv, align 8
  %14 = call ptr @aiml_db_kv_get(ptr %kv_ptr15, ptr @12)
  store ptr %14, ptr %got, align 8
  %prop_ptr_base16 = load ptr, ptr %got, align 8
  %prop_ptr17 = getelementptr inbounds %Deck, ptr %prop_ptr_base16, i32 0, i32 0
  %name_val = load ptr, ptr %prop_ptr17, align 8
  %15 = icmp eq ptr %name_val, null
  %16 = icmp eq ptr %name_val, @13
  %17 = or i1 %15, false
  br i1 %17, label %cmp_check_null, label %cmp_do_strcmp

cmp_check_null:                                   ; preds = %if_merge10
  br label %cmp_merge

cmp_do_strcmp:                                    ; preds = %if_merge10
  %18 = call i32 @strcmp(ptr %name_val, ptr @13)
  %19 = icmp eq i32 %18, 0
  br label %cmp_merge

cmp_merge:                                        ; preds = %cmp_do_strcmp, %cmp_check_null
  %strcmp_res = phi i1 [ %16, %cmp_check_null ], [ %19, %cmp_do_strcmp ]
  %20 = sext i1 %strcmp_res to i32
  %21 = icmp eq i32 %20, 0
  br i1 %21, label %if_then18, label %if_merge19

if_then18:                                        ; preds = %cmp_merge
  %prop_ptr_base20 = load ptr, ptr %got, align 8
  %prop_ptr21 = getelementptr inbounds %Deck, ptr %prop_ptr_base20, i32 0, i32 0
  %name_val22 = load ptr, ptr %prop_ptr21, align 8
  %22 = call ptr @__aiml_str_concat(ptr @14, ptr %name_val22)
  %print_null_check23 = icmp eq ptr %22, null
  %print_safe_str24 = select i1 %print_null_check23, ptr @print_null_str.1, ptr %22
  %print_call25 = call i32 (ptr, ...) @printf(ptr @15, ptr %print_safe_str24)
  call void @exit(i32 1)
  br label %if_merge19

if_merge19:                                       ; preds = %if_then18, %cmp_merge
  %prop_ptr_base26 = load ptr, ptr %got, align 8
  %prop_ptr27 = getelementptr inbounds %Deck, ptr %prop_ptr_base26, i32 0, i32 4
  %total_mkp_val = load i32, ptr %prop_ptr27, align 4
  %23 = icmp ne i32 %total_mkp_val, 7
  br i1 %23, label %if_then28, label %if_merge29

if_then28:                                        ; preds = %if_merge19
  %prop_ptr_base30 = load ptr, ptr %got, align 8
  %prop_ptr31 = getelementptr inbounds %Deck, ptr %prop_ptr_base30, i32 0, i32 4
  %total_mkp_val32 = load i32, ptr %prop_ptr31, align 4
  %24 = call ptr @__aiml_int_to_string(i32 %total_mkp_val32)
  %25 = call ptr @__aiml_str_concat(ptr @16, ptr %24)
  %print_null_check33 = icmp eq ptr %25, null
  %print_safe_str34 = select i1 %print_null_check33, ptr @print_null_str.2, ptr %25
  %print_call35 = call i32 (ptr, ...) @printf(ptr @17, ptr %print_safe_str34)
  call void @exit(i32 1)
  br label %if_merge29

if_merge29:                                       ; preds = %if_then28, %if_merge19
  %chain_base = load ptr, ptr %got, align 8
  %chain_gep36 = getelementptr inbounds %Deck, ptr %chain_base, i32 0, i32 3
  %sizePtr37 = getelementptr inbounds %GenericList, ptr %chain_gep36, i32 0, i32 0
  %sizeVal38 = load i32, ptr %sizePtr37, align 4
  %26 = icmp ne i32 %sizeVal38, 2
  br i1 %26, label %if_then39, label %if_merge40

if_then39:                                        ; preds = %if_merge29
  %chain_base41 = load ptr, ptr %got, align 8
  %chain_gep42 = getelementptr inbounds %Deck, ptr %chain_base41, i32 0, i32 3
  %sizePtr43 = getelementptr inbounds %GenericList, ptr %chain_gep42, i32 0, i32 0
  %sizeVal44 = load i32, ptr %sizePtr43, align 4
  %27 = call ptr @__aiml_int_to_string(i32 %sizeVal44)
  %28 = call ptr @__aiml_str_concat(ptr @18, ptr %27)
  %print_null_check45 = icmp eq ptr %28, null
  %print_safe_str46 = select i1 %print_null_check45, ptr @print_null_str.3, ptr %28
  %print_call47 = call i32 (ptr, ...) @printf(ptr @19, ptr %print_safe_str46)
  call void @exit(i32 1)
  br label %if_merge40

if_merge40:                                       ; preds = %if_then39, %if_merge29
  %chain_base48 = load ptr, ptr %got, align 8
  %chain_gep49 = getelementptr inbounds %Deck, ptr %chain_base48, i32 0, i32 3
  %dataPtr50 = getelementptr inbounds %GenericList, ptr %chain_gep49, i32 0, i32 2
  %dataBase51 = load ptr, ptr %dataPtr50, align 8
  %29 = getelementptr inbounds ptr, ptr %dataBase51, i32 0
  %chain_elem52 = load ptr, ptr %29, align 8
  %30 = icmp eq ptr %chain_elem52, null
  %31 = icmp eq ptr %chain_elem52, @20
  %32 = or i1 %30, false
  br i1 %32, label %cmp_check_null53, label %cmp_do_strcmp54

cmp_check_null53:                                 ; preds = %if_merge40
  br label %cmp_merge55

cmp_do_strcmp54:                                  ; preds = %if_merge40
  %33 = call i32 @strcmp(ptr %chain_elem52, ptr @20)
  %34 = icmp eq i32 %33, 0
  br label %cmp_merge55

cmp_merge55:                                      ; preds = %cmp_do_strcmp54, %cmp_check_null53
  %strcmp_res56 = phi i1 [ %31, %cmp_check_null53 ], [ %34, %cmp_do_strcmp54 ]
  %35 = sext i1 %strcmp_res56 to i32
  %36 = icmp eq i32 %35, 0
  br i1 %36, label %if_then57, label %if_merge58

if_then57:                                        ; preds = %cmp_merge55
  %chain_base59 = load ptr, ptr %got, align 8
  %chain_gep60 = getelementptr inbounds %Deck, ptr %chain_base59, i32 0, i32 3
  %dataPtr61 = getelementptr inbounds %GenericList, ptr %chain_gep60, i32 0, i32 2
  %dataBase62 = load ptr, ptr %dataPtr61, align 8
  %37 = getelementptr inbounds ptr, ptr %dataBase62, i32 0
  %chain_elem63 = load ptr, ptr %37, align 8
  %38 = call ptr @__aiml_str_concat(ptr @21, ptr %chain_elem63)
  %print_null_check64 = icmp eq ptr %38, null
  %print_safe_str65 = select i1 %print_null_check64, ptr @print_null_str.4, ptr %38
  %print_call66 = call i32 (ptr, ...) @printf(ptr @22, ptr %print_safe_str65)
  call void @exit(i32 1)
  br label %if_merge58

if_merge58:                                       ; preds = %if_then57, %cmp_merge55
  %chain_base67 = load ptr, ptr %got, align 8
  %chain_gep68 = getelementptr inbounds %Deck, ptr %chain_base67, i32 0, i32 3
  %dataPtr69 = getelementptr inbounds %GenericList, ptr %chain_gep68, i32 0, i32 2
  %dataBase70 = load ptr, ptr %dataPtr69, align 8
  %39 = getelementptr inbounds ptr, ptr %dataBase70, i32 1
  %chain_elem71 = load ptr, ptr %39, align 8
  %40 = icmp eq ptr %chain_elem71, null
  %41 = icmp eq ptr %chain_elem71, @23
  %42 = or i1 %40, false
  br i1 %42, label %cmp_check_null72, label %cmp_do_strcmp73

cmp_check_null72:                                 ; preds = %if_merge58
  br label %cmp_merge74

cmp_do_strcmp73:                                  ; preds = %if_merge58
  %43 = call i32 @strcmp(ptr %chain_elem71, ptr @23)
  %44 = icmp eq i32 %43, 0
  br label %cmp_merge74

cmp_merge74:                                      ; preds = %cmp_do_strcmp73, %cmp_check_null72
  %strcmp_res75 = phi i1 [ %41, %cmp_check_null72 ], [ %44, %cmp_do_strcmp73 ]
  %45 = sext i1 %strcmp_res75 to i32
  %46 = icmp eq i32 %45, 0
  br i1 %46, label %if_then76, label %if_merge77

if_then76:                                        ; preds = %cmp_merge74
  %chain_base78 = load ptr, ptr %got, align 8
  %chain_gep79 = getelementptr inbounds %Deck, ptr %chain_base78, i32 0, i32 3
  %dataPtr80 = getelementptr inbounds %GenericList, ptr %chain_gep79, i32 0, i32 2
  %dataBase81 = load ptr, ptr %dataPtr80, align 8
  %47 = getelementptr inbounds ptr, ptr %dataBase81, i32 1
  %chain_elem82 = load ptr, ptr %47, align 8
  %48 = call ptr @__aiml_str_concat(ptr @24, ptr %chain_elem82)
  %print_null_check83 = icmp eq ptr %48, null
  %print_safe_str84 = select i1 %print_null_check83, ptr @print_null_str.5, ptr %48
  %print_call85 = call i32 (ptr, ...) @printf(ptr @25, ptr %print_safe_str84)
  call void @exit(i32 1)
  br label %if_merge77

if_merge77:                                       ; preds = %if_then76, %cmp_merge74
  %49 = call i32 @aiml_db_checkpoint_all()
  %print_call86 = call i32 (ptr, ...) @printf(ptr @27, ptr @26)
  ret i32 0

ret_dead:                                         ; No predecessors!
  ret i32 0
}

define void @__aiml_init() {
entry:
  ret void
}

declare i32 @__gxx_personality_v0(...)

declare ptr @aiml_db_kv_open_typed_persist(ptr, i64, ptr, ptr, i32)

declare ptr @memcpy(ptr, ptr, i64)

declare void @aiml_db_kv_set(ptr, ptr, ptr, i64)

; Function Attrs: noinline
define internal ptr @__aiml_deser_Deck(ptr %json) #0 {
entry:
  %r = call ptr @aiml_jr_parse(ptr %json)
  %hp = call ptr @malloc(i64 56)
  call void @llvm.memset.p0.i64(ptr align 8 %hp, i8 0, i64 56, i1 false)
  %fp_name = getelementptr inbounds %Deck, ptr %hp, i32 0, i32 0
  %sv = call ptr @aiml_jr_str(ptr %r, ptr @fk_name)
  store ptr %sv, ptr %fp_name, align 8
  %fp_deck_type = getelementptr inbounds %Deck, ptr %hp, i32 0, i32 1
  %sv1 = call ptr @aiml_jr_str(ptr %r, ptr @fk_deck_type)
  store ptr %sv1, ptr %fp_deck_type, align 8
  %fp_description = getelementptr inbounds %Deck, ptr %hp, i32 0, i32 2
  %sv2 = call ptr @aiml_jr_str(ptr %r, ptr @fk_description)
  store ptr %sv2, ptr %fp_description, align 8
  %fp_card_ids = getelementptr inbounds %Deck, ptr %hp, i32 0, i32 3
  %sizePtr = getelementptr inbounds %GenericList, ptr %fp_card_ids, i32 0, i32 0
  store i32 0, ptr %sizePtr, align 4
  %capPtr = getelementptr inbounds %GenericList, ptr %fp_card_ids, i32 0, i32 1
  store i32 4, ptr %capPtr, align 4
  %0 = call ptr @malloc(i64 mul (i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), i64 4))
  %dataPtr = getelementptr inbounds %GenericList, ptr %fp_card_ids, i32 0, i32 2
  store ptr %0, ptr %dataPtr, align 8
  %cnt = call i32 @aiml_jr_arr_len(ptr %r, ptr @fk_card_ids)
  br label %card_ids_dlc

card_ids_dlc:                                     ; preds = %cont, %entry
  %dli = phi i32 [ 0, %entry ], [ %ni, %cont ]
  %1 = icmp slt i32 %dli, %cnt
  br i1 %1, label %card_ids_dlb, label %card_ids_dle

card_ids_dlb:                                     ; preds = %card_ids_dlc
  %ae = call ptr @aiml_jr_arr_str(ptr %r, ptr @fk_card_ids, i32 %dli)
  %sizePtr3 = getelementptr inbounds %GenericList, ptr %fp_card_ids, i32 0, i32 0
  %capPtr4 = getelementptr inbounds %GenericList, ptr %fp_card_ids, i32 0, i32 1
  %size = load i32, ptr %sizePtr3, align 4
  %capacity = load i32, ptr %capPtr4, align 4
  %2 = icmp eq i32 %size, %capacity
  br i1 %2, label %grow, label %cont

card_ids_dle:                                     ; preds = %card_ids_dlc
  %fp_total_mkp = getelementptr inbounds %Deck, ptr %hp, i32 0, i32 4
  %sv7 = call i32 @aiml_jr_i32(ptr %r, ptr @fk_total_mkp)
  store i32 %sv7, ptr %fp_total_mkp, align 4
  %fp_created_at = getelementptr inbounds %Deck, ptr %hp, i32 0, i32 5
  %sv8 = call i64 @aiml_jr_i64(ptr %r, ptr @fk_created_at)
  store i64 %sv8, ptr %fp_created_at, align 8
  call void @aiml_jr_free(ptr %r)
  ret ptr %hp

grow:                                             ; preds = %card_ids_dlb
  %3 = mul i32 %capacity, 2
  %4 = icmp sgt i32 %3, 0
  %5 = select i1 %4, i32 %3, i32 4
  store i32 %5, ptr %capPtr4, align 4
  %dataPtr5 = getelementptr inbounds %GenericList, ptr %fp_card_ids, i32 0, i32 2
  %6 = load ptr, ptr %dataPtr5, align 8
  %7 = zext i32 %5 to i64
  %8 = mul i64 ptrtoint (ptr getelementptr inbounds (ptr, ptr null, i32 1) to i64), %7
  %9 = call ptr @realloc(ptr %6, i64 %8)
  store ptr %9, ptr %dataPtr5, align 8
  br label %cont

cont:                                             ; preds = %grow, %card_ids_dlb
  %dataPtr6 = getelementptr inbounds %GenericList, ptr %fp_card_ids, i32 0, i32 2
  %dataBase = load ptr, ptr %dataPtr6, align 8
  %10 = getelementptr inbounds ptr, ptr %dataBase, i32 %size
  store ptr %ae, ptr %10, align 8
  %11 = add i32 %size, 1
  store i32 %11, ptr %sizePtr3, align 4
  %ni = add i32 %dli, 1
  br label %card_ids_dlc
}

declare ptr @aiml_jr_parse(ptr)

declare ptr @malloc(i64)

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr nocapture writeonly, i8, i64, i1 immarg) #1

declare ptr @aiml_jr_str(ptr, ptr)

declare i32 @aiml_jr_arr_len(ptr, ptr)

declare ptr @aiml_jr_arr_str(ptr, ptr, i32)

declare ptr @realloc(ptr, i64)

declare i32 @aiml_jr_i32(ptr, ptr)

declare i64 @aiml_jr_i64(ptr, ptr)

declare void @aiml_jr_free(ptr)

declare i32 @aiml_db_kv_has(ptr, ptr)

declare ptr @aiml_db_kv_get(ptr, ptr)

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

declare i32 @strcmp(ptr, ptr)

declare i32 @aiml_db_checkpoint_all()

attributes #0 = { noinline }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: write) }
