#pragma once

namespace bounded_states {
  namespace detail {
    template <typename Aut>
    class no_bounded_states {
      public:
        no_bounded_states (Aut aut, int K, int verbose) : aut {aut}, K {K}, verbose {verbose} {}

        size_t operator() () const {
          return 0;
        }
      private:
        const Aut aut;
        const int K, verbose;
    };
  }

  struct no_bounded_states {
      template <typename Aut>
      static auto make (Aut aut, int K, int verbose) {
        return detail::no_bounded_states<Aut> (aut, K, verbose);
      }
  };
}
