#pragma once

namespace aut_preprocessors {
  namespace detail {
    template <typename Aut>
    class standard {
      public:
        standard (Aut& aut, bdd input_support, bdd output_support, unsigned K) :
          aut {aut}, input_support {input_support}, output_support {output_support}, K {K}
        {}

        auto operator() () {
          aut->merge_edges ();
          aut->merge_states();
        }

        Aut& aut;
        const bdd input_support, output_support;
        const unsigned K;
    };
  }

  struct standard {
      template <typename Aut>
      static auto make (Aut& aut, bdd input_support, bdd output_support, unsigned K) {
        return detail::standard (aut, input_support, output_support, K);
      }
  };
}
