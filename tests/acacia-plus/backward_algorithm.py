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

import time

from constants import *
from library_linker import *
from synthesis import *
from utils import *

#### Computes the fix point from start_antichain_1 which belongs to starting_player (calls a dylib C)
def compute_fix_point(start_antichain_1, start_antichain_2, alphabet, starting_player, critical, verbosity):
    start_time = time.clock()
    
    # Initialize cache memory (C)
    initialize_cache_c()
    if critical == ON:
        initialize_cache_critical_set_c() # initialize cache used by compute_critical_set C function
    set_not_defined_tuple_c()

    fp_comp_antichain = []
    fp_comp_antichain.append(start_antichain_2)
    fp_comp_antichain.append(start_antichain_1)
    
    # Compute the fix point from the starting antichain by applying Pre_O and Pre_I in the right order depending on the starting player
    a1 = start_antichain_1
    
    controled_print("Backward algorithm:\n", [ALLTEXT, MINTEXT], verbosity)
    if starting_player == P_O:
        controled_print('Antichain size (System): %d\n' % a1.contents.size, [ALLTEXT], verbosity)
    else:
        controled_print('Antichain size (Environment): %d\n' % a1.contents.size, [ALLTEXT], verbosity)
    if verbosity == ALLTEXT:
        print_antichain_c(a1, PRINT_TUPLE_FUNC(print_tuple_c))
        
    (a2, size_crit_set) = compute_pre(a1, start_antichain_2, alphabet, critical, switch_player(starting_player), verbosity)
    fp_comp_antichain.append(a2)
    size_max_crit_set = size_crit_set
    (a3, size_crit_set) = compute_pre(a2, a1, alphabet, critical, starting_player, verbosity)
    size_max_crit_set = max(size_max_crit_set, size_crit_set)
    fp_comp_antichain.append(a3)
    
    nb_iter = 0
    size_max_starting_player = max(a1.contents.size, a3.contents.size)
    size_max_other_player = a2.contents.size
    
    # Iterate while the fix point isn't reached
    while not compare_antichains_c(a1, a3, COMPARE_TUPLES_FUNC(compare_tuples_c)):
        nb_iter += 1
 
        a1 = a3
        (a2, size_crit_set) = compute_pre(a1, a2, alphabet, critical, switch_player(starting_player), verbosity)
        fp_comp_antichain.append(a2)

        size_max_crit_set = max(size_max_crit_set, size_crit_set)

        if starting_player == P_O and size_crit_set == 0: # if there were no critical signals, we don't need to recompute Pre_O(a2) as a2 hasn't changed since last iteration so the fix point is reached
            break
        
        (a3, size_crit_set) = compute_pre(a2, a1, alphabet, critical, starting_player, verbosity)
        fp_comp_antichain.append(a3)
        size_max_crit_set = max(size_max_crit_set, size_crit_set)

        size_max_other_player = max(size_max_other_player, a2.contents.size)
        size_max_starting_player = max(size_max_starting_player, a3.contents.size)
        
    controled_print("Nb of iterations: %d\n" % nb_iter, [ALLTEXT, MINTEXT], verbosity)
    controled_print("Maximum size of critical set: %d\n" % size_max_crit_set, [ALLTEXT, MINTEXT], verbosity)
    controled_print("Maximum sizes of antichains (Starting player, Other player): (%d, %d)\n" % (size_max_starting_player, size_max_other_player), [ALLTEXT, MINTEXT], verbosity)
    controled_print("Nb elements in fix point (Starting player, Other player): (%d, %d)\n" % (a1.contents.size, a2.contents.size), [ALLTEXT, MINTEXT], verbosity)
    controled_print("Elapsed time: " + str(time.clock()-start_time) + "\n\n", [ALLTEXT, MINTEXT], verbosity)
    
    a1_sg = clone_antichain_c(a1, CLONE_TUPLE_FUNC(clone_tuple_c))
    a2_sg = clone_antichain_c(a2, CLONE_TUPLE_FUNC(clone_tuple_c))

    # Build the safety game (structure containing the two antichains (one for each player))
    if starting_player == P_O:
        sg = new_safety_game_c(a2_sg, a1_sg, starting_player)
    else:
        sg = new_safety_game_c(a1_sg, a2_sg, starting_player)

    # Free memory
    clean_cache_c()
    if critical == ON:
        clean_cache_critical_set_c()
    free_not_defined_tuple_c()
    for a in fp_comp_antichain:
        free_antichain_full_c(a, FREE_TUPLE_FULL_FUNC(free_tuple_full_c))

    return sg
    
#### Applies the Pre function
#### Implements the critical signals optimization   
#### antichain is the antichain on which we are computing the Pre_I (player == P_I) or Pre_O (player == P_O) function
#### prev_antichain is the last computed antichain for player
def compute_pre(antichain, prev_antichain, alphabet, critical, player, verbosity):
    nb_critical_signals = -1
    if critical == ON and player == P_I: # if optimization is enabled and we are computing Pre_I
        critical_signals_index = compute_critical_set_c(antichain, alphabet) # compute the critical signals set
        nb_critical_signals = critical_signals_index[0] # the first element of the array is the number of critical signals
        pre = pre_crit_c(antichain, prev_antichain, critical_signals_index, alphabet)
        free_c(critical_signals_index)
        
        controled_print("Nb critical signals: %d\n" % nb_critical_signals, [ALLTEXT], verbosity)
    else: # optimization is disabled or we are computing Pre_O
        pre = pre_c(antichain, prev_antichain, player, alphabet)

    if player == P_I:
        controled_print('Antichain size (Environment): %d\n' % pre.contents.size, [ALLTEXT], verbosity)
    else:
        controled_print('Antichain size (System): %d\n' % pre.contents.size, [ALLTEXT], verbosity)
    if verbosity == ALLTEXT:
        print_antichain_c(pre, PRINT_TUPLE_FUNC(print_tuple_c))

    return (pre, nb_critical_signals)