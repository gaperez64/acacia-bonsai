#include "research/exact_buchi.hh"

#include "solver/translator_options.hh"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <spot/tl/parse.hh>
#include <spot/tl/randomltl.hh>
#include <spot/twaalgos/contains.hh>
#include <spot/twaalgos/hoa.hh>
#include <spot/twaalgos/isdet.hh>
#include <spot/twaalgos/translate.hh>

namespace {
  unsigned failures = 0;
  std::array<unsigned, 5> tiers {};

  void expect (bool condition, const char* message) {
    if (not condition) {
      std::cerr << message << '\n';
      ++failures;
    }
  }

  spot::twa_graph_ptr translate (spot::formula f, const spot::bdd_dict_ptr& dict, bool det) {
    auto options = acacia::translation::make_options ();
    spot::translator translator (dict, &options);
    translator.set_type (det ? spot::postprocessor::Generic : spot::postprocessor::BA);
    translator.set_pref (det ? spot::postprocessor::Deterministic
                             : spot::postprocessor::Small | spot::postprocessor::SBAcc);
    translator.set_level (spot::postprocessor::High);
    return translator.run (f);
  }

  void equivalent (spot::formula f) {
    auto dict = spot::make_bdd_dict ();
    auto reference = translate (f, dict, false);
    auto tela = translate (f, dict, true);
    expect (spot::is_deterministic (tela),
            "generic translator did not produce deterministic TELA");
    const auto snapshot = spot::make_twa_graph (tela, spot::twa::prop_set::all ());
    bool general = false;
    auto routed = acacia::research::route_buchi (
        tela, false, false, [&] (const char* stage, const auto&, double, unsigned, bool) {
          general |= std::string (stage) == "generalized";
        });
    auto result = routed.aut;
    ++tiers[routed.tier];
    expect (general == (routed.tier == 4), "general conversion used outside tier 4");
    expect (acacia::research::plain_buchi (result), "invalid Buchi interface");
    if (routed.tier <= 3)
      expect (spot::is_deterministic (result), "tier 1-3 lost determinism");
    if (routed.tier == 1)
      expect (result == tela, "tier 1 did not preserve the cleaned graph");
    if (routed.tier == 4)
      expect (result->prop_state_acc ().is_true (), "tier 4 did not produce SBA");
    expect (spot::are_equivalent (snapshot, result), "worker-language mismatch");
    if (not spot::are_equivalent (reference, result)) {
      std::cerr << "language mismatch: " << f << '\n';
      ++failures;
    }
  }

  void constants () {
    for (bool accepting : {false, true}) {
      auto dict = spot::make_bdd_dict ();
      auto aut = spot::make_twa_graph (dict);
      aut->register_ap ("unused");
      aut->new_state ();
      aut->set_init_state (0);
      aut->set_acceptance (
          0, accepting ? spot::acc_cond::acc_code::t () : spot::acc_cond::acc_code::f ());
      aut->new_edge (0, 0, bddtrue);
      auto result = acacia::research::exact_buchi (aut);
      expect (spot::are_equivalent (aut, result), "zero-set language mismatch");
      expect (result->ap ().size () == 1 and result->ap ()[0].ap_name () == "unused",
              "constant conversion lost AP declaration");
      auto tba = spot::degeneralize_tba (spot::to_generalized_buchi (aut));
      auto sba = spot::sbacc (tba);
      expect (spot::are_equivalent (aut, sba), "TBA/sbacc zero-set language mismatch");
      expect (result == aut, "tier 1 constant graph changed");
      expect (result->acc ().is_t () or result->acc ().is_f (),
              "tier 1 constant acceptance was converted");
    }
  }

  // Adding a Fin color on the first transition preserves the language. Keep
  // the original initial labels: inserting an unconditional prefix would shift it.
  spot::twa_graph_ptr rabin_copy (const spot::twa_graph_ptr& ba) {
    auto aut = spot::make_twa_graph (ba, spot::twa::prop_set::all ());
    const unsigned initial = aut->get_init_state_number ();
    const unsigned prefix = aut->new_state ();
    std::vector<spot::twa_graph::edge_storage_t> outgoing;
    for (const auto& edge : aut->out (initial))
      outgoing.push_back (edge);
    for (const auto& edge : outgoing)
      aut->new_edge (prefix, edge.dst, edge.cond, edge.acc | spot::acc_cond::mark_t {1});
    aut->set_init_state (prefix);
    aut->set_acceptance (2, spot::acc_cond::acc_code ("Fin(1) & Inf(0)"));
    aut->prop_state_acc (false);
    aut->prop_weak (spot::trival::maybe ());
    aut->prop_inherently_weak (spot::trival::maybe ());
    return aut;
  }

  void tier_conversions () {
    for (unsigned index = 0; index < 6; ++index) {
      const std::string name = "signal" + std::to_string (index);
      const auto f = spot::parse_infix_psl ("GF(" + name + ")").f;
      auto dict = spot::make_bdd_dict ();
      auto ba = translate (f, dict, false);
      auto rabin = rabin_copy (ba);
      std::vector<std::string> stages;
      auto routed = acacia::research::route_buchi (
          rabin, false, false, [&] (const char* stage, const auto&, double, unsigned, bool) {
            stages.emplace_back (stage);
          });
      expect (routed.tier == 3, "Rabin-realizable graph not classified tier 3");
      expect (spot::is_deterministic (routed.aut), "Rabin relabelling lost determinism");
      expect (spot::are_equivalent (ba, routed.aut), "Rabin relabelling changed language");
      expect (std::find (stages.begin (), stages.end (), "parity-reduce") != stages.end (),
              "parity reduction not exercised");
      ++tiers[routed.tier];

      // A complete weak co-Buchi graph for F(signal), with genuinely used Fin.
      auto weak = spot::make_twa_graph (dict);
      const auto variable = weak->register_ap (name);
      weak->new_states (2);
      weak->set_init_state (0);
      weak->set_acceptance (1, spot::acc_cond::acc_code::cobuchi ());
      weak->new_edge (0, 0, bdd_nithvar (variable), {0});
      weak->new_edge (0, 1, bdd_ithvar (variable), {0});
      weak->new_edge (1, 1, bddtrue);
      auto reference = translate (spot::parse_infix_psl ("F(" + name + ")").f, dict, false);
      auto result = acacia::research::route_buchi (weak, false, false, [] (auto&&...) {});
      expect (result.tier == 3, "weak Fin graph not classified tier 3");
      expect (spot::are_equivalent (reference, result.aut), "weak relabelling changed language");
      ++tiers[result.tier];
    }
    auto dict = spot::make_bdd_dict ();
    auto strong = spot::parse_infix_psl ("FG(a)").f;
    auto hinted = translate (strong, dict, true);
    hinted->prop_weak (true);
    hinted->prop_very_weak (true);
    hinted->prop_inherently_weak (true);
    hinted->prop_terminal (true);
    auto checked = acacia::research::route_buchi (hinted, false, false, [] (auto&&...) {});
    expect (checked.tier == 4, "cached strength flags changed structural classification");
    expect (spot::are_equivalent (translate (strong, dict, false), checked.aut),
            "cached strength flags changed the worker language");
    ++tiers[checked.tier];
    for (bool fin : {false, true}) {
      auto aut = spot::make_twa_graph (dict);
      const int a = aut->register_ap ("a"), b = aut->register_ap ("b");
      const int c = aut->register_ap ("c");
      aut->new_states (2);
      aut->set_init_state (fin ? 1 : 0);
      aut->set_acceptance (
          fin ? 4 : 3,
          spot::acc_cond::acc_code (fin ? "(Fin(2)&Inf(0)) | (Fin(3)&Inf(1))"
                                        : "(Inf(0)|Inf(1)) & (Inf(1)|Inf(2)) & (Inf(2)|Inf(0))"));
      for (unsigned bits = 0; bits < 8; ++bits) {
        const bdd label = (bits & 1 ? bdd_ithvar (a) : bdd_nithvar (a)) &
                          (bits & 2 ? bdd_ithvar (b) : bdd_nithvar (b)) &
                          (bits & 4 ? bdd_ithvar (c) : bdd_nithvar (c));
        spot::acc_cond::mark_t acc {};
        for (unsigned color = 0; color < (fin ? 2 : 3); ++color)
          if (bits & (1 << color))
            acc.set (color);
        aut->new_edge (0, 0, label, acc);
        if (fin) {
          acc.set (bits & 1 ? 2 : 3);
          aut->new_edge (1, 0, label, acc);
        }
      }
      const auto f = spot::parse_infix_psl (fin ? "GF(a) | GF(b)"
                                                : "(GF(a)|GF(b)) & (GF(b)|GF(c)) & (GF(c)|GF(a))")
                         .f;
      auto reference = translate (f, dict, false);
      bool exercised = false;
      auto routed = acacia::research::route_buchi (
          aut, false, false, [&] (const char* stage, const auto&, double, unsigned, bool) {
            exercised |= std::string (stage) == (fin ? "rabin-buchi" : "finless-normalize");
          });
      if (routed.tier != (fin ? 3u : 2u))
        std::cerr << "encoding Fin=" << fin << " tier=" << routed.tier
                  << " acceptance=" << routed.aut->get_acceptance () << '\n';
      expect (routed.tier == (fin ? 3u : 2u), "wrong tier for generated acceptance encoding");
      expect (exercised, "Rabin/CNF conversion not exercised");
      expect (spot::are_equivalent (reference, routed.aut), "Rabin/CNF language mismatch");
      ++tiers[routed.tier];
    }
  }

  void fixtures (const std::filesystem::path& directory) {
    std::filesystem::create_directories (directory);
    std::ofstream manifest (directory / "games.tsv");
    // Oracles describe the original objective, never the conversion dispatch.
    struct game {
        const char* formula;
        const char* inputs;
        const char* outputs;
        bool real;
    };
    const game games[] = {
        {"1", "", "", true},
        {"0", "", "", false},
        {"G(o <-> i)", "i", "o", true},
        {"G(o <-> X(i))", "i", "o", false},
        {"G(i -> F(o))", "i", "o", true},
        {"GF(i)", "i", "o", false},
        {"G(o)", "", "o", true},
        {"F(i)", "i", "", false},
        {"G(o <-> i)", "i,unused", "o,spare", true},
        {"G(response <-> request)", "unused,request", "spare,response", true},
        {"G(X(o) <-> i)", "i", "o", true},
        {"GF(o) & GF(!o)", "i", "o", true},
        {"FG(!o)", "i", "o", true},
        {"FG(!i)", "i", "o", false},
    };
    unsigned index = 0;
    for (const auto& game : games) {
      auto original = spot::parse_infix_psl (game.formula).f;
      const auto output_ap = std::string (game.outputs) == "spare,response" ? "response" : "o";
      auto shift = [&] (auto&& self, spot::formula f) -> spot::formula {
        if (f.is (spot::op::ap) and f.ap_name () == output_ap)
          return spot::formula::X (f);
        return f.map ([&] (spot::formula child) { return self (self, child); });
      };
      for (const char* orientation : {"real", "unreal-formula", "unreal-automaton"}) {
        auto worker = std::string (orientation) == "real" ? spot::formula::Not (original)
                      : std::string (orientation) == "unreal-formula" ? shift (shift, original)
                                                                      : original;
        auto dict = spot::make_bdd_dict ();
        auto ba = translate (worker, dict, false);
        auto tela = translate (worker, dict, true);
        auto converted = acacia::research::exact_buchi (tela);
        const std::string stem = std::to_string (index) + "-" + orientation;
        std::ofstream direct_file (directory / (stem + "-ba.hoa"));
        std::ofstream tela_file (directory / (stem + "-tela.hoa"));
        std::ofstream converted_file (directory / (stem + "-sba.hoa"));
        spot::print_hoa (direct_file, ba);
        spot::print_hoa (tela_file, tela);
        spot::print_hoa (converted_file, converted);
        manifest << stem << '\t' << orientation << '\t' << game.inputs << '\t' << game.outputs
                 << '\t' << (game.real ? "REALIZABLE" : "UNREALIZABLE") << '\n';
        if (tela->acc ().is_buchi () and spot::is_deterministic (tela) and
            not spot::is_inherently_weak_automaton (tela)) {
          auto rabin = rabin_copy (tela);
          const auto variant = stem + "-rabin";
          std::ofstream variant_ba (directory / (variant + "-ba.hoa"));
          std::ofstream variant_tela (directory / (variant + "-tela.hoa"));
          spot::print_hoa (variant_ba, ba);
          spot::print_hoa (variant_tela, rabin);
          manifest << variant << '\t' << orientation << '\t' << game.inputs << '\t' << game.outputs
                   << '\t' << (game.real ? "REALIZABLE" : "UNREALIZABLE") << '\n';
        }
      }
      ++index;
    }
  }
}

int main (int argc, char** argv) {
  try {
    if (argc == 3 and std::string (argv[1]) == "--fixtures") {
      fixtures (argv[2]);
      return 0;
    }
    constants ();
    tier_conversions ();
    for (const char* text : {"1", "0", "GF(a)", "GF(a) & GF(b)", "FG(a)", "GF(a) | FG(b)",
                             "(GF(a) -> GF(b)) & (GF(b) -> GF(c))", "G(a -> X(b))"})
      equivalent (spot::parse_infix_psl (text).f);
    for (int seed : {173, 947, 2026}) {
      spot::option_map options;
      options.set ("output", spot::randltlgenerator::LTL);
      options.set ("seed", seed);
      options.set ("tree_size_min", 1);
      options.set ("tree_size_max", 10);
      options.set ("unique", 1);
      spot::randltlgenerator generator (3, options);
      for (unsigned i = 0; i < 20; ++i)
        equivalent (generator.next ());
    }
  } catch (const std::exception& error) {
    std::cerr << error.what () << '\n';
    return 1;
  }
  for (unsigned tier = 1; tier <= 4; ++tier) {
    std::cout << "tier " << tier << ": " << tiers[tier] << '\n';
    expect (tiers[tier] > 0, "generated formulas did not cover every tier");
  }
  return failures ? 1 : 0;
}
