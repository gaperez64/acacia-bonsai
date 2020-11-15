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

from library_linker import *
from utils import *

#### Calls the a variant of the OTFUR algorithm implemented in C
def otfur(start_antichain1, start_antichain2, cf_info, alphabet, starting_player, dimension, c_value, options):
    (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, unrea, set_of_strategies, path, filename) = options
    controled_print("Forward algorithm (OTFUR):\n", [ALLTEXT, MINTEXT], verbosity)
    if starting_player == P_O:
        result = otfur_c(start_antichain1, start_antichain2, cf_info, alphabet, starting_player, dimension, c_value)
    else:
        result = otfur_c(start_antichain2, start_antichain1, cf_info, alphabet, starting_player, dimension, c_value)
    
    free_antichain_full_c(start_antichain1, FREE_TUPLE_FULL_FUNC(free_tuple_full_c))
    free_antichain_full_c(start_antichain2, FREE_TUPLE_FULL_FUNC(free_tuple_full_c))
    
    controled_print("Nb of iterations: " + str(result.contents.nb_iter) + "\n", [ALLTEXT, MINTEXT], verbosity)
    controled_print("Nb of states explored: " + str(result.contents.nb_cf_passed) + "\n", [ALLTEXT, MINTEXT], verbosity)
    controled_print("OTFUR time: " + str(round(result.contents.otfur_time, 2)) + "\n", [ALLTEXT, MINTEXT], verbosity)
    controled_print("Winning positions extraction time: " + str(round(result.contents.winning_positions_computation_time, 2)) + "\n", [ALLTEXT, MINTEXT], verbosity)
    controled_print("Size of antichain of winning positions (System, Environment): (" + str(result.contents.winning_positions.contents.positions_O.contents.size) + ", " + str(result.contents.winning_positions.contents.positions_I.contents.size) + ")\n", [ALLTEXT, MINTEXT], verbosity)
    controled_print("Elapsed time: " + str(round(result.contents.otfur_time + result.contents.winning_positions_computation_time, 2)) + "\n\n", [ALLTEXT, MINTEXT], verbosity)
    
    return result.contents.winning_positions
            