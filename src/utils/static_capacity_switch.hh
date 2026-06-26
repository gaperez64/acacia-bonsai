#pragma once

#include "utils/static_switch.hh"

#include <memory>
#include <type_traits>
#include <utility>

template <size_t Max, size_t Step>
struct static_capacity_switch_t {
    static_assert (Step > 0);

    template <class F, class... Args>
    using R = std::invoke_result_t<F, index_t<0>, Args...>;

    template <class F, class G, class... Args>
    R<F, Args...> operator() (F&& f, G&& g, size_t i, Args&&... args) const {
      if (i > Max or i % Step != 0)
        return g (i, std::forward<Args> (args)...);

      return invoke (std::make_index_sequence<Max / Step + 1> {}, std::forward<F> (f), i / Step,
                     std::forward<Args> (args)...);
    }

  private:
    template <size_t... Is, class F, class... Args>
    R<F, Args...> invoke (std::index_sequence<Is...>, F&& f, size_t i, Args&&... args) const {
      using pF = decltype (std::addressof (f));
      using call_func = R<F, Args...> (*) (pF pf, Args&&... args);

      static const call_func table[sizeof...(Is)] = {
          [] (pF pf, Args&&... args) -> R<F, Args...> {
            return std::forward<F> (*pf) (index_t<Is * Step> {},
                                          std::forward<Args> (args)...);
          }...};

      return table[i](std::addressof (f), std::forward<Args> (args)...);
    }
};
