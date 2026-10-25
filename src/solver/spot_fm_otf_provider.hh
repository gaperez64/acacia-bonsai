#pragma once

#include "solver/spot_lazy_buchi_view.hh"
#include "solver/translator_options.hh"
#include <spot/twaalgos/ltl2tgba_fm.hh>

#include <spot/tl/relabel.hh>
#include <spot/twa/formula2bdd.hh>

namespace acacia::spot_fm_otf {
  struct Prepared {
      spot::formula formula;
      spot::relabeling_map labels;
  };

  // Same BA/SBAcc, High, tls-impl=1 preprocessing as create_automaton.hh
  // and Spot 2.16 translate.cc: build_simplifier()/run()/translate_via_fm().
  // Reproduced by build_scratch/otf2/probe/probe.cc:26-66, :144-153;
  // unlike that counting probe, retain the map to restore the worker alphabet.
  inline Prepared prepare (spot::formula f, const spot::bdd_dict_ptr& dict) {
    if (translation::needs_event_univ (translation::distinct_promises (f)))
      f = translation::favor_event_univ (f);
    spot::tl_simplifier_options options;
    options.containment_max_states = 64;
    options.containment_max_ops = 16;
    spot::tl_simplifier simplifier (options, dict);
    std::set<spot::formula> aps;
    spot::atomic_prop_collect (f, &aps);
    auto count = aps.size ();
    spot::relabeling_map labels;
    if (count >= 4) {
      auto quick = options;
      quick.synt_impl = false;
      quick.nenoform_stop_on_boolean = true;
      spot::tl_simplifier quick_simplifier (quick, dict);
      const auto simplified = f = quick_simplifier.simplify (f);
      bool has_boolean_sub = false;
      f.traverse ([&] (spot::formula child) {
        if (child.is_boolean () && !child.is (spot::op::ap, spot::op::Not)) {
          has_boolean_sub = true;
          return true;
        }
        return false;
      });
      if (has_boolean_sub) {
        auto relabeled = spot::relabel_bse (f, spot::Pnn, &labels);
        if (labels.size () < count) {
          count = labels.size ();
          f = relabeled;
        }
        else
          labels.clear ();
        if (count >= 8) {
          spot::relabeling_map overlap;
          relabeled = spot::relabel_overlapping_bse (simplified, spot::Pnn, &overlap);
          if (overlap.size () < count) {
            f = relabeled;
            labels = std::move (overlap);
          }
        }
      }
    }
    f = simplifier.simplify (f);
    simplifier.clear_as_bdd_cache ();
    return {f, std::move (labels)};
  }

  class Provider final : public spot::twa {
      class State final : public spot::state {
        public:
          const Provider* owner;
          spot::formula formula;
          State (const Provider* p, spot::formula f) : owner (p), formula (f) {}
          int compare (const spot::state* other) const override {
            const auto& rhs = *static_cast<const State*> (other);
            if (owner != rhs.owner)
              return std::less<const Provider*> {}(owner, rhs.owner) ? -1 : 1;
            return formula < rhs.formula ? -1 : formula == rhs.formula ? 0 : 1;
          }
          size_t hash () const override {
            return std::hash<spot::formula> {}(formula) ^ std::hash<const Provider*> {}(owner);
          }
          State* clone () const override { return new State (*this); }
      };
      class Iterator final : public spot::twa_succ_iterator {
          const Provider* owner_;
          std::vector<spot::fm_edge> edges_;
          size_t index_ = 0;

        public:
          Iterator (const Provider* p, std::vector<spot::fm_edge> edges)
            : owner_ (p),
              edges_ (std::move (edges)) {}
          bool first () override {
            index_ = 0;
            return !done ();
          }
          bool next () override {
            ++index_;
            return !done ();
          }
          bool done () const override { return index_ == edges_.size (); }
          bdd cond () const override { return edges_.at (index_).cond; }
          spot::acc_cond::mark_t acc () const override { return edges_.at (index_).acc; }
          const spot::state* dst () const override {
            return new State (owner_, edges_.at (index_).dst);
          }
      };
      spot::twa_graph_ptr metadata_;
      std::unique_ptr<spot::ltl_to_tgba_fm_otf> explorer_;
      std::unique_ptr<bddPair, decltype (&bdd_freepair)> labels_ {nullptr, bdd_freepair};
      mutable size_t expanded_ = 0;

      void check_bound () const {
        if (metadata_->num_sets () > num_sets ())
          throw spot_lazy::Declined (spot_lazy::FailureCause::integrity,
                                     "FM allocated a promise beyond N_max");
      }

    public:
      // n_max is an overflow-injection hook for tests, never a worker option.
      Provider (spot::formula worker_formula, const spot::bdd_dict_ptr& dict,
                spot_lazy::Limits limits = {}, std::optional<unsigned> n_max = {})
        : twa (dict),
          metadata_ (spot::make_twa_graph (dict)) {
        if (!worker_formula.is_ltl_formula ())
          throw spot_lazy::Declined (spot_lazy::FailureCause::applicability,
                                     "FM demand route requires LTL");
        // Freeze the outward AP inventory before FM discovers any successors.
        std::set<spot::formula> aps;
        spot::atomic_prop_collect (worker_formula, &aps);
        for (auto ap : aps)
          register_ap (ap);
        auto prepared = prepare (worker_formula, dict);
        aps.clear ();
        spot::atomic_prop_collect (prepared.formula, &aps);
        for (auto ap : aps)
          metadata_->register_ap (ap);
        if (!prepared.labels.empty ()) {
          labels_.reset (bdd_newpair ());
          for (const auto& [ap, original] : prepared.labels)
            bdd_setbddpair (labels_.get (), metadata_->register_ap (ap),
                            spot::formula_to_bdd (original, dict, this));
        }
        spot::ltl_to_tgba_fm_otf::options options;
        options.exprop = true;  // High; all other FM options match the eager worker.
        explorer_ =
            std::make_unique<spot::ltl_to_tgba_fm_otf> (prepared.formula, metadata_, options);
        // Spot 2.16 ltl2tgba_fm.cc: register_a_variable()/ltl_trad_visitor
        // allocate only P(child) for F, P(rhs) for U, and P(lhs & rhs) for M.
        // With LTL, no destination simplifier and unambiguous=false, successors
        // only recombine subformulas; a generated F(rhs) reuses its U's promise.
        // Promise normalization and the single_acc optimization only merge sets.
        // Thus distinct F/U/M nodes in orig_formula() (FM's actual NNF, before
        // canonicalization) bound every future allocation. PSL is excluded above.
        const size_t bound =
            n_max ? *n_max : translation::distinct_promises (explorer_->orig_formula ());
        if (bound > limits.max_acceptance_sets || bound > translation::acceptance_set_width ())
          throw spot_lazy::ResourceLimit ("FM acceptance budget exceeded");
        set_generalized_buchi (bound);
        prop_state_acc (false);
        check_bound ();
      }
      const spot::state* get_init_state () const override {
        return new State (this, explorer_->init_state ());
      }
      spot::twa_succ_iterator* succ_iter (const spot::state* source) const override {
        const auto* s = dynamic_cast<const State*> (source);
        if (!s || s->owner != this)
          throw spot_lazy::Declined (spot_lazy::FailureCause::integrity, "foreign FM state");
        auto edges = explorer_->succ_as_edges (s->formula);
        check_bound ();  // Before any part of a row can reach the game.
        for (auto& edge : edges) {
          // A missing/unallocated promise is fulfilled. Complement against the
          // FIXED bound, then reuse LazyBuchiView's existing level counter.
          edge.acc = acc ().comp (edge.acc);
          if (labels_)
            edge.cond = bdd_veccompose (edge.cond, labels_.get ());
        }
        std::erase_if (edges, [] (const auto& e) { return e.cond == bddfalse; });
        ++expanded_;
        return new Iterator (this, std::move (edges));
      }
      std::string format_state (const spot::state*) const override { return "FM"; }
      size_t rows_explored () const { return expanded_; }
      unsigned allocated_sets () const { return metadata_->num_sets (); }
  };
}  // namespace acacia::spot_fm_otf
