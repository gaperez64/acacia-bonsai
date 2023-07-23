#pragma once

namespace ios_precomputers {
  namespace detail {
    template <typename Aut, typename TransSet>
    class standard_container {
      public:
        standard_container (Aut aut,
                            bdd input_support, bdd output_support, bdd invariant) :
          aut {aut}, input_support {input_support}, output_support {output_support}, invariant {invariant}
        { }

      private:
        Aut aut;
        bdd input_support, output_support;
        bdd invariant;

        class bdd_it {
          public:
            using iterator_category = std::input_iterator_tag;
            using value_type = bdd;

            bdd_it (bdd support) :
              letter_set {bddtrue},
              support {support}
            { get_next_letter (); }

            virtual ~bdd_it () = default;

            bdd_it& operator++()
            {
              get_next_letter ();
              return *this;
            }

            auto operator* () const {
              return current_letter;
            }

            bool operator== (const bdd_it& rhs) const {
              return letter_set == rhs.letter_set &&
                support == rhs.support;
            }
            bool operator!= (const bdd_it& rhs) const {
              return !(operator== (rhs));
            }

          protected:
            virtual void get_next_letter () {
              if (letter_set == bddfalse)
                support = bddfalse;
              else {
                current_letter = bdd_satoneset (letter_set,
                                                support,
                                                bddtrue);
                letter_set -= current_letter;
              }
            }
            bdd letter_set, support, current_letter;
        };

        // class that stores vector<pair<p, q>> and also the IO, iterating over it iterates over the (p, q) pairs
        struct transitions_io_pair {
        public:
          TransSet transitions;
          bdd IO;

          auto begin () const {
            return transitions.begin ();
          }
          auto end () const {
            return transitions.end ();
          }

          auto& operator[] (size_t i) {
            return transitions[i];
          }
        };

        class ios_it : public bdd_it {
          public:
            using iterator_category = std::input_iterator_tag;
            using value_type = TransSet;

            ios_it (bdd input, bdd output_support, Aut aut, bdd inv) :
              bdd_it (output_support), input {input}, aut {aut}
            {
              // set the invariant to include the input, then keep iterating until we find the first output valuation
              // that satisfies the invariant
              invariant = inv & input;
              while (((invariant & bdd_it::current_letter) == bddfalse) && (bdd_it::support != bddfalse)) {
                bdd_it::get_next_letter ();
              }
              update_transset ();
            }

            auto operator* () const {
              // return current_io (vector<pair<p, q>>) AND the IO compatible with I that gave this action
              return transitions_io_pair(current_io, letter);
            }

          private:
            virtual void get_next_letter () {
              // not just the next letter, but the next letter that satisfies the invariant
              do {
                bdd_it::get_next_letter ();
              }
              while (((invariant & bdd_it::current_letter) == bddfalse) && (bdd_it::support != bddfalse));
              update_transset ();
            }

            void update_transset () {
              // this updates current_io (set of (p, q) pairs)
              letter = input & bdd_it::current_letter;
              current_io.clear ();
              for (size_t p = 0; p < aut->num_states (); ++p) {
                for (const auto& e : aut->out (p)) {
                  unsigned q = e.dst;
                  if ((e.cond & letter) != bddfalse) {
                      current_io.push_back (std::pair (p, q));
                  }
                }
              }
            }

            bdd input;
            bdd letter;
            TransSet current_io;
            Aut aut;

            bdd invariant; // includes input
        };

        class ios {
          public:
            ios (bdd input, bdd output_support, Aut aut, bdd invariant) :
              input {input}, output_support {output_support}, aut {aut}, invariant {invariant} { }
            ios (ios&& rhs) : input {rhs.input}, output_support {rhs.output_support}, aut {rhs.aut}, invariant {rhs.invariant} {}
            ios& operator= (ios&&) = default;
            ios_it begin () const { return ios_it (input, output_support, aut, invariant); }
            ios_it end ()   const { return ios_it (bddfalse, bddfalse, aut, invariant); }
          private:
            bdd input, output_support;
            Aut aut;
            bdd invariant;
        };

        class in_it : public bdd_it {
          public:
            using iterator_category = std::input_iterator_tag;
            using value_type = std::pair<bdd, ios>;

            in_it (bdd input_support, bdd output_support, Aut aut, bdd _invariant) :
              bdd_it (input_support),
              current_ios (bdd_it::current_letter, ios (bdd_it::current_letter, output_support, aut, _invariant)),
              output_support {output_support}, aut {aut}, invariant {_invariant}
            { }

            auto& operator* ()  { return current_ios; }
            auto* operator-> () { return &current_ios; }
          private:
            virtual void get_next_letter () {
              bdd_it::get_next_letter ();
              auto theios = std::pair (bdd_it::current_letter, ios (bdd_it::current_letter, output_support, aut, invariant));
              current_ios = std::move (theios);
            }
            std::pair<bdd, ios> current_ios;
            bdd output_support;
            Aut aut;
            bdd invariant;
        };

      public:
        in_it begin () const { return in_it (input_support, output_support, aut, invariant); }
        in_it end () const { return in_it (bddfalse, bddfalse, aut, invariant); }
    };
  }

  struct standard {
    static const bool supports_invariant = true; // note: this is only true for this implementation right now, false for the other ios_precomputers

      template <typename Aut, typename TransSet = std::vector<std::pair<int, int>>>
      static auto make (Aut aut,
                        bdd input_support, bdd output_support, bdd invariant) {
        return [&] () {
          return detail::standard_container<Aut, TransSet> (aut, input_support, output_support, invariant);
        };
      }
  };
}
