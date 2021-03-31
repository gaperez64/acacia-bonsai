#pragma once

namespace safe_states {
  namespace detail {
    template <typename SetOfStates, typename Aut>
    class standard {
        typedef typename SetOfStates::value_type State;
      public:
        standard (Aut aut, int K, int verbose) : aut {aut}, K {K}, verbose {verbose} {}

        SetOfStates operator() () const {
          State f (aut->num_states ());

          for (unsigned src = 0; src < aut->num_states (); ++src)
            f[src] = K - 1;

          SetOfStates S (std::move (f));

          return S;
        }
      private:
        const Aut aut;
        const int K, verbose;
    };
  }

  struct standard {
      template <typename SetOfStates, typename Aut>
      static auto make (Aut aut, int K, int verbose) {
        return detail::standard<SetOfStates, Aut> (aut, K, verbose);
      }
  };
}
