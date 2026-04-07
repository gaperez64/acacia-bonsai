%module acacia_python
%{
#include "python_interface.hh"
%}

// Tell SWIG to wrap std::vector<std::string>
%include "std_string.i"
%include "std_vector.i"
%template(StringVector) std::vector<std::string>;

// this file is included to make Spot objects non-opaque in the Python interface when returned by Acacia functions.
// NOTE: this introduces ALL Spot functionality into the "acacia_python" package
%include "impl.i"

// this generates the Python bindings
%include "python_interface.hh"


%extend VECTOR_iter {
    vector_iterator *__iter__() {
        return $self;
    }

    VECTOR_ELT_T __next__() {
        if (!$self->has_next()) {
            PyErr_SetNone(PyExc_StopIteration);
            return 0;
        }
        return $self->next();
    }

    vector_iterator __iter__() {
        return vector_iterator(*$self);
    }
}

%extend winreg_iterator {
    winreg_iterator *__iter__() {
        return $self;
    }

    const vector_iterator& __next__() {
        if (!$self->has_next()) {
            PyErr_SetNone(PyExc_StopIteration);
            return *(vector_iterator*)0; // never used
        }
        return $self->next();
    }

    winreg_iterator __iter__() {
        return winreg_iterator(*$self);
    }
}

