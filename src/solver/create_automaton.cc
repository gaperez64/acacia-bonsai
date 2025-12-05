

#include <iostream>

#include "utils/verbose.hh"

#include "create_automaton.hh"

spot::twa_graph_ptr create_automaton(spot::formula f, spot::translator &trans) {
    // To Universal co-Büchi Automaton
    trans.set_type(spot::postprocessor::BA);
    // "Desired characteristics": Small and state-based acceptance (implied by BA).
    trans.set_pref(spot::postprocessor::Small |
                    //spot::postprocessor::Complete | // TODO: We did not need that originally; do we now?
                    spot::postprocessor::SBAcc);
    f = spot::formula::Not (f);
    verb_do (1, vout << "Formula: " << f << std::endl);
    auto aut = trans.run (&f);
    return aut;
}
