#pragma once

#include <type_traits>
#include <utility>

// From https://stackoverflow.com/questions/14417430/using-an-int-as-a-template-parameter-that-is-not-known-until-run-time

template<size_t N>
using index_t = std::integral_constant<size_t, N>;

template<size_t M>
struct static_switch_t {
    template<class F, class...Args>
    using R = std::invoke_result_t<F, index_t<0>, Args...>;

    template<class F, class G, class...Args>
    R<F, Args...> operator () (F&& f, G&& g, size_t i, Args&& ...args) const {
      if (i > M)
        return g (i, std::forward<Args> (args)...);

      return invoke (std::make_index_sequence<M + 1> {}, std::forward<F> (f), i, std::forward<Args> (args)...);
    }

  private:

    template<size_t...Is, class F, class...Args>
    R<F, Args...> invoke (std::index_sequence<Is...>, F&& f, size_t i, Args&& ...args) const {
      using pF = decltype (std::addressof (f));
      using call_func = R<F, Args...> (*) (pF pf, Args&& ...args);

      static const call_func table[M + 1] = {
        [] (pF pf, Args&& ...args) -> R<F, Args...>{
          return std::forward<F> (*pf) (index_t<Is>{}, std::forward<Args> (args)...);
        }...
      };

      return table[i] (std::addressof (f), std::forward<Args> (args)...);
    }
};
