#pragma once

namespace ios_precomputers {
  namespace detail {
    template <typename Aut, typename TransSet>
    class standard_container {
      public:
        standard_container (Aut aut,
                            bdd input_support, bdd output_support, int verbose) :
          aut {aut}, input_support {input_support}, output_support {output_support}, verbose {verbose}
        { }

      private:
        Aut aut;
        bdd input_support, output_support;
        int verbose;

        class bdd_it :
          public std::iterator<std::input_iterator_tag, bdd> {
          public:
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


        using ios_it_actual_type = std::iterator<std::input_iterator_tag,
                                                 TransSet>;

        class ios_it : public ios_it_actual_type,
                       public bdd_it {
          public:
            ios_it (bdd input, bdd output_support, Aut aut) :
              bdd_it (output_support), input {input}, aut {aut}
            { update_transset (); }

            auto operator* () const {
              return current_io;
            }

          private:
            virtual void get_next_letter () {
              bdd_it::get_next_letter ();
              update_transset ();
            }

            void update_transset () {
              bdd letter = input & bdd_it::current_letter;
              current_io.clear ();
              for (size_t p = 0; p < aut->num_states (); ++p) {
                for (const auto& e : aut->out (p)) {
                  unsigned q = e.dst;
                  if ((e.cond & letter) != bddfalse)
                    current_io.push_back (std::pair (p, q));
                }
              }
            }

            bdd input;
            TransSet current_io;
            Aut aut;
        };

        class ios {
          public:
            ios (bdd input, bdd output_support, Aut aut) :
              input {input}, output_support {output_support}, aut {aut} {}
            ios (ios&& rhs) : input {rhs.input}, output_support {rhs.output_support}, aut {rhs.aut} {}
            ios& operator= (ios&&) = default;
            ios_it begin () const { return ios_it (input, output_support, aut); }
            ios_it end ()   const { return ios_it (bddfalse, bddfalse, aut); }
          private:
            bdd input, output_support;
            Aut aut;
        };

        using in_it_actual_type = typename std::iterator<std::input_iterator_tag,
                                                         std::pair<bdd, ios>>;
        class in_it : public in_it_actual_type,
                      public bdd_it {
          public:
            in_it (bdd input_support, bdd output_support, Aut aut) :
              bdd_it (input_support),
              current_ios (bdd_it::current_letter, ios (bdd_it::current_letter, output_support, aut)),
              output_support {output_support}, aut {aut}
            { }

            auto& operator* ()  { return current_ios; }
            auto* operator-> () { return &current_ios; }
          private:
            virtual void get_next_letter () {
              bdd_it::get_next_letter ();
              auto theios = std::pair (bdd_it::current_letter, ios (bdd_it::current_letter, output_support, aut));
              current_ios = std::move (theios);
            }
            std::pair<bdd, ios> current_ios;
            bdd output_support;
            Aut aut;
        };

      public:
        in_it begin () const { return in_it (input_support, output_support, aut); }
        in_it end () const { return in_it (bddfalse, bddfalse, aut); }
    };
  }

  struct standard {
      template <typename Aut, typename TransSet = std::vector<std::pair<int, int>>>
      static auto make (Aut aut,
                        bdd input_support, bdd output_support, int verbose) {
        return [&] () {
          return detail::standard_container<Aut, TransSet> (aut, input_support, output_support, verbose);
        };
      }
  };
}
