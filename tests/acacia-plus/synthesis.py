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

from pygraph.classes.digraph import *
# from pygraphviz import *
from math import *
import os

from constants import *
from utils import *
from library_linker import *
from qm import *
    
#### Converts a TransitionSystem C representing one or several winning strategies to a digraph
def convert_transition_system_into_digraph(ts, starting_player, alphabet, mp_parameters, options):
    (tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, unrea, set_of_winning_strategies, path, filename) = options
    (inputs, outputs, dimension, values_I, values_O, values_not_I, values_not_O, nu, c_start, c_bound, c_step) = mp_parameters

    # If the transition system is small enough, simplify labels of transitions
    label_simplification = False
    if ts.contents.size_states_PO <= 20:
        label_simplification = True
       
    if starting_player == P_O:
        alphabet_starting_p = alphabet.contents.output
        alphabet_starting_p_size = alphabet.contents.output_size
        alphabet_other_p = alphabet.contents.input
        alphabet_other_p_size = alphabet.contents.input_size
        first_state_index = 0
        last_state_index = ts.contents.size_states_PO
    else:
        alphabet_starting_p = alphabet.contents.input
        alphabet_starting_p_size = alphabet.contents.input_size
        alphabet_other_p = alphabet.contents.output
        alphabet_other_p_size = alphabet.contents.output_size
        first_state_index = ts.contents.size_states_PO
        last_state_index = ts.contents.size_states_PO + ts.contents.size_states_PI
        
    alphabet = (alphabet_starting_p, alphabet_starting_p_size, alphabet_other_p, alphabet_other_p_size)

    # Initialize the digraph
    g = digraph()
    g.__init__()
    
    if label_simplification == True:
        for state_index in range(first_state_index, last_state_index):
            add_state_to_digraph_with_formula_simplification(g, ts, state_index, alphabet, starting_player)
    else:
        for state_index in range(first_state_index, last_state_index):
            add_state_to_digraph(g, ts, state_index, alphabet)
    
    initial_states = convert_c_array_to_python_list(ts.contents.initial_states, ts.contents.nb_initial_states)

    for initial_state in initial_states:
        g.add_node_attribute(initial_state, ('initial', True))
    
    return g

#### Part of the convert_transition_system_into_digraph function: adds a new state to the digraph and deals its successors
def add_state_to_digraph(g, ts, state_index, alphabet):
    state_sp = get_ts_state_c(ts, state_index)

    if is_ts_state_null_c(state_sp) == FALSE:
        (alphabet_starting_p, alphabet_starting_p_size, alphabet_other_p, alphabet_other_p_size) = alphabet
    
        try:
            g.add_node(state_index)
            g.add_node_attribute(state_index, ("label", str(len(g)-1)))
        except AdditionError:
            pass

        curtran_sp = state_sp.contents.transitions
        for i in range(0, state_sp.contents.nb_tr):
            tr_sp = get_ts_transition_from_link_data_c(curtran_sp)
            label_sp = convert_proptab_to_formula(tr_sp.contents.label, alphabet_starting_p, alphabet_starting_p_size)
            state_op = get_ts_state_c(ts, tr_sp.contents.to)
            curtran_op = state_op.contents.transitions
            for j in range(0, state_op.contents.nb_tr):
                tr_op = get_ts_transition_from_link_data_c(curtran_op)
                state_to_sp = tr_op.contents.to
                
                try:
                    g.add_node(state_to_sp)
                    g.add_node_attribute(state_to_sp, ("label", str(len(g)-1)))
                except AdditionError:
                    pass
                
                label_op = convert_proptab_to_formula(tr_op.contents.label, alphabet_other_p, alphabet_other_p_size)
                label = "((" + label_sp + ") U (" + label_op + "))"
                if not g.has_edge((state_index, state_to_sp)): # new edge from predecessor to new state
                    g.add_edge((state_index, state_to_sp), 1, label)
                else: # existing edge
                    new_label = g.edge_label((state_index, state_to_sp)) + " || " + label
                    g.set_edge_label((state_index, state_to_sp), new_label)
                    
                curtran_op = curtran_op.contents.__next__
            curtran_sp = curtran_sp.contents.__next__

#### Part of the convert_transition_system_into_digraph function: adds a new state to the digraph and deals its successors
#### Labels of transition are simplified formulas with Quine-McCluskey function
def add_state_to_digraph_with_formula_simplification(g, ts, state_index, alphabet, player):
    state_sp = get_ts_state_c(ts, state_index)

    if is_ts_state_null_c(state_sp) == FALSE:
        (alphabet_starting_p, alphabet_starting_p_size, alphabet_other_p, alphabet_other_p_size) = alphabet
    
        try:
            g.add_node(state_index)
            g.add_node_attribute(state_index, ("label", str(len(g)-1)))
        except AdditionError:
            pass
        
        label_dict = dict()
        curtran_sp = state_sp.contents.transitions
        for i in range(0, state_sp.contents.nb_tr):
            tr_sp = get_ts_transition_from_link_data_c(curtran_sp)
            state_op = get_ts_state_c(ts, tr_sp.contents.to)
            curtran_op = state_op.contents.transitions
            for j in range(0, state_op.contents.nb_tr):
                tr_op = get_ts_transition_from_link_data_c(curtran_op)
                state_to_sp = tr_op.contents.to

                try:
                    g.add_node(state_to_sp)
                    g.add_node_attribute(state_to_sp, ("label", str(len(g)-1)))
                except AdditionError:
                    pass
                
                if player == P_O:
                    if j == 0:
                        label_O = convert_proptab_to_formula(tr_sp.contents.label, alphabet_starting_p, alphabet_starting_p_size)
                    cur_label_I_int = convert_proptab_to_int_value(tr_op.contents.label, alphabet_other_p_size)
                    if not g.has_edge((state_index, state_to_sp)):
                        g.add_edge((state_index, state_to_sp))
                    try:
                        label_dict[(label_O, state_to_sp)].append(cur_label_I_int)
                    except KeyError: 
                        label_dict[(label_O, state_to_sp)] = [cur_label_I_int]
                else:
                    label_O = convert_proptab_to_formula(tr_op.contents.label, alphabet_other_p, alphabet_other_p_size)
                    if not g.has_edge((state_index, state_to_sp)):
                        g.add_edge((state_index, state_to_sp))
                    if j == 0:
                        label_I_int = convert_proptab_to_int_value(tr_sp.contents.label, alphabet_starting_p_size)
                    try:
                        label_dict[(label_O, state_to_sp)].append(label_I_int)
                    except KeyError:                                            
                        label_dict[(label_O, state_to_sp)] = [label_I_int]
                
                curtran_op = curtran_op.contents.__next__
            curtran_sp = curtran_sp.contents.__next__
            
        for key in label_dict:
            (label_O, state_to) = key
            label_I = compute_transition_label(label_dict[key], player, alphabet)
            if label_I ==  "":
                label_I = "T"
            if player == P_O:
                label = "((" + label_O + ") U (" + label_I + "))"
            else:
                label = "((" + label_I + ") U (" + label_O + "))"
            if g.edge_label((state_index, state_to)) == "":
                g.set_edge_label((state_index, state_to), label)
            else:
                new_label = g.edge_label((state_index, state_to)) + " || " + label
                g.set_edge_label((state_index, state_to), new_label)
   
#### Compute the transition label from tab
def compute_transition_label(tab, player, alphabet):
    if len(tab) == 1 and tab[0] == "T":
        return ""
    
    (alphabet_starting_p, alphabet_starting_p_size, alphabet_other_p, alphabet_other_p_size) = alphabet
    if player == P_O:
        props = alphabet_other_p
        props_size = alphabet_other_p_size
    else:
        props = alphabet_starting_p
        props_size = alphabet_starting_p_size
    
    qm_result = qm(tab)
    
    label = ""
    j=0
    for conj in qm_result:
        l_diff = props_size - len(conj)
        i=0
        first = True
        if len(qm_result) > 1:
            label += "("
        while i<l_diff:
            if not first:
                label += " && "
            label += "!"+props[i]
            first = False
            i+=1
        i=0
        while i<len(conj):
            if not first and conj[i] != "X":
                label += " && "
            if conj[i] == "1":
                label += props[i+l_diff]
            elif conj[i] == "0":
                label += "!"+props[i+l_diff]
            #else: conj[i] = X and this prop is skiped
            if conj[i] != "X":
                first = False
            i+=1
        if len(qm_result) > 1:
            label += ")"
        if j != len(qm_result)-1:
            label += " || "
        j+=1
    
    return label

#### Writes the transition system representing a solution in a file
def print_solution(g, inputs, outputs, player, filename, path, verbosity):
    # Find initial states
    init = []
    for node in g.nodes():
        try:
            dict(g.node_attributes(node))['initial']
            init.append(node)
        except KeyError:
            pass

    solution = "Transition system {\n"
    
    for node in g.nodes():
        solution += "    State " + str(dict(g.node_attributes(node))["label"])
        if node in init:
            solution += ", initial:\n"
        else:
            solution += ":\n" 

        solution += "        Outgoing transitions:\n"
        for successor in g.neighbors(node):            
            solution += "            to state " + str(dict(g.node_attributes(successor))["label"]) + " labeled " + str(g.edge_label((node, successor))) + "\n"
    
    solution += "}\n"
    
    controled_print("Solution: \n", [ALLTEXT], verbosity)
    controled_print(solution+"\n", [ALLTEXT], verbosity)
    
    solutionfile = open(path+filename+".txt", "w")                       
    solutionfile.write(solution)
    solutionfile.close() 
            

# Converts the digraph (pygraph) into a AGraph (pygraphviz) to visualize the solution
#def display_solution(g, inputs, outputs, player, filename, path):
#    viz_g = AGraph(name="G", directed=True, strict=False)
#    for node in g.nodes():
#        node_attributes = dict(g.node_attributes(node))
#        lab = ""
#        try:
#            lab = node_attributes['label']
#        except KeyError:
#            pass
#        try:
#            node_attributes['initial']
#            initial = True
#        except KeyError:
#            initial = False
#        
#        if initial:
#            viz_g.add_node(node, label=lab, color='red', shape="ellipse")
#        else:
#            viz_g.add_node(node, label=lab, shape="ellipse")
#        
#    for (a, b) in g.edges():
#        viz_g.add_edge(a, b, label=g.edge_label((a, b)).replace("&&", "&") + "  ")
#            
#    try:
#        # Write in .png
#        viz_g.draw(path+filename+".png", "png", prog="dot")
#        # Write in .dot
#        viz_g.draw(path+filename+".dot", "dot", prog="dot")
#    except IOError:
#        pass
