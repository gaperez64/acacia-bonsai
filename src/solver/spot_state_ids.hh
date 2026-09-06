#pragma once

#include <spot/twa/twa.hh>

#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace acacia::spot_rows {

  using StateId = std::uint32_t;

  /// One arena per provider.  intern() consumes an owned Spot state, including
  /// on duplicates and exceptions.  state_map compares hash()/compare(), never
  /// addresses or format_state().  Returned pointers are borrowed canonical states.
  class SpotStateIds {
    public:
      explicit SpotStateIds (spot::const_twa_ptr provider)
        : provider_ (std::move (provider)) {
        if (not provider_)
          throw std::invalid_argument ("SpotStateIds requires a provider");
      }

      SpotStateIds (const SpotStateIds&) = delete;
      SpotStateIds& operator= (const SpotStateIds&) = delete;
      SpotStateIds (SpotStateIds&&) = delete;
      SpotStateIds& operator= (SpotStateIds&&) = delete;

      StateId intern (const spot::state* owned) {
        owned_state incoming {owned};
        if (not incoming)
          throw std::invalid_argument ("Spot provider returned a null state");
        const auto found = ids_.find (incoming.get ());
        if (found != ids_.end ())
          return found->second;  // incoming.destroy(), even for refcounted aliases
        if (states_.size () > std::numeric_limits<StateId>::max ())
          throw std::length_error ("Spot StateId space exhausted");

        const auto id = static_cast<StateId> (states_.size ());
        states_.push_back (std::move (incoming));
        try {
          ids_.emplace (states_.back ().get (), id);
        }
        catch (...) {
          states_.pop_back ();
          throw;
        }
        return id;
      }

      std::size_t size () const { return states_.size (); }
      const spot::state* operator[] (StateId id) const { return states_.at (id).get (); }

    private:
      struct destroy_state {
          void operator() (const spot::state* state) const { state->destroy (); }
      };
      using owned_state = std::unique_ptr<const spot::state, destroy_state>;

      // Reverse destruction: map, canonical states, then provider (which owns
      // its dictionary, AP registrations and any state allocation pools).
      spot::const_twa_ptr provider_;
      std::vector<owned_state> states_;
      spot::state_map<StateId> ids_;
  };

}  // namespace acacia::spot_rows
