#pragma once

#include "ios_precomputers/alphabet_census.hh"
#include "solver/diagnostics.hh"
#include "solver/equivariance_budget.hh"
#include "utils/transition_enumerator.hh"
#include <type_traits>
#include <unordered_set>

#include <bddx.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <spot/twa/bdddict.hh>
#include <vector>

namespace ios_precomputers {
  namespace detail {
    /// `quotient` keeps only the first output path that reaches each residual
    /// BDD node at the state-variable frontier.  BuDDy nodes are canonical, so
    /// two paths reaching the same node denote the same endpoint relation and
    /// decode to identical transition sets; CPre unions over outputs and union
    /// is idempotent, so dropping the repeats cannot change the fixed point.
    ///
    /// Order is load-bearing and must be first-occurrence.  `input_pickers`
    /// scans an input's actions in order, breaks at the first action that keeps
    /// the state in the region, and splices that action to the front, so a
    /// sorted or hashed order is a different algorithm.  First-occurrence order
    /// is a subsequence of the order the traversal already produces.
    template <typename Aut, typename TransSet, bool quotient = false>
    class mona {
      public:
        mona (Aut aut, bdd input_support, bdd output_support)
          : aut {aut},
            input_support {input_support},
            output_support {output_support} {}

        using input_to_ios_t = typename std::list<std::pair<bdd, std::list<TransSet>>>;

        auto operator() () const {
          if constexpr (quotient)
            if (auto result = explicit_letters ())
              return std::move (*result);
          return symbolic ();
        }

        auto symbolic () const {
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
          auto first_src_var = base_var, first_dst_var = base_var + log_states;
          // Projection removes declared APs that do not occur in the
          // automaton. If every output is unused, output_support is true and
          // has no root variable; the output segment is then simply empty.
          auto first_output = output_support == bddtrue ? first_src_var : bdd_var (output_support);

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

          for (const auto& [formula, trans] :
               transition_enumerator (aut, transition_formater::src_and_dst (aut))) {
            acacia::equivariance_budget::checkpoint ();
            bdd_iopq = bdd_iopq | (formula & encode_src (trans.first) & encode_dst (trans.second));
          }

          // When the pq part of the BDD is reached, iterate through all its
          // satisfying valuations, decode the src and dst.
          auto add_src_dst = [&] (TransSet& ts, bdd src_dsts) {
            std::unique_ptr<mintermEnumerator, decltype (&bdd_free_minterm)> mt (
                bdd_init_minterm (src_dsts, bdd_state_vars), bdd_free_minterm);
            if (not mt)
              throw std::bad_alloc ();
            for (int more = bdd_first_minterm (mt.get ()); more;
                 more = bdd_next_minterm (mt.get ())) {
              acacia::equivariance_budget::checkpoint ();
              unsigned from = 0, to = 0;
              for (int i = 0; i < mt->nvars; ++i) {
                // BuDDy encodes false as ~var, true as var, in BDD level order.
                const int var = mt->vars[i];
                auto& endpoint = (var < 0 ? ~var : var) < first_dst_var ? from : to;
                endpoint = (endpoint << 1) | (var >= 0);
              }
              ts.emplace_back (from, to);
            }
          };

#if ACACIA_ENABLE_DIAGNOSTICS
          // `decoded` is how many transition sets this expansion built;
          // `unique_decoded` is how many distinct residual relations those
          // decodes covered, per input class.  A quotient must decode exactly
          // the second number, so the pair is what validates stage A1.
          unsigned long long decoded = 0, unique_decoded = 0;
          const bool count_residuals = quotient or acacia::diagnostics::semantic_decode_census ();
#endif

          // Residual roots already decoded for the input class being expanded.
          // Only the quotient consults it; the diagnostics build also fills it
          // to count what a quotient would save.
          std::unordered_set<int> residual_roots;

          auto recurse_outputs = [&] (this const auto& self, auto& tss, bdd bdd_opq) {
            acacia::equivariance_budget::checkpoint ();
            if (bdd_opq == bddfalse)
              return;
            if (bdd_opq == bddtrue or bdd_var (bdd_opq) >= first_src_var) {
              if constexpr (quotient) {
                if (not residual_roots.emplace (bdd_opq.id ()).second)
                  return;
              }
#if ACACIA_ENABLE_DIAGNOSTICS
              ++decoded;
              // Under the quotient the set is already updated above, and every
              // decode is by construction a first occurrence.
              if constexpr (quotient)
                ++unique_decoded;
              else if (count_residuals and residual_roots.emplace (bdd_opq.id ()).second)
                ++unique_decoded;
#endif
              add_src_dst (tss.emplace_back (), bdd_opq);
            }
            else {
              self (tss, bdd_high (bdd_opq));
              self (tss, bdd_low (bdd_opq));
            }
          };

          auto recurse_inputs = [&] (this const auto& self, auto& i_to_tss, bdd bdd_iopq,
                                     bdd bdd_input) {
            acacia::equivariance_budget::checkpoint ();
            if (bdd_iopq == bddfalse)
              return;
            if (bdd_iopq == bddtrue or bdd_var (bdd_iopq) >= first_output) {
              using vt = typename std::remove_cvref_t<decltype (i_to_tss)>::value_type;
              i_to_tss.emplace_back (vt (bdd_input, {}));
              // Per input class: two inputs reaching the same residual root
              // each decode it, because each input owns its own action list.
              // Guarded so the shipping build, which neither quotients nor
              // counts, does not touch the set at all.
              if constexpr (quotient)
                residual_roots.clear ();
#if ACACIA_ENABLE_DIAGNOSTICS
              else if (count_residuals)
                residual_roots.clear ();
#endif
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
            record_alphabet_census (bdd_iopq, first_output, first_src_var);
            if (acacia::diagnostics::alphabet_census_only ())
              return input_to_ios_t {};
          }
#endif

          acacia::legacy_phase decoding ("io-decoding");
          input_to_ios_t i_to_tss;
#if ACACIA_ENABLE_DIAGNOSTICS
          const auto decode_started = acacia::diagnostics::clock::now ();
#endif
          recurse_inputs (i_to_tss, bdd_iopq, bddtrue);
#if ACACIA_ENABLE_DIAGNOSTICS
          acacia::diagnostics::set_decode_census (
              decoded, unique_decoded,
              (unsigned long long) std::chrono::duration_cast<std::chrono::milliseconds> (
                  acacia::diagnostics::clock::now () - decode_started)
                  .count ());
#endif
          record_payload (i_to_tss);
          return i_to_tss;
        }

        std::optional<input_to_ios_t> explicit_letters () const {
#if ACACIA_ENABLE_DIAGNOSTICS
          // The relation census still needs the original MONA relation.
          if (acacia::diagnostics::enabled ())
            return std::nullopt;
#endif
          using endpoint = std::pair<unsigned, unsigned>;
          // Fixed pilot admission: at most 12 active APs and 128 MiB for guard
          // rows, CSR plus final endpoints, and conservative metadata allowances.
          constexpr size_t byte_limit = 128 * 1024 * 1024;
          acacia::equivariance_budget::checkpoint ();
          if (bdd_getreorder_method () != BDD_REORDER_NONE)
            return std::nullopt;
          for (int var = 0; var < bdd_varnum (); ++var)
            if (bdd_var2level (var) != var)
              return std::nullopt;
          size_t bytes = 0, endpoint_bound = 0;
          auto add_bytes = [&] (size_t count, size_t width) {
            if (width and count > (byte_limit - bytes) / width)
              return false;
            bytes += count * width;
            return true;
          };
          // Bound metadata before collecting unique guards; round floating point
          // satisfying-letter counts upward for a conservative endpoint bound.
          if (not add_bytes (aut->num_edges (), 128) or
              not add_bytes (aut->num_states (), sizeof (unsigned)))
            return std::nullopt;
          std::map<int, size_t> ids;
          std::vector<bdd> guards;
          bdd active = bddtrue;
          for (unsigned src = 0; src < aut->num_states (); ++src)
            for (const auto& edge : aut->out (src)) {
              acacia::equivariance_budget::checkpoint ();
              auto [it, added] = ids.emplace (edge.cond.id (), guards.size ());
              if (added) {
                guards.push_back (edge.cond);
                active &= bdd_support (edge.cond);
              }
            }
          std::vector<int> vars;
          for (bdd rest = active; rest != bddtrue; rest = bdd_high (rest)) {
            if (vars.size () == 12)
              return std::nullopt;
            vars.push_back (bdd_var (rest));
          }
          const size_t aps = vars.size ();
          bool output_seen = false;
          for (size_t j = 0; j < vars.size (); ++j) {
            const auto var = bdd_ithvar (vars[j]);
            const bool input = (input_support & var) == input_support;
            if (input and output_seen)
              return std::nullopt;
            if (not input) {
              if ((output_support & var) != output_support)
                return std::nullopt;
              output_seen = true;
            }
          }
          // BuDDy counts over all global variables before dividing out inactive
          // ones. Overflow there can turn a valid small count into a spurious 1.
          if (bdd_varnum () >= std::numeric_limits<double>::max_exponent)
            return std::nullopt;
          const size_t letters = size_t {1} << aps;
          const size_t words = (letters + 63) / 64;
          std::vector<size_t> satisfying (guards.size ());
          for (size_t g = 0; g < guards.size (); ++g) {
            acacia::equivariance_budget::checkpoint ();
            const double count = guards[g] == bddfalse ? 0 : guards[g] == bddtrue ? letters :
                                     bdd_satcountset (guards[g], active);
            if (not (count >= 0 and count <= letters))
              return std::nullopt;
            satisfying[g] = size_t (std::ceil (count));
          }
          // Each edge may emit one endpoint for each satisfying letter. Count all
          // duplicates as an upper bound, and budget BOTH CSR and final endpoints.
          for (unsigned src = 0; src < aut->num_states (); ++src)
            for (const auto& edge : aut->out (src)) {
              acacia::equivariance_budget::checkpoint ();
              const auto count = satisfying[ids.at (edge.cond.id ())];
              if (not add_bytes (count, 2 * sizeof (endpoint)))
                return std::nullopt;
              endpoint_bound += count;
            }
          if (not add_bytes (guards.size (), words * sizeof (uint64_t)) or
              not add_bytes (letters + 1, 3 * sizeof (size_t) + 1024))
            return std::nullopt;

          const auto width = std::max (1, std::bit_width (unsigned (letters - 1)));
          int owner = 0;
          const auto dict = aut->get_dict ();
          struct owner_guard {
              spot::bdd_dict_ptr dict;
              const void* owner;
              ~owner_guard () { dict->unregister_all_my_variables (owner); }
          } cleanup {dict, &owner};
          const int base = dict->register_anonymous_variables (width, &owner);
          for (bdd support : {input_support, output_support, active})
            for (; support != bddtrue; support = bdd_high (support))
              if (bdd_var (support) >= base)
                return std::nullopt;
          for (int var = base; var < base + width; ++var)
            if (bdd_var2level (var) != var)
              return std::nullopt;
          std::vector<int> id_vars (width);
          std::iota (id_vars.begin (), id_vars.end (), base);

          acacia::legacy_phase decoding ("io-decoding");
          std::vector<uint64_t> masks (guards.size () * words);
          for (size_t g = 0; g < guards.size (); ++g) {
            std::unique_ptr<mintermEnumerator, decltype (&bdd_free_minterm)> mt (
                bdd_init_minterm (guards[g], active), bdd_free_minterm);
            if (not mt)
              throw std::bad_alloc ();
            size_t enumerated = 0;
            for (int more = bdd_first_minterm (mt.get ()); more;
                 more = bdd_next_minterm (mt.get ())) {
              acacia::equivariance_budget::checkpoint ();
              size_t letter = 0;
              for (int j = 0; j < mt->nvars; ++j)
                letter = (letter << 1) | (mt->vars[j] >= 0);
              masks[g * words + letter / 64] |= uint64_t {1} << (letter % 64);
              ++enumerated;
            }
            if (enumerated > satisfying[g])
              throw std::runtime_error ("guard count mismatch");
          }
          auto visit = [&] (auto emit) {
            for (unsigned src = 0; src < aut->num_states (); ++src)
              for (const auto& edge : aut->out (src)) {
                acacia::equivariance_budget::checkpoint ();
                const auto g = ids.at (edge.cond.id ());
                for (size_t w = 0; w < words; ++w) {
                  auto bits = masks[g * words + w];
                  while (bits) {
                    const size_t letter = w * 64 + std::countr_zero (bits);
                    emit (letter, endpoint {src, edge.dst});
                    bits &= bits - 1;
                  }
                }
              }
          };
          std::vector<size_t> offsets (letters + 1);
          visit ([&] (size_t letter, endpoint) { ++offsets[letter + 1]; });
          std::partial_sum (offsets.begin (), offsets.end (), offsets.begin ());
          if (offsets.back () > endpoint_bound)
            throw std::runtime_error ("endpoint count mismatch");
          std::vector<endpoint> endpoints (offsets.back ());
          auto cursor = offsets;
          visit ([&] (size_t letter, endpoint edge) { endpoints[cursor[letter]++] = edge; });
          for (size_t letter = 0; letter < letters; ++letter) {
            auto first = endpoints.begin () + offsets[letter];
            auto last = endpoints.begin () + offsets[letter + 1];
            std::sort (first, last);
            cursor[letter] = std::unique (first, last) - first;
          }
          using row = std::span<const endpoint>;
          const auto less = [] (row a, row b) {
            return std::lexicographical_compare (a.begin (), a.end (), b.begin (), b.end ());
          };
          std::map<row, bdd, decltype (less)> ids_by_row;
          std::map<int, row> rows_by_id;
          std::vector<bdd> roots (letters, bddfalse);
          for (size_t letter = 0; letter < letters; ++letter) {
            acacia::equivariance_budget::checkpoint ();
            const auto endpoints_for_letter =
                row (endpoints).subspan (offsets[letter], cursor[letter]);
            if (endpoints_for_letter.empty ())
              continue;
            auto [it, added] = ids_by_row.emplace (endpoints_for_letter, bddfalse);
            if (added) {
              it->second = bdd_ibuildcube (ids_by_row.size () - 1, width, id_vars.data ());
              rows_by_id.emplace (it->second.id (), endpoints_for_letter);
            }
            roots[letter] = it->second;
          }
          // An injective terminal encoding leaves AP cofactors equal exactly when
          // MONA's endpoint relations are equal. BuDDy supplies the same reductions;
          // the low-first input / high-first output walk below therefore stays exact.
          size_t count = letters;
          for (size_t j = vars.size (); j-- > 0;) {
            for (size_t p = 0; p < count / 2; ++p)
              roots[p] = bdd_ite (bdd_ithvar (vars[j]), roots[2 * p + 1], roots[2 * p]);
            count /= 2;
          }
          const int first_output = output_support == bddtrue ? base : bdd_var (output_support);
          std::unordered_set<int> emitted;
          auto visit_outputs = [&] (this const auto& self, auto& actions, bdd node) {
            acacia::equivariance_budget::checkpoint ();
            if (node == bddfalse)
              return;
            if (bdd_var (node) >= base) {
              if (emitted.emplace (node.id ()).second) {
                const auto r = rows_by_id.at (node.id ());
                actions.emplace_back (r.begin (), r.end ());
              }
            }
            else {
              self (actions, bdd_high (node));
              self (actions, bdd_low (node));
            }
          };
          input_to_ios_t result;
          auto visit_inputs = [&] (this const auto& self, bdd node, bdd input) {
            acacia::equivariance_budget::checkpoint ();
            if (node == bddfalse)
              return;
            if (bdd_var (node) >= first_output) {
              emitted.clear ();
              result.emplace_back (input, std::list<TransSet> {});
              visit_outputs (result.back ().second, node);
            }
            else {
              self (bdd_low (node), input & !bdd_ithvar (bdd_var (node)));
              self (bdd_high (node), input & bdd_ithvar (bdd_var (node)));
            }
          };
          visit_inputs (roots[0], bddtrue);
          record_payload (result);
          return result;
        }

      private:
        static void record_payload (const input_to_ios_t& result) {
          if (acacia::phase_records_enabled ()) {
            size_t bytes = sizeof (result), sets = 0, endpoints = 0;
            for (const auto& [input, outputs] : result) {
              (void) input;
              bytes += sizeof (typename input_to_ios_t::value_type);
              sets += outputs.size ();
              for (const auto& transitions : outputs) {
                bytes += sizeof (transitions) +
                         transitions.capacity () * sizeof (typename TransSet::value_type);
                endpoints += transitions.size ();
              }
            }
            // Append-only decoded containers: accounted payload peaks at retention.
            acacia::legacy_count ("decoded_set_peak_payload_bytes_estimate", bytes);
            acacia::legacy_count ("decoded_set_payload_bytes_estimate", bytes);
            acacia::legacy_count ("decoded_sets", sets);
            acacia::legacy_count ("decoded_endpoints", endpoints);
          }
        }

        Aut aut;
        const bdd input_support, output_support;
    };

  }

  struct mona {
      // MONA encodes endpoints as bit pairs; carrying acceptance here needs a
      // separate design from the transition payload used by other precomputers.
      template <typename Aut, typename TransSet = std::vector<std::pair<unsigned, unsigned>>>
      static auto make (Aut aut, bdd input_support, bdd output_support) {
        return detail::mona<Aut, TransSet, false> (aut, input_support, output_support);
      }
  };
}
