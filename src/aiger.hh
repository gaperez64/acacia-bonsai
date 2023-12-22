//
// Created by nils on 25/02/23.
//

#pragma once

#include <vector>
#include "utils/typeinfo.hh"

class aiger {
  public:
  aiger (const std::vector<bdd>& _inputs, const std::vector<bdd>& _latches, const std::vector<bdd>& _outputs, spot::twa_graph_ptr aut) {
    inputs = bddvec_to_idvec (_inputs); // mapping of vector<bdd> to vector<int> using bdd_var to get the AP number
    latches = bddvec_to_idvec (_latches); // '
    latches_id = std::vector<int> (_latches.size ()); // for each latch: the ID of the gate it will be equal to next step
    outputs = std::vector<int> (_outputs.size ()); // for each output: the ID of the gate it is equal to
    vi = 2 + 2 * inputs.size () + 2 * latches.size (); // first free variable index

    // maybe not the cleanest way to get the atomic propositions as a string again
    for (const bdd& b : _inputs) {
      std::stringstream ss;
      ss << spot::bdd_to_formula (b, aut->get_dict ());
      input_names.push_back (ss.str ());
    }

    for (const bdd& b : _outputs) {
      std::stringstream ss;
      ss << spot::bdd_to_formula (b, aut->get_dict ());
      output_names.push_back (ss.str ());
    }

    verb_do (2, vout << "I: " << input_names << "\n");
    verb_do (2, vout << "O: " << output_names << "\n");
  }

  // pass formula to calculate i-th latch (primed state)
  void add_latch (int i, const bdd& func) {
    latches_id[i] = bdd2aig (func);
  }

  // pass formula to calculate i-th output
  void add_output (int i, const bdd& func) {
    outputs[i] = bdd2aig (func);
  }

  // output, info prints some stuff (that is not allowed in the ascii format)
  void output (std::ostream& ost, bool info) {
    ost << "aag " << ((vi - 2) / 2) << " " << inputs.size () << " " << latches.size () << " ";
    ost << outputs.size () << " " << gates.size () << "\n";

    // inputs
    for (int i = 0; i < (int) inputs.size (); i++) {
      ost << input_index (i);
      if (info) ost << "   <- input " << i;
      ost << "\n";
    }

    // latches
    for (int i = 0; i < (int) latches.size (); i++) {
      ost << latch_index (i) << " " << latches_id[i];
      if (info) ost << "  <- latch " << i;
      ost << "\n";
    }

    // outputs
    for (int i = 0; i < (int) outputs.size (); i++) {
      ost << outputs[i];
      if (info) ost << "   <- output " << i;
      ost << "\n";
    }

    // gates
    for (const auto& gate : gates) {
      // key = {i1, i2} tuple, value = o
      ost << gate.second << " " << gate.first.first << " " << gate.first.second << "\n";
    }

    // input + output names for model checking
    for (int i = 0; i < (int) input_names.size (); i++) {
      ost << "i" << i << " " << input_names[i] << "\n";
    }

    for (int i = 0; i < (int) output_names.size (); i++) {
      ost << "o" << i << " " << output_names[i] << "\n";
    }

    verb_do (1, vout << "Aiger output: " << gates.size () << " gates\n");
  }

  private:
  std::vector<int> inputs, latches; // index i gives bdd_var(b) of i-th input or i-th state AP
  int vi; // free variable index

  std::vector<int> latches_id, outputs; // index i gives gate number for next step's state of the i-th latch, or the i-th output

  std::map<int, int> cache; // map bdd.id() to a gate number
  std::map<int, bdd> refcache; // dirty trick to keep ref counts on all bdds
                               // we are using
  std::map<std::pair<int, int>, int> gates; // map i1 x i2 to an output so we don't make gates with the same inputs

  std::vector<std::string> input_names, output_names; // names of the atomic propositions

  // inputs go from 2  to  2*inputs
  int input_index (int i) {
    return 2 + 2 * i;
  }
  // latches go from 2*inputs + 2  to  2*inputs + 2*latches
  int latch_index (int i) {
    return 2 + 2 * (int) inputs.size () + 2 * i;
  }

  // get a new unused gate number
  int get_gate () {
    vi += 2;
    return vi - 2;
  }

  int add_gate (int i1, int i2) {
    // add gate g, but only if we have to!
    if (i1 > i2) std::swap (i1, i2); // i1 should be smaller than i2

    if (i1 == 0) return 0; // false & x = false
    if (i1 == 1) return i2; // true & x = x
    if (i1 == i2) return i1; // x & x = x
    if ((i1 ^ i2) == 1) return 0; // x & !x = false

    if (gates.find ({ i1, i2 }) != gates.end ()) {
      return gates[{ i1, i2 }];
    }

    // actually create a new gate
    int n = get_gate ();
    gates[{ i1, i2 }] = n;
    return n;
  }

  std::vector<int> bddvec_to_idvec (const std::vector<bdd>& vec) {
    std::vector<int> res;
    for (const bdd& b : vec) {
      res.push_back (bdd_var (b));
    }
    return res;
  }

  // pass a bdd_var of an input or a state AP, gives the right gate number
  int bddvar_to_gate (int var) {
    int o = -2;

    // if var is an input: refer to input_index
    if (std::find (inputs.begin (), inputs.end (), var) != inputs.end ()) {
      o = input_index (std::find (inputs.begin (), inputs.end (), var) - inputs.begin ());
    }

    // if var is a state: refer to latch_index
    if (std::find (latches.begin (), latches.end (), var) != latches.end ()) {
      o = latch_index (std::find (latches.begin (), latches.end (), var) - latches.begin ());
    }

    assert (o != -2);
    return o;
  }

  // recursive bdd2aig function, can return a negated gate
  int bdd2aig (const bdd& f) {
    // base cases
    if (f == bddfalse) return 0;
    if (f == bddtrue) return 1;

    // reuse gate if we encountered this BDD before
    // doesn't actually make size smaller because no duplicate gates are allowed, but should make it faster for large BDDs
    if (cache.find (f.id ()) != cache.end ()) {
      assert (f == refcache[f.id ()]);
      return cache[f.id ()];
    }
    bdd nf = !f;
    if (cache.find (nf.id ()) != cache.end ()) {
      assert (nf == refcache[nf.id ()]);
      return cache[nf.id ()] ^ 1;
    }
    verb_do (3, vout << "Cache miss on BDD id " << f.id () << std::endl);

    // get gate with the value we're looking at + look at high/low branch
    int gate = bddvar_to_gate (bdd_var (f)); // will be an input or a latch -> a value that is already known
    bdd low = bdd_low (f);
    bdd high = bdd_high (f);

    // use recursive call on low
    int low_g = add_gate (bdd2aig (low), gate ^ 1); // low & !var

    // same as above but for high to calculate high & var
    int high_g = add_gate (bdd2aig (high), gate); // high & var

    // we have low_g = (low & !var) and high_g = (high & var),
    // now we want to AND their negations, and then invert this again to get their OR
    int output = add_gate (low_g ^ 1, high_g ^ 1) ^ 1; // !(!(low & !var) & !(high & var))

    // store in the cache for when we possibly encounter the same BDD in the future
    cache[f.id ()] = output;
    refcache[f.id ()] = f;

    return output;
  }
};

