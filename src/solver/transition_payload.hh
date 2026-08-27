#pragma once

#include "configuration.hh"

#include <spot/twa/acc.hh>

#include <type_traits>
#include <utility>

#ifndef ACACIA_TRANSITION_ACCEPTANCE
# define ACACIA_TRANSITION_ACCEPTANCE 0
#endif

namespace acacia::transitions {
  struct triple {
      unsigned src = 0;
      unsigned dst = 0;
      bool acc = false;
  };

#if ACACIA_TRANSITION_ACCEPTANCE
  using element = triple;
#else
  using element = std::pair<int, int>;
#endif

  namespace detail {
    template <typename Payload, typename Edge>
    Payload make (unsigned p, unsigned q, const Edge& e) {
      if constexpr (std::is_same_v<Payload, triple>)
        return triple {p, q, e.acc != spot::acc_cond::mark_t {}};
      else
        return std::pair<int, int> {static_cast<int> (p), static_cast<int> (q)};
    }

    template <typename Payload, typename Aut>
    bool increment (const Payload& t, const Aut& aut) {
      if constexpr (std::is_same_v<Payload, triple>) {
        (void) aut;
        return t.acc;
      }
      else
        return aut->state_is_accepting (t.second);
    }

    template <typename Payload>
    unsigned source (const Payload& t) {
      if constexpr (std::is_same_v<Payload, triple>)
        return t.src;
      else
        return static_cast<unsigned> (t.first);
    }

    template <typename Payload>
    unsigned dest (const Payload& t) {
      if constexpr (std::is_same_v<Payload, triple>)
        return t.dst;
      else
        return static_cast<unsigned> (t.second);
    }
  }

  template <typename Edge>
  element make (unsigned p, unsigned q, const Edge& e) {
    return detail::make<element> (p, q, e);
  }

  template <typename Aut>
  bool increment (const element& t, const Aut& aut) {
    return detail::increment (t, aut);
  }

  inline unsigned source (const element& t) { return detail::source (t); }
  inline unsigned dest (const element& t) { return detail::dest (t); }

  // Legacy explicitly typed transition sets, including MONA's bit-decoded
  // endpoint pairs, keep the state-based fallback even when element is triple.
  template <typename Src, typename Dst, typename Aut>
    requires (not std::is_same_v<std::pair<Src, Dst>, element>)
  bool increment (const std::pair<Src, Dst>& t, const Aut& aut) {
    return aut->state_is_accepting (t.second);
  }

  template <typename Src, typename Dst>
    requires (not std::is_same_v<std::pair<Src, Dst>, element>)
  unsigned source (const std::pair<Src, Dst>& t) {
    return static_cast<unsigned> (t.first);
  }

  template <typename Src, typename Dst>
    requires (not std::is_same_v<std::pair<Src, Dst>, element>)
  unsigned dest (const std::pair<Src, Dst>& t) {
    return static_cast<unsigned> (t.second);
  }
}
