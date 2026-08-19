#pragma once

#include <spot/misc/optionmap.hh>

namespace acacia::translation {
  inline spot::option_map make_options () {
    spot::option_map options;
    options.set ("simul", 0);
    options.set ("ba-simul", 0);
    options.set ("det-simul", 0);
    options.set ("tls-impl", 1);
    options.set ("wdba-minimize", 2);
    return options;
  }

  // spot::translator consumes its known options in its constructor.  Calling
  // this immediately afterward turns unknown names into a visible exception
  // instead of silently benchmarking a misspelled no-op.
  inline void validate_options (const spot::option_map& options) {
    options.report_unused_options ();
  }
}  // namespace acacia::translation
