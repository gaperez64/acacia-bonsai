#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

#include <bddx.h>
#include <spot/tl/delta2.hh>
#include <spot/tl/nenoform.hh>
#include <spot/twa/formula2bdd.hh>
#include <spot/twa/twagraph.hh>

namespace acacia::mp_nba {

  struct cube {
    std::vector<bdd> fin;  // Colors that must occur only finitely often.
    std::vector<bdd> inf;  // Colors that must occur infinitely often.
  };

  class cube_set {
  public:
    cube_set () = default;

    explicit cube_set (spot::bdd_dict_ptr dict)
      : dict_ (std::move (dict)) {
    }

    ~cube_set () {
      release_registration ();
    }

    cube_set (cube_set&& other) noexcept {
      take_ownership (other);
    }

    cube_set& operator= (cube_set&& other) noexcept {
      if (this != &other) {
        cubes_.clear ();
        release_registration ();
        take_ownership (other);
      }
      return *this;
    }

    cube_set (const cube_set&) = delete;
    cube_set& operator= (const cube_set&) = delete;

    const std::vector<cube>& cubes () const {
      return cubes_;
    }

    size_t size () const {
      return cubes_.size ();
    }

    bool empty () const {
      return cubes_.empty ();
    }

  private:
    void release_registration () noexcept {
      if (dict_) {
        dict_->unregister_all_my_variables (this);
        dict_.reset ();
      }
    }

    void take_ownership (cube_set& other) noexcept {
      if (other.dict_) {
        other.dict_->register_all_variables_of (&other, this);
        other.dict_->unregister_all_my_variables (&other);
      }
      dict_ = std::move (other.dict_);
      cubes_ = std::move (other.cubes_);
    }

    friend std::optional<cube_set>
    extract_cubes (spot::formula, const spot::bdd_dict_ptr&, size_t);

    spot::bdd_dict_ptr dict_;
    std::vector<cube> cubes_;
  };

  namespace detail {

    struct formula_cube {
      std::vector<spot::formula> fin;
      std::vector<spot::formula> inf;
    };

    inline bool append_clause (spot::formula clause,
                               std::vector<formula_cube>& cubes) {
      if (clause.is (spot::op::tt))
        return true;

      formula_cube result;
      const auto append_disjunct = [&result] (spot::formula disjunct) {
        if (disjunct.is (spot::op::ff))
          return true;
        if (disjunct.is (spot::op::tt))
          return false;

        if (disjunct.is (spot::op::G)
            and disjunct[0].is (spot::op::F)
            and disjunct[0][0].is_boolean ()) {
          result.fin.push_back (disjunct[0][0]);
          return true;
        }
        if (disjunct.is (spot::op::F)
            and disjunct[0].is (spot::op::G)
            and disjunct[0][0].is_boolean ()) {
          result.inf.push_back (spot::formula::Not (disjunct[0][0]));
          return true;
        }
        return false;
      };

      if (clause.is (spot::op::Or)) {
        for (spot::formula disjunct : clause) {
          if (disjunct.is (spot::op::tt))
            return true;
          if (not append_disjunct (disjunct))
            return false;
        }
      }
      else if (not append_disjunct (clause)) {
        return false;
      }

      cubes.push_back (std::move (result));
      return true;
    }

    inline void register_support (const spot::twa_graph_ptr& aut,
                                  const spot::bdd_dict_ptr& dict,
                                  bdd color) {
      bdd support = bdd_support (color);
      while (support != bddtrue) {
        const int variable = bdd_var (support);
        // bdd_dict::ap_from_var was added after Spot 2.15.1, which CI pins;
        // read the public variable table directly so both versions build.
        if (unsigned (variable) < dict->bdd_map.size ()
            and dict->bdd_map[variable].type == spot::bdd_dict::var)
          aut->register_ap (dict->bdd_map[variable].f);
        support = bdd_high (support);
      }
    }

    inline void add_edge (const spot::twa_graph_ptr& aut,
                          unsigned source, unsigned destination,
                          bdd condition,
                          spot::acc_cond::mark_t acceptance = {}) {
      if (condition != bddfalse)
        aut->new_edge (source, destination, condition, acceptance);
    }

  }  // namespace detail

  inline std::optional<cube_set>
  extract_cubes (spot::formula f, const spot::bdd_dict_ptr& dict,
                 size_t cube_cap) {
    if (not f or not dict)
      return std::nullopt;

    try {
      cube_set result {dict};
      const spot::formula normalized =
          spot::negative_normal_form (spot::to_delta2 (f));
      std::vector<detail::formula_cube> formula_cubes;

      if (normalized.is (spot::op::tt))
        return result;

      if (normalized.is (spot::op::And)) {
        for (spot::formula clause : normalized) {
          if (clause.is (spot::op::tt))
            continue;
          if (formula_cubes.size () == cube_cap
              or not detail::append_clause (clause, formula_cubes))
            return std::nullopt;
        }
      }
      else {
        if (cube_cap == 0
            or not detail::append_clause (normalized, formula_cubes))
          return std::nullopt;
      }

      result.cubes_.reserve (formula_cubes.size ());
      for (const detail::formula_cube& formula_cube : formula_cubes) {
        cube converted;
        converted.fin.reserve (formula_cube.fin.size ());
        converted.inf.reserve (formula_cube.inf.size ());
        for (spot::formula color : formula_cube.fin)
          converted.fin.push_back (
              spot::formula_to_bdd (color, dict, &result));
        for (spot::formula color : formula_cube.inf)
          converted.inf.push_back (
              spot::formula_to_bdd (color, dict, &result));
        result.cubes_.push_back (std::move (converted));
      }
      return result;
    }
    catch (...) {
      return std::nullopt;
    }
  }

  inline spot::twa_graph_ptr
  build_violation_nba (const cube_set& cubes,
                       const spot::bdd_dict_ptr& dict) {
    auto aut = spot::make_twa_graph (dict);
    const spot::acc_cond::mark_t accepting = aut->set_buchi ();
    const unsigned initial = aut->new_state ();
    aut->set_init_state (initial);

    for (const cube& cube : cubes.cubes ()) {
      for (bdd color : cube.fin)
        detail::register_support (aut, dict, color);
      for (bdd color : cube.inf)
        detail::register_support (aut, dict, color);
    }

    for (const cube& cube : cubes.cubes ()) {
      bdd safe = bddtrue;
      for (bdd color : cube.fin)
        safe &= !color;

      const unsigned uncommitted = aut->new_state ();
      aut->new_edge (initial, uncommitted, bddtrue);
      aut->new_edge (uncommitted, uncommitted, bddtrue);

      if (cube.inf.empty ()) {
        const unsigned committed = aut->new_state ();
        detail::add_edge (aut, initial, committed, safe);
        detail::add_edge (aut, uncommitted, committed, safe);
        detail::add_edge (aut, committed, committed, safe, accepting);
        continue;
      }

      const unsigned committed = aut->new_states (cube.inf.size ());
      for (size_t i = 0; i < cube.inf.size (); ++i) {
        const bdd hit = safe & cube.inf[i];
        const bdd miss = safe & !cube.inf[i];
        const bool wraps = i + 1 == cube.inf.size ();
        const unsigned next = committed + (wraps ? 0 : i + 1);
        detail::add_edge (aut, committed + i, committed + i, miss);
        detail::add_edge (aut, committed + i, next, hit,
                          wraps ? accepting : spot::acc_cond::mark_t {});
      }

      const bdd first_hit = safe & cube.inf.front ();
      const bdd first_miss = safe & !cube.inf.front ();
      const bool first_wraps = cube.inf.size () == 1;
      const unsigned first_next = committed + (first_wraps ? 0 : 1);
      for (unsigned source : {initial, uncommitted}) {
        detail::add_edge (aut, source, committed, first_miss);
        detail::add_edge (aut, source, first_next, first_hit,
                          first_wraps ? accepting
                                      : spot::acc_cond::mark_t {});
      }
    }

    return aut;
  }

}  // namespace acacia::mp_nba
