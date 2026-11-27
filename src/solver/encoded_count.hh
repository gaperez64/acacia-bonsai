#pragma once

#include "encoded_family.hh"

#include <algorithm>
#include <functional>
#include <set>
#include <span>
#include <spot/twa/twagraph.hh>
#include <tuple>

namespace acacia::parity {
  struct arena_game;
  struct solution;
}

namespace acacia::symcount {
  // Raw letters are packed by owner, with each bus in declaration bit order.
  struct encoded_alphabet {
      static constexpr unsigned absent = UINT32_MAX;
      struct value {
          unsigned coord = absent, mode = absent;
      };
      struct bus {
          unsigned shift, radix;
          std::vector<value> values;
      };
      const symmetry::encoded_certificate& cert;
      std::vector<bus> buses;
      std::vector<std::string> raw_names, inputs, outputs;
      std::vector<const symmetry::encoded_certificate::predicate*> now, future;
      unsigned input_width = 0, history_mask = 0;

      explicit encoded_alphabet (const symmetry::encoded_certificate& c) : cert (c) {
        buses.resize (c.buses.size ());
        for (bool input : {true, false}) {
          auto& names = input ? inputs : outputs;
          for (size_t b = 0; b < buses.size (); ++b) {
            const auto& src = c.buses[b];
            if (src.is_input != input)
              continue;
            auto& dst = buses[b];
            dst.shift = raw_names.size ();
            dst.radix = 1u << src.members.size ();
            dst.values.resize (dst.radix);
            for (unsigned i = 0; i < src.codes.size (); ++i) {
              names.push_back (src.selectors[i].ap_name ());
              if (i < c.moving)
                dst.values[src.codes[i]] = {i, 0};
              if (src.aliases[i] and i < c.moving)
                dst.values[*src.aliases[i]] = {i, 1};
            }
            for (unsigned k = 0; k < src.unused_classes.size (); ++k)
              for (auto code : src.unused_classes[k])
                dst.values[code].mode = k + 2;
            for (auto bit : src.members)
              raw_names.push_back (bit.ap_name ());
          }
          for (const auto& name : input ? c.inputs : c.outputs)
            if (std::ranges::find (raw_names, name) == raw_names.end ()) {
              raw_names.push_back (name);
              names.push_back (name);
            }
          if (input)
            input_width = raw_names.size ();
        }
        for (const auto& p : c.predicates) {
          if (std::ranges::any_of (p.slots, [] (auto slot) { return slot.next; })) {
            future.push_back (&p);
            for (auto slot : p.slots)
              if (not slot.next)
                history_mask |= mask (slot.bus);
          } else {
            now.push_back (&p);
            // This only determines a bit's position in the shared full-letter table.
            outputs.push_back (p.atom.ap_name ());
          }
        }
      }

      unsigned mask (size_t b) const { return (buses[b].radix - 1) << buses[b].shift; }

      unsigned code (unsigned word, size_t b) const {
        return (word & mask (b)) >> buses[b].shift;
      }

      unsigned put (unsigned word, size_t b, unsigned value) const {
        return (word & ~mask (b)) | (value << buses[b].shift);
      }

      unsigned owner_mask (bool input) const {
        const unsigned low = (1u << input_width) - 1;
        return input ? low : ((1u << raw_names.size ()) - 1) ^ low;
      }

      bool table (const symmetry::encoded_certificate::predicate& p,
                  unsigned previous, unsigned current) const {
        size_t row = 0, stride = 1;
        for (auto [b, next] : p.slots) {
          row += stride * code (next ? current : previous, b);
          stride *= buses[b].radix;
        }
        return p.truth[row];
      }

      // In a cell, place the anchored coordinates first, in slot order. For
      // each residual class, put its previous and current codes in slot order.
      struct transport {
          std::vector<unsigned> coord;
          std::vector<symmetry::encoded_certificate::swap> residual;
      };

      transport canonicalize (std::vector<unsigned>& local, unsigned& history,
                              unsigned* input = nullptr) const {
        transport p {std::vector<unsigned> (local.size (), absent), {}};
        std::vector<unsigned> sorted = local;
        std::ranges::sort (sorted);
        std::vector<unsigned> first (sorted.back () + 1);
        for (size_t i = sorted.size (); i-- > 0;)
          first[sorted[i]] = i;
        auto mark = [&] (unsigned word, unsigned mask_bits) {
          for (size_t b = 0; b < buses.size (); ++b)
            if (mask_bits & mask (b)) {
              const auto i = buses[b].values[code (word, b)].coord;
              if (i != absent and p.coord[i] == absent)
                p.coord[i] = first[local[i]]++;
            }
        };
        if (history != absent)
          mark (history, history_mask);
        if (input)
          mark (*input, owner_mask (true));
        for (unsigned i = 0; i < local.size (); ++i)
          if (p.coord[i] == absent)
            p.coord[i] = first[local[i]]++;
        local = std::move (sorted);
        for (size_t b = 0; b < buses.size (); ++b) {
          const auto swap = [&] (unsigned word, unsigned pinned) {
            const auto x = code (word, b);
            const auto v = buses[b].values[x];
            if (v.mode < 2 or v.mode == absent or x == pinned)
              return;
            const auto& group = cert.buses[b].unused_classes[v.mode - 2];
            const unsigned y = group.front () == pinned ? group[1] : group.front ();
            if (x != y)
              p.residual.push_back ({b, x, y});
          };
          if (history != absent and (history_mask & mask (b)))
            swap (history, absent);
          if (input and cert.buses[b].is_input) {
            const auto pinned = history != absent and (history_mask & mask (b))
                                  ? code (apply (history, history_mask, p), b) : absent;
            swap (apply (*input, owner_mask (true), p), pinned);
          }
        }
        if (history != absent)
          history = apply (history, history_mask, p);
        if (input)
          *input = apply (*input, owner_mask (true), p);
        return p;
      }

      unsigned apply (unsigned word, unsigned mask_bits, const transport& p,
                      bool inverse = false) const {
        for (size_t b = 0; b < buses.size (); ++b)
          if (mask_bits & mask (b)) {
            const auto v = buses[b].values[code (word, b)];
            if (v.coord != absent) {
              const auto i = p.coord[v.coord];
              const auto& src = cert.buses[b];
              word = put (word, b, v.mode == 0 ? src.codes[i] : *src.aliases[i]);
            }
          }
        for (size_t i = 0; i < p.residual.size (); ++i) {
          const auto [b, a, c] = p.residual[inverse ? p.residual.size () - i - 1 : i];
          if (mask_bits & mask (b)) {
            const auto x = code (word, b);
            if (x == a or x == c)
              word = put (word, b, x == a ? c : a);
          }
        }
        return word;
      }

      // Extend the stabilizer's marked prefix one bus at a time. A free
      // coordinate is represented by the first available member of its cell.
      void moves (bool input, unsigned history, unsigned prefix, std::span<const unsigned> local,
                  auto&& poll, auto&& safe_weight, auto&& emit) const {
        const unsigned owner = owner_mask (input);
        unsigned scalars = owner;
        std::vector<bool> marked (local.size (), false);
        auto mark = [&] (size_t b, unsigned word) {
          const auto i = buses[b].values[code (word, b)].coord;
          if (i != absent)
            marked[i] = true;
        };
        for (size_t b = 0; b < buses.size (); ++b) {
          if (history != absent and (history_mask & mask (b)))
            mark (b, history);
          if (not input and cert.buses[b].is_input)
            mark (b, prefix);
          scalars &= ~mask (b);
        }
        auto visit = [&] (auto&& self, size_t b, unsigned word) -> void {
          if (b == buses.size ()) {
            for (unsigned bits = 0;; bits = (bits - scalars) & scalars) {
              poll ();
              emit (word | bits);
              if (bits == scalars)
                break;
            }
            return;
          }
          if (cert.buses[b].is_input != input) {
            self (self, b + 1, word);
            return;
          }
          const auto previous = history != absent and (history_mask & mask (b))
                                  ? code (history, b) : absent;
          const auto choose = [&] (unsigned x) {
            poll ();
            const auto [i, mode] = buses[b].values[x];
            if (not input and not safe_weight (b, unsigned (mode == 0)))
              return;
            const bool fresh = i != absent and not marked[i];
            if (fresh)
              marked[i] = true;
            self (self, b + 1, put (word, b, x));
            if (fresh)
              marked[i] = false;
          };
          const auto& src = cert.buses[b];
          for (unsigned i = local.size (); i < src.codes.size (); ++i) {
            choose (src.codes[i]);
            if (auto alias = src.aliases[i])
              choose (*alias);
          }
          std::set<unsigned> free_cells;
          for (unsigned i = 0; i < local.size (); ++i)
            if (marked[i] or free_cells.insert (local[i]).second) {
              choose (src.codes[i]);
              if (auto alias = src.aliases[i])
                choose (*alias);
            }
          for (const auto& group : src.unused_classes) {
            if (std::ranges::find (group, previous) == group.end ())
              choose (group.front ());
            else {
              choose (previous);
              if (group.size () > 1)
                choose (group[group.front () == previous]);
            }
          }
        };
        visit (visit, 0, prefix);
      }
  };

  // Borrow the round engine and the checked arena for the optional test replay.
  struct encoded_audit {
      using words = std::vector<unsigned>;
      using view = std::span<const unsigned>;

      const encoded_alphabet& alphabet;
      const spot::twa_graph_ptr& global;
      const std::vector<int>& global_vars;
      const std::vector<int>& event_vars;
      const std::vector<words>& succ;
      const std::vector<words>& witnesses;
      const parity::arena_game& checked;
      const parity::solution& solution;
      bool system_wins, output_safety;
      size_t max_vertices, max_edges;
      view initial, initial_local;
      std::function<void ()> poll;
      std::function<std::tuple<words, words, unsigned, unsigned> (view, view, unsigned)> finish_round;
      std::function<unsigned (view, view)> find_vertex;
      std::function<bool (size_t, unsigned)> safe_weight;
  };
}  // namespace acacia::symcount
