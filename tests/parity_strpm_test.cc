#include "solver/parity/game.hh"

#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace acacia::parity;

void require (bool condition, const char* message) {
  if (not condition)
    throw std::runtime_error (message);
}
void compare (const game& g) {
  const auto ours = solve_strpm (g);
  const auto reference = solve_spot (g, to_spot (g));
  require (ours.winner == reference.winner, "winning regions differ from Spot");
  std::string reason;
  if (not check_strategy (g, ours, &reason))
    throw std::runtime_error (reason);
  require (check_strategy (g, reference), "Spot strategy check failed");
}
int main () {
  try {
    for (unsigned seed : {17u, 42u, 313u, 2026u}) {
      std::mt19937 random (seed);
      for (unsigned n : {1u, 2u, 5u, 12u, 40u, 128u}) {
        for (unsigned max_priority : {1u, 2u, 5u, 12u, 31u}) {
          for (unsigned rep = 0; rep < 4; ++rep) {
            std::vector<uint8_t> owners (n);
            std::vector<unsigned> priorities (n);
            std::vector<std::vector<unsigned>> edges (n);
            for (unsigned v = 0; v < n; ++v) {
              owners[v] = random () % 2;
              priorities[v] = random () % max_priority;
              for (unsigned e = 0, count = 1 + random () % 5; e < count; ++e)
                edges[v].push_back (random () % n);
            }
            compare ({std::move (owners), std::move (priorities), edges});
          }
        }
      }
    }
    // Observer records must describe attempted pairs, even if solving is interrupted.
    game normal {{0, 1}, {0, 1}, {{0}, {1}}};
    std::vector<statistics> parameters;
    limits observed;
    observed.parameter_observer = [&] (const statistics& stats) { parameters.push_back (stats); };
    auto completed = solve_strpm (normal, observed);
    require (not completed.stats.fallback and not parameters.empty (),
             "normal run omitted parameter records");
    require (parameters.size () == completed.stats.pairs and
                 parameters.back ().k == completed.stats.k and
                 parameters.back ().t == completed.stats.t,
             "parameter records differ from completed search");
    for (unsigned i = 0; i < parameters.size (); ++i)
      require (parameters[i].pairs == i + 1 and parameters[i].k and parameters[i].t,
               "parameter record is incomplete");
    struct interruption {};
    parameters.clear ();
    observed.parameter_observer = [&] (const statistics& stats) {
      parameters.push_back (stats);
      throw interruption {};
    };
    bool interrupted = false;
    try {
      solve_strpm (normal, observed);
    } catch (const interruption&) {
      interrupted = true;
    }
    require (interrupted and parameters.size () == 1 and parameters[0].k == 1 and
                 parameters[0].t == 1 and parameters[0].pairs == 1 and
                 parameters[0].lift_attempts == 0,
             "interrupted run lost its attempted parameters");
    game mixed {{0, 1, 0, 1}, {1, 2, 3, 4}, {{0, 1}, {0, 1}, {2, 3}, {2, 3}}};
    const auto reference = solve_spot (mixed, to_spot (mixed));
    for (limits bounds : {limits {1}, limits {0, 10, 1}, limits {0, 1, 8}}) {
      std::vector<std::pair<statistics, double>> fallback_records;
      parameters.clear ();
      bounds.parameter_observer = [&] (const statistics& stats) { parameters.push_back (stats); };
      bounds.fallback_observer = [&] (const statistics& stats, double seconds) {
        fallback_records.emplace_back (stats, seconds);
      };
      auto result = solve_strpm (mixed, bounds);
      require (fallback_records.size () == 2 and fallback_records[0].second < 0 and
                   std::isfinite (fallback_records[1].second) and fallback_records[1].second >= 0,
               "fallback omitted begin or completion records");
      for (const auto& [stats, seconds] : fallback_records)
        require (stats.fallback and stats.k == result.stats.k and stats.t == result.stats.t and
                     stats.fallback_vertices == result.stats.fallback_vertices and
                     stats.fallback_reason == result.stats.fallback_reason,
                 "fallback record metadata differs from result");
      require (fallback_records[1].second == result.stats.fallback_seconds,
               "fallback completion timer differs from result");
      require (parameters.size () == result.stats.pairs, "fallback run lost parameter records");
      require (result.stats.fallback, "fallback did not run");
      require (result.winner == reference.winner, "fallback regions differ");
      require (check_strategy (mixed, result), "fallback strategy failed");
    }
    game high {{0, 1}, {131070, 131071}, {{0, 1}, {0, 1}}};
    auto high_result = solve_strpm (high);
    require (high_result.stats.fallback, "level overflow did not fall back");
    require (high_result.winner == solve_spot (high, to_spot (high)).winner,
             "level overflow differs from Spot");
    require (check_strategy (high, high_result), "high-priority strategy failed");
    // Closure alone cannot validate a winning strategy: reject a bad subcycle.
    game subcycle {{0, 0}, {2, 1}, {{0, 1}, {0, 1}}};
    solution bad {{0, 0}, {1, 1}, {}};
    require (not check_strategy (subcycle, bad), "losing subcycle passed strategy check");
    std::cout << "480 random games, strategies and overflow/budget fallback passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
}
