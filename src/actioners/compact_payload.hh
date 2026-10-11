#pragma once

#include "solver/transition_payload.hh"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <utility>
#include <vector>

namespace actioners {
  // A single allocation holds the state count, sorted destination/offset records,
  // and endpoints. Empty rows have no records. The mutable action list still owns
  // vector-sized handles, so moving an action to the front never moves its payload.
  class compact_payload {
      struct endpoint {
          unsigned source;
          bool increment;
      };
      // Index words and endpoint words are always read through their active member.
      union word {
          uint64_t index;
          endpoint edge;
          word () : index {0} {}
      };
      static_assert (sizeof (unsigned) == sizeof (uint32_t));
      static_assert (sizeof (word) == sizeof (uint64_t));
      std::vector<word> storage;

      static compact_payload allocate (unsigned states, std::vector<size_t>& cursors) {
        compact_payload result;
        if (states == 0)
          return result;
        size_t rows = 0, endpoints = 0;
        for (const auto count : cursors) {
          rows += count != 0;
          endpoints += count;
        }
        const size_t base = 1 + 2 * rows;
        result.storage.resize (base + endpoints);
        result.storage[0].index = uint64_t (states) | (uint64_t (rows) << 32);
        size_t row = 0, offset = base;
        for (unsigned destination = 0; destination < states; ++destination) {
          const size_t count = cursors[destination];
          if (count) {
            result.storage[1 + 2 * row].index = destination;
            result.storage[2 + 2 * row++].index = offset;
          }
          cursors[destination] = offset;
          offset += count;
        }
        return result;
      }

    public:
      class row_view {
          const word* data = nullptr;
          size_t count = 0;

        public:
          class iterator {
              const word* position = nullptr;

            public:
              using iterator_category = std::forward_iterator_tag;
              using value_type = std::pair<unsigned, bool>;
              using reference = value_type;
              using difference_type = std::ptrdiff_t;
              iterator () = default;
              explicit iterator (const word* position) : position {position} {}
              value_type operator* () const {
                return {position->edge.source, position->edge.increment};
              }
              iterator& operator++ () {
                ++position;
                return *this;
              }
              iterator operator++ (int) {
                auto previous = *this;
                ++*this;
                return previous;
              }
              friend bool operator== (const iterator&, const iterator&) = default;
          };
          row_view () = default;
          row_view (const word* data, size_t count) : data {data}, count {count} {}
          size_t size () const { return count; }
          bool empty () const { return count == 0; }
          auto begin () const { return iterator (data); }
          auto end () const { return iterator (count ? data + count : data); }
          auto operator[] (size_t i) const {
            return std::pair (data[i].edge.source, data[i].edge.increment);
          }
      };
      using value_type = row_view;
      class iterator {
          const compact_payload* payload = nullptr;
          size_t destination = 0;

        public:
          using iterator_category = std::forward_iterator_tag;
          using value_type = row_view;
          using reference = value_type;
          using difference_type = std::ptrdiff_t;
          iterator () = default;
          iterator (const compact_payload* payload, size_t destination)
            : payload {payload},
              destination {destination} {}
          value_type operator* () const { return (*payload)[destination]; }
          iterator& operator++ () {
            ++destination;
            return *this;
          }
          iterator operator++ (int) {
            auto previous = *this;
            ++*this;
            return previous;
          }
          friend bool operator== (const iterator&, const iterator&) = default;
      };

      template <typename Set, typename Aut>
      static compact_payload make (unsigned states, const Set& transitions, const Aut& aut) {
        compact_payload result;
        if (states == 0)
          return result;
        std::vector<size_t> cursors (states);
        for (const auto& t : transitions)
          ++cursors[acacia::transitions::dest (t)];
        result = allocate (states, cursors);
        // The second pass preserves the original order within each destination.
        for (const auto& t : transitions)
          result.storage[cursors[acacia::transitions::dest (t)]++].edge = {
              acacia::transitions::source (t), acacia::transitions::increment (t, aut)};
        return result;
      }

      template <typename Rows>
      static compact_payload from_rows (const Rows& rows) {
        std::vector<size_t> cursors (rows.size ());
        for (size_t destination = 0; destination < rows.size (); ++destination)
          cursors[destination] = rows[destination].size ();
        auto result = allocate (rows.size (), cursors);
        for (size_t destination = 0; destination < rows.size (); ++destination)
          for (const auto& [source, increment] : rows[destination])
            result.storage[cursors[destination]++].edge = {source, increment};
        return result;
      }

      size_t size () const { return storage.empty () ? 0 : uint32_t (storage[0].index); }
      bool empty () const { return size () == 0; }
      size_t nonempty_size () const { return storage.empty () ? 0 : storage[0].index >> 32; }
      unsigned destination (size_t row) const { return storage[1 + 2 * row].index; }
      row_view nonempty_row (size_t row) const {
        const size_t begin = storage[2 + 2 * row].index;
        const size_t end =
            row + 1 < nonempty_size () ? storage[4 + 2 * row].index : storage.size ();
        return {storage.data () + begin, end - begin};
      }
      row_view operator[] (size_t destination) const {
        size_t first = 0, last = nonempty_size ();
        while (first < last) {
          const size_t middle = first + (last - first) / 2;
          if (this->destination (middle) < destination)
            first = middle + 1;
          else
            last = middle;
        }
        return first < nonempty_size () and this->destination (first) == destination
                   ? nonempty_row (first)
                   : row_view {};
      }
      auto begin () const { return iterator (this, 0); }
      auto end () const { return iterator (this, size ()); }
      size_t endpoint_count () const {
        return storage.empty () ? 0 : storage.size () - (1 + 2 * nonempty_size ());
      }
      size_t endpoint_bytes () const {
        return storage.empty ()
                   ? 0
                   : (storage.capacity () - (1 + 2 * nonempty_size ())) * sizeof (word);
      }
      size_t payload_bytes () const {
        return sizeof (*this) + storage.capacity () * sizeof (word);
      }

      friend std::strong_ordering operator<=> (const compact_payload& a,
                                               const compact_payload& b) {
        size_t i = 0, j = 0;
        const size_t limit = std::min (a.size (), b.size ());
        while (true) {
          const size_t left = i < a.nonempty_size () ? a.destination (i) : limit;
          const size_t right = j < b.nonempty_size () ? b.destination (j) : limit;
          if (std::min (left, right) >= limit)
            break;
          // At the first differing destination, an empty row sorts first,
          // exactly as in the former vector of row vectors.
          if (left != right)
            return left < right ? std::strong_ordering::greater : std::strong_ordering::less;
          const auto l = a.nonempty_row (i++), r = b.nonempty_row (j++);
          const auto order =
              std::lexicographical_compare_three_way (l.begin (), l.end (), r.begin (), r.end ());
          if (order != 0)
            return order;
        }
        return a.size () <=> b.size ();
      }
      friend bool operator== (const compact_payload& a, const compact_payload& b) {
        return (a <=> b) == 0;
      }
  };
}
