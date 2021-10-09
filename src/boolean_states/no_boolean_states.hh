#pragma once

namespace boolean_states {
  namespace detail {
    template <typename Aut>
    class no_boolean_states {
      public:
        no_boolean_states (Aut aut, int K) : aut {aut}, K {K} {}

        size_t operator() () const {
          return aut->num_states ();
        }
      private:
        const Aut aut;
        const int K;
    };
  }

  struct no_boolean_states {
      template <typename Aut>
      static auto make (Aut aut, int K) {
        return detail::no_boolean_states<Aut> (aut, K);
      }
  };
}
