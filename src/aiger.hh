//
// Created by nils on 25/02/23.
//

#pragma once

#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>
#include <vector>

struct and_gate
{
    int o, i1, i2;
};

class aiger
{
public:
    aiger(const std::vector<bdd>& _inputs, const std::vector<bdd>& _latches, int _outputs)
    {
        inputs = bddvec_to_idvec(_inputs); // mapping of vector<bdd> to vector<int> using bdd_var to get the AP number
        latches = bddvec_to_idvec(_latches); // '
        latches_id = std::vector<int>(_latches.size()); // for each latch: the ID of the gate it will be equal to next step
        outputs = std::vector<int>(_outputs); // for each output: the ID of the gate it is equal to
        vi = 2 + 2*inputs.size() + 2*latches.size(); // first free variable index
    }

    // pass formula to calculate i-th latch (primed state)
    void add_latch(int i, const bdd& func)
    {
        latches_id[i] = bdd2aig(func);
    }

    // pass formula to calculate i-th output
    void add_output(int i, const bdd& func)
    {
        outputs[i] = bdd2aig(func);
    }

    // output, info prints some stuff (that is not allowed in the ascii format)
    void output(std::ostream& ost, bool info)
    {
        ost << "aag " << ((vi-2)/2) << " " << inputs.size() << " " << latches.size() << " ";
        ost << outputs.size() << " " << gates.size() << "\n";

        // inputs
        for(int i = 0; i < inputs.size(); i++)
        {
            ost << input_index(i);
            if (info) ost << "   <- input " << i;
            ost << "\n";
        }

        // latches
        for(int i = 0; i < latches.size(); i++)
        {
            ost << latch_index(i) << " " << latches_id[i] ;
            if (info) ost << "  <- latch " << i;
            ost << "\n";
        }

        // outputs
        for(int i = 0; i < outputs.size(); i++)
        {
            ost << outputs[i];
            if (info) ost << "   <- output " << i;
            ost << "\n";
        }

        // gates
        for(const and_gate& gate: gates)
        {
            ost << gate.o << " " << gate.i1 << " " << gate.i2 << "\n";
        }
    }

private:
    std::vector<int> inputs, latches; // index i gives bdd_var(b) of i-th input or i-th state AP
    int vi; // free variable index

    std::vector<int> latches_id, outputs; // index i gives gate number for next step's state of the i-th latch, or the i-th output
    std::vector<and_gate> gates; // all gates

    std::map<int, int> cache; // map bdd.id() to a gate number to reuse gates

    // inputs go from 2  to  2*inputs
    int input_index(int i)
    {
        return 2 + 2*i;
    }
    // latches go from 2*inputs + 2  to  2*inputs + 2*latches
    int latch_index(int i)
    {
        return 2 + 2*(int)inputs.size() + 2*i;
    }

    // get a new gate
    int get_gate()
    {
        vi += 2;
        return vi-2;
    }

    std::vector<int> bddvec_to_idvec(const std::vector<bdd>& vec)
    {
        std::vector<int> res;
        for(const bdd& b: vec)
        {
            res.push_back(bdd_var(b));
        }
        return res;
    }

    // pass a bdd_var of an input or a state AP, gives the right gate number
    int bddvar_to_gate(int var)
    {
        int o = -2;
        // if var is an input: refer to input_index
        // if var is a state: refer to latch_index
        if (std::find(inputs.begin(), inputs.end(), var) != inputs.end())
        {
            o = input_index(std::find(inputs.begin(), inputs.end(), var) - inputs.begin());
        }

        if (std::find(latches.begin(), latches.end(), var) != latches.end())
        {
            o = latch_index(std::find(latches.begin(), latches.end(), var) - latches.begin());
        }

        assert(o != -2);
        return o;
    }

    // recursive bdd2aig function, can return a negated gate
    int bdd2aig(const bdd& f)
    {
        // base cases
        if (f == bddtrue)
        {
            return 1;
        }
        if (f == bddfalse)
        {
            return 0;
        }

        // use cache if we can
        if (cache.find(f.id()) != cache.end())
        {
            return cache[f.id()];
        }

        // get gate with the value we're looking at + look at high/low branch
        int gate = bddvar_to_gate(bdd_var(f));
        bdd low = bdd_low(f);
        bdd high = bdd_high(f);

        // recursive call on low
        int low_g = bdd2aig(low);
        // goal: get gate low_g to contain low & !var
        if (low_g >= 2) // low_g is not true/false
        {
            int tmp = low_g;
            low_g = get_gate();
            gates.push_back({low_g, tmp, gate | 1}); // low & !var
        }
        else if (low_g == 1)
        {
            // true & (gate | 1) -> low_g = gate | 1
            low_g = gate | 1;
        }
        // low_g == 0: false & (gate | 1) -> low_g = false = 0, and it's already 0

        // same cases as above but for high to calculate high & var
        int high_g = bdd2aig(high);
        if (high_g >= 2) // not true or false
        {
            int tmp = high_g;
            high_g = get_gate();
            gates.push_back({high_g, tmp, gate}); // high & var
        }
        else if (high_g == 1)
        {
            high_g = gate;
        }


        // we have "low & !var" and "high & var", now we want to AND their negations, and then invert this again to get their OR
        int output_unnegated; // will be the gate that does not yet have the final negation

        // if they are both not true/false: make a gate that ANDs their negations
        if ((low_g >= 2) && (high_g >= 2))
        {
            output_unnegated = get_gate();
            gates.push_back({output_unnegated, low_g ^ 1, high_g ^ 1}); // !(low & !var) & !(high & var)
        }
        else if (low_g >= 2) // high_g is either true or false: don't need a gate
        {
            // o = low_g^1 & z       z = true/false = high_g^1
            if (high_g == 0)
            {
                // low_g^1 & true
                output_unnegated = low_g ^ 1;
            }
            else
            {
                // low_g^1 & false
                output_unnegated = 0;
            }
        }
        else if (high_g >= 2) // same case as above but for low_g being true or false
        {
            // o = high_g^1 & z       z = true/false = low_g^1
            if (low_g == 0)
            {
                output_unnegated = high_g ^ 1;
            }
            else
            {
                output_unnegated = 0;
            }
        }
        else
        {
            output_unnegated = (low_g^1) & (high_g^1);
            utils::vout << "it happens!\n";
            // does this ever happen?
        }

        /*
        int output = get_gate();
        gates.push_back({output, output_unnegated ^ 1, output_unnegated ^ 1}); // !(!(low & !var) & !(high & var)) = (low & !var) | (high & var)
        */

        int output = output_unnegated ^ 1; // negate this, store in the cache
        cache[f.id()] = output;

        return output;
    }
};