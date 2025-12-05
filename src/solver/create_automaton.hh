#pragma once

#include <spot/twa/twagraph.hh>
#include <spot/twaalgos/translate.hh>

spot::twa_graph_ptr create_automaton(spot::formula f, spot::translator &trans);
