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

import sys
import subprocess
import re

from string import *
from pygraph.classes.digraph import digraph

from constants import *
from utils import *

#### reads formula (resp. formulas if nbw_constr = COMP) in filename (in Wring syntax) 
def read_formula(filename, nbw_constr):
    o = open(filename,'r')
    formula = [] # list of formulas
    spec_names = [] # list of specifications names
                
    if nbw_constr == MONO: # Monolithic construction -> one spec to extract
        spec_names.append("u0")
        form = ""
        l = o.readline()
        while l != "":
            if not l.startswith("#") and not l.startswith("[spec_unit") and not l.startswith("group_order"):
                form += l
            l = o.readline()
        formula.append(form)
        # formula.append(o.read())
        group_order = "(u0)";
    else: # Compositionnal construction -> several spec to extract
        l = o.readline()
        i = 0 # counter of formulas
        done = False
        while l != "":
            # Find first line of ######...
            while not l.startswith("[spec_unit") and l != "":
                l = o.readline()

            if l == "": # end of file -> only one spec
                print("Formula problem: [spec_unit name] pattern not found! You probably choose compositional Buchi construction while there is only one specification.")
                exit(0)
                
            spec_names.append(split(split(l, ']')[0])[1])
            
            l = o.readline() # first line of current spec
            # Get the spec
            cur_formula = ""
            while not l.startswith("[spec_unit") and not l.startswith("group_order") and l != "":
                if not l.startswith("#"):
                    cur_formula += l
                l = o.readline()
            
            formula.append(cur_formula)            
            
            # If this is the last spec, it is followed by the group_order method   
            if l.startswith("group_order"):
                group_order = l.split("=")[1].lstrip().split(";")[0]
                done = True #all spec and group_order method have been read

            if done:
                break
            
            i += 1
        if not done: #group_order line not found
            group_order = FLAT;

    return (spec_names, formula, group_order)

#### negates formula (Wring format)
def negate_formula(formula):
    (assumptions, guarantees) = extract_assumptions_and_guarantees(formula)
    
    formula = "!("
    if len(assumptions) > 0:
        formula += "("
    for i in range(len(assumptions)):
        assumptions[i] = assumptions[i].replace("assume", "")
        formula += "("+assumptions[i]+")"
        if i != len(assumptions)-1:
            formula += " * "
    if len(assumptions) > 0:
        formula += ")"
        formula += " -> "    
    
    if len(guarantees) > 0:
        formula += "("
    for i in range(len(guarantees)):
        formula += "("+guarantees[i]+")"
        if i != len(guarantees)-1:
            formula += " * "
    if len(guarantees) > 0:
        formula += ")"
    
    formula += ");\n"
    
    return formula
          
#### extract assumptions and guarantees from formula    
def extract_assumptions_and_guarantees(formula):
    lines = formula.splitlines()
    # remove comments
    formula = ""
    for l in lines:
        if l != "":
            ls = re.split(re.compile('#'),l)
            formula = formula + ls[0] + '\n'
    subformulas = re.split(re.compile(';'),formula)

    assumptions = []
    guarantees = []
    for f in subformulas:
        if re.match(re.compile('\s*assume(.|\n)*'),f):
            assumptions.append(f)
        elif re.match(re.compile('(.|\s)*\w(.|\s)*'),f):
            guarantees.append(f.lstrip("\n"))

    return (assumptions, guarantees)
            
#### formula conversion, from LTL2BA to Wring format
def ltl2ba_to_wring(formula,inputs,outputs):
    newformula = ''
    (assumptions, guarantees) = extract_assumptions_and_guarantees(formula)

    def convert_local(subform):
        subform = subform.replace('assume','')
        subform = subform.replace('[]','G')
        subform = subform.replace('<>','F')
        subform = subform.replace('||',' + ')
        subform = subform.replace('&&',' * ')
        #### careful here: inputs + outputs should contain all the inputs and outputs that occur in the formula
        for i in (inputs+outputs):
            subform = subform.replace(i+'=0','!'+i)
            subform = subform.replace(i+'=1',i)
            subform = subform.replace(i, '('+i+'=1'+')')
            subform = subform.replace('!('+i+'=1'+')', '(!('+i+'=1'+'))')
        return (subform)

    for f in assumptions:
        newformula = newformula + "assume " + convert_local(f) + ';\n'
    
    for f in guarantees:
        newformula = newformula + convert_local(f) + ';\n'

    return newformula+"\n"

#### formula conversion, from Wring to LTL2BA format
def wring_to_ltl2ba(formula,inputs,outputs):
    newformula = ''
    (assumptions, guarantees) = extract_assumptions_and_guarantees(formula)

    def convert_local(subform):
        subform = subform.replace('assume','')
        subform = subform.replace('\t',' ')
        subform = subform.replace('\n','')
        subform = subform.replace('G','[] ')
        subform = subform.replace('F','<> ')
        subform = subform.replace('+',' || ')
        subform = subform.replace('*',' && ')
        #### careful here: inputs + outputs should contain all the inputs and outputs that occur in the formula
        for i in (inputs+outputs):
            subform = subform.replace(i+'=0','!'+i)
            subform = subform.replace(i+'=1',i)
        return (subform)

    newassumptions = ''
    if assumptions.__len__() > 0:
        newassumptions = convert_local(assumptions[0])
        for f in assumptions[1:]:
            newassumptions = newassumptions + ' && (' + convert_local(f) + ')'
        newassumptions = '(' + newassumptions + ')'

    newguarantees = ''
    if guarantees.__len__() > 0:
        newguarantees = convert_local(guarantees[0])
        for f in guarantees[1:]:
            newguarantees = newguarantees + '&& (' + convert_local(f) + ')'
        newguarantees = '(' + newguarantees + ')'

    if newassumptions != '' and newguarantees != '':
        newformula = newassumptions + ' -> ' + newguarantees
    elif newassumptions != '':
        newformula = '!(' + newassumptions + ')'
    elif newguarantees != '':
        newformula = newguarantees
    else:
        print('Empty formula')
        exit(0)

    if re.match(re.compile('.*(=1|=0).*'),newformula):
        print('Partition file doesn\'t match formula!')
        exit(0)
        
    return newformula

#### Constructs an automaton from ltl2ba or ltl3ba for each formula in formulas_list
def construct_automata(formulas_list, spec_names, verbosity, tool):
    nb_formulas = len(formulas_list)
    g_list = nb_formulas*[]
    accepting_list = nb_formulas*[]
    
    controled_print("Calling " + tool + " to convert each specification to automaton\n", [ALLTEXT, MINTEXT], verbosity)
    
    if tool == LTL2BA:
        tool_cmd = [LTL2BA_PATH+"ltl2ba",'-f']
    elif tool == LTL3BA:
        tool_cmd = [LTL3BA_PATH+"ltl3ba",'-f']
    elif tool == SPOT:
        tool_cmd = [LTL2TGBA_BIN,'--spin','--deterministic','-f']
    else:
        print("Wrong tool!")
        exit(0)

    formula_index = 0
    for formula in formulas_list:
        try:
            controled_print("spec " + spec_names[formula_index] + "...", [ALLTEXT, MINTEXT], verbosity)
            controled_print("executing: " + str(tool_cmd + [formula]), [ALLTEXT, MINTEXT], verbosity)
            out = subprocess.Popen(tool_cmd+[formula],stdout=subprocess.PIPE)
            (automata,err) = out.communicate()
        except:
            print("Unexpected error:", sys.exc_info()[0])
            print("Don't forget to install " + tool + " and set the " + tool + "_PATH static variable in file constants.py.")
            exit(0)

        automata = automata.decode ('latin-1')
        controled_print(" done\n", [ALLTEXT, MINTEXT], verbosity)
        controled_print(tool + " output for " + spec_names[formula_index] + ": \n", [ALLTEXT], verbosity)
        controled_print(automata+"\n", [ALLTEXT], verbosity)

        accepting_states = []

        # automaton parsing
        s = automata.split('*/\n')
        if s.__len__()<2:
            print("empty automaton, LTL syntax error?")
            exit(0)
    
        automata = s[1]
   
        r=re.compile(';\n\}?\n?')
        transitions=re.split(r,automata)
        nb_states = len(transitions)
        nb_trans = 0
    
        # create a new graph
        g = digraph()
        g.__init__()
    
        #local function which determines if a state is initial
        def isinitial(s):
            return re.match(re.compile('.*init'),s)
    
        # iterate over transitions (except the last which is the empty string)
        l = transitions.__len__()-1
        if transitions[l] == '':
            tobound = l
        else:
            tobound = l+1
    
        for t in transitions[0:tobound]:
            # split transition into head (state) and body (rules)
            rstate = re.compile(':\n')
            trans = re.split(rstate,t)
            
            # state extraction
            s = re.split(re.compile('\Aaccept_'),trans[0])
            if len(s)==1:
                state = s[0]
                accept = False
            elif len(s)==2:
                state = s[1]
                accept = True
            if isinitial(state):
                state = 'initial'
            if accept:
                accepting_states.append(state)
            if not g.has_node(state):
                g.add_node(state)
    
            # rules extraction
            if re.match(re.compile('(.|\n)*skip(.|\n)*'), trans[1]):
                tuple = (state, state)
                g.add_edge(tuple, wt=1, label='(1)')
                nb_trans += 1
            
            elif not re.match(re.compile('(.|\n)*false(.|\n)*'),trans[1]): # if match then it's false (no transition)
                l = re.split(re.compile('::'),trans[1])
                for y in l:
                    if not re.match(re.compile('\s*if\s'),y):
                        fr = re.split(re.compile(' -> goto '), y)
                        edgelab = fr[0]
                        if tool == SPOT:
                            edgelab = edgelab.replace("true", "1")
                        if re.match(re.compile('.*accept.*'),fr[1]):
                            accept = True # the state is accepting
                        else:
                            accept = False
                        gre = re.split(re.compile('accept_'), fr[1])
                        gre = re.split(re.compile('\s+f?i?'),gre[len(gre)-1])
                        goto_state = gre[0]
                        if isinitial(goto_state):
                            goto_state = 'initial'
                        if not g.has_node(goto_state): # if the goto state is new then add it
                            g.add_node(goto_state)
                        
                        tuple = (state, goto_state)
                        disj_size = edgelab.count("||")+1
                        g.add_edge(tuple, wt=disj_size, label=edgelab)
                        nb_trans = nb_trans + 1
    
        controled_print('Nb states: %d\n' % len(g.nodes()), [ALLTEXT], verbosity)
        controled_print('Nb transitions: %d\n' % nb_trans, [ALLTEXT], verbosity)
        controled_print('Accepting states ('+str(len(accepting_states))+'): ' + str(accepting_states)+"\n\n", [ALLTEXT], verbosity)
                    
        g_list.append(g)
        accepting_list.append(accepting_states)
        formula_index += 1
    
    controled_print("\n", [ALLTEXT, MINTEXT], verbosity)

    return (g_list, accepting_list) 

#### Constructs an automaton from Wring
def construct_automata_wring(formulas, spec_names, partition, inputs, outputs, unique_id, verbosity):
    call_wring(formulas, spec_names, partition, verbosity, unique_id)
    
    nb_spec = len(formulas)
    g_list = nb_spec*[]
    accepting_list = nb_spec*[]
   
    for i in range(1, nb_spec+1):
        try:
            o = open(TMP_PATH+str(unique_id)+"/spec_"+str(i)+"/nbw.l2a", 'r')
        except IOError:
            print("Wring error: Partition file might not match the formula")
            exit(0)
        l = o.readline()
        states = []
        transitions = []
        initial_state = ""
        while l != "":
            if l.startswith("States"): # parsing states
                l = o.readline()
                while not l.startswith("Arcs") and l != "":
                   states.append(l.split(":")[0])
                   l = o.readline()
            elif l.startswith("Arcs"): # parsing transitions
                l = o.readline()
                while not l.startswith("Fair Sets") and l != "":
                    if l.startswith("->"): # detecting initial state
                        initial_state = l.split("->")[1]
                        initial_state = initial_state.strip() # remove white spaces
                        l = "  " + l[2:len(l)-1]
                    transitions.append(l)
                    l = o.readline()
            elif l.startswith("Fair Sets"): # parsing non accepting states set
                l = o.readline()
                non_accepting_states = l[1:len(l)-2].split(",")
            else:
                l = o.readline()
       
        o.close()
       
        # extracting transitions informations
        regex = re.compile("\[|\]|}|{")
        tr = []
        for tran in transitions:
            state_from = tran.split("->")[0]
            tran = tran.split("->")[1]
            state_from = state_from.strip() # remove white spaces
            tran = tran.strip() # remove white spaces
            tran_list = []
            stack = []
            start_index = 0
            for i in range(0, len(tran)):
                if re.match(regex, tran[i]):
                    if len(stack) > 0 and ((re.match('\[', stack[len(stack)-1]) and re.match('\]', tran[i])) or (re.match('{', stack[len(stack)-1]) and re.match('}', tran[i]))):
                        stack.pop()
                    else:
                        stack.append(tran[i])
                
                if (tran[i] == "," and len(stack) == 1) or len(stack) == 0:
                    tran_list.append(tran[start_index+1:i])
                    start_index = i
    
            # add all transitions from state_from
            tr.append((state_from, tran_list))
      
        # split transitions
        all_transitions = []
        for (state_from, tran_list) in tr:
            for j in range(len(tran_list)):
                tran = tran_list[j]
                stack = []
                for i in range(0, len(tran)):
                    if re.match(regex, tran[i]):
                        if len(stack) > 0 and ((re.match('\[', stack[len(stack)-1]) and re.match('\]', tran[i])) or (re.match('{', stack[len(stack)-1]) and re.match('}', tran[i]))):
                            stack.pop()
                        else:
                            stack.append(tran[i])
                    
                    if (tran[i] == "," and len(stack) == 1):
                        label = tran[2:i-1]
                        state_to = tran[i+1:len(tran)-1]
                        # labels conversion
                        if label == "":
                            label = "1"
                        else:
                            label = label.replace(',',' && ')
                            for i in (inputs+outputs):
                                label = label.replace(i+'=0','!'+i)
                                label = label.replace(i+'=1',i)
                        
                        all_transitions.append((state_from, label, state_to)) # add the transition
                        break
        
        # create a new graph
        g = digraph()
        g.__init__()
    
        for state in states:
            if state == initial_state:
                if non_accepting_states.count(state) > 0:
                    non_accepting_states.remove(state)
                    non_accepting_states.append("initial")
                state = "initial"
            g.add_node(state)
        
        for (state_from, label, state_to) in all_transitions:
            if state_from == initial_state:
                state_from = "initial"
            if state_to == initial_state:
                state_to = "initial"
                
            if not g.has_edge((state_from, state_to)):
                g.add_edge((state_from, state_to), wt=1, label=label)
            else:
                new_label = g.edge_label((state_from, state_to))
                new_label += " || " + label 
                weight = g.edge_weight((state_from, state_to)) + 1
                g.set_edge_label((state_from, state_to), new_label)
                g.set_edge_weight((state_from, state_to), weight)

        g_list.append(g)
        accepting_list.append(non_accepting_states)
     
    return (g_list, accepting_list)     

#### Calls the Wring module included to Lily-AC to convert ltl formula to automaton
def call_wring(formulas_list, spec_names, partition, verbosity, unique_id):
    controled_print('Calling Wring to convert each specification to automaton\n', [ALLTEXT, MINTEXT], verbosity)

    dir = TMP_PATH+str(unique_id)+"/"
    out = subprocess.Popen(['mkdir', dir])
    out.communicate()
    i = 0
    for f in formulas_list:
        controled_print('spec ' + spec_names[i] + "...", [ALLTEXT, MINTEXT], verbosity)
        i += 1
        
        out = subprocess.Popen(['mkdir', dir+'spec_'+str(i)])
        out.communicate()
        
        spec_file = open(dir+'spec.ltl', 'w')
        spec_file.write(f) 
        spec_file.close()
        
        out = subprocess.Popen(['lily-AC.pl', '-ltl', dir+'spec.ltl', '-syn', partition, '-v', '0', '-syndir', dir+'spec_'+str(i)],stdout=subprocess.PIPE)
        out.communicate()
        
        out = subprocess.Popen(['cp', dir+'spec.ltl', dir+'spec_'+str(i)+'/spec.ltl'])
        out.communicate()
        
        controled_print(' done\n', [ALLTEXT, MINTEXT], verbosity)
    controled_print('\n', [ALLTEXT, MINTEXT], verbosity) 
    

#### Optimization 1: computes an under approximation of the set of states which cannot carry a counter value at least k (bounded vs unbounded states)
def counters_optimization(aut, accepting_states, verbosity):
    nb_states = len(aut.nodes())
    nb_accepting_states = len(accepting_states)
    c = nb_states*[0]
    if "initial" in accepting_states:
        c[aut.nodes().index("initial")] = 1
    
    has_changed = True
    c_prime = nb_states*[0]
    while has_changed:
        has_changed = False
        c_prime = c[:]

        i = 0
        for state in aut.nodes():
            new_counter = c[i]
                        
            for pred_state in aut.incidents(state):
                if pred_state in accepting_states:
                    temp = min(nb_accepting_states+1, c[aut.nodes().index(pred_state)]+1)
                else:
                    temp = min(nb_accepting_states+1, c[aut.nodes().index(pred_state)])
              
                if(temp > new_counter):
                    new_counter = temp
                    has_changed = True        
            c_prime[i] = new_counter
            i += 1                    
        c = c_prime[:]
 
    for i in range(0, nb_states):
        if c[i] < nb_accepting_states+1:
            aut.add_node_attribute(aut.nodes()[i], ('unbounded', FALSE))
            controled_print("state %s is bounded\n" % aut.nodes()[i], [ALLTEXT], verbosity)

    return aut
