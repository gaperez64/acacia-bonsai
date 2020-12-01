# This file is part of Acacia+, a tool for synthesis of reactive systems using antichain-based techniques
# Copyright (C) 2011-2013 UMONS-ULB
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

from ctypes import *
import os

from constants import *

#### STRUCTURES ####
#### GList C structure
class GList(Structure):
    pass

GList._fields_ = [("data", c_void_p),
                ("next", POINTER(GList)),
                ("pred", POINTER(GList))]
    
#### GNode C structure
class GNode(Structure):
    pass

GNode._fields_ = [("data", POINTER(c_void_p)),
                  ("next", POINTER(GNode)),
                  ("pred", POINTER(GNode)),
                  ("parent", POINTER(GNode)),
                  ("children", POINTER(GNode))]

#### AlphabetInfo C structure
class AlphabetInfo(Structure):
    _fields_ = [("input_size", c_int),
                ("output_size", c_int),
                ("input", POINTER(c_char_p)),
                ("output", POINTER(c_char_p)),
                ("sigma_input_size", c_int),
                ("sigma_output_size", c_int),
                ("sigma_input", POINTER(POINTER(c_ubyte))),
                ("sigma_output", POINTER(POINTER(c_ubyte)))]
    
#### Label C structure
class Label(Structure):
    _fields_ = [("disjunction_size", c_int),
                ("disjunction", POINTER(POINTER(c_ubyte)))]

#### TBUCW_tran C structure
class TBUCW_tran(Structure):
    pass

#### TBUCW_state C structure
class TBUCW_state(Structure):
    _fields_ = [("state_label", c_int),
                ("nb_in_tran", c_int),
                ("nb_out_tran", c_int),
                ("in_tran", POINTER(POINTER(TBUCW_tran))),
                ("out_tran", POINTER(POINTER(TBUCW_tran))),
                ("is_accepting", c_byte),
                ("player", c_byte),
                ("unbounded", c_byte),
                ("is_complete", c_byte),
                ("is_trash", c_byte)]

TBUCW_tran._fields_ = [("state_from", POINTER(TBUCW_state)),
                       ("state_to", POINTER(TBUCW_state)),
                       ("label", POINTER(Label))]

#### TBUCW C structure
class TBUCW(Structure):
      _fields_ = [("nb_states", c_int),
                  ("initial_state_index", c_int),
                  ("alphabet", POINTER(AlphabetInfo)),
                  ("v_I", POINTER(POINTER(c_int))),
                  ("v_O", POINTER(POINTER(c_int))),
                  ("dimension", c_int),
                  ("states", POINTER(POINTER(TBUCW_state)))]    
    
#### Antichain C structure      
class Antichain(Structure):
    _fields_ = [("size", c_int),
                ("incomparable_elements", POINTER(GList))]
      
#### SafetyGame C structure
class SafetyGame(Structure):
    _fields_ = [("positions_O", POINTER(Antichain)),
                ("positions_I", POINTER(Antichain)),
                ("first_to_play", c_byte)]

#### CFInfo C structure
class CFInfo(Structure):
    _fields_ = [("starting_player", c_byte),
                ("composition_size", c_int),
                ("cf_range_size_sum", c_int),
                ("k_value", POINTER(c_int)),
                ("nb_lost_bits", POINTER(c_int)),
                ("nb_states_by_integer", POINTER(c_int)),
                ("cf_range_size", POINTER(c_int)),
                ("end_index_starting_p", POINTER(c_int)),
                ("start_index_other_p", POINTER(c_int)),
                ("first_state_other_p_index", POINTER(c_int)),
                ("automaton", POINTER(POINTER(TBUCW)))]

#### CountingFunction C structure
class CountingFunction(Structure):
    _fields_ = [("player", c_byte),
                ("sum_of_counters", c_int),
                ("max_counter", c_int),
                ("mapping", POINTER(c_ubyte)),
                ("info", POINTER(GNode))]
    
#### Vector C structure    
class Vector(Structure):
    _fields_ = [("dimension", c_int),
                ("max_value", POINTER(c_int)),
                ("values", POINTER(c_int))]

#### Tuple C structure
class Tuple(Structure):
    _fields_ = [("cf", POINTER(CountingFunction)),
                ("credits", POINTER(Vector))]

#### OtfurResult C structure    
class OtfurResult(Structure):
    _fields_ = [("winning_positions", POINTER(SafetyGame)),
                 ("otfur_time", c_float),
                 ("winning_positions_computation_time", c_float),
                 ("nb_cf_passed", c_int),
                 ("nb_iter", c_int)]

#### TSTransition C structure    
class TSTransition(Structure):
    _fields_ = [("from", c_int),
                ("to", c_int),
                ("label", POINTER(c_char))]

#### TSState C structure    
class TSState(Structure):
    _fields_ = [("player", c_byte),
                ("nb_tr", c_int),
                ("transitions", POINTER(GList))] 
    
#### TransitionSystem C structure
class TransitionSystem(Structure):
    _fields_ = [("nb_states_PO", c_int),
                ("nb_states_PI", c_int),
                ("size_states_PO", c_int),
                ("size_states_PI", c_int),
                ("nb_initial_states", c_int),
                ("initial_states", POINTER(c_int)),
                ("states", POINTER(POINTER(TSState)))]


#### FUNCTIONS LOADING ####
if os.uname()[0] == "Darwin":
    ext = 'dylib'
elif os.uname()[0] == "Linux":
    ext = 'so'
else:
    print("OS not supported")
    exit(0)

libpath = os.getenv ('ACACIA_PLUS_LIB',
                     MAIN_DIR_PATH + '/lib/acacia_plus.' + ext)

lib = cdll.LoadLibrary(libpath)

##TBUCW
init_tbucw_c = lib.init_tbucw
init_tbucw_c.argtypes = [c_int]
init_tbucw_c.restype = POINTER(TBUCW)

add_state_c = lib.add_state
add_state_c.argtypes = [POINTER(TBUCW), c_int, c_int, c_int, c_byte, c_byte, c_byte, c_byte]
add_state_c.restype = None

add_tran_c = lib.add_tran
add_tran_c.argtypes = [POINTER(TBUCW), c_char_p, c_int, c_int, c_int]
add_tran_c.restype = None

set_initial_state_c = lib.set_initial_state
set_initial_state_c.argtypes = [POINTER(TBUCW), c_int]
set_initial_state_c.restype = None

set_is_accepting_c = lib.set_is_accepting
set_is_accepting_c.argtypes = [POINTER(TBUCW), c_int, c_byte]
set_is_accepting_c.restype = None

set_alphabet_c = lib.set_alphabet
set_alphabet_c.argtypes = [POINTER(TBUCW), POINTER(AlphabetInfo)]
set_alphabet_c.restype = POINTER(TBUCW)

report_accepting_states_c = lib.report_accepting_states
report_accepting_states_c.argtypes = [POINTER(TBUCW)]
report_accepting_states_c.restype = None

duplicate_all_tran_c = lib.duplicate_all_tran
duplicate_all_tran_c.argtypes = [POINTER(TBUCW)]
duplicate_all_tran_c.restype = None

set_is_complete_c = lib.set_is_complete
set_is_complete_c.argtypes = [POINTER(TBUCW)]
set_is_complete_c.restype = None

is_accepting_c = lib.is_accepting
is_accepting_c.argtypes = [POINTER(TBUCW), c_int]
is_accepting_c.restype = c_byte

is_complete_c = lib.is_complete
is_complete_c.argtypes = [POINTER(TBUCW), c_int]
is_complete_c.restype = c_byte

get_player_id_c = lib.get_player_id
get_player_id_c.argtypes = [POINTER(TBUCW), c_int]
get_player_id_c.restype = c_byte

get_formula_c = lib.get_formula
get_formula_c.argtypes = [POINTER(AlphabetInfo), c_byte, c_int]
get_formula_c.restype = c_char_p

get_tbucw_size_c = lib.get_tbucw_size
get_tbucw_size_c.argtypes = [POINTER(TBUCW)]
get_tbucw_size_c.restype = c_int

init_alphabet_c = lib.init_alphabet
init_alphabet_c.argtypes = [c_int, c_int]
init_alphabet_c.restype = POINTER(AlphabetInfo)

add_input_prop_c = lib.add_input_prop
add_input_prop_c.argtypes = [POINTER(AlphabetInfo), c_char_p]
add_input_prop_c.restype = None

add_output_prop_c = lib.add_output_prop
add_output_prop_c.argtypes = [POINTER(AlphabetInfo), c_char_p]
add_output_prop_c.restype = None

compute_alphabets_c = lib.compute_alphabets
compute_alphabets_c.argtypes = [POINTER(AlphabetInfo)]
compute_alphabets_c.restype = None

get_succ_from_sigma_index_c = lib.get_succ_from_sigma_index
get_succ_from_sigma_index_c.argtypes = [POINTER(TBUCW), c_int, c_int]
get_succ_from_sigma_index_c.restype = POINTER(c_int)

get_all_succ_c = lib.get_all_succ
get_all_succ_c.argtypes = [POINTER(TBUCW), c_int]
get_all_succ_c.restype = POINTER(c_int)

print_tbucw_c = lib.print_tbucw
print_tbucw_c.argtypes = [POINTER(TBUCW)]
print_tbucw_c.restype = None

print_tbucw_stats_c = lib.print_tbucw_stats
print_tbucw_stats_c.argtypes = [POINTER(TBUCW)]
print_tbucw_stats_c.restype = None

print_formula_c = lib.print_formula
print_formula_c.argtypes = [POINTER(TBUCW), POINTER(c_ubyte), c_int, POINTER(c_char_p)]
print_formula_c.restype = None

free_tbucw_c = lib.free_tbucw
free_tbucw_c.argtypes = [POINTER(TBUCW)]
free_tbucw_c.restype = None

optimize_tbucw_c = lib.optimize_tbucw
optimize_tbucw_c.argtypes = [POINTER(TBUCW), POINTER(c_byte)]
optimize_tbucw_c.restype = POINTER(TBUCW)

reset_tbucw_states_labels_c = lib.reset_tbucw_states_labels
reset_tbucw_states_labels_c.argtypes = [POINTER(TBUCW)]
reset_tbucw_states_labels_c.restype = None

set_weight_function_c = lib.set_weight_function
set_weight_function_c.argtypes = [POINTER(TBUCW), c_byte, POINTER(POINTER(c_int))]
set_weight_function_c.restype = POINTER(TBUCW)

set_dimension_c = lib.set_dimension
set_dimension_c.argtypes = [POINTER(TBUCW), c_int]
set_dimension_c.restype = POINTER(TBUCW)

##GList
is_link_null_c = lib.is_link_null
is_link_null_c.argtypes = [POINTER(GList)]
is_link_null_c.restype = c_byte

get_link_data_c = lib.get_link_data
get_link_data_c.argtypes = [POINTER(GList)]
get_link_data_c.restype = POINTER(Tuple)

## CountingFunction
build_cf_info_c = lib.build_cf_info
build_cf_info_c.argtypes = [POINTER(TBUCW), c_int] 
build_cf_info_c.restype = POINTER(GNode)

compose_cf_info_c = lib.compose_cf_info
compose_cf_info_c.argtypes = [POINTER(POINTER(GNode)), c_int]
compose_cf_info_c.restype = POINTER(GNode)

##Tuple
build_initial_tuple_c = lib.build_initial_tuple
build_initial_tuple_c.argtypes = [POINTER(GNode), c_int, POINTER(c_int)]
build_initial_tuple_c.restype = POINTER(Tuple)

set_not_defined_tuple_c = lib.set_not_defined_tuple
set_not_defined_tuple_c.argtypes = None
set_not_defined_tuple_c.restype = None

compare_tuples_c = lib.compare_tuples
compare_tuples_c.argtypes = [POINTER(Tuple), POINTER(Tuple)]
compare_tuples_c.restype = c_byte

tuple_succ_c = lib.tuple_succ
tuple_succ_c.argtypes = [POINTER(Tuple), c_int, POINTER(AlphabetInfo)]
tuple_succ_c.restype = POINTER(Tuple)

clone_tuple_c = lib.clone_tuple
clone_tuple_c.argtypes = [POINTER(Tuple)]
clone_tuple_c.restype = c_void_p

compose_tuples_c = lib.compose_tuples
compose_tuples_c.argtypes = [POINTER(POINTER(Tuple)), c_int, POINTER(GNode)]
compose_tuples_c.restype = c_void_p

print_tuple_c = lib.print_tuple
print_tuple_c.argtypes = [POINTER(Tuple)]
print_tuple_c.restype = None

free_tuple_full_c = lib.free_tuple_full
free_tuple_full_c.argtypes = [POINTER(Tuple)]
free_tuple_full_c.restype = None

free_not_defined_tuple_c = lib.free_not_defined_tuple
free_not_defined_tuple_c.argtypes = None
free_not_defined_tuple_c.restype = None

##Antichain
PRINT_ELEMENT_FUNC = CFUNCTYPE(None, c_void_p)
PRINT_TUPLE_FUNC = CFUNCTYPE(None, POINTER(Tuple))
COMPARE_TUPLES_FUNC = CFUNCTYPE(c_byte, POINTER(Tuple), POINTER(Tuple))
FREE_TUPLE_FULL_FUNC = CFUNCTYPE(None, POINTER(Tuple))
CLONE_TUPLE_FUNC = CFUNCTYPE(c_void_p, POINTER(Tuple))
COMPOSE_TUPLES_FUNC = CFUNCTYPE(c_void_p, POINTER(POINTER(Tuple)), c_int, POINTER(GNode))

compare_antichains_c = lib.compare_antichains
compare_antichains_c.argtypes = [POINTER(Antichain), POINTER(Antichain), COMPARE_TUPLES_FUNC]
compare_antichains_c.restype = c_byte

contains_element_c = lib.contains_element
contains_element_c.argtypes = [POINTER(Antichain), c_void_p, COMPARE_TUPLES_FUNC]
contains_element_c.restype = c_byte

compose_antichains_c = lib.compose_antichains
compose_antichains_c.argtypes = [POINTER(POINTER(Antichain)), c_int, COMPOSE_TUPLES_FUNC, POINTER(GNode)]
compose_antichains_c.restype = POINTER(Antichain)

clone_antichain_c = lib.clone_antichain
clone_antichain_c.argtypes = [POINTER(Antichain), CLONE_TUPLE_FUNC]
clone_antichain_c.restype = POINTER(Antichain)

free_antichain_full_c = lib.free_antichain_full
free_antichain_full_c.argtypes = [POINTER(Antichain), FREE_TUPLE_FULL_FUNC]
free_antichain_full_c.restype = None

print_antichain_c = lib.print_antichain
print_antichain_c.argtypes = [POINTER(Antichain), PRINT_TUPLE_FUNC]
print_antichain_c.restype = None

##BackwardAlgorithm
build_start_antichain_c = lib.build_start_antichain
build_start_antichain_c.argtypes = [c_byte, POINTER(GNode)]
build_start_antichain_c.restype = POINTER(Antichain)

pre_c = lib.pre
pre_c.argtypes = [POINTER(Antichain), POINTER(Antichain), c_byte, POINTER(AlphabetInfo)]
pre_c.restype = POINTER(Antichain)

pre_crit_c = lib.pre_crit
pre_crit_c.argtypes = [POINTER(Antichain), POINTER(Antichain), POINTER(c_int), POINTER(AlphabetInfo)]
pre_crit_c.restype = POINTER(Antichain)

compute_critical_set_c = lib.compute_critical_set
compute_critical_set_c.argtypes = [POINTER(Antichain), POINTER(AlphabetInfo)]
compute_critical_set_c.restype = POINTER(c_int)

##SafetyGame
new_safety_game_c = lib.new_safety_game
new_safety_game_c.argtypes = [POINTER(Antichain), POINTER(Antichain), c_byte]
new_safety_game_c.restype = POINTER(SafetyGame)

add_credits_to_safety_game_c = lib.add_credits_to_safety_game
add_credits_to_safety_game_c.argtypes = [POINTER(SafetyGame), c_int, POINTER(c_int)]
add_credits_to_safety_game_c.restype = POINTER(SafetyGame)

free_safety_game_c = lib.free_safety_game
free_safety_game_c.argtypes = [POINTER(SafetyGame)]
free_safety_game_c.restype = None

##Cache
initialize_cache_c = lib.initialize_cache
initialize_cache_c.argtypes = None
initialize_cache_c.restype = None

initialize_cache_critical_set_c = lib.initialize_cache_critical_set
initialize_cache_critical_set_c.argtypes = None
initialize_cache_critical_set_c.restype = None

clean_cache_c = lib.clean_cache
clean_cache_c.argtypes = None
clean_cache_c.restype = None

clean_cache_critical_set_c = lib.clean_cache_critical_set
clean_cache_critical_set_c.argtypes = None
clean_cache_critical_set_c.restype = None

##ForwardAlgorithm
otfur_c = lib.otfur
otfur_c.argtypes = [POINTER(Antichain), POINTER(Antichain), POINTER(GNode), POINTER(AlphabetInfo), c_byte, c_int, POINTER(c_int)]
otfur_c.restype = POINTER(OtfurResult)

##Synthesis
extract_strategies_from_safety_game_c = lib.extract_strategies_from_safety_game
extract_strategies_from_safety_game_c.argtypes = [POINTER(SafetyGame), POINTER(AlphabetInfo), c_byte, c_byte, c_byte] 
extract_strategies_from_safety_game_c.restype = POINTER(TransitionSystem)

has_a_winning_strategy_c = lib.has_a_winning_strategy
has_a_winning_strategy_c.argtypes = [POINTER(SafetyGame), POINTER(AlphabetInfo), c_byte] 
has_a_winning_strategy_c.restype = c_byte

##TransitionSystem
get_ts_state_c = lib.get_ts_state
get_ts_state_c.argtypes = [POINTER(TransitionSystem), c_int]
get_ts_state_c.restype = POINTER(TSState)

get_ts_transition_from_link_data_c = lib.get_ts_transition_from_link
get_ts_transition_from_link_data_c.argtypes = [POINTER(GList)]
get_ts_transition_from_link_data_c.restype = POINTER(TSTransition)

is_ts_state_null_c = lib.is_ts_state_null
is_ts_state_null_c.argtypes = [POINTER(TSState)]
is_ts_state_null_c.restype = c_byte

free_transition_system_c = lib.free_transition_system
free_transition_system_c.argtypes = [POINTER(TransitionSystem)]
free_transition_system_c.restype = None

#MemoryManagement
free_c = lib.free_memory
free_c.argtypes = [c_void_p]
free_c.restype = None
