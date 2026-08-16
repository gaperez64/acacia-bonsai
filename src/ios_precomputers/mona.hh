#pragma once


#include <bit>
#include <bddx.h>
#include <list>
#include <limits>
#include <map>
#include <numeric>
#include <spot/twa/bdddict.hh>
#include <type_traits>
#include <unordered_set>
#include <vector>

#include "solver/diagnostics.hh"
#include "utils/transition_enumerator.hh"

namespace ios_precomputers {
  namespace detail {
    template <typename Aut, typename TransSet>
    class mona {
      public:
        mona (Aut aut, bdd input_support, bdd output_support)
          : aut {aut},
            input_support {input_support},
            output_support {output_support}
        {}

        using input_to_ios_t = typename std::list<std::pair<bdd, std::list<TransSet>>>;

        auto operator() () const {

          // States are binary encoded using extra variables.
          // We allocate them via spot's bdd_dict (rather than calling
          // bdd_extvarnum directly) so the dict's internal var_refs
          // bookkeeping stays in sync. Going around the dict has been
          // observed to corrupt its state and either trigger
          // "maps are empty but var_refs is not" diagnostics, std::bad_alloc,
          // or glibc heap-corruption aborts on subsequent destruction.
          auto log_states = std::bit_width (aut->num_states ());
          auto dict = aut->get_dict ();
          // Owner token: any unique pointer scoped to this call works; we use
          // a stack-allocated dummy and pair it with an RAII guard so the
          // anonymous variables are released as soon as we are done with
          // them (all local BDDs that reference them are destroyed before
          // the guard fires, since they go out of scope at function exit).
          int owner_tag = 0;
          auto base_var = dict->register_anonymous_variables (2 * log_states, &owner_tag);
          struct anon_guard {
            spot::bdd_dict_ptr dict;
            const void* owner;
            ~anon_guard () { dict->unregister_all_my_variables (owner); }
          } guard {dict, &owner_tag};
          auto  first_output = bdd_var (output_support),
            first_src_var = base_var,
            first_dst_var = base_var + log_states;

          std::vector<int> state_vars (2 * log_states);
          std::iota (state_vars.begin (), state_vars.end (), base_var);

          // TODO Not sure if it's worth caching.
          auto encode_src = [&] (size_t s) {
            return bdd_ibuildcube (s, log_states, state_vars.data ());
          };
          auto encode_dst = [&] (size_t s) {
            return bdd_ibuildcube (s, log_states, state_vars.data () + log_states);
          };

          auto bdd_state_vars = bdd_ibuildcube (-1, 2 * log_states, state_vars.data ());

          // Create the mona BDD.
          bdd bdd_iopq = bddfalse;

          for (const auto& [formula, trans] : transition_enumerator (aut, transition_formater::src_and_dst (aut)))
            bdd_iopq = bdd_iopq |
              (formula & encode_src (trans.first) & encode_dst (trans.second));

          // When the pq part of the BDD is reached, iterate through all its
          // satisfying valuations, decode the src and dst.
          auto add_src_dst = [&] (this const auto& self, TransSet& ts, bdd src_dsts) {
            for (auto mt : minterms_of (src_dsts, bdd_state_vars)) {
              unsigned from = 0, to = 0;
              while (mt != bddtrue) {
                bool true_lit = (bdd_low (mt) == bddfalse);
                if (bdd_var (mt) < first_dst_var)
                  from = (from << 1) | true_lit;
                else
                  to = (to << 1) | true_lit;
                mt = true_lit ? bdd_high (mt) : bdd_low (mt);
              }
              ts.emplace_back (from, to);
            }
          };

          auto recurse_outputs = [&] (this const auto& self, auto& tss, bdd bdd_opq) {
            if (bdd_opq == bddfalse)
              return;
            // TODO There may be more than one way to reach this bdd_pq; cache.
            if (bdd_opq == bddtrue or bdd_var (bdd_opq) >= first_src_var)
              add_src_dst (tss.emplace_back (), bdd_opq);
            else {
              self (tss, bdd_high (bdd_opq));
              self (tss, bdd_low (bdd_opq));
            }
          };

          auto recurse_inputs = [&] (this const auto& self, auto& i_to_tss, bdd bdd_iopq, bdd bdd_input) {
            if (bdd_iopq == bddfalse)
              return;
            if (bdd_iopq == bddtrue or bdd_var (bdd_iopq) >= first_output) {
              using vt = typename std::remove_cvref_t<decltype (i_to_tss)>::value_type;
              i_to_tss.emplace_back (vt (bdd_input, {}));
              recurse_outputs (i_to_tss.back ().second, bdd_iopq);
            }
            else {
              self (i_to_tss, bdd_low (bdd_iopq), bdd_input & !bdd_ithvar (bdd_var (bdd_iopq)));
              self (i_to_tss, bdd_high (bdd_iopq), bdd_input & bdd_ithvar (bdd_var (bdd_iopq)));
            }
          };

#if ACACIA_ENABLE_DIAGNOSTICS
          if (acacia::diagnostics::enabled ()) {
            // Count DAG frontier nodes without changing the path-enumerating
            // implementation being measured.  This runs before the expensive
            // enumeration so even a child killed during action construction
            // leaves the decisive census checkpoint behind.
            auto collect_frontier = [&] (this const auto& self, bdd root,
                                         int boundary,
                                         std::unordered_set<int>& visited,
                                         std::map<int, bdd>& frontier) -> void {
              if (root == bddfalse)
                return;
              if (root == bddtrue or bdd_var (root) >= boundary) {
                frontier.emplace (root.id (), root);
                return;
              }
              if (not visited.emplace (root.id ()).second)
                return;
              self (bdd_low (root), boundary, visited, frontier);
              self (bdd_high (root), boundary, visited, frontier);
            };

            auto count_paths = [&] (this const auto& self, bdd root,
                                    int boundary,
                                    std::map<int, unsigned long long>& memo)
                -> unsigned long long {
              if (root == bddfalse)
                return 0;
              if (root == bddtrue or bdd_var (root) >= boundary)
                return 1;
              if (auto found = memo.find (root.id ()); found != memo.end ())
                return found->second;
              const auto low = self (bdd_low (root), boundary, memo);
              const auto high = self (bdd_high (root), boundary, memo);
              const auto max = std::numeric_limits<unsigned long long>::max ();
              const auto total = low > max - high ? max : low + high;
              memo.emplace (root.id (), total);
              return total;
            };

            std::unordered_set<int> input_visited;
            std::map<int, bdd> input_frontier;
            collect_frontier (bdd_iopq, first_output, input_visited, input_frontier);

            std::map<int, unsigned long long> input_path_memo;
            const auto input_paths =
                count_paths (bdd_iopq, first_output, input_path_memo);
            std::map<int, unsigned long long> output_path_memo;
            const auto output_paths =
                count_paths (bdd_iopq, first_src_var, output_path_memo);

            unsigned long long output_nodes = 0;
            for (const auto& [id, output_root] : input_frontier) {
              (void) id;
              std::unordered_set<int> output_visited;
              std::map<int, bdd> output_frontier;
              collect_frontier (output_root, first_src_var, output_visited,
                                output_frontier);
              output_nodes += output_frontier.size ();
            }

            acacia::diagnostics::set_alphabet_census (
                input_paths, input_frontier.size (), output_paths,
                output_nodes, bdd_nodecount (bdd_iopq));
            acacia::diagnostics::snapshot ("alphabet-census");
            if (acacia::diagnostics::alphabet_census_only ())
              return input_to_ios_t {};
          }
#endif

          input_to_ios_t i_to_tss;
          recurse_inputs (i_to_tss, bdd_iopq, bddtrue);
          return i_to_tss;
        }

      private:
        Aut aut;
        const bdd input_support, output_support;
    };

  }

  struct mona {
      template <typename Aut, typename TransSet = std::vector<std::pair<unsigned, unsigned>>>
      static auto make (Aut aut, bdd input_support, bdd output_support) {
        return detail::mona<Aut, TransSet> (aut, input_support, output_support);
      }
  };
}
