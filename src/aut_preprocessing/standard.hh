#pragma once

namespace aut_preprocessing {
  namespace detail {
    template <typename Aut>
    class standard {
      public:
        standard (Aut& aut, bdd input_support, bdd output_support, unsigned K, int verbose) :
          aut {aut}, input_support {input_support}, output_support {output_support}, K {K}, verbose {verbose}
        {}

        auto operator() () {
          aut->merge_edges ();
          aut->merge_states();
        }

        Aut& aut;
        const bdd input_support, output_support;
        const unsigned K;
        const int verbose;
    };
  }

  struct standard {
      template <typename Aut>
      static auto make (Aut& aut, bdd input_support, bdd output_support, unsigned K, int verbose) {
        return detail::standard (aut, input_support, output_support, K, verbose);
      }
  };
}
