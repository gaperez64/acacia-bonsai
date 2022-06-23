#pragma once

// A simple enumerator of all the transitions.  It returns (cond, (src, dst)) objects.
template <typename Aut, typename Formater>
class transition_enumerator {
  public:
    transition_enumerator (Aut aut, Formater formater) :
      aut {aut}, start_state {0}, one_state {false}, formater {formater} {}

    transition_enumerator (Aut aut, unsigned state, Formater formater) :
      aut {aut}, start_state {state}, one_state {true}, formater {formater} {}

    using value_type = std::pair<bdd, std::pair<unsigned, unsigned>>;
    typedef value_type& reference;

  private:
    Aut aut;
    unsigned start_state;
    bool one_state;
    Formater formater;

    class trans_it {
      public:
        using iterator_category = std::input_iterator_tag;
        using value_type = bdd;
        
        trans_it (Aut aut, unsigned state, Formater formater) : aut {aut}, state {state}, formater {formater} {
          if (state == aut->num_states ())
            return;
          it = aut->out (state).begin ();
          skip_states_with_no_succ ();
        }
        auto operator* () const {
          assert (state < aut->num_states () && it->dst < aut->num_states ());
          return std::pair (it->cond, formater (*it));
        }
        bool operator!= (const trans_it& rhs) const { // Minimal implementation
          return rhs.state != state;
        }

        trans_it& operator++ () {
          ++it;
          skip_states_with_no_succ ();
          return *this;
        }

      private:
        void skip_states_with_no_succ () {
          assert (state != aut->num_states ());
          while (it == aut->out (state).end ()) {
            state++;
            if (state == aut->num_states ())
              break;
            it = aut->out (state).begin ();
          }
        }


        Aut aut;
        decltype (aut->out (0).begin ()) it;
        unsigned state;
        Formater formater;
    };
  public:
    auto begin () const { return trans_it (aut, start_state, formater); }
    auto end () const { return trans_it (aut, one_state ? start_state + 1 : aut->num_states (), formater); }
};


namespace transition_formater {
  template <typename Aut>
  using edge_t = decltype (*(Aut()->out(0).begin ())) ;

  template <typename Aut>
  struct src_and_dst {
      src_and_dst (const Aut& aut) {}
      auto operator() (edge_t<Aut> e) const {
        return std::pair (e.src, e.dst);
      }
  };
  template <typename Aut>
  struct cond_and_dst {
      cond_and_dst (const Aut& aut) {}
      auto operator() (edge_t<Aut> e) const {
        return std::pair (e.cond, e.dst);
      }
  };
}
