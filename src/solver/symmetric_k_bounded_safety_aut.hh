#pragma once

// Experimental decision-only symmetry-quotient K-bounded safety solver.
//
// This wires the already-tested symmetry building blocks into a live CPre
// loop. It is deliberately conservative in two ways:
//   - synthesis is not supported here; callers must fall back for -s;
//   - if the quotient loop does not prove a win, callers should fall back to
//     the existing explicit antichain solver.

#include "actioners/direction.hh"
#include "actioners/standard.hh"
#include "configuration.hh"
#include "posets/utils/vector_mm.hh"
#include "posets/vectors.hh"
#include "solver/symmetric_blocks.hh"
#include "solver/symmetric_conversion.hh"
#if ACACIA_SYMMETRY_DENSE_SIMD
# include "solver/symmetric_dense_downset.hh"
#endif
#include "solver/symmetric_downset.hh"
#include "solver/symmetric_profile.hh"
#include "solver/symmetry.hh"
#include "utils/verbose.hh"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include <bddx.h>
#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>

#ifndef ACACIA_SYMMETRY_MAX_INPUT_REPS
# define ACACIA_SYMMETRY_MAX_INPUT_REPS 2048
#endif

#ifndef ACACIA_SYMMETRY_MAX_OUTPUT_LETTERS
# define ACACIA_SYMMETRY_MAX_OUTPUT_LETTERS 4096
#endif

#ifndef ACACIA_SYMMETRY_MAX_PRE_WORK
# define ACACIA_SYMMETRY_MAX_PRE_WORK 1024
#endif

#ifndef ACACIA_SYMMETRY_MAX_TOTAL_PRE_WORK
# define ACACIA_SYMMETRY_MAX_TOTAL_PRE_WORK 8192
#endif

#ifndef ACACIA_SYMMETRY_MAX_TI_SIZE
# define ACACIA_SYMMETRY_MAX_TI_SIZE 128
#endif

#ifndef ACACIA_SYMMETRY_MAX_OUTPUT_REPS
# define ACACIA_SYMMETRY_MAX_OUTPUT_REPS 4096
#endif

#ifndef ACACIA_SYMMETRY_UNIONO_HYBRID_EXTRA
# define ACACIA_SYMMETRY_UNIONO_HYBRID_EXTRA 4
#endif

namespace acacia::solver_detail::symmetric {

  using raw_state = posets::utils::vector_mm<VECTOR_ELT_T>;
  using count_vector = symmetric_downset::count_vector;
  using transset = std::vector<std::pair<int, int>>;
  using ios_for_input = std::vector<transset>;
  using inputs_to_ios = std::vector<std::pair<bdd, ios_for_input>>;
  using action = std::vector<std::pair<unsigned, bool>>;
  using action_vec = std::vector<action>;
  using action_vecs = std::vector<action_vec>;

  struct representative_input_meta {
      bdd input = bddfalse;
      std::vector<unsigned> input_type_counts;
      size_t raw_output_letters = 0;
      size_t output_rep_letters = 0;
      bool output_rep_capped = false;
      action_vecs output_rep_actions;
  };

  struct representative_io_data {
      inputs_to_ios raw;
      std::vector<representative_input_meta> meta;
  };

  namespace detail {

    inline bool support_contains_var (bdd support, int var) {
      const bdd v = bdd_ithvar (var);
      return bdd_exist (support, v) != support;
    }

    inline std::vector<bdd> enumerate_letters (bdd support, size_t cap) {
      std::vector<bdd> letters;
      bdd remaining = bddtrue;
      while (remaining != bddfalse) {
        if (letters.size () >= cap)
          return {};
        bdd letter = bdd_satoneset (remaining, support, bddtrue);
        letters.push_back (letter);
        remaining -= letter;
      }
      return letters;
    }

    inline transset compute_transset (const spot::twa_graph_ptr& aut, bdd letter) {
      transset ts;
      for (size_t p = 0; p < aut->num_states (); ++p) {
        for (const auto& e : aut->out (p)) {
          if ((e.cond & letter) != bddfalse)
            ts.push_back (std::pair ((int) p, (int) e.dst));
        }
      }
      return ts;
    }

    inline bool build_client_family_vars (
        const spot::twa_graph_ptr& aut, const symmetry::group& G, const symmetry::block_layout& L,
        bool want_input,
        std::vector<std::vector<int>>& family_slot_vars, std::set<int>& indexed_input_vars) {
      auto dict = aut->get_dict ();
      for (const auto& [prefix, fam] : G.families) {
        if (fam.is_input != want_input)
          continue;
        std::vector<int> slot_vars (L.num_clients, -1);
        for (unsigned slot = 0; slot < L.num_clients; ++slot) {
          const long idx = L.slot_to_index[slot];
          auto it = fam.idx2ap.find (idx);
          if (it == fam.idx2ap.end ())
            return false;
          const int var = dict->varnum (it->second);
          slot_vars[slot] = var;
          indexed_input_vars.insert (var);
        }
        family_slot_vars.push_back (std::move (slot_vars));
      }
      return not family_slot_vars.empty ();
    }

    inline std::vector<int> shared_vars (const spot::twa_graph_ptr& aut, bdd support,
                                         const std::set<int>& indexed_vars) {
      std::vector<int> vars;
      auto dict = aut->get_dict ();
      for (const spot::formula& ap : aut->ap ()) {
        const int var = dict->varnum (ap);
        if (indexed_vars.contains (var))
          continue;
        if (support_contains_var (support, var))
          vars.push_back (var);
      }
      std::sort (vars.begin (), vars.end ());
      return vars;
    }

    inline void enumerate_type_counts_rec (unsigned type, unsigned num_types, unsigned remaining,
                                           std::vector<unsigned>& counts,
                                           std::vector<std::vector<unsigned>>& out,
                                           size_t cap) {
      if (out.size () >= cap)
        return;
      if (type + 1 == num_types) {
        counts[type] = remaining;
        out.push_back (counts);
        return;
      }
      for (unsigned c = 0; c <= remaining; ++c) {
        counts[type] = c;
        enumerate_type_counts_rec (type + 1, num_types, remaining - c, counts, out, cap);
        if (out.size () >= cap)
          return;
      }
    }

    inline std::vector<std::vector<unsigned>> enumerate_type_counts (unsigned num_clients,
                                                                     unsigned num_types,
                                                                     size_t cap) {
      std::vector<std::vector<unsigned>> out;
      std::vector<unsigned> counts (num_types, 0);
      enumerate_type_counts_rec (0, num_types, num_clients, counts, out, cap);
      return out;
    }

    inline size_t mul_capped (size_t a, size_t b, size_t cap) {
      if (a == 0 or b == 0)
        return 0;
      if (a > cap / b)
        return cap + 1;
      const size_t res = a * b;
      return res > cap ? cap + 1 : res;
    }

    inline size_t binomial_capped (unsigned n, unsigned k, size_t cap) {
      if (k > n)
        return 0;
      k = std::min (k, n - k);
      size_t res = 1;
      for (unsigned i = 1; i <= k; ++i) {
        const unsigned numerator = n - k + i;
        if (res > cap / numerator)
          return cap + 1;
        res *= numerator;
        res /= i;
        if (res > cap)
          return cap + 1;
      }
      return res;
    }

    inline size_t distribution_count_capped (unsigned items, unsigned types, size_t cap) {
      if (types == 0)
        return 0;
      if (items == 0 or types == 1)
        return 1;
      return binomial_capped (items + types - 1, types - 1, cap);
    }

    inline size_t output_representative_count_capped (
        const std::vector<unsigned>& input_type_counts, unsigned output_type_count,
        unsigned shared_output_vars, size_t cap) {
      size_t total = 1;
      for (unsigned bucket_size : input_type_counts) {
        const size_t bucket_count =
            distribution_count_capped (bucket_size, output_type_count, cap);
        total = mul_capped (total, bucket_count, cap);
        if (total > cap)
          return cap + 1;
      }
      if (shared_output_vars >= sizeof (size_t) * 8)
        return cap + 1;
      total = mul_capped (total, ((size_t) 1) << shared_output_vars, cap);
      return total > cap ? cap + 1 : total;
    }

    inline std::vector<std::vector<unsigned>> bucket_slots_from_type_counts (
        const std::vector<unsigned>& input_type_counts) {
      std::vector<std::vector<unsigned>> buckets;
      buckets.reserve (input_type_counts.size ());
      unsigned slot = 0;
      for (unsigned count : input_type_counts) {
        std::vector<unsigned> bucket;
        bucket.reserve (count);
        for (unsigned i = 0; i < count; ++i)
          bucket.push_back (slot++);
        buckets.push_back (std::move (bucket));
      }
      return buckets;
    }

    inline void fill_bucket_output_types (std::vector<unsigned>& output_slot_types,
                                          const std::vector<unsigned>& bucket_slots,
                                          const std::vector<unsigned>& output_type_counts) {
      size_t pos = 0;
      for (unsigned type = 0; type < output_type_counts.size (); ++type) {
        for (unsigned i = 0; i < output_type_counts[type]; ++i)
          output_slot_types[bucket_slots[pos++]] = type;
      }
    }

    inline void enumerate_output_type_assignments_rec (
        unsigned bucket_idx, const std::vector<std::vector<unsigned>>& buckets,
        const std::vector<std::vector<std::vector<unsigned>>>& bucket_distributions,
        std::vector<unsigned>& current, std::vector<std::vector<unsigned>>& out, size_t cap,
        bool& capped) {
      if (capped)
        return;
      if (bucket_idx == buckets.size ()) {
        if (out.size () >= cap) {
          capped = true;
          return;
        }
        out.push_back (current);
        return;
      }
      for (const auto& dist : bucket_distributions[bucket_idx]) {
        auto next = current;
        fill_bucket_output_types (next, buckets[bucket_idx], dist);
        enumerate_output_type_assignments_rec (bucket_idx + 1, buckets, bucket_distributions,
                                               next, out, cap, capped);
        if (capped)
          return;
      }
    }

    inline std::optional<std::vector<std::vector<unsigned>>> output_type_assignments (
        const std::vector<unsigned>& input_type_counts, unsigned output_type_count, size_t cap) {
      if (output_type_count == 0)
        return std::nullopt;
      const size_t client_count = [&] {
        size_t n = 0;
        for (unsigned c : input_type_counts)
          n += c;
        return n;
      } ();
      auto buckets = bucket_slots_from_type_counts (input_type_counts);
      std::vector<std::vector<std::vector<unsigned>>> bucket_distributions;
      bucket_distributions.reserve (buckets.size ());
      for (const auto& bucket : buckets) {
        auto dists = enumerate_type_counts ((unsigned) bucket.size (), output_type_count, cap + 1);
        if (dists.size () > cap)
          return std::nullopt;
        bucket_distributions.push_back (std::move (dists));
      }

      std::vector<std::vector<unsigned>> out;
      std::vector<unsigned> current (client_count, 0);
      bool capped = false;
      enumerate_output_type_assignments_rec (0, buckets, bucket_distributions, current, out, cap,
                                             capped);
      if (capped)
        return std::nullopt;
      return out;
    }

    inline bdd input_letter_from_counts (const std::vector<std::vector<int>>& family_slot_vars,
                                         const std::vector<int>& shared_vars,
                                         const std::vector<unsigned>& type_counts,
                                         unsigned shared_mask) {
      bdd letter = bddtrue;
      unsigned slot = 0;
      for (unsigned type = 0; type < type_counts.size (); ++type) {
        for (unsigned n = 0; n < type_counts[type]; ++n, ++slot) {
          for (unsigned fam = 0; fam < family_slot_vars.size (); ++fam) {
            const bdd v = bdd_ithvar (family_slot_vars[fam][slot]);
            letter &= ((type >> fam) & 1U) ? v : !v;
          }
        }
      }
      for (unsigned i = 0; i < shared_vars.size (); ++i) {
        const bdd v = bdd_ithvar (shared_vars[i]);
        letter &= ((shared_mask >> i) & 1U) ? v : !v;
      }
      return letter;
    }

    inline bdd output_letter_from_types (const std::vector<std::vector<int>>& family_slot_vars,
                                         const std::vector<int>& shared_vars,
                                         const std::vector<unsigned>& output_slot_types,
                                         unsigned shared_mask) {
      bdd letter = bddtrue;
      for (unsigned slot = 0; slot < output_slot_types.size (); ++slot) {
        const unsigned type = output_slot_types[slot];
        for (unsigned fam = 0; fam < family_slot_vars.size (); ++fam) {
          const bdd v = bdd_ithvar (family_slot_vars[fam][slot]);
          letter &= ((type >> fam) & 1U) ? v : !v;
        }
      }
      for (unsigned i = 0; i < shared_vars.size (); ++i) {
        const bdd v = bdd_ithvar (shared_vars[i]);
        letter &= ((shared_mask >> i) & 1U) ? v : !v;
      }
      return letter;
    }

    inline action_vec compute_action_vec (const spot::twa_graph_ptr& aut, const transset& ts) {
      action_vec ret (aut->num_states ());
      for (const auto& [p, q] : ts)
        ret[q].push_back (std::make_pair ((unsigned) p, aut->state_is_accepting (q)));
      return ret;
    }

    inline representative_input_meta make_meta (
        const spot::twa_graph_ptr& aut, bdd input, const std::vector<unsigned>& input_type_counts,
        size_t raw_output_letters, const std::vector<std::vector<int>>& output_family_slot_vars,
        const std::vector<int>& shared_output_vars) {
      ACACIA_SYMMETRY_PROFILE_SCOPE (output_representatives);
      representative_input_meta meta;
      meta.input = input;
      meta.input_type_counts = input_type_counts;
      meta.raw_output_letters = raw_output_letters;

#if ACACIA_SYMMETRY_OPTIMIZE_UNIONO || ACACIA_SYMMETRY_UNIONO_SPIKE
      if (output_family_slot_vars.size () >= sizeof (unsigned) * 8 or
          shared_output_vars.size () >= sizeof (unsigned) * 8) {
        meta.output_rep_capped = true;
        meta.output_rep_letters = ACACIA_SYMMETRY_MAX_OUTPUT_REPS + 1;
        return meta;
      }
      const unsigned output_type_count = 1U << (unsigned) output_family_slot_vars.size ();
      const auto expected = output_representative_count_capped (
          input_type_counts, output_type_count, (unsigned) shared_output_vars.size (),
          ACACIA_SYMMETRY_MAX_OUTPUT_REPS);
      if (expected > ACACIA_SYMMETRY_MAX_OUTPUT_REPS) {
        meta.output_rep_capped = true;
        meta.output_rep_letters = expected;
        return meta;
      }

      auto assignments =
          output_type_assignments (input_type_counts, output_type_count,
                                   ACACIA_SYMMETRY_MAX_OUTPUT_REPS + 1);
      if (not assignments.has_value ()) {
        meta.output_rep_capped = true;
        meta.output_rep_letters = ACACIA_SYMMETRY_MAX_OUTPUT_REPS + 1;
        return meta;
      }

      const unsigned shared_assignments = 1U << (unsigned) shared_output_vars.size ();
      std::set<action_vec> unique_actions;
      for (unsigned shared_mask = 0; shared_mask < shared_assignments; ++shared_mask) {
        for (const auto& output_slot_types : *assignments) {
          if (meta.output_rep_letters >= ACACIA_SYMMETRY_MAX_OUTPUT_REPS) {
            meta.output_rep_capped = true;
            meta.output_rep_letters = ACACIA_SYMMETRY_MAX_OUTPUT_REPS + 1;
            meta.output_rep_actions.clear ();
            return meta;
          }
          bdd output =
              output_letter_from_types (output_family_slot_vars, shared_output_vars,
                                        output_slot_types, shared_mask);
          ++meta.output_rep_letters;
          unique_actions.insert (compute_action_vec (aut, compute_transset (aut, input & output)));
        }
      }
      meta.output_rep_actions.assign (unique_actions.begin (), unique_actions.end ());
#else
      (void) aut;
      (void) output_family_slot_vars;
      (void) shared_output_vars;
#endif
      return meta;
    }

    inline std::optional<representative_io_data> representative_inputs_to_ios (
        const spot::twa_graph_ptr& aut, bdd input_support, bdd output_support,
        const symmetry::group& G, const symmetry::block_layout& L) {
      ACACIA_SYMMETRY_PROFILE_SCOPE (representative_io);
      std::vector<std::vector<int>> family_slot_vars;
      std::set<int> indexed_input_vars;
      if (not build_client_family_vars (aut, G, L, true, family_slot_vars, indexed_input_vars))
        return std::nullopt;

      if (family_slot_vars.size () >= sizeof (unsigned) * 8)
        return std::nullopt;
      const unsigned num_types = 1U << (unsigned) family_slot_vars.size ();
      auto type_counts = enumerate_type_counts (L.num_clients, num_types,
                                                ACACIA_SYMMETRY_MAX_INPUT_REPS + 1);
      if (type_counts.size () > ACACIA_SYMMETRY_MAX_INPUT_REPS)
        return std::nullopt;

      auto shared_input_vars = shared_vars (aut, input_support, indexed_input_vars);
      if (shared_input_vars.size () >= sizeof (unsigned) * 8)
        return std::nullopt;
      const unsigned shared_assignments = 1U << (unsigned) shared_input_vars.size ();
      if ((size_t) shared_assignments * type_counts.size () > ACACIA_SYMMETRY_MAX_INPUT_REPS)
        return std::nullopt;

      auto output_letters = enumerate_letters (output_support, ACACIA_SYMMETRY_MAX_OUTPUT_LETTERS);
      if (output_letters.empty ())
        return std::nullopt;

      std::vector<std::vector<int>> output_family_slot_vars;
      std::set<int> indexed_output_vars;
      build_client_family_vars (aut, G, L, false, output_family_slot_vars, indexed_output_vars);
      auto shared_output_vars = shared_vars (aut, output_support, indexed_output_vars);

      representative_io_data data;
      data.raw.reserve ((size_t) shared_assignments * type_counts.size ());
      data.meta.reserve ((size_t) shared_assignments * type_counts.size ());
      for (unsigned shared_mask = 0; shared_mask < shared_assignments; ++shared_mask) {
        for (const auto& counts : type_counts) {
          bdd input =
              input_letter_from_counts (family_slot_vars, shared_input_vars, counts, shared_mask);
          ios_for_input ios;
          ios.reserve (output_letters.size ());
          for (bdd output : output_letters)
            ios.push_back (compute_transset (aut, input & output));
          data.meta.push_back (make_meta (aut, input, counts, output_letters.size (),
                                          output_family_slot_vars, shared_output_vars));
          data.raw.push_back (std::pair (input, std::move (ios)));
        }
      }
      return data;
    }

    inline raw_state safe_raw_state (const spot::twa_graph_ptr& aut, VECTOR_ELT_T k) {
      raw_state safe (aut->num_states (), k - 1);
      for (size_t i = posets::vectors::bool_threshold; i < aut->num_states (); ++i)
        safe[i] = 0;
      return safe;
    }

    inline raw_state init_raw_state (const spot::twa_graph_ptr& aut) {
      raw_state init (aut->num_states ());
      init.assign (aut->num_states (), -1);
      init[aut->get_init_state_number ()] = 0;
      return init;
    }

    inline count_vector increment_k (const symmetry::block_layout& L, const count_vector& c,
                                     VECTOR_ELT_T kinc) {
      count_vector out;
      out.shared.resize (c.shared.size ());
      for (size_t i = 0; i < c.shared.size (); ++i) {
        const unsigned state = L.shared_states[i];
        out.shared[i] =
            (state < posets::vectors::bool_threshold) ? (c.shared[i] + kinc) : 0;
      }
      for (const auto& [t, cnt] : c.counts) {
        symmetric_downset::type_t nt (t.size ());
        for (unsigned b = 0; b < t.size (); ++b) {
          const unsigned state = L.block_slot_state[b][0];
          nt[b] = (state < posets::vectors::bool_threshold) ? (t[b] + kinc) : 0;
        }
        out.counts[nt] += cnt;
      }
      return out;
    }

    inline const representative_input_meta* find_meta (const representative_io_data& data,
                                                       bdd input) {
      const int id = input.id ();
      for (const auto& meta : data.meta)
        if (meta.input.id () == id)
          return &meta;
      return nullptr;
    }

    inline size_t pre_work_units (size_t f_size, size_t action_count) {
      const size_t keys = symmetry::candidate_split_keys ().size ();
      const size_t max = std::numeric_limits<size_t>::max ();
      if (f_size != 0 and action_count > max / f_size)
        return max;
      size_t work = f_size * action_count;
      if (keys != 0 and work > max / keys)
        return max;
      return work * keys;
    }

    inline bool charge_pre_work (size_t work, size_t& remaining) {
#if ACACIA_SYMMETRY_MAX_TOTAL_PRE_WORK == 0
      (void) work;
      (void) remaining;
      return true;
#else
      if (work > remaining)
        return false;
      remaining -= work;
      return true;
#endif
    }

  }  // namespace detail

  inline size_t output_representative_count (
      const std::vector<unsigned>& input_type_counts, unsigned output_type_count,
      unsigned shared_output_vars, size_t cap = ACACIA_SYMMETRY_MAX_OUTPUT_REPS) {
    return detail::output_representative_count_capped (input_type_counts, output_type_count,
                                                       shared_output_vars, cap);
  }

  inline std::optional<std::vector<std::vector<unsigned>>>
  output_type_assignments_for_input_counts (const std::vector<unsigned>& input_type_counts,
                                            unsigned output_type_count,
                                            size_t cap = ACACIA_SYMMETRY_MAX_OUTPUT_REPS) {
    return detail::output_type_assignments (input_type_counts, output_type_count, cap);
  }

  inline void add_maximal (std::vector<count_vector>& antichain, count_vector candidate) {
    if (symmetric_downset::contains (antichain, candidate))
      return;
    antichain.erase (
        std::remove_if (antichain.begin (), antichain.end (),
                        [&] (const count_vector& existing) {
                          return symmetric_downset::dominates (candidate, existing);
                        }),
        antichain.end ());
    antichain.push_back (std::move (candidate));
  }

  inline std::vector<count_vector> quotient_union_with (const std::vector<count_vector>& A,
                                                        const std::vector<count_vector>& B) {
#if ACACIA_SYMMETRY_DENSE_SIMD
    return symmetric_dense_downset::union_with (A, B);
#else
    return symmetric_downset::union_with (A, B);
#endif
  }

  inline std::vector<count_vector> quotient_intersect_with (const std::vector<count_vector>& A,
                                                            const std::vector<count_vector>& B) {
#if ACACIA_SYMMETRY_DENSE_SIMD
    return symmetric_dense_downset::intersect_with (A, B);
#else
    return symmetric_downset::intersect_with (A, B);
#endif
  }

  template <typename Actions, typename Actioner>
  inline std::optional<std::vector<count_vector>>
  pre_for_input (const std::vector<count_vector>& f, const Actions& actions, Actioner& actioner,
                 const symmetry::block_layout& L) {
    ACACIA_SYMMETRY_PROFILE_SCOPE (pre_for_input);
    std::vector<count_vector> candidates;
    const auto& keys = symmetry::candidate_split_keys ();
    const size_t work = detail::pre_work_units (f.size (), actions.size ());
    if (work > ACACIA_SYMMETRY_MAX_PRE_WORK)
      return std::nullopt;
    candidates.reserve (std::min (work, (size_t) ACACIA_SYMMETRY_MAX_TI_SIZE + 1));
    raw_state raw (L.num_states);
    std::vector<symmetric_downset::type_t> expanded;
    for (const auto& u : f) {
      for (const auto& key : keys) {
        {
          ACACIA_SYMMETRY_PROFILE_SCOPE (realize);
          symmetry::realize_into (L, u, key, raw, expanded);
        }
        for (const auto& action_vec : actions) {
          raw_state pre;
          {
            ACACIA_SYMMETRY_PROFILE_SCOPE (action_apply);
            pre = actioner.apply (raw, action_vec, actioners::direction::backward);
          }
          count_vector counted;
          {
            ACACIA_SYMMETRY_PROFILE_SCOPE (count_conversion);
            counted = symmetry::to_count_vector (L, pre);
          }
          {
            ACACIA_SYMMETRY_PROFILE_SCOPE (dominance_union);
            acacia::solver_detail::symmetric::add_maximal (candidates, std::move (counted));
          }
          if (candidates.size () > ACACIA_SYMMETRY_MAX_TI_SIZE)
            return std::nullopt;
        }
      }
    }
    return candidates;
  }

  inline std::optional<std::pair<VECTOR_ELT_T, std::vector<count_vector>>> solve (
      spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax, const VECTOR_ELT_T& kmin,
      const VECTOR_ELT_T& kinc, const bdd& all_inputs, const bdd& all_outputs,
      const symmetry::group& G, const symmetry::block_layout& L) {
    ACACIA_SYMMETRY_PROFILE_SCOPE (solve_total);
    auto data = detail::representative_inputs_to_ios (aut, all_inputs, all_outputs, G, L);
    if (not data.has_value () or data->raw.empty ())
      return std::nullopt;

    VECTOR_ELT_T k = kmin;
    auto actioner = [&] {
      ACACIA_SYMMETRY_PROFILE_SCOPE (actioner_build);
      return actioners::standard<raw_state>::make (aut, data->raw, k);
    } ();
    auto& rep_actions = actioner.actions ();
    if (rep_actions.empty ())
      return std::nullopt;

    std::vector<count_vector> f = {symmetry::to_count_vector (L, detail::safe_raw_state (aut, k))};
    const count_vector init = symmetry::to_count_vector (L, detail::init_raw_state (aut));
    size_t remaining_pre_work = ACACIA_SYMMETRY_MAX_TOTAL_PRE_WORK;

    int loopcount = 0;
    while (true) {
      ++loopcount;
      bool changed = false;
      verb_do (1, vout << "[symmetry] Loop# " << loopcount << ", f of size " << f.size ()
                       << ", representative inputs=" << rep_actions.size () << std::endl);

      bool incremented = false;
      for (auto& input_and_actions : rep_actions) {
        auto& [input, actions] = input_and_actions;
        verb_do (2, vout << "[symmetry] representative input "
                         << spot::bdd_to_formula (input, aut->get_dict ()) << " with "
                         << actions.size () << " output actions\n");

        const auto* meta = detail::find_meta (*data, input);
        bool rep_pre_computed = false;
        std::optional<std::vector<count_vector>> rep_pre_result;
#if ACACIA_SYMMETRY_UNIONO_SPIKE
        if (meta != nullptr) {
          const auto& keys = symmetry::candidate_split_keys ();
          const size_t raw_work = f.size () * actions.size () * keys.size ();
          const size_t rep_work =
              meta->output_rep_actions.empty ()
                  ? 0
                  : f.size () * meta->output_rep_actions.size () * keys.size ();
          const size_t hybrid_actions =
              std::min (actions.size (),
                        meta->output_rep_actions.size () + ACACIA_SYMMETRY_UNIONO_HYBRID_EXTRA);
          const size_t hybrid_work = f.size () * hybrid_actions * keys.size ();
          utils::vout << "[symmetry][union_o_spike] raw_letters=" << meta->raw_output_letters
                      << " raw_actions=" << actions.size ()
                      << " rep_letters=" << meta->output_rep_letters
                      << (meta->output_rep_capped ? "+(capped)" : "")
                      << " rep_actions=" << meta->output_rep_actions.size ()
                      << " raw_work=" << raw_work
                      << " rep_work=" << rep_work
                      << " hybrid_actions=" << hybrid_actions
                      << " hybrid_work=" << hybrid_work;

          if (not meta->output_rep_capped and not meta->output_rep_actions.empty ()) {
            rep_pre_result = pre_for_input (f, meta->output_rep_actions, actioner, L);
            rep_pre_computed = true;
            if (rep_pre_result.has_value ())
              utils::vout << " rep_Ti=" << rep_pre_result->size ();
            else
              utils::vout << " rep_Ti=capped";
          }
          utils::vout << "\n";
        }
#endif

        std::optional<std::vector<count_vector>> f1i;
#if ACACIA_SYMMETRY_OPTIMIZE_UNIONO
        if (meta != nullptr and not meta->output_rep_capped and
            not meta->output_rep_actions.empty ()) {
          verb_do (2, vout << "[symmetry] union_o using output representatives "
                           << meta->output_rep_actions.size () << "/" << actions.size ()
                           << "\n");
          const size_t rep_work =
              detail::pre_work_units (f.size (), meta->output_rep_actions.size ());
          if (rep_work <= ACACIA_SYMMETRY_MAX_PRE_WORK and
              not detail::charge_pre_work (rep_work, remaining_pre_work)) {
            verb_do (1, vout << "[symmetry] aggregate quotient work budget exceeded; "
                                "falling back\n");
            return std::nullopt;
          }
          if (rep_pre_computed)
            f1i = std::move (rep_pre_result);
          else
            f1i = pre_for_input (f, meta->output_rep_actions, actioner, L);
          if (not f1i.has_value ()) {
            verb_do (1, vout << "[symmetry] output-representative work budget exceeded; "
                                "falling back\n");
            return std::nullopt;
          }
        }
        else {
          verb_do (2, vout << "[symmetry] union_o using raw output actions\n");
        }
#endif
        if (not f1i.has_value ()) {
          const size_t raw_work = detail::pre_work_units (f.size (), actions.size ());
          if (raw_work <= ACACIA_SYMMETRY_MAX_PRE_WORK and
              not detail::charge_pre_work (raw_work, remaining_pre_work)) {
            verb_do (1, vout << "[symmetry] aggregate quotient work budget exceeded; "
                                "falling back\n");
            return std::nullopt;
          }
          f1i = pre_for_input (f, actions, actioner, L);
        }
        if (not f1i.has_value ()) {
          verb_do (1, vout << "[symmetry] work budget exceeded; falling back\n");
          return std::nullopt;
        }
        std::vector<count_vector> next;
        {
          ACACIA_SYMMETRY_PROFILE_SCOPE (intersect);
          next = quotient_intersect_with (f, *f1i);
        }
        if (next != f)
          changed = true;
        f = std::move (next);

        if (not symmetric_downset::contains (f, init)) {
          if (k >= kmax) {
            verb_do (1, vout << "[symmetry] initial state out at max K\n");
            return std::nullopt;
          }
          verb_do (1, vout << "[symmetry] Incrementing k from " << (int) k << " to "
                           << (int) (k + kinc) << std::endl);
          k += kinc;
          actioner.setK (k);
          std::vector<count_vector> widened;
          widened.reserve (f.size ());
          for (const auto& c : f)
            widened.push_back (detail::increment_k (L, c, kinc));
          {
            ACACIA_SYMMETRY_PROFILE_SCOPE (k_increment_union);
            f = quotient_union_with (widened, {});
          }
          incremented = true;
          break;
        }
      }

      if (incremented)
        continue;
      if (not changed) {
        verb_do (1, vout << "[symmetry] fixed point reached at K=" << (int) k
                         << ", f of size " << f.size () << std::endl);
        return std::make_pair (k, std::move (f));
      }
    }
  }

  inline std::optional<bool> try_solve (spot::twa_graph_ptr aut, const VECTOR_ELT_T& kmax,
                                        const VECTOR_ELT_T& kmin, const VECTOR_ELT_T& kinc,
                                        const bdd& all_inputs, const bdd& all_outputs,
                                        bool do_synthesis) {
    if (do_synthesis)
      return std::nullopt;

    profile::global ().reset ();

    symmetry::group G;
    {
      ACACIA_SYMMETRY_PROFILE_SCOPE (detect);
      G = symmetry::largest_full_symmetric_subgroup (
          symmetry::detect (aut, all_inputs, all_outputs));
    }
    if (not G.full_symmetric) {
      profile::global ().report ();
      return std::nullopt;
    }
    std::optional<symmetry::block_layout> L;
    {
      ACACIA_SYMMETRY_PROFILE_SCOPE (block_layout);
      L = symmetry::compute_block_layout (G, aut->num_states ());
    }
    if (not L.has_value ()) {
      profile::global ().report ();
      return std::nullopt;
    }

    verb_do (1, vout << "[symmetry] trying quotient solver: generators=" << G.size ()
                     << " clients=" << G.indices.size () << " blocks=" << L->num_blocks
                     << " shared=" << L->shared_states.size () << std::endl);
    auto res = solve (aut, kmax, kmin, kinc, all_inputs, all_outputs, G, *L);
    profile::global ().report ();
    if (res.has_value ())
      return true;
    return false;
  }

}  // namespace acacia::solver_detail::symmetric
