#pragma once

namespace boolean_states {
  namespace detail {
    template <typename Aut>
    class no_boolean_states {
      public:
        no_boolean_states (Aut aut, int K, int verbose) : aut {aut}, K {K}, verbose {verbose} {}

        size_t operator() () const {
          return aut->num_states ();
        }
      private:
        const Aut aut;
        const int K, verbose;
    };
  }

  struct no_boolean_states {
      template <typename Aut>
      static auto make (Aut aut, int K, int verbose) {
        return detail::no_boolean_states<Aut> (aut, K, verbose);
      }
  };
}
