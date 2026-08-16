#include <algorithm>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <spot/tl/formula.hh>
#include <spot/tl/parse.hh>

namespace {

  struct canonical_node {
      spot::op kind;
      std::vector<canonical_node> children;
      std::string key;
  };

  bool commutative (spot::op kind) {
    return kind == spot::op::And or kind == spot::op::Or or kind == spot::op::Xor or
           kind == spot::op::Equiv;
  }

  canonical_node make_node (spot::op kind, std::vector<canonical_node> children = {}) {
    if (kind == spot::op::And or kind == spot::op::Or) {
      std::vector<canonical_node> flattened;
      for (auto& child : children) {
        if (child.kind == kind)
          for (auto& grandchild : child.children)
            flattened.push_back (std::move (grandchild));
        else
          flattened.push_back (std::move (child));
      }
      children = std::move (flattened);
    }
    if (commutative (kind))
      std::ranges::sort (children, {}, &canonical_node::key);
    canonical_node result {kind, std::move (children),
                           std::to_string (static_cast<unsigned> (kind)) + ':'};
    for (const auto& child : result.children)
      result.key += std::to_string (child.key.size ()) + ':' + child.key;
    return result;
  }

  canonical_node canonicalize (spot::formula formula) {
    const auto kind = formula.kind ();
    if (kind == spot::op::ap) {
      const auto& name = formula.ap_name ();
      return {kind, {}, std::to_string (static_cast<unsigned> (kind)) + ':' +
                            std::to_string (name.size ()) + ':' + name};
    }
    if (kind == spot::op::Star or kind == spot::op::FStar) {
      auto child = canonicalize (formula[0]);
      canonical_node result {
          kind,
          {std::move (child)},
          std::to_string (static_cast<unsigned> (kind)) + ':' +
              std::to_string (formula.min ()) + ':' + std::to_string (formula.max ()) + ':'};
      result.key += std::to_string (result.children[0].key.size ()) + ':' +
                    result.children[0].key;
      return result;
    }

    std::vector<canonical_node> children;
    children.reserve (formula.size ());
    for (auto child : formula)
      children.push_back (canonicalize (child));

    // Match `ltlfilt --unabbreviate=RWM` without invoking its expensive
    // general simplifier.  The rewrites are exact LTL identities.
    if (kind == spot::op::W) {
      auto until = make_node (spot::op::U, {children[0], children[1]});
      auto globally = make_node (spot::op::G, {children[0]});
      return make_node (spot::op::Or, {std::move (until), std::move (globally)});
    }
    if (kind == spot::op::R) {
      auto both = make_node (spot::op::And, {children[0], children[1]});
      auto until = make_node (spot::op::U, {children[1], std::move (both)});
      auto globally = make_node (spot::op::G, {children[1]});
      return make_node (spot::op::Or, {std::move (until), std::move (globally)});
    }
    if (kind == spot::op::M) {
      auto both = make_node (spot::op::And, {children[0], children[1]});
      return make_node (spot::op::U, {children[1], std::move (both)});
    }
    return make_node (kind, std::move (children));
  }

  std::string parse_key (const std::string& text) {
    auto parsed = spot::parse_infix_psl (text);
    if (not parsed.f or not parsed.errors.empty ()) {
      parsed.format_errors (std::cerr);
      return {};
    }
    return canonicalize (parsed.f).key;
  }

}  // namespace

int main (int argc, char** argv) {
  if (argc == 2 and std::string {argv[1]} == "--self-test") {
    const auto first = parse_key ("G(a & b) & F(c)");
    const auto second = parse_key ("F(c) & G(b & a)");
    const bool derived = parse_key ("a W b") == parse_key ("(a U b) | G(a)") and
                         parse_key ("a R b") == parse_key ("(b U (a & b)) | G(b)") and
                         parse_key ("a M b") == parse_key ("b U (a & b)");
    return not first.empty () and first == second and derived ? 0 : 1;
  }
  if (argc != 1) {
    std::cerr << "usage: ltl-formula-canonicalize [--self-test]\n";
    return 2;
  }
  const std::string text {std::istreambuf_iterator<char> {std::cin},
                          std::istreambuf_iterator<char> {}};
  const auto key = parse_key (text);
  if (key.empty ())
    return 1;
  std::cout << key << '\n';
  return 0;
}
