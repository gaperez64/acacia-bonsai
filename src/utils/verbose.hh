#pragma once
#include <iostream>
#include <sstream>

namespace utils {
  // https://stackoverflow.com/questions/37490881/overloading-operator-in-c-with-a-prefix
  class prefixstringbuf : public std::stringbuf {
    private:
      std::ostream &outstream;
      bool prev_ended_in_eol = true;
      std::string prefix = "";

    protected:
      virtual int sync()
      {
        int ret = std::stringbuf::sync();
        bool ends_in_eol = (view ().back () == '\n');
        std::istringstream s (str ());
        str (""); // erase buffer

        bool first_line = true;
        for (std::string line; std::getline (s, line); ) {
          if (first_line and not prev_ended_in_eol)
            outstream << line;
          else
            outstream << prefix << line;
          first_line = false;
          if (not s.eof () or ends_in_eol)
            outstream << std::endl;
        }
        outstream << std::flush;
        prev_ended_in_eol = ends_in_eol;
        return ret;
      };

    public:
      prefixstringbuf(std::ostream &outstream)
        : std::stringbuf (std::ios_base::out), outstream (outstream) {}

      ~prefixstringbuf() { sync (); }

      void set_prefix (std::string s) { prefix = s; }
  };

  class voutstream : public std::ostream {
    private:
      prefixstringbuf buf;

    public:
      voutstream () : std::ostream(0), buf (std::cout) { init (&buf); }

      template<typename T>
      std::ostream& operator<< (const T& data) {
        return static_cast<std::ostream&>(*this) << data;
      }

      void set_prefix (const std::string& s) { buf.set_prefix (s); }
  };

  extern voutstream vout;
  extern int verbose;
}

#ifndef NO_VERBOSE
# define verb_do(level, acts...) do { \
    using namespace utils;           \
    if (verbose >= level) {          \
      acts;                          \
    }                                \
  } while (0)
#else
# define verb_do(x...)
#endif
