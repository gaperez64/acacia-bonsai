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

from pygraph.classes.digraph import digraph

from constants import *
from utils import *
from library_linker import *

## builds a tbucw structure (calls a dylib C)
## Implements Optimization_fusion (goal: to avoid useless state duplication):
##    it fusions transitions and turn based states when a state has several outgoing transition with the same label
##    ex (assume input starts): state_0 --i,o--> state_1 and state_O --i,o'--> state_2 gives 
##                              state_0 --i--> tb_state_0, tb_state_0 --o--> state_1 and tb_state_0 --o'--> state_1 rather than
##                              state_0 --i--> tb_state_0, tb_state_0 --o--> state_1, state_0 --i--> tb_state_0' and tb_state_0' --o'--> state_1
def build_tbucw(aut_list, accepting_states_list, alphabet, inputs, outputs, starting_player, weight_function_I, weight_function_O, dimension):
    # If aut_list is a unique ucw A, build a list containing A (in the next, we deal with an ucw list)
    if type(aut_list).__name__ != "list":
        aut_list = [aut_list]
        accepting_states_list = [accepting_states_list]

    # Initialization: count the number of states (sum of states and transitions of each automaton)
    nb_states = 0
    nb_trans = 0
    for aut in aut_list:
        nb_states += len(aut.nodes())
        for edge in aut.edges():
            nb_trans += aut.edge_weight(edge) # the weight of the edge represents the disjunction size of its label (on the turn based automaton, these disjunctions will be split into several distinct transitions)
            
    nb_automata = len(aut_list)
    if nb_automata > 1:
        nb_states = nb_states + 1 # a common initial state will be added if there are more than one automaton in aut_list
        # Count the number of outgoing transitions of the initial state
        nb_out_tran_init = 0
        for aut in aut_list:
            for neighbor in aut.neighbors("initial"):
                nb_out_tran_init += aut.edge_weight(("initial", neighbor)) # sum of number of outgoing transitions of the initial state of each automaton
           
    a = init_tbucw_c(nb_states+nb_trans)
    
    # Set the alphabet
    a = set_alphabet_c(a, alphabet)
    
    # Set the weight functions and dimension
    a = set_dimension_c(a, dimension)
    a = set_weight_function_c(a, P_I, weight_function_I)
    a = set_weight_function_c(a, P_O, weight_function_O)
    
    # Add states and transitions      
    i=0 #non turn based states counter
    j=0 #turn based states counter
    automaton_index = 0 # index of the current automaton
    nb_states_added = 0; # counter of non turn based states added for all previous automata
    if nb_automata > 1: # more than one automaton -> add a common non accepting initial state
        add_state_c(a, i, 0, nb_out_tran_init, FALSE, starting_player, TRUE, FALSE)
        set_initial_state_c(a, i) # set the initial_state_index variable of the automaton
        i += 1
        nb_states_added += 1
    for aut in aut_list:
        cur_state_local_index = 0 # index of the current state in the current automaton
        cur_nb_states_added = 0 # counter of non turn based states added for this automaton
        for cur_state in aut.nodes():
            states_to_add = [] #states and transitions will be added to the automaton_c when all successors will have been visited (Optimization_fusion)
            tran_to_add = []
            array = [] #keeps in memory the outgoing transitions already added to this state (Optimization_fusion)
            
            # Optimization1: Get the unbounded value computed before if Optimization1 is enabled
            try:
                unbounded = dict(aut.node_attributes(aut.nodes()[cur_state_local_index]))['unbounded']
            except KeyError:
                unbounded = TRUE
            
            # If there is only one automaton, its initial state has to be conserved
            if nb_automata == 1:    
                if cur_state == "initial":
                    set_initial_state_c(a, i) # set the initial_state_index variable of the automaton

            # Add cur state
            # the number of outgoing transitions is set to 1, and will be incremented each time a transition is added (goal: do not use more memory than necessery because of the fusion of transitions with same label)
            nb_pred = 0
            for pred in aut.incidents(cur_state):
                nb_pred += aut.edge_weight((pred, cur_state))
            is_accepting = accepting_states_list[automaton_index].count(cur_state) # the turn based states reachable in one step from cur_state will be set accepting (and cur_state non accepting)
            states_to_add.append((i, nb_pred, 1, FALSE, starting_player, unbounded))
            cur_nb_states_added += 1

            # Add outgoing transitions from cur_state
            for cur_succ in aut.neighbors(cur_state):
                # Index of the current successor
                cur_succ_index = (aut.nodes()).index(cur_succ) + nb_states_added # the index in the turn based automata
                
                labels_array = aut.edge_label((cur_state, cur_succ)).split("||") # split the disjunction
                for lab in labels_array: # for each disjunction, add a turn based state if necessary and 2 transitions
                    is_partition_ok = check_partition_with_label(lab, inputs, outputs)
                    if not is_partition_ok:
                        print "Partition file doesn't match the formula!"
                        exit(0)
                        
                    # Label    
                    (disj_I, disj_size_I) = convert_formula_to_proptab(lab, inputs)
                    (disj_O, disj_size_O) = convert_formula_to_proptab(lab, outputs)
                    
                    # Optimization_fusion: fusion of transitions with same label (goal: minimize number of states)
                    # Check whether there already exists a transition with that label outgoing from cur_state (if so, use it (and his associated turn based state), if not create a new one)
                    if starting_player == P_I:
                        index = check_transition(array, (disj_I, disj_size_I), len(inputs))
                    else:
                        index = check_transition(array, (disj_O, disj_size_O), len(outputs))
        
                    # Add transition(s) (and a turn based state)
                    if index == -1: #new transition
                        # if there are more than one automaton and if this is a transition starting from the initial state of aut, 
                        # this transition will be duplicated to also start from the unique initial state of the turn based automaton (-> 2 incoming transitions for this turn based state)
                        if nb_automata > 1 and cur_state == "initial":
                            states_to_add.append((nb_states+j, 2, 1, is_accepting, switch_player(starting_player), unbounded))
                        else: # otherwise, initialize it with only one incoming transition
                            states_to_add.append((nb_states+j, 1, 1, is_accepting, switch_player(starting_player), unbounded))
                        states_to_add[0] = (states_to_add[0][0], states_to_add[0][1], states_to_add[0][2]+1, states_to_add[0][3], states_to_add[0][4], states_to_add[0][5]) #increment the number of outgoing transitions of cur_state  
                         
                        if starting_player == P_I:
                            array.append((disj_I, disj_size_I)) # Optimization_fusion: add this label to the array of outgoing labels
                            tran_to_add.append((disj_I, disj_size_I, i, nb_states+j))
                            if nb_automata > 1 and cur_state == "initial": # add the duplicate transition from the unique initial state to the current turn based state
                                tran_to_add.append((disj_I, disj_size_I, 0, nb_states+j))
                            tran_to_add.append((disj_O, disj_size_O, nb_states+j, cur_succ_index))
                        else:
                            array.append((disj_O, disj_size_O)) # Optimization_fusion: add this label to the array of outgoing labels
                            tran_to_add.append((disj_O, disj_size_O, i, nb_states+j))
                            if nb_automata > 1 and cur_state == "initial": # add the duplicate transition from the unique initial state to the current turn based state
                                tran_to_add.append((disj_O, disj_size_O, 0, nb_states+j))
                            tran_to_add.append((disj_I, disj_size_I, nb_states+j, cur_succ_index))
                        j += 1
                    else: #transition already exists -> don't add a new state but use an existing one
                        # Increment the number of outgoing transitions of the current turn based state
                        states_to_add[index+1] = (states_to_add[index+1][0], states_to_add[index+1][1], states_to_add[index+1][2]+1, states_to_add[index+1][3], states_to_add[index+1][4], states_to_add[index+1][5]) 
                        
                        state_index = nb_states+j-(len(array)-index) #get the index of the right state
                        if starting_player == P_I:
                            tran_to_add.append((disj_O, disj_size_O, state_index, cur_succ_index))
                        else:
                            tran_to_add.append((disj_I, disj_size_I, state_index, cur_succ_index))
            
            # Construct states and transitions in states_to_add and tran_to_add
            for state in states_to_add:
                add_state_c(a, state[0], state[1], state[2], state[3], state[4], state[5], FALSE)
            for tran in tran_to_add:
                add_tran_c(a, tran[0], tran[1], tran[2], tran[3])      
            
            i += 1
            cur_state_local_index += 1
      
        nb_states_added += cur_nb_states_added
        automaton_index += 1

    # Duplicate transitions (fill the in_tran list of each state from the out_tran lists of all states)
    duplicate_all_tran_c(a)

    # Set the is_complete variable of each state: to True if the state has a transition for each set of propositions, to False otherwise
    set_is_complete_c(a)

    return a


#### Builds the alphabet information C structure for inputs and outputs
def build_alphabet(inputs, outputs):
    # Initialize alphabet
    alphabet = init_alphabet_c(len(inputs), len(outputs))
    
    # Add inputs and outputs
    for i in inputs :
        add_input_prop_c(alphabet, i)
    for o in outputs :
        add_output_prop_c(alphabet, o)
    
    # Compute the alphabet
    compute_alphabets_c(alphabet)
  
    return alphabet
    
#### Builds the weight functions associated to the alphabet of each player
def build_weight_functions(inputs, outputs, alphabet, mp_parameters, options):
    (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, unrea, set_of_strategies, path, filename) = options
    (inputs, outputs, dimension, values_I, values_O, values_not_I, values_not_O, nu, c_start, c_bound, c_step) = mp_parameters
    
    nu_int = [0]*dimension
    values_I_int = []
    for i in range(len(values_I)):
        values_I_int.append([0]*dimension)
    values_not_I_int = []
    for i in range(len(values_not_I)):
        values_not_I_int.append([0]*dimension)
    values_O_int = []
    for i in range(len(values_O)):
        values_O_int.append([0]*dimension)
    values_not_O_int = []
    for i in range(len(values_not_O)):
        values_not_O_int.append([0]*dimension)
    
    for d in range(dimension):
        (cur_nu, cur_mult) = nu[d]
    
        cur_nu = cur_nu*500000/cur_mult
        all_values = [cur_nu]
        for i in range(len(values_I)):
            values_I_int[i][d] = values_I[i][d]*1000000
            all_values.append(values_I_int[i][d])
        for i in range(len(values_O)):
            values_O_int[i][d] = values_O[i][d]*1000000
            all_values.append(values_O_int[i][d])
        for i in range(len(values_not_I)):
            values_not_I_int[i][d] = values_not_I[i][d]*1000000
            all_values.append(values_not_I_int[i][d])
        for i in range(len(values_not_O)):
            values_not_O_int[i][d] = values_not_O[i][d]*1000000
            all_values.append(values_not_O_int[i][d])

        pgcd = pgcdn(abs_list(all_values))
        
        if pgcd != 0:
            nu_int[d] = cur_nu*1./pgcd
            for i in range(len(values_I)):
                values_I_int[i][d] = values_I_int[i][d]*1./pgcd
            for i in range(len(values_O)):
                values_O_int[i][d] = values_O_int[i][d]*1./pgcd
            for i in range(len(values_not_I)):
                values_not_I_int[i][d] = values_not_I_int[i][d]*1./pgcd
            for i in range(len(values_not_O)):
                values_not_O_int[i][d] = values_not_O_int[i][d]*1./pgcd
           
    sigma_O_size = alphabet.contents.sigma_output_size
    prop_O_size = len(outputs)
    sigma_I_size = alphabet.contents.sigma_input_size
    prop_I_size = len(inputs)

    v_O = []
    v_I = []
    for i in range(sigma_O_size):
        v_O.append([0]*dimension)
    for i in range(sigma_I_size):
        v_I.append([0]*dimension)
    for d in range(dimension):
        for i in range(sigma_O_size):
            k=1
            for j in range(prop_O_size):
                if (i/k)%2 != 0:
                    v_O[i][d] = v_O[i][d] + values_O_int[j][d]
                else:
                    v_O[i][d] = v_O[i][d] + values_not_O_int[j][d]
                k*=2
            v_O[i][d] = (int)(v_O[i][d] - nu_int[d])
    
    for d in range(dimension):        
        for i in range(sigma_I_size):
            k=1
            for j in range(prop_I_size):
                if (i/k)%2 != 0:
                    v_I[i][d] = v_I[i][d] + values_I_int[j][d]
                else:
                    v_I[i][d] = v_I[i][d] + values_not_I_int[j][d]
                k*=2
            v_I[i][d] = (int)(v_I[i][d] - nu_int[d])
    
    if dimension > 0:
        sigma_O_string = []
        sigma_I_string = []

        for i in range(sigma_O_size):
            sigma_O_string.append("")
            k=1
            for j in range(prop_O_size):
                if (i/k)%2 != 0:
                    sigma_O_string[i] += str(outputs[j])
                else:
                    sigma_O_string[i] += "!"+str(outputs[j])
                k*=2
        for i in range(sigma_I_size):
            sigma_I_string.append("")
            k=1
            for j in range(prop_I_size):
                if (i/k)%2 != 0:
                    sigma_I_string[i] += str(inputs[j])
                else:
                    sigma_I_string[i] += "!"+str(inputs[j])
                k*=2
            
        controled_print("Cost function for Sigma_I:\n", [ALLTEXT], verbosity)
        for i in range(sigma_I_size):
            controled_print("        v("+sigma_I_string[i] + ") = " + str(v_I[i])+"\n", [ALLTEXT], verbosity)
        controled_print("\n", [ALLTEXT], verbosity)
        
        controled_print("Cost function for Sigma_O:\n", [ALLTEXT], verbosity)
        for i in range(sigma_O_size):
            controled_print("        v("+sigma_O_string[i] + ") = " + str(v_O[i])+"\n", [ALLTEXT], verbosity)
        controled_print("\n\n", [ALLTEXT], verbosity)
        
    return (convert_list_of_lists_to_c_format(v_I), convert_list_of_lists_to_c_format(v_O))

   
#### Optimization_fusion: Checks whether proptab is already represented in array. If so, returns the index of it into array. If not, appends it to array and returns -1.
def check_transition(array, proptab, nb_prop):
    i = 0
    for transition in array:
        if transition[1] == proptab[1]: #same disj_size
            for j in range(0, transition[1]):
                found = False
                for k in range(0, proptab[1]):
                    if transition[0][j*nb_prop:(j+1)*nb_prop] == proptab[0][k*nb_prop:(k+1)*nb_prop]:
                        found = True
                        break
                if not found:
                    break
    
                if j == transition[1]-1:
                    return i
        i += 1

    return -1


#### Optimization 2: Compute a fix point on the states of the tbucw_automaton to find loosing states (states such that there exists a PI strategy to visit more than K accepting states) (calls a dylib C)
def tbucw_size_optimization(automaton_c, k_value):
    nb_states = automaton_c.contents.nb_states
    
    # Detect loosing states
    c = nb_states*[0]
    for i in range(0, nb_states):
        if is_accepting_c(automaton_c, i) == TRUE:
            c[i] = 1
                   
    has_changed = True
    c_prime = nb_states*[0]
    while has_changed:
        has_changed = False
        c_prime = c[:]

        for q in range(0, nb_states):
            if is_accepting_c(automaton_c, q):
                is_accepting = 1
            else:
                is_accepting = 0
                
            if get_player_id_c(automaton_c, q) == P_O: # min_sigma(max_transition(c[succ]))
                if is_complete_c(automaton_c, q): # if q has a transition for all sets of propositions (i.e. if q is complete)
                    cur_min = k_value+2
                    for sigma_index in range(0, automaton_c.contents.alphabet.contents.sigma_output_size):
                        succ_list = get_succ_from_sigma_index_c(automaton_c, q, sigma_index)
                        cur_max = is_accepting
                        for q_prime in range(1, succ_list[0]+1):
                            cur_max = max(cur_max, c[succ_list[q_prime]] + is_accepting)
                        free_c(succ_list)
                        
                        if cur_min != k_value+2:
                            cur_min = min(cur_min, cur_max)
                        else:
                            cur_min = cur_max
                              
                    c_prime[q] = min(cur_min, k_value+1)               
            else: # max_transition(c[succ])
                succ_list = get_all_succ_c(automaton_c, q)
                cur_max = is_accepting
                for q_prime in range(1, succ_list[0]+1):
                    cur_max = max(cur_max, c[succ_list[q_prime]] + is_accepting)
                free_c(succ_list)            
                c_prime[q] = min(cur_max, k_value+1)
                
            if not has_changed and c[q] != c_prime[q]:
                has_changed = True
                
        c = c_prime[:]

    # Remove from automaton_c those loosing states (states which have counter k_value+1 in c)
    states_to_remove = (c_byte*nb_states)(FALSE)
    nb_states_to_remove = 0
    for i in range(0, nb_states):
        if c[i] == k_value+1:
            states_to_remove[i] = TRUE
            nb_states_to_remove += 1  
    
    if nb_states_to_remove > 0:    
        return optimize_tbucw_c(automaton_c, states_to_remove)

    return automaton_c 