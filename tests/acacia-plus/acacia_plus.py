# This file is part of Acacia+, a tool for synthesis of reactive systems
# using antichain-based techniques
# Copyright (C) 2011-2016 UMONS-ULB
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

import optparse
import sys
import re
import subprocess
import uuid
import time
import string
import math

from pygraph.classes.digraph import digraph

from automaton import *
from tbucw import *
from backward_algorithm import *
from forward_algorithm import *
from synthesis import *
from library_linker import *
from constants import *
from utils import *
    
#### Solves the synthesis problem for formula and partition when player makes the first move, under a set of options        
def synthetize(ltl_file, partition_file, player, options):
    (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, tocheck, set_of_winning_strategies, path, filename) = options
    unique_id = uuid.uuid4().hex
    
    start_time = os.times()[4]
    
    # Partition parsing and values extraction
    (inputs, outputs, values_I, values_O, values_not_I, values_not_O, nu, dimension, c_start, c_bound, c_step) = parse_partition(partition_file)
    for i in range(dimension):
        if c_step[i] == 0 and (c_bound[i]-c_start[i]) != 0:
            if (k_bound-k_start) != 0:
                c_step[i] = ((int)(math.ceil((c_bound[i]-c_start[i])*1./math.ceil(((k_bound-k_start)*1.)/k_step))))
            else:
                c_step[i] = c_bound[i]
    mp_parameters = (inputs, outputs, dimension, values_I, values_O, values_not_I, values_not_O, nu, c_start, c_bound, c_step)
    
    if tocheck in [UNREAL, BOTH] and dimension > 0: # No unrealizability testing with costs -> removing costs (dimension 0)
        print("Warning: mean-payoff objectives turned off since unrealizability checking is on")
        dimension = 0
        c_start = [0]
        c_bound = [0]
        c_step = [0]
        mp_parameters = (inputs, outputs, dimension, values_I, values_O, values_not_I, values_not_O, nu, c_start, c_bound, c_step)
        
    controled_print('Input signals: ' + str(inputs) + '\n', [ALLTEXT, MINTEXT], verbosity)
    controled_print('Output signals: ' + str(outputs) + '\n\n', [ALLTEXT, MINTEXT], verbosity)
    if dimension > 0:
        controled_print('Mean-payoff objective parameters:\n', [ALLTEXT, MINTEXT], verbosity)
        controled_print('    Dimension: ' + str(dimension) + "\n", [ALLTEXT, MINTEXT], verbosity)
        controled_print('    Input signals values: ' + str(values_I) + "\n", [ALLTEXT, MINTEXT], verbosity)
        controled_print('    Input signals negation values: ' + str(values_not_I) + "\n", [ALLTEXT, MINTEXT], verbosity)
        controled_print('    Output signals values: ' + str(values_O) + "\n", [ALLTEXT, MINTEXT], verbosity)
        controled_print('    Output signals negation values: ' + str(values_not_O) + "\n", [ALLTEXT, MINTEXT], verbosity)
        controled_print("    nu = " + fractions_list_to_string(nu) +"\n", [ALLTEXT, MINTEXT], verbosity)
        controled_print("    Minimum credit vector considered: " + str(c_start) +"\n", [ALLTEXT, MINTEXT], verbosity)
        controled_print("    Maximum credit vector considered: " + str(c_bound) +"\n", [ALLTEXT, MINTEXT], verbosity)
        controled_print("    Incremental step for credit vector: " + str(c_step) +"\n\n", [ALLTEXT, MINTEXT], verbosity)
    
    # Formula parsing
    (spec_names, formulas, group_order) = read_formula(ltl_file, nbw_constr)
    nb_spec = len(formulas)

    if tool == WRING:
        subprocess.Popen(['mkdir', TMP_PATH])

    if tocheck in [REAL, BOTH]: # realizability checking
        controled_print('Specifications for realizability checking:\n', [ALLTEXT, MINTEXT], verbosity)
        for i in range(len(formulas)):
            controled_print('  Spec ' + spec_names[i] + ':\n' + formulas[i] + '\n', [ALLTEXT, MINTEXT], verbosity)
        if chk_method == COMP:
            controled_print('Parenthesizing: ' + group_order + '\n', [ALLTEXT, MINTEXT], verbosity)
        controled_print('\n', [ALLTEXT, MINTEXT], verbosity)
         
    if tocheck in [UNREAL, BOTH]: # unrealizability checking
        player_unreal = switch_player(player)
        controled_print('Specifications for unrealizability checking:\n', [ALLTEXT, MINTEXT], verbosity)
        (spec_names_unreal, formulas_unreal, group_order_unreal) = (["!"+spec_names[0]], [negate_formula(formulas[0])], FLAT)
        controled_print('  Spec ' + spec_names_unreal[0] + ':\n' + formulas_unreal[0] + '\n', [ALLTEXT, MINTEXT], verbosity)
        controled_print('\n', [ALLTEXT, MINTEXT], verbosity)
        partition_unreal = ""
        if tool == WRING:
            partition_unreal = inverse_partition(inputs, outputs)
        mp_parameters_unreal = (outputs, inputs, dimension, values_O, values_I, values_not_O, values_not_I, nu, c_start, c_bound, c_step)
    
    if tocheck == REAL:
        controled_print('########### Realizability checking and synthesis ###############\n', [ALLTEXT, MINTEXT], verbosity)
    elif tocheck == UNREAL:
        controled_print('########### Unrealizability checking and synthesis (without MP objectives) ###############\n', [ALLTEXT, MINTEXT], verbosity)
    else:
        controled_print('########### Realizability and unrealizability checking and synthesis (without MP objectives) ###############\n', [ALLTEXT, MINTEXT], verbosity)
        
    if tool in [LTL2BA, LTL3BA, SPOT]: # Convert to LTL2/3BA, SPOT and negates formulas
        if tocheck in [REAL, BOTH]: # realizability checking
            for i in range(len(formulas)):
                formulas[i] = "!("+wring_to_ltl2ba(formulas[i], inputs, outputs)+")"
        if tocheck in [UNREAL, BOTH]: # unrealizability checking
            formulas_unreal[0] = "!("+wring_to_ltl2ba(formulas_unreal[0], outputs, inputs)+")"
    else: # Convert to wring
        if tocheck in [REAL, BOTH]: # realizability checking
            for i in range(len(formulas)):
                formulas[i] = ltl2ba_to_wring(formulas[i], inputs, outputs)
        if tocheck in [UNREAL, BOTH]: # unrealizability checking
            formulas_unreal[0] = ltl2ba_to_wring(formulas_unreal[0], outputs, inputs)

    # Automata construction
    if tocheck in [REAL, BOTH]: # realizability checking
        (tbucw_c_list, alphabet) = automata_construction(formulas, nb_spec,
                spec_names, partition_file, mp_parameters, player, options, unique_id)
    if tocheck in [UNREAL, BOTH]: # unrealizability checking
        (tbucw_c_list_unreal, alphabet) = automata_construction(formulas_unreal, nb_spec, spec_names_unreal, partition_unreal, mp_parameters_unreal, player_unreal, options, unique_id)

    if tool == WRING:
        subprocess.Popen(['rm', '-rf', TMP_PATH])

    if tocheck in [REAL, BOTH]:
        nb_tbucw = len(tbucw_c_list)
    else:
        nb_tbucw = len(tbucw_c_list_unreal)
        
    if nb_tbucw == 1 and chk_method == COMP: # Change chk_method to MONO if there is only one tbucw (user input error protection)
        chk_method = MONO
        options = (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, tocheck, set_of_winning_strategies, path, filename)
    if tocheck == REAL and nbw_constr == COMP and chk_method == MONO:
        spec_names[0] = "Phi" # rename to display
    tbucw_time = os.times()[4] - start_time
    
    # Group order tree building
    if tocheck in [REAL, BOTH]: # realizability checking
        (group_order_tree, tree_root) = build_group_order_tree(group_order, spec_names, tbucw_c_list)
        group_order_tree.add_node_attribute(tree_root, ("OPT2", True))
    if tocheck in [UNREAL, BOTH]: # unrealizability checking
        (group_order_tree_unreal, tree_root_unreal) = build_group_order_tree(group_order_unreal, spec_names_unreal, tbucw_c_list_unreal)
        group_order_tree_unreal.add_node_attribute(tree_root_unreal, ("OPT2", True))
    
    # Find a winning strategy
    realizable = False
    unrealizable = False
    if tocheck in [UNREAL, BOTH]: # Check realizability and unrealizability or only unrealizability (synthesis is done monolithicaly)
        extract_solution = False
        c_value = [0]
        k_value = k_start-k_step
        while not realizable and not unrealizable: # iterate while we have not prove realizability or unrealizability
            if k_value == k_bound: # Bound on k reached -> abort computation
                controled_print("Bound on k reached for spec "+ spec_names[0] +" -> computation aborted (no winning solution within the bound k = " + str(k_bound) + ")\n\n", [MINTEXT, ALLTEXT], verbosity) 
                break
            
            k_value = min(k_value+k_step, k_bound)
                                
            # Test realizability
            if tocheck == BOTH:
                (realizable, solution, sg, sol_extr_time) = test_realizability(tbucw_c_list[0], k_value, c_value, player, tree_root, group_order_tree, options, mp_parameters, extract_solution)
            # If formula is not realizable for k = k_value, test the unrealizability for the same value of k
            if not realizable:
                (unrealizable, solution, sg, sol_extr_time) = test_realizability(tbucw_c_list_unreal[0], k_value, c_value, player_unreal, tree_root_unreal, group_order_tree_unreal, options, mp_parameters, extract_solution)
                        
        if realizable:
            group_order_tree.add_node_attribute(tree_root, ("k_value", k_value))
            group_order_tree.add_node_attribute(tree_root, ("c_value", c_value))
        elif unrealizable:
            group_order_tree_unreal.add_node_attribute(tree_root_unreal, ("k_value", k_value))
            group_order_tree_unreal.add_node_attribute(tree_root_unreal, ("c_value", c_value))
    else: # Only check realizability
        if dimension > 0:
            controled_print("First checking realizability of formula without costs\n\n", [MINTEXT, ALLTEXT], verbosity)
            extract_solution = False
            # Forced to apply backward algorithm here to compute all strategies (otherwise, when adding costs on the winning strategies (that is a subset of all 
            # winning strategies) computed by OTFUR, it might be no winning strategies for values of k and c for which there actually are)
            options_with_back_algo = (tool, opt, critical, verbosity, nbw_constr, chk_method, BACKWARD, k_start, k_bound, k_step, tocheck, set_of_winning_strategies, path, filename)
            cur_mp_parameters = (inputs, outputs, 0, values_I, values_O, values_not_I, values_not_O, nu, c_start, c_bound, c_step)
            (realizable, solution, sg, sol_extr_time) = find_a_winning_strategy(group_order_tree, tree_root, alphabet, player, options_with_back_algo, cur_mp_parameters, extract_solution)
            
            if realizable:
                c_value = dict(group_order_tree.node_attributes(tree_root))["c_value"]
                controled_print("Formula is realizable -> check if it is still realizable with costs for c = " + str(c_value) + "\n\n", [MINTEXT, ALLTEXT], verbosity)
                extract_solution = False
                sg = add_credits_to_safety_game_c(sg, dimension, convert_list_to_c_format(c_value))
                
                if player == P_I:
                    start_antichain_1 = sg.contents.positions_I
                    start_antichain_2 = sg.contents.positions_O
                else:
                    start_antichain_1 = sg.contents.positions_O
                    start_antichain_2 = sg.contents.positions_I
                cf_info = cf_info = get_link_data_c(start_antichain_1.contents.incomparable_elements).contents.cf.contents.info
                spec_index = dict(group_order_tree.node_attributes(tree_root))["spec_index"]
                
                (realizable, solution, sg, sol_extr_time) = solve_safety_game(start_antichain_1, start_antichain_2, cf_info, alphabet, player, dimension, c_value, spec_index, options, mp_parameters, chk_dir, extract_solution)
                    
                if not realizable:
                    controled_print("No solution found for spec " + spec_index + " with costs for current k and c values -> start over with higher values of k and c\n\n", [MINTEXT, ALLTEXT], verbosity)
                    (realizable, solution, sg, sol_extr_time) = find_a_winning_strategy(group_order_tree, tree_root, alphabet, player, options, mp_parameters, extract_solution)
                else:
                    controled_print("Solution found for spec " + spec_index + " with costs for current k and c values\n\n", [MINTEXT, ALLTEXT], verbosity)
        else: # dimension = 0
            extract_solution = False
            (realizable, solution, sg, sol_extr_time) = find_a_winning_strategy(group_order_tree, tree_root, alphabet, player, options, mp_parameters, extract_solution)

    check_time = os.times()[4] - start_time - tbucw_time
    total_time = os.times()[4] - start_time
  
    # Write the solution
    if extract_solution:
        if realizable:
            print_solution(solution, inputs, outputs, player, filename, path, verbosity)
            if len(solution.nodes()) <= 10:
                display_solution(solution, inputs, outputs, player, filename, path)
            else:
                controled_print("The solution has more than 100 nodes", [MINTEXT,
                                                                         ALLTEXT], verbosity)
        elif unrealizable:
            print_solution(solution, outputs, inputs, player_unreal, filename, path, verbosity)
            if len(solution.nodes()) <= 20:
                display_solution(solution, outputs, inputs, player_unreal, filename, path)      

        # Print stats and finish
        if realizable:
            print_stats(verbosity, tbucw_time, check_time, sol_extr_time, total_time, realizable, unrealizable, solution, player, group_order_tree, spec_names, nb_tbucw, dimension)
        elif unrealizable:
            print_stats(verbosity, tbucw_time, check_time, sol_extr_time, total_time, realizable, unrealizable, solution, player_unreal, group_order_tree_unreal, spec_names_unreal, nb_tbucw, dimension)
        else:
            if tocheck == UNREAL:
                group_order_tree = group_order_tree_unreal
                print_stats(verbosity, tbucw_time, check_time, sol_extr_time, total_time, realizable, unrealizable, solution, player, group_order_tree, spec_names, nb_tbucw, dimension)
    return (realizable != unrealizable, realizable)
              
#### Opens the partition file and fills the inputs and outputs lists and read the optional values for mean-payoff objective        
def parse_partition(partition):
    o = open(partition,'r')
    partition = o.readlines()
    o.close()
    
    values_I = values_O = values_not_I = values_not_O = ""
    c_start = c_bound = c_step = nu = []
    
    for l in partition:
        l = l.lstrip(" ") # remove white spaces in front of l
        if re.match(re.compile('\.inputs.*'),l):
            inputs = re.split(re.compile('\s+'),l)[1:]
        elif re.match(re.compile('\.outputs.*'),l):
            outputs = re.split(re.compile('\s+'),l)[1:]
        elif re.match(re.compile('\.values_i.*'),l):
            values_I = re.split(re.compile('\s+'),l)[1:]
        elif re.match(re.compile('\.values_o.*'),l):
            values_O = re.split(re.compile('\s+'),l)[1:]
        elif re.match(re.compile('\.values_!i.*'),l):
            values_not_I = re.split(re.compile('\s+'),l)[1:]
        elif re.match(re.compile('\.values_!o.*'),l):
            values_not_O = re.split(re.compile('\s+'),l)[1:]
        elif re.match(re.compile('\.nu.*'),l):
            nu = re.split(re.compile('\s+'),l)[1:]
        elif re.match(re.compile('\.c_start.*'),l):
            c_start = re.split(re.compile('\s+'),l)[1:]
        elif re.match(re.compile('\.c_bound.*'),l):
            c_bound = re.split(re.compile('\s+'),l)[1:]
        elif re.match(re.compile('\.c_step.*'),l):
            c_step = re.split(re.compile('\s+'),l)[1:]
           
    try:
        inputs_temp = []
        for x in inputs:
            if x!='' and x!='\n':
                inputs_temp.append(x)
        inputs = inputs_temp
    except UnboundLocalError:
        print("Input signals not found")
        exit(0)
    
    try:
        outputs_temp = []
        for x in outputs:
            if x!='' and x!='\n':
                outputs_temp.append(x)
        outputs = outputs_temp
    except UnboundLocalError:
        print("Output signals not found")
        exit(0)
        
    for prop in (inputs+outputs):
        if prop != prop.lower():
            print("Atomic signals must be lowercase strings!")
            exit(0)
           
    values_for_I = False
    values_for_O = False
    values_for_not_I = False
    values_for_not_O = False
    
    # local function that converts the values of signals from string to list
    def convert_signals_values(v):
        values_temp = []
        for x in v:
            if x!='' and x!='\n':
                values_temp.append(x.strip('()').split(','))
        return values_temp
    
    if values_I != "" and values_I != [""]:
        values_for_I = True
        values_I = convert_signals_values(values_I)
    else:
        values_I = []
    
    if values_O != "" and values_O != [""]:
        values_for_O = True
        values_O = convert_signals_values(values_O)
    else:
        values_O = []
                
    if values_not_I != "" and values_not_I != [""]:
        values_for_not_I = True
        values_not_I = convert_signals_values(values_not_I)
    else:
        values_not_I = []
        
    if values_not_O != "" and values_not_O != [""]:
        values_for_not_O = True
        values_not_O = convert_signals_values(values_not_O)
    else:
        values_not_O = []
             
    if values_for_I or values_for_O or values_for_not_I or values_for_not_O:
        dimension = len((values_I+values_O+values_not_I+values_not_O)[0])
        if not values_for_I:
            for i in range(len(inputs)):
                values_I.append(dimension*[0])
        if not values_for_O:
            for i in range(len(outputs)):
                values_O.append(dimension*[0])
        if not values_for_not_I:
            for i in range(len(inputs)):
                values_not_I.append(dimension*[0])
        if not values_for_not_O:
            for i in range(len(outputs)):
                values_not_O.append(dimension*[0])
    else:
        dimension = 0
        if not values_for_I:
            for i in range(len(inputs)):
                values_I.append([0])
        if not values_for_O:
            for i in range(len(outputs)):
                values_O.append([0])
        if not values_for_not_I:
            for i in range(len(inputs)):
                values_not_I.append([0])
        if not values_for_not_O:
            for i in range(len(outputs)):
                values_not_O.append([0])
                
    if dimension != 0:
        for x in nu:
            if x!='' and x!='\n':
                nu = x.strip('()').split(',')
        for x in c_start:
            if x!='' and x!='\n':
                c_start = x.strip('()').split(',')
        for x in c_bound:
            if x!='' and x!='\n':
                c_bound = x.strip('()').split(',')
        for x in c_step:
            if x!='' and x!='\n':
                c_step = x.strip('()').split(',')
                           
        if len(inputs) != len(values_I):
            print("The values of input signals do not correspond with the input signals")
            exit(0)
        if len(outputs) != len(values_O):
            print("The values of output signals do not correspond with the output signals")
            exit(0)
        if len(inputs) != len(values_not_I):
            print("The values of negation of input signals do not correspond with the input signals")
            exit(0)
        if len(outputs) != len(values_not_O):
            print("The values of negation of output signals do not correspond with the output signals")
            exit(0)
                 
        for v in values_I+values_O+values_not_I+values_not_O:
            if len(v) != dimension:
                print("All vectors of values do not have the same dimension")
                exit(0)
        
        if nu == [] or nu == [""]:
            print("You must specify a nu vector")
            exit(0)
        if c_start == [] or c_start == [""]:
            c_start = dimension*[0]
        if c_bound == [] or c_bound == [""]:
            c_bound = dimension*[10]
        if c_step == [] or c_step == [""]:
            c_step = dimension*[2]
        
        if len(nu) != dimension:
            print("Wrong dimension of the nu vector")
            exit(0)
            
        if len(c_start) != dimension:
            print("Wrong dimension of the c_start vector")
            exit(0)
        
        if len(c_bound) != dimension:
            print("Wrong dimension of the c_bound vector")
            exit(0)
        
        if len(c_step) != dimension:
            print("Wrong dimension of the c_step vector")
            exit(0)
            
        # local function that converts string values to integer values
        def convert_string_values_to_integer(values):
            temp = []
            for x in values:
                temp.append(int(x))
            return temp
                  
        def convert_list_of_string_values_to_list_of_integer_values(list):
           temp = []
           for v in list:
                temp.append(convert_string_values_to_integer(v))
           return temp
        
        try:
            values_I = convert_list_of_string_values_to_list_of_integer_values(values_I)
            values_O = convert_list_of_string_values_to_list_of_integer_values(values_O)
            values_not_I = convert_list_of_string_values_to_list_of_integer_values(values_not_I)
            values_not_O = convert_list_of_string_values_to_list_of_integer_values(values_not_O)
        except ValueError:
            print("The values associated to signals must be integers")
            exit(0)

        
        # local function that converts real numbers represented by strings to fractions represented by pairs (numerator, denominator)
        def convert_to_frac(s):
            spt = string.split(s, ".")
            if len(spt) > 1:
                mult = math.pow(10, len(spt[1]))
                res = int(spt[0]) * mult
                if s.startswith('-'):
                    if s.startswith('-0'):
                        res = -res
                    res -= int(spt[1])
                else:
                    res += int(spt[1])
            else:
                mult = 1
                res = int(spt[0])
            return (res,mult)
        
        nu_temp = []
        for x in nu:
            try:                
                nu_temp.append(convert_to_frac(x))
            except ValueError:
                print("Nu values must be numbers")
                exit(0)
        nu = nu_temp
        
        try:
            c_start = convert_string_values_to_integer(c_start)
            c_bound = convert_string_values_to_integer(c_bound)
            c_step = convert_string_values_to_integer(c_step)
            for i in range(dimension):
                if c_start[i] < 0 or c_bound[i] < 0 or c_step[i] < 0:
                    raise ValueError
        except ValueError:
            print("c_start, c_bound and c_step must be vectors of positive integers")
            exit(0)
        
        for i in range(dimension):
            if c_start[i] > c_bound[i]:
                c_bound[i] = c_start[i]
        
    else:
        nu = [(0,1)]
        c_start = [0]
        c_bound = [0]
        c_step = [0]
               
    return (inputs, outputs, values_I, values_O, values_not_I, values_not_O, nu, dimension, c_start, c_bound, c_step)

#### Creates a new partition file where the input (resp. output) signals are set as output (resp.input) signals
def inverse_partition(inputs, outputs):
    f = open(TMP_PATH+"partition_unreal.part", "w")
    o = ".outputs "
    i = ".inputs "
    for input in inputs:
        o += input + " "
    for output in outputs:
        i += output + " "
    f.write(i)
    f.write("\n")
    f.write(o)
    f.close()
    
    return TMP_PATH+"partition_unreal.part"

#### Constructs an automaton UCW (digraph python) for each formula, applies optimization 0 and 1 considering opt parameter and builds equivalent tbUCW automaton (in C) (one for each formula if chk_method == COMP)    
def automata_construction(formulas, nb_spec, spec_names, partition, mp_parameters, player, options, unique_id):
    (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, unrea, set_of_winning_strategies, path, filename) = options
    (inputs, outputs, dimension, values_I, values_O, values_not_I, values_not_O, nu, c_start, c_bound, c_step) = mp_parameters

    # Build the alphabet
    alphabet = build_alphabet(inputs, outputs)

    # Build the weight functions
    (weight_function_I, weight_function_O) = build_weight_functions(inputs, outputs, alphabet, mp_parameters, options)
    
    # Automata construction: ucw_python and acceptings_states are lists (containing one (nbw_constr = MONO) or several (nbw_contr = COMP) ucw)
    if tool in [LTL2BA, LTL3BA, SPOT]: # get the ucw from ltl2/3ba or SPOT
        (ucw_python, accepting_states) = construct_automata(formulas, spec_names, verbosity, tool)
    elif tool == WRING:
        (ucw_python, accepting_states) = construct_automata_wring(formulas, spec_names, partition, inputs, outputs, unique_id, verbosity)
    else:
        print("Wrong tool")
        exit(0)

    # Optimization 1
    for i in range(len(formulas)):
        # Apply optimization 1: detects bounded/unbounded states (which cannot/can carry a counter k, whatever k is)
        if opt in [OPT1, OPT12]:
            controled_print("Optimization on automaton for " + spec_names[i] + ": Detect bounded/unbounded states...\n", [ALLTEXT, MINTEXT], verbosity)
            ucw_python[i] = counters_optimization(ucw_python[i], accepting_states[i], verbosity)
            controled_print("\n", [ALLTEXT, MINTEXT], verbosity)

    # tbUCW construction    
    tbucw_c_list = [] 
    if chk_method == MONO: 
        tbucw_c_list.append(build_tbucw(ucw_python, accepting_states, alphabet, inputs, outputs, player, weight_function_I, weight_function_O, dimension))
    else: 
        for i in range(nb_spec):       
            tbucw_c_list.append(build_tbucw(ucw_python[i], accepting_states[i], alphabet, inputs, outputs, player, weight_function_I, weight_function_O, dimension))

    if nbw_constr == COMP and chk_method == MONO:
        spec_names[0] = "Phi" # rename to display
    
    # Display tbucws           
    for i in range(len(tbucw_c_list)):
        controled_print("Turn-based automaton for " + spec_names[i] + ":\n", [ALLTEXT, MINTEXT], verbosity)
        if verbosity == ALLTEXT:
            print_tbucw_c(tbucw_c_list[i])
        elif verbosity == MINTEXT:
            print_tbucw_stats_c(tbucw_c_list[i])
        controled_print("\n", [ALLTEXT, MINTEXT], verbosity)
   
    return (tbucw_c_list, alphabet)

#### Builds the tree representing the composition order (compositionnal checking) and associates a tbucw to each leaf
def build_group_order_tree(group_order, spec_names, tbucw_c_list):
    nb_spec = len(tbucw_c_list)
    
    if nb_spec == 1:
        group_order = FLAT
    # Create flat representation
    if group_order == FLAT:
        group_order = "("
        for i in range(nb_spec):
            group_order += spec_names[i]
            if i != nb_spec-1:
                group_order += " "
        group_order += ")"
    # Create binary representation
    elif group_order == BINARY:
        group_order = "("
        i=0
        while i < nb_spec:
            if i != nb_spec-1:
                group_order += "(" + spec_names[i] + " " + spec_names[i+1] + ")"
                if i!= nb_spec-2:
                    group_order += " "
            else:
                group_order += spec_names[i]
            i+=2
        group_order += ")"

    # Add global parenthesis if missing
    if not re.match("\(", group_order[0]):
        group_order = "("+group_order+")"

    # Build group order tree
    group_order_tree = digraph()
    group_order_tree.__init__()
    
    stack = []
    parent_stack = []
    value = ""
    parent = 0
    for i in range(0, len(group_order)):
        if re.match("\(", group_order[i]):
            if len(stack) > 0:
                while len(stack) > 0:
                    value = stack.pop() + value
                nodes = value.strip().split(" ")
                value = ""
                
                for node in nodes:
                    if node != "":
                        group_order_tree.add_node(node)
                        try:
                            group_order_tree.add_edge(("parent_"+str(parent_stack[len(parent_stack)-1]), node))
                        except IndexError:
                            print("Parenthesizing: parenthesis problem (too many right parenthesis)")
                            exit(0)
                    
            parent += 1
            parent_stack.append(parent)
            group_order_tree.add_node("parent_"+str(parent))
            if parent != 1:
                group_order_tree.add_edge(("parent_"+str(parent_stack[len(parent_stack)-2]), "parent_"+str(parent_stack[len(parent_stack)-1])))
        elif re.match("\)", group_order[i]):
            if len(stack) > 0:
                while len(stack) > 0:
                    value = stack.pop() + value
                nodes = value.strip().split(" ")
                value = ""
        
                for node in nodes:
                    group_order_tree.add_node(node)
                    try:
                        group_order_tree.add_edge(("parent_"+str(parent_stack[len(parent_stack)-1]), node))
                    except IndexError:
                        print("Parenthesizing: parenthesis problem (too many right parenthesis)")
                        exit(0)
            try:
                parent_stack.pop()
            except IndexError:
                print("Parenthesizing: parenthesis problem (too many right parenthesis)")
                exit(0)
        else:
            stack.append(group_order[i]) 
    
    if len(parent_stack) > 0:
        print("Parenthesizing: parenthesis problem (too many left parenthesis)")
        exit(0)   

    # Remove useless nodes (nodes that have only one son)
    leafs_count = 0
    for node in group_order_tree.nodes():
        cur_sons = group_order_tree.neighbors(node)
        if len(cur_sons) == 1:
            if len(group_order_tree.incidents(node)) > 0: # not the root
                parent = group_order_tree.incidents(node)[0]
                group_order_tree.add_edge((parent, cur_sons[0]))
            elif len(group_order_tree.neighbors(cur_sons[0])) > 0: # not parent of a leaf
                for grandson in group_order_tree.neighbors(cur_sons[0]):
                    group_order_tree.add_edge((node, grandson))
        
            group_order_tree.del_node(node)
        elif len(cur_sons) == 0:
            leafs_count += 1
   
    # Test whether there is one leaf for each spec        
    if leafs_count != nb_spec:
        print("Parenthesizing: number of specifications problem")
        exit(0)
        
    # Associate each leaf with the corresponding tbucw and find root
    root = -1
    for node in group_order_tree.nodes():
        cur_sons = group_order_tree.neighbors(node)
        if len(cur_sons) == 0:
            try:
                group_order_tree.add_node_attribute(node, ("tbucw", (tbucw_c_list[spec_names.index(node)])))
                group_order_tree.add_node_attribute(node, ("spec_index", node))
                group_order_tree.add_node_attribute(node, ("OPT2", True))
            except ValueError:
                print(("Parenthesizing: specification not found (" + node + ")"))
                exit(0)
        
        if len(group_order_tree.incidents(node)) == 0:
            root = node

    # No root ?
    if root == -1:
        print("Parenthesizing problem: no root")
        exit(0)

    return (group_order_tree, root)


#### Recursive method which computes a winning strategy according to a tree of which leafs corresponds to tbucw, starting from node tree_node
def find_a_winning_strategy(group_order_tree, tree_node, alphabet, player, options, mp_parameters, extract_solution):
    (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, unrea, set_of_winning_strategies, path, filename) = options
    (inputs, outputs, dimension, values_I, values_O, values_not_I, values_not_O, nu, c_start, c_bound, c_step) = mp_parameters
    
    # If tree_node is a leaf: find a winning strategy for its associated spec monolithicaly
    if len(group_order_tree.neighbors(tree_node)) == 0:
        controled_print("Fix point computation for " + str(dict(group_order_tree.node_attributes(tree_node))["spec_index"])+"\n", [ALLTEXT, MINTEXT], verbosity)
        
        try:
            tbucw_c = dict(group_order_tree.node_attributes(tree_node))["tbucw"]
        except KeyError: # Impossible?
            print("No tbUCW ?")
            exit(0) 
        
        # If we have already found a winning strategy for this spec, try with a higher value of K, otherwise, start with 0
        try:
            k_value = dict(group_order_tree.node_attributes(tree_node))["k_value"]
            c_value = dict(group_order_tree.node_attributes(tree_node))["c_value"]
        except KeyError:
            k_value = k_start-k_step
            c_value = []
            for i in range(len(c_bound)):
                c_value.append(c_start[i]-c_step[i])
            
        winning_strategy = False
        sg = None
        while not winning_strategy:
            if k_value == k_bound and c_value == c_bound: # Bound on k and c reached -> abort computation
                controled_print("Bound on k and c reached for spec "+ tree_node +" -> computation aborted (no winning solution within the bounds k = " + str(k_bound) + " and c = " + str(c_bound) + ")\n\n", [MINTEXT, ALLTEXT], verbosity) 
                return (False, None, None, 0)
                
            k_value = min(k_value+k_step, k_bound) 
            for i in range(len(c_bound)):
                c_value[i] = min(c_value[i]+c_step[i], c_bound[i])
                  
            # Free previously computed fix point
            if sg != None:
                free_antichain_full_c(sg.contents.positions_O, FREE_TUPLE_FULL_FUNC(free_tuple_full_c))
                free_antichain_full_c(sg.contents.positions_I, FREE_TUPLE_FULL_FUNC(free_tuple_full_c))
                
            # Test realizability
            (winning_strategy, solution, sg, sol_extr_time) = test_realizability(tbucw_c, k_value, c_value, player, tree_node, group_order_tree, options, mp_parameters, extract_solution)
            
        group_order_tree.add_node_attribute(tree_node, ("k_value", k_value))
        group_order_tree.add_node_attribute(tree_node, ("c_value", c_value))
        return (winning_strategy, solution, sg, sol_extr_time)
    
    # If tree_node is not a leaf: find a winning strategy recursively
    else: 
        spec_index = "("
        nb_sons = len(group_order_tree.neighbors(tree_node))
        start_antichains_PO = (POINTER(Antichain)*nb_sons)(None)
        start_antichains_PI = (POINTER(Antichain)*nb_sons)(None)
        cfs_info = (POINTER(GNode)*nb_sons)(None)

        i = 0
        # Compute the fix point for each son and compose them
        sons_c_values = []
        for son in group_order_tree.neighbors(tree_node):
            (winning_strategy, cur_solution, cur_sg, sol_extr_time) = find_a_winning_strategy(group_order_tree, son, alphabet, player, options, mp_parameters, extract_solution)
            
            if not winning_strategy:
                return (False, None, None, 0)
            
            spec_index += str(dict(group_order_tree.node_attributes(son))["spec_index"]) + " "
            sons_c_values.append(dict(group_order_tree.node_attributes(son))["c_value"])

            start_antichains_PO[i] = cur_sg.contents.positions_O
            start_antichains_PI[i] = cur_sg.contents.positions_I

            cfs_info[i] = get_link_data_c(cur_sg.contents.positions_O.contents.incomparable_elements).contents.cf.contents.info

            i += 1

        if len(c_start) == 0:
            c_value = [0]
        else:
            c_value = len(c_start)*[0]
            for i in range(len(c_start)):
                for j in range(len(sons_c_values)):
                    c_value[i] = max(c_value[i], sons_c_values[j][i])
        group_order_tree.add_node_attribute(tree_node, ("c_value", c_value))
        
        # Build the start antichains
        if nb_sons > 1:
            cf_info = compose_cf_info_c(cfs_info, nb_sons)
            if player == P_O:
                start_antichain1 = compose_antichains_c(start_antichains_PO, nb_sons, COMPOSE_TUPLES_FUNC(compose_tuples_c), cf_info)
                start_antichain2 = compose_antichains_c(start_antichains_PI, nb_sons, COMPOSE_TUPLES_FUNC(compose_tuples_c), cf_info)
            else:
                start_antichain1 = compose_antichains_c(start_antichains_PI, nb_sons, COMPOSE_TUPLES_FUNC(compose_tuples_c), cf_info)
                start_antichain2 = compose_antichains_c(start_antichains_PO, nb_sons, COMPOSE_TUPLES_FUNC(compose_tuples_c), cf_info)
            
            # Free the former fix points    
            for a in start_antichains_PO:
                free_antichain_full_c(a, FREE_TUPLE_FULL_FUNC(free_tuple_full_c))
            for a in start_antichains_PI:
                free_antichain_full_c(a, FREE_TUPLE_FULL_FUNC(free_tuple_full_c))
        else:
            print("Only one son?")
            exit(0)
                
        spec_index = spec_index[0:len(spec_index)-1]+")"
        group_order_tree.add_node_attribute(tree_node, ("spec_index", spec_index))
        controled_print("Composition of fix points of specs " + str(spec_index)+"\n", [ALLTEXT, MINTEXT], verbosity)
            
        if chk_dir == FORWARD and len(group_order_tree.incidents(tree_node)) == 0: # Root -> Forward algorithm (OTFUR)
            sg = otfur(start_antichain1, start_antichain2, cf_info, alphabet, player, dimension, convert_list_to_c_format(c_value), options)
        else:  # Backward algorithm
            sg = compute_fix_point(start_antichain1, start_antichain2, alphabet, player, critical, verbosity)
        
        if len(group_order_tree.incidents(tree_node)) == 0 and extract_solution: # Root -> extract the solution if we have to
            (winning_strategy, solution, sg, sol_extr_time) = extract_solution_from_safety_game(sg, alphabet, player, mp_parameters, options)
        else: # check if there is a winning strategy, but do not extract a solution
            solution = None
            sol_extr_time = 0
            winning_strategy = has_a_winning_strategy_c(sg, alphabet, player)
            if not winning_strategy: # no winning strategy -> free the fix point
                free_antichain_full_c(sg.contents.positions_O, FREE_TUPLE_FULL_FUNC(free_tuple_full_c))
                free_antichain_full_c(sg.contents.positions_I, FREE_TUPLE_FULL_FUNC(free_tuple_full_c))
                sg = None

        if winning_strategy:
            controled_print("Solution found for composition of specs " + str(spec_index)+"\n\n", [ALLTEXT, MINTEXT], verbosity)
        else:
            controled_print("No solution found for composition of specs " + str(spec_index)+"\n\n", [ALLTEXT, MINTEXT], verbosity)

        # If there is a winning strategy, return the solution and the fix point
        if winning_strategy:
            return (winning_strategy, solution, sg, sol_extr_time)
        # Otherwise, start over from the leafs with k_value incrementation
        else:
            return find_a_winning_strategy(group_order_tree, tree_node, alphabet, player, options, mp_parameters, extract_solution)  

#### Tests the realizability of formula represented by tbucw_c for K=k_value and C=c_value when player starts
#### 1) applies optimization 2 on tbucw_c if enabled
#### 2) builds starting antichains and cf_info
#### 3) calls corresponding algorithm (forward or backward) for solving the safety game
def test_realizability(tbucw_c, k_value, c_value, player, tree_node, group_order_tree, options, mp_parameters, extract_solution):
    (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, unrea, set_of_winning_strategies, path, filename) = options
    (inputs, outputs, dimension, values_I, values_O, values_not_I, values_not_O, nu, c_start, c_bound, c_step) = mp_parameters
    
    tbucw_c_k = tbucw_c # tbucw_c_k is an instance of tbucw_c which will be optimized for k=k_value if opt2 is enabled
    
    controled_print("Realizability checking for " + tree_node + ", k = " + str(k_value), [ALLTEXT, MINTEXT], verbosity)
    if dimension > 0:
        controled_print("and c = " + str(c_value), [ALLTEXT, MINTEXT], verbosity)
    controled_print("\n", [ALLTEXT, MINTEXT], verbosity)
    
    # Check if opt2 is still usefull for that values of k and c
    try:
        opt2 = dict(group_order_tree.node_attributes(tree_node))["OPT2"]
    except KeyError:
        opt2 = True
        
    # Apply optimization 2 (be careful, a new automaton will be created but the labels of the states of automaton_c will be modified -> don't forget to reset them when finished)
    opt2_time = 0
    if opt2 == True and opt in [OPT12, OPT2]:
        controled_print("Optimization: Detect k-surely losing states...\n", [ALLTEXT, MINTEXT], verbosity)
       
        start_opt2_time = time.process_time()
        # If k_value > 0, reset the state labels of the starting automaton_c
        if k_value > 0: # Only if k_value > 0 because otherwise, optimization 2 hasn't been applied yet
            reset_tbucw_states_labels_c(tbucw_c)
        tbucw_c_k = tbucw_size_optimization(tbucw_c, k_value)
        opt2_time = time.process_time()-start_opt2_time
        
        # Test whether optimization 2 has affected the tbucw
        if get_tbucw_size_c(tbucw_c) == get_tbucw_size_c(tbucw_c_k):
            controled_print("No improvement made on the size of the turn-based automaton\n", [ALLTEXT, MINTEXT], verbosity)
            controled_print("Detect k-surely losing states optimization is now turned off for specification " + tree_node + " (useless for larger values of k)\n\n", [ALLTEXT, MINTEXT], verbosity)
            group_order_tree.add_node_attribute(tree_node, ("OPT2", False))
            reset_tbucw_states_labels_c(tbucw_c)
            tbucw_c_k = tbucw_c
        else:    
            controled_print("Optimized turn-based automaton: \n", [ALLTEXT, MINTEXT], verbosity)
            if verbosity == MINTEXT: 
                print_tbucw_stats_c(tbucw_c_k)
            elif verbosity == ALLTEXT: 
                print_tbucw_c(tbucw_c_k)
            controled_print("Optimization time: " + str(opt2_time) + "\n", [ALLTEXT, MINTEXT], verbosity)
            controled_print("\n", [ALLTEXT, MINTEXT], verbosity)
    
    # Build the counting functions information structure
    cf_info = build_cf_info_c(tbucw_c_k, k_value)
    c_values_c_format = convert_list_to_c_format(c_value)
    
    # Compute the starting antichains
    start_antichain_1 = build_start_antichain_c(player, cf_info, dimension, c_values_c_format)
    start_antichain_2 = build_start_antichain_c(switch_player(player), cf_info, dimension, c_values_c_format)
    
    # Realizability checking
    if chk_method == MONO and chk_dir == FORWARD:
        direction = FORWARD
    else:
        direction = BACKWARD
    extract_solution = False
    spec_index = dict(group_order_tree.node_attributes(tree_node))["spec_index"]
    
    (winning_strategy, solution, sg, sol_extr_time) = solve_safety_game(start_antichain_1, start_antichain_2, cf_info, tbucw_c_k.contents.alphabet, player, dimension, c_value, spec_index, options, mp_parameters, direction, extract_solution)

    if winning_strategy:
        controled_print("Solution found for spec " + str(spec_index) + " for k = " + str(k_value), [MINTEXT, ALLTEXT], verbosity)
    else:
        controled_print("No solution found for spec " + str(spec_index) + " for k = " + str(k_value), [MINTEXT, ALLTEXT], verbosity)
    if dimension > 0:
        controled_print("and c = " + str(c_value), [MINTEXT, ALLTEXT], verbosity)
    controled_print("\n\n", [MINTEXT, ALLTEXT], verbosity)
        
    return (winning_strategy, solution, sg, sol_extr_time)

#### Solves the safety game (forwardly or backwardly according to direction) represented by start_antichain_1 and start_antichain_2
def solve_safety_game(start_antichain_1, start_antichain_2, cf_info, alphabet, player, dimension, c_value, spec_index, options, mp_parameters, direction, extract_solution):
    (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, unrea, set_of_winning_strategies, path, filename) = options
    chk_dir = direction

    # Solve the safety game
    if chk_dir == FORWARD: # Forward algorithm (OTFUR)
        sg = otfur(start_antichain_1, start_antichain_2, cf_info, alphabet, player, dimension, convert_list_to_c_format(c_value), options)
    else: # Backward algorithm
        sg = compute_fix_point(start_antichain_1, start_antichain_2, alphabet, player, critical, verbosity)

    # Analyze the solved safety game    
    if extract_solution: # Extract winning strategies from the safety game (if there are)
        (winning_strategy, solution, sg, sol_extr_time) = extract_solution_from_safety_game(sg, alphabet, player, mp_parameters, options)
    else: # Check if there is a winning strategy, but do not extract a solution
        solution = None
        sol_extr_time = 0
        winning_strategy = has_a_winning_strategy_c(sg, alphabet, player)
        if not winning_strategy: # no winning strategy -> free the safety game
            free_safety_game_c(sg)
            sg = None
            
    return (winning_strategy, solution, sg, sol_extr_time)

#### Extracts one or several strategies from safety game and represents them as a transition system
def extract_solution_from_safety_game(sg, alphabet, player, mp_parameters, options):
    (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, unrea, set_of_winning_strategies, path, filename) = options
    start_extr_time = os.times()[4]
    solution = None
    if critical == ON and chk_dir == BACKWARD:
        crit = TRUE
    else:
        crit = FALSE
    
    strategy = extract_strategies_from_safety_game_c(sg, alphabet, player, crit, set_of_winning_strategies)
    
    winning_strategy = False
    if strategy.contents.nb_states_PO > 0:
        winning_strategy = True
        solution = convert_transition_system_into_digraph(strategy, player, alphabet, mp_parameters, options)
    free_transition_system_c(strategy)
    free_safety_game_c(sg)
    sg = None
    sol_extr_time = os.times()[4]-start_extr_time

    return (winning_strategy, solution, sg, sol_extr_time)

#### Prints statistics
def print_stats(verbosity, tbucw_time, check_time, sol_extr_time, total_time, realizable, unrealizable, solution, player, group_order_tree, spec_names, nb_tbucw, dimension):
    controled_print("################ Execution recap ####################\n", [ALLTEXT, MINTEXT, RECAP], verbosity)
    if realizable:
        controled_print("Formula is realizable -> at least one strategy (for the system) to realize it has been computed.\n\n", [ALLTEXT, MINTEXT, RECAP], verbosity)
    elif unrealizable:
        controled_print("Formula is unrealizable -> at least one strategy (for the environment) to prevent the system to realize it has been computed.\n\n", [ALLTEXT, MINTEXT, RECAP], verbosity)
    else:
        controled_print("Neither realizability nor unrealizability has been proved. You may retry with higher k and c values.\n", [ALLTEXT, MINTEXT, RECAP], verbosity)
    
    if realizable or unrealizable:    
        spec_k_values = nb_tbucw*[-1]
        spec_c_values = nb_tbucw*[-1]
        tbucw_sizes = nb_tbucw*[-1]
        for node in group_order_tree.nodes():
            if len(group_order_tree.neighbors(node)) == 0:
               spec_k_values[spec_names.index(node)] = dict(group_order_tree.node_attributes(node))["k_value"]
               spec_c_values[spec_names.index(node)] = dict(group_order_tree.node_attributes(node))["c_value"]
               tbucw_sizes[spec_names.index(node)] = get_tbucw_size_c(dict(group_order_tree.node_attributes(node))["tbucw"])
    
        controled_print("Automata construction time: %.2fs\n" % tbucw_time, [ALLTEXT, MINTEXT, RECAP], verbosity)
        controled_print("\n", [ALLTEXT, MINTEXT, RECAP], verbosity)
        for i in range(nb_tbucw):
            controled_print("Number of states of automaton for spec " + spec_names[i] + ": " + str(tbucw_sizes[i]) + "\n", [ALLTEXT, MINTEXT, RECAP], verbosity)
        controled_print("\n", [ALLTEXT, MINTEXT, RECAP], verbosity)
        for i in range(nb_tbucw):
            controled_print("Spec " + spec_names[i] + ": k = " + str(spec_k_values[i]), [ALLTEXT, MINTEXT, RECAP], verbosity)
            if dimension > 0:
                controled_print("and c = " + str(spec_c_values[i]), [ALLTEXT, MINTEXT, RECAP], verbosity)
            controled_print("\n", [ALLTEXT, MINTEXT, RECAP], verbosity)
        controled_print("\n", [ALLTEXT, MINTEXT, RECAP], verbosity)        
        controled_print("Synthesis time: %.2fs\n" % check_time, [ALLTEXT, MINTEXT, RECAP], verbosity)
        controled_print("\n", [ALLTEXT, MINTEXT, RECAP], verbosity)        
        if player == P_O:
            nb_states_in_solution = len(solution.nodes())
        else: # if starting player = P_I, there is a fake node in the graph (the initial node) which do not belong to the solution (but there might be several initial states)
            nb_states_in_solution = (len(solution.nodes())-1)
        controled_print("Number of states of transition system representing the solution(s): %d\n" % nb_states_in_solution, [ALLTEXT, MINTEXT, RECAP], verbosity)
        controled_print("\n", [ALLTEXT, MINTEXT, RECAP], verbosity)        
        controled_print("Solution(s) extraction time: %.2fs\n" % sol_extr_time, [ALLTEXT, MINTEXT, RECAP], verbosity)    
        controled_print("\n", [ALLTEXT, MINTEXT, RECAP], verbosity)        
    
    controled_print("Total time: %.2fs\n" % total_time, [ALLTEXT, MINTEXT, RECAP], verbosity)    
    controled_print("\n", [ALLTEXT, MINTEXT, RECAP], verbosity)        

#### Displays an error message and exit    
def exit_acaciaplus(error_text):
    print(error_text)
    print("Use the -h or --help option to display the helper")
    exit(0)

###########################################################################################################
############## Main: Retrieves the formula, partition and options and launches the synthesis ##############
###########################################################################################################
def main(hardargs=None):
    parser = optparse.OptionParser("")
    parser.add_option("-L", "--ltl", dest="ltl", default="",
                      type="string", help="formula (.ltl file)")
    parser.add_option("-P", "--part", dest="part", default="",
                      type="string",
                      help="partition of atomic signals file (.part file)")
    parser.add_option("-t", "--tool", dest="tool", default=LTL2BA, type="string", help="LTL to Buchi automata tool (LTL2BA, LTL3BA or WRING), default: LTL2BA")
    parser.add_option("-n", "--nbw", dest="nbw_constr", default=MONO, type="string", help="Buchi automata construction method (COMP or MONO), default: MONO")
    parser.add_option("-s", "--syn", dest="syn_method", default=MONO, type="string", help="LTL synthesis method (COMP or MONO), default: MONO")
    parser.add_option("-a", "--algo", dest="syn_algo", default=FORWARD, type="string", help="LTL synthesis algorithm (FORWARD or BACKWARD), default: FORWARD")
    parser.add_option("-p", "--player", dest="player", default=P_O, type="int", help="starting player (1, environment or 2, system), default: 2")
    parser.add_option("-k", "--kstart", dest="k_start", default=0, type="int", help="starting value of k (<= 30), default: 0")
    parser.add_option("-K", "--kbound", dest="k_bound", default=5, type="int", help="bound on k (<= 30), default: 5")
    parser.add_option("-y", "--kstep", dest="k_step", default=1, type="int", help="incremental step for k (to range on values of k), default: 1")
    parser.add_option("-C", "--check", dest="tocheck", default=REAL, type="string", help="to check realizability (REAL), unrealizability (UNREAL) or both in parallel (BOTH), default: REAL")
    parser.add_option("-v", "--verb", dest="verbosity", default=1, type=int, help="verbosity (0, 1 or 2), default: 1")
    parser.add_option("-c", "--crit", dest="critical", default=ON, type="string", help="critical signals optimization (ON or OFF), default: ON")
    parser.add_option("-o", "--opt", dest="opt", default=OPT12, type="string", help="to enable/disable optimizations 1 (detect bounded/unbounded states) and 2 (detect k-surely losing states) (1, 2, 12 or none), default: 12 (both enabled)")
    parser.add_option("-f", "--format", dest="ltl_format", default=WRING, type="string", help="LTL formula format (Wring or LTL2BA), default: WRING")
    parser.add_option("--setofstrategies", "--setofstrategies", dest="set_of_strategies", default=FALSE, type="string", help="Set to TRUE to obtain a set of winning strategies instead of one winning strategy, default= FALSE")

    if hardargs is not None:
        (options, args) = parser.parse_args(hardargs)
    else:
        (options, args) = parser.parse_args()

    formula = options.ltl
    if formula != "":
        path = re.split(re.compile('/'),formula)
        formulaname = path[len(path)-1] #demo/demo-vx
        filename = formulaname.split(".")[0]
        path = formula.replace(formulaname, "")
    else:
        exit_acaciaplus("You must specify an LTL formula file (.ltl extension)")
           
    partition = options.part
    if partition == "":
        exit_acaciaplus("You must specify a partition file (.part extension)")

    tool = str(options.tool).lower()    
    if tool == "wring":
        tool = WRING
    elif tool == "ltl3ba":
        tool = LTL3BA
    elif tool == "ltl2ba":
        tool = LTL2BA
    elif tool == "spot":
        tool = SPOT
    else:
        exit_acaciaplus("Wrong LTL to Buchi automata translation tool")
         
    nbw_constr = str(options.nbw_constr).lower()
    if nbw_constr == "mono":
        nbw_constr = MONO
    elif nbw_constr == "comp":
        nbw_constr = COMP
    else:
        exit_acaciaplus("Wrong LTL to Buchi automata construction method (MONO or COMP)")
        
    chk_method = str(options.syn_method).lower()
    if chk_method == "mono":
        chk_method = MONO
    elif chk_method == "comp":
        chk_method = COMP
    else:
        exit_acaciaplus("Wrong LTL synthesis method (MONO or COMP)")
        
    if chk_method == COMP and nbw_constr == MONO:
        exit_acaciaplus("LTL synthesis method can not be compositional with monolithic Buchi automata construction")
        
    chk_dir = str(options.syn_algo).lower()
    if chk_dir.startswith("back"):
        chk_dir = BACKWARD
    elif chk_dir.startswith("for"):
        chk_dir = FORWARD
    else:
        exit_acaciaplus("Wrong LTL synthesis algorithm (FORWARD or BACKWARD)")
        
    player = options.player
    if player == 1:
        player = P_I
    elif player == 2:
        player = P_O
    else:
        exit_acaciaplus("Wrong arguement for starting player (1: environment, 2: system)")
        
    k_start = options.k_start    
    k_bound = options.k_bound
    k_step = options.k_step
    if k_start < 0 or k_bound < 0 or k_step < 0:
        exit_acaciaplus("Bounds of values of k and k incremental step must be positive integers")
        
    if k_start > k_bound:
        k_bound = k_start   
        
    tocheck = str(options.tocheck).lower()
    if tocheck == "real":
        tocheck = REAL
    elif tocheck == "unreal":
        tocheck = UNREAL
    elif tocheck == "both":
        tocheck = BOTH
    else:
        exit_acaciaplus("Wrong argument for -C, --check (REAL: realizability, UNREAL: unrealizability, BOTH: both in parallel)")
        
    verbosity = options.verbosity
    if verbosity not in [0, 1, 2, 3]:
        exit_acaciaplus("Wrong argument for -v, --verb (0, 1, or 2)")
        
    critical = str(options.critical).lower()
    if critical == "on":
        critical = ON
    elif critical == "off":
        critical = OFF
    else:
        exit_acaciaplus("Wrong argument for -c, --crit")
        
    opt = options.opt
    if opt not in [NO_OPT, OPT1, OPT2, OPT12]:
        exit_acaciaplus("Wrong argument for -o, --opt")
        
    set_of_strategies = str(options.set_of_strategies).lower()
    if set_of_strategies == "false" or set_of_strategies == "0":
        set_of_strategies = FALSE
    elif set_of_strategies == "true" or set_of_strategies == "1":
        set_of_strategies = TRUE
    else:
        exit_acaciaplus("Wrong argument for --setofstrategies")

    if tocheck in [UNREAL, BOTH] and nbw_constr == COMP:
        exit_acaciaplus("Unrealizability checking is only available for monolithic formulas")
        
    display_parameters(player, tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, tocheck, set_of_strategies)
    return synthetize(formula, partition, player, (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, tocheck, set_of_strategies, path, filename))


if __name__ == "__main__":
    (solved, is_real) = main()
    if (not solved):
        print ("UNKNOWN")
        sys.exit(15)
    if (is_real):
        print ("REALIZABLE")
        sys.exit(0)
    else:
        print ("UNREALIZABLE")
        sys.exit(1)

