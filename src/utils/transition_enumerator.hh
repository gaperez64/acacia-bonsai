#pragma once

// A simple enumerator of all the transitions.  It returns (cond, (src, dst)) objects.
template <typename Aut>
class transition_enumerator {
  public:
    transition_enumerator (Aut aut) : aut {aut} {}

    using value_type = std::pair<bdd, std::pair<unsigned, unsigned>>;
    typedef value_type& reference;

  private:
    Aut aut;
    class trans_it :
      public std::iterator<std::input_iterator_tag, bdd> {
      public:
        trans_it (Aut aut, unsigned state) : aut {aut}, state {state} {
          if (state < aut->num_states ())
            it = aut->out (state).begin ();
        }
        auto operator* () const {
          return std::pair (it->cond, std::pair (state, it->dst));
        }
        bool operator!= (const trans_it& rhs) const { // Minimal implementation
          return rhs.state != state;
        }

        trans_it& operator++ () {
          if (it == aut->out (state).end ()) {
            state++;
            if (state < aut->num_states ())
              it = aut->out (state).begin ();
          }
          else
            ++it;
          return *this;
        }

        Aut aut;
        decltype (aut->out (0).begin ()) it;
        unsigned state;
    };
  public:
    auto begin () const { return trans_it (aut, 0); }
    auto end () const { return trans_it (aut, aut->num_states ()); }
};
