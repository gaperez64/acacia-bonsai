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

import re
from ctypes import *
from constants import *

#### Returns P_I if player == P_O, P_O otherwise
def switch_player(player):
    if player == P_I:
        return P_O
    return P_I

#### Prints test is verbosity corresponds to the req_verb
def controled_print(text, req_verb, verbosity):
    if verbosity in req_verb:
        print text,

#### Displays the execution parameters
def display_parameters(player, tool, opt, critical, verbosity, nbw_constr, chk_method, chk_dir, k_start, k_bound, k_step, tocheck, set_of_strategies):
    controled_print("Execution parameters: \n", [ALLTEXT, MINTEXT], verbosity)
    controled_print("   -Method: LTL to coBuchi translation: "+nbw_constr+", LTL synthesis: "+chk_method+",", [ALLTEXT, MINTEXT], verbosity)
    controled_print("Algorithm: "+chk_dir+"\n" , [ALLTEXT, MINTEXT], verbosity)
    controled_print("   -Options: LTL to coBuchi translation tool: "+tool+", Starting player:", [ALLTEXT, MINTEXT], verbosity)
    if player == P_O:
        controled_print("system,", [ALLTEXT, MINTEXT], verbosity)
    else:
        controled_print("environment,", [ALLTEXT, MINTEXT], verbosity)
    if verbosity == MINTEXT:
        verb = 1
    elif verbosity == ALLTEXT:
        verb = 2
    else:
        verb = 0
    controled_print("k values: from "+str(k_start)+" to "+str(k_bound)+" with incremental step of "+str(k_step)+", Verbosity: "+str(verb)+", To check: "+str(tocheck)+", To compute:", [ALLTEXT, MINTEXT], verbosity)
    if set_of_strategies == TRUE:
        controled_print("several winning strategies\n", [ALLTEXT, MINTEXT], verbosity)
    else:
        controled_print("one winning strategy\n", [ALLTEXT, MINTEXT], verbosity)
    opt1 = "off"
    opt2 = "off"
    if opt in [OPT12, OPT1]:
        opt1 = "on"
    if opt in [OPT12, OPT2]:
        opt2 = "on"  
    controled_print("   -Optimizations: Detect bounded/unbounded states: "+opt1+", Detect k-surely losing states: "+opt2+", Critical signals: "+critical+"\n\n" , [ALLTEXT, MINTEXT], verbosity)
 
         
#### Converts a boolean formula to a propositions array to create the c labels
def convert_formula_to_proptab(formula, props):
    f = formula.split('||')
    disj_size = len(f)
    nb_props = len(props)

    init_disj = ""
    for prop in props:
        init_disj += "0"
       
    result = ""
    i = 0
    for disj in f:
        if result != "":
            result += "-"
        result += init_disj
        
        if re.search(r'\b(1)\b', disj) != None:
            result = "T"
            disj_size = 1
            break
        
        j=0
        for prop in props:
            if re.search('![(]?'+prop+'[)]?'+r'\b', disj) != None:
                pos = j+i*(nb_props+1)
                result = result[0:pos]+"2"+result[pos+1:len(result)]
            
            elif re.search(r'\b'+prop+r'\b', disj) != None:
                pos = j+i*(nb_props+1)
                result = result[0:pos]+"1"+result[pos+1:len(result)]    
            j+=1
        i+=1

    # Prevents duplicated labels TODO: use BDD to improve complexity
    if disj_size > 1:
        for i in range(0, disj_size):
            j = i+1
            start_pos_i = i*(nb_props+1)
            end_pos_i = start_pos_i + nb_props
            # o1 OR o2 OR ... OR T = T => check if current element is True (i.e. nb_props*0)
            if result[start_pos_i:end_pos_i] == nb_props*"0":
                result = "T"
                disj_size = 1
                break
            start_pos_j = end_pos_i + 1
            end_pos_j = start_pos_j + nb_props
            while j < disj_size:
                if result[start_pos_i:end_pos_i] == result[j*(nb_props+1):(j+1)*(nb_props+1)-1]:
                    result = result[0:start_pos_j-1]+result[end_pos_j:len(result)]
                    disj_size -= 1
                else:
                    j += 1
                    start_pos_j += nb_props + 1
                    end_pos_j += nb_props + 1
    
    # Standardize the label true
    if result == nb_props*"0":
        result = "T"
    
    return (result, disj_size)

#### Checks if all signals in label are in the partition
def check_partition_with_label(label, inputs, outputs):
    for i in inputs:
        label = label.replace(i, "")
    for o in outputs:
        label = label.replace(o, "")

    label = label.replace("&&", "")
    label = label.replace("(", "")
    label = label.replace(")", "")
    label = label.replace("||", "")
    label = label.replace("!", "")
    label = label.replace("1", "")
    label = label.replace(" ", "")
    
    if label == "":
        return True
    else:
        return False

#### Converts a propositions array to a boolean formula to display in Moore machine
def convert_proptab_to_formula(prop_tab, props, props_size):
    if props_size == 0:
        result = "1"
    elif prop_tab[0] == "T":
        result = "1"
    else:
        result = ""
        i = 0 
        while i < props_size:
            if prop_tab[i] == "1":
                result += props[i]
            else:
                result += '!' + props[i]
            i += 1
            if i < props_size:
                result += " && "

    return result

#### Converts a propositions array to an integer value (to simplify formula)
def convert_proptab_to_int_value(prop_tab, props_size):
    if props_size == 0 or prop_tab[0] == "T": # impossible?
        return "T"
    else:
        result_string = ""
        i = 0 
        while i < props_size:
            if prop_tab[i] == "1":
                result_string += "1"
            else:
                result_string += "0"
            i += 1
            
    return int(result_string, 2)
    

#### Converts the list to a POINTER(c_int) (to pass to the dylib C)
def convert_list_to_c_format(list):
    if len(list) > 0:
        array = (c_int*len(list))(-1)
        for i in range(len(list)):
            array[i] = list[i]
    else:
        array = None
    return array

#### Converts the list to a POINTER(POINTER(c_int)) (to pass to the dylib C)
def convert_list_of_lists_to_c_format(list):
    if len(list) > 0:
        array = (POINTER(c_int)*len(list))(None)
        for i in range(len(list)):
            array[i] = convert_list_to_c_format(list[i])
    else:
        array = None
    return array

#### Converts a POINTER(c_int) (built from the dylib C) to a list
def convert_c_array_to_python_list(array, length):
    list = []
    for i in range (0, length):
        list.append(array[i])
    return list

#### Mutiplies all elements of l by n
def mult_list(l, n):
    for i in range(len(l)):
        l[i] = l[i] * n
    return l

#### Takes the absolute value of each element of l
def abs_list(l):
    for i in range(len(l)):
        l[i] = abs(l[i])
    return l

#### Computes the pgcd of two integers
def pgcd(a,b):
    while b != 0:
        a,b = b,a%b
    return a

#### Computes the pgcd of a list of integers
def pgcdn(l):
    p = pgcd(l[0], l[1])
    for x in l[2:]:
        p = pgcd(p, x)
    return p

#### Puts all fractions of the list to the same denominator
def to_same_denominator(l):
    max_denom = 0
    for e in l:
        (num, denom) = e
        if denom > max_denom:
            max_denom = denom
            
    new_l = []
    for e in l:
        (num, denom) = e
        new_l.append(num * (max_denom/denom))
        
    return (new_l, max_denom)

#### Converts to a string a list of fractions represented by pairs (numerator, denominator)
def fractions_list_to_string(l):
    res = "["
    i=0
    for frac in l:
        i+=1
        (num, denom) = frac
        res += str(num)+"/"+str(denom)
        if i < len(l):
            res += ", "
    res += "]"
    return res

#### Inverts a dict (source: Recipe 4.14 in the Python Cookbook, 2nd Edition)
def invert_dict(d):
    return dict([(v, k) for k, v in d.iteritems()])
    