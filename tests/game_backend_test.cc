#include "portfolio_arm.hh"
#include "solver/game_backend.hh"
#include "solver/spot_lazy_worker.hh"
#include <spot/tl/parse.hh>

#include <string_view>

#include <array>
#include <iostream>
#include <utility>

namespace utils {
  unsigned verbose = 0;
  voutstream vout;
}
namespace posets::vectors {
  size_t bool_threshold = 0;
}

int main () {
  using acacia::game_backend;
  using acacia::automaton_provider;
  using acacia::candidate_mode;

  for (auto provider : {automaton_provider::frozen_graph, automaton_provider::spot_lazy, automaton_provider::spot_eager}) {
    if (acacia::parse_automaton_provider (acacia::automaton_provider_name (provider)) != provider)
      return 1;
    if (acacia::synthesis_provider (provider, true) != automaton_provider::frozen_graph)
      return 1;
  }
  for (auto name : {"", "forward", "spot-guarded", "spot_lazy", "frozen-graph "})
    if (acacia::parse_automaton_provider (name)) return 1;
  for (auto mode : {candidate_mode::only, candidate_mode::fallback})
    if (acacia::parse_candidate_mode (acacia::candidate_mode_name (mode)) != mode) return 1;
  if (acacia::parse_candidate_mode ("backward")) return 1;
  for (auto backend : {game_backend::backward, game_backend::forward, game_backend::spot_guarded}) {
    if (acacia::synthesis_backend (backend, true) != game_backend::backward) return 1;
    if (acacia::synthesis_backend (backend, false) != backend) return 1;
  }

  // This worker adapter is deliberately tested without its compile-time
  // dispatch option. In particular, it must never negate the captured formula.
  const auto dict = spot::make_bdd_dict ();
  using acacia::spot_lazy_worker::Outcome;
  using acacia::spot_lazy_worker::solve;
  if (solve (spot::formula::ff (), dict, bddtrue, bddtrue, 1, 3, 1) != Outcome::win or
      solve (spot::formula::tt (), dict, bddtrue, bddtrue, 1, 3, 1) != Outcome::kmax)
    return 1;
  acacia::spot_guarded::Limits capped;
  capped.max_expansions = 0;
  if (solve (spot::formula::ff (), dict, bddtrue, bddtrue, 1, 3, 1, capped) != Outcome::unknown)
    return 1;

  constexpr std::array names {
      std::pair {game_backend::backward, std::string_view {"backward"}},
      std::pair {game_backend::forward, std::string_view {"forward"}},
      std::pair {game_backend::spot_guarded, std::string_view {"spot-guarded"}},
  };
  for (const auto& [backend, name] : names) {
    const auto parsed = acacia::parse_game_backend (name);
    if (not parsed.has_value () or *parsed != backend or
        acacia::game_backend_name (backend) != name) {
      std::cerr << "backend name and value are not inverses: " << name << '\n';
      return 1;
    }
  }

  constexpr std::array<std::string_view, 5> invalid {"", "Backward", "fwd", "backward ", "local"};
  for (std::string_view name : invalid)
    if (acacia::parse_game_backend (name).has_value ()) {
      std::cerr << "invalid backend name was accepted: " << name << '\n';
      return 1;
    }

  const auto parsed = parse_portfolio_arms (
      "real:any:backward,real:small:forward,unreal:formula:forward,"
      "unreal:automaton:forward,real:any:spot-guarded,unreal:formula:spot-guarded");
  const std::vector<portfolio_arm> expected {
      {false, spot::postprocessor::Any, UNREAL_X_FORMULA, game_backend::backward},
      {false, spot::postprocessor::Small, UNREAL_X_FORMULA, game_backend::forward},
      {true, ACACIA_TRANSLATION_PREF, UNREAL_X_FORMULA, game_backend::forward},
      {true, ACACIA_TRANSLATION_PREF, UNREAL_X_AUTOMATON, game_backend::forward},
      {false, spot::postprocessor::Any, UNREAL_X_FORMULA, game_backend::spot_guarded},
      {true, ACACIA_TRANSLATION_PREF, UNREAL_X_FORMULA, game_backend::spot_guarded},
  };
  if (parsed.error != portfolio_arm_parse_error::none or parsed.arms != expected) {
    std::cerr << "valid portfolio did not parse in order\n";
    return 1;
  }
  for (const auto& arm : parsed.arms)
    if (arm.provider != automaton_provider::frozen_graph) return 1;
  auto lazy_arm = parsed.arms.front ();
  lazy_arm.provider = automaton_provider::spot_lazy;
  if (lazy_arm == parsed.arms.front () or lazy_arm.backend != parsed.arms.front ().backend)
    return 1;

  const auto backend_pair =
      parse_portfolio_arms ("real:any:backward,real:any:forward");
  if (backend_pair.error != portfolio_arm_parse_error::none or
      backend_pair.arms.size () != 2) {
    std::cerr << "same transform with different backends was rejected\n";
    return 1;
  }

  const auto mixed = parse_portfolio_arms (
      "real:small:backward,real:small:spot-guarded:spot-lazy,unreal:formula:spot-guarded:spot-eager");
  if (mixed.error != portfolio_arm_parse_error::none || mixed.arms.size () != 3 ||
      mixed.arms[1].provider != automaton_provider::spot_lazy ||
      mixed.arms[2].provider != automaton_provider::spot_eager) return 1;
  if (parse_portfolio_arms ("real:small:backward,real:small:backward:frozen-graph").error !=
      portfolio_arm_parse_error::duplicate) return 1;
  if (parse_portfolio_arms ("real:small:spot-guarded:bad").error !=
      portfolio_arm_parse_error::provider) return 1;

  const auto duplicate =
      parse_portfolio_arms ("real:any:backward,real:any:backward");
  if (duplicate.error != portfolio_arm_parse_error::duplicate or
      duplicate.spec != "real:any:backward") {
    std::cerr << "duplicate arm was not identified\n";
    return 1;
  }

  return 0;
}
