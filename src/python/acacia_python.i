%module acacia_python
%{
#include "python_interface.hh"
%}

// Tell SWIG to wrap std::vector<std::string>
%include "std_string.i"
%include "std_vector.i"
%template(StringVector) std::vector<std::string>;

// this file is included to make Spot objects non-opaque in the Python interface when returned by Acacia functions.
%include "impl.i"
