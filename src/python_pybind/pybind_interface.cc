

#include <stddef.h>

#include "pybind_interface.hh"



int add(int i, int j) {
  return i + j;
}

class CharIterator {
  private:
    const char* cur;
    const char* end;

  public:
    CharIterator(const char* data, size_t size) : cur(data), end(data + size) {}

    bool has_next() const {
      return cur != end;
    }

    char next() {
      if (cur == end) {
        throw py::stop_iteration();  // raise StopIteration in Python
      }
      return *cur++;
    }
};

// A wrapper container to hold the data and provide __iter__
class CharContainer {
  private:
    const char* data;
    size_t size;

  public:
    CharContainer(const char* d, size_t s) : data(d), size(s) {}

    CharIterator iter() const {
      return CharIterator(data, size);
    }

    size_t len() const { return size; }
};



#include <utility>

// #include "python_interface.hh"
#include "solver/solver_invoker.hh"

// TODO: this is to get the verbose debug printing working, needs to refactored out.
utils::voutstream utils::vout;
int               utils::verbose = 0;

// TODO We need to figure out some clean way to define these, instead of copying them everywhere
size_t posets::vectors::bool_threshold = 0;
size_t posets::vectors::bitset_threshold = 0;


io_spec get_io_spec(const std::vector<std::string>& input_aps, const std::vector<std::string>& output_aps) {
  return io_spec{.input_aps = input_aps, .output_aps = output_aps};
}

void prep_unreal_formula (spot::formula& formula, std::vector<std::string>& output_aps) {
  add_x_to_outputs (formula, output_aps);
}

bdd_io_spec create_bdds (const io_spec& io_spec) {
  // Set up the dictionary now: BuDDy's initialization
  spot::bdd_dict_ptr dict = spot::make_bdd_dict ();

  bdd all_inputs = bddtrue;
  bdd all_outputs = bddtrue;
  for (std::string ap : io_spec.input_aps) {
    // TODO: make signed int, that is what reg_prop returns
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), nullptr);
    all_inputs &= bdd_ithvar (v);
  }
  for (std::string ap : io_spec.output_aps) {
    const unsigned v = dict->register_proposition (spot::formula::ap (ap), nullptr);
    all_outputs &= bdd_ithvar (v);
  }

  return bdd_io_spec {.inputs = all_inputs, .outputs = all_outputs, .dict = dict};
}

// spot::twa_graph_ptr create_twa (spot::formula& formula, bdd_io_spec& io_spec) {
// TODO: not yet compatible with spot::formula
spot::twa_graph_ptr create_twa(py::object formula, bdd_io_spec& io_spec) {
  spot::formula& f = formula.cast<spot::formula&>();

  spot::option_map extra_options;
  extra_options.set ("simul", 0);
  extra_options.set ("ba-simul", 0);
  extra_options.set ("det-simul", 0);
  extra_options.set ("tls-impl", 1);
  extra_options.set ("wdba-minimize", 2);

  spot::translator trans (io_spec.dict, &extra_options);

  auto aut = create_automaton (f, trans);

  return aut;
}

void prep_unreal_automaton (spot::twa_graph_ptr twa, bdd_io_spec& io_spec) {
  push_outputs (twa, io_spec.inputs, io_spec.outputs);
}

void preprocess_aut_standard (spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max) {
  aut_preprocessors::standard::make (twa, io_spec.inputs,
    io_spec.outputs, k_max) ();
}

void preprocess_aut_surely_losing (spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max) {
  aut_preprocessors::surely_losing::make (twa, io_spec.inputs,
      io_spec.outputs, k_max) ();
}

void set_bool_thresh_no_bool_states (spot::twa_graph_ptr twa, int k_max) {
  posets::vectors::bool_threshold = boolean_states::no_boolean_states::make (twa, k_max)();
}

void set_bool_thresh_forward_saturation (spot::twa_graph_ptr twa, int k_max) {
  posets::vectors::bool_threshold = boolean_states::forward_saturation::make (twa, k_max)();
}

bool solve_acacia_safety_game (spot::twa_graph_ptr twa, bdd_io_spec& io_spec, int k_max, int k_min, int k_inc) {
  bool res = solve_game (twa, k_max, k_min, k_inc, io_spec.inputs, io_spec.outputs);
  return res;
}

const winreg_iterator* get_winning_region_of_game (spot::twa_graph_ptr twa, bdd_io_spec& io_spec,
                                                   int k_max, int k_min, int k_inc) {
  auto winning_region = get_winning_region (twa, k_max, k_min, k_inc, io_spec.inputs, io_spec.outputs);

  if (winning_region.has_value ()) {
    // return iterator that has access to the winning region
    return new winreg_iterator{winning_region.value ()};
  }

  return nullptr;
}





PYBIND11_MODULE(acacia_bonsai_pybind, m) {
  m.doc() = "pybind11 example plugin"; // optional module docstring

  m.def("add", &add, "A function that adds two numbers");

  py::class_<CharIterator>(m, "CharIterator")
      .def("__iter__", [](CharIterator &self) -> CharIterator& { return self; })
      .def("__next__", &CharIterator::next);

  py::class_<CharContainer>(m, "CharContainer")
      .def(py::init<const char*, size_t>())
      .def("__iter__", &CharContainer::iter)
      .def("__len__", &CharContainer::len);

  py::class_<io_spec>(m, "io_spec")
      .def(py::init<>())  // default constructor
      .def_readwrite("input_aps", &io_spec::input_aps)
      .def_readwrite("output_aps", &io_spec::output_aps);

  // Expose get_io_spec function
  m.def("get_io_spec", &get_io_spec,
        py::arg("input_aps"),
        py::arg("output_aps"),
        "Create an io_spec from input and output AP lists");


  // Expose bdd as opaque (Python will see it as a type)
  py::class_<bdd>(m, "bdd");

  // Expose spot::bdd_dict_ptr as opaque
  py::class_<spot::bdd_dict, std::shared_ptr<spot::bdd_dict>>(m, "bdd_dict");

  // py::class_<spot::formula>(m, "formula");
  // py::class_<spot::twa_graph, spot::twa_graph_ptr>(m, "twa_graph");

  // Expose bdd_io_spec
  py::class_<bdd_io_spec>(m, "bdd_io_spec")
      .def(py::init<>())
      .def_readwrite("inputs", &bdd_io_spec::inputs)
      .def_readwrite("outputs", &bdd_io_spec::outputs)
      .def_readwrite("dict", &bdd_io_spec::dict);

  // Expose prep_unreal_formula
  m.def("prep_unreal_formula", &prep_unreal_formula,
        py::arg("formula"),
        py::arg("output_aps"),
        "Prepare a formula for UNREAL_X_FORMULA using the given outputs");

  m.def("create_bdds", &create_bdds,
      py::arg("output_aps"),
      "Create a bdd_io_spec from an io_spec object");


  // Expose functions
  m.def("create_twa", &create_twa,
// TODO: make interoperable with SPOT formula
        py::arg("formula"), py::arg("io_spec"),
        "Create a TWA from a formula and bdd_io_spec");

  m.def("prep_unreal_automaton", &prep_unreal_automaton,
        py::arg("twa"), py::arg("io_spec"),
        "Prepare the TWA for UNREAL_X_AUTOMATON");

  m.def("preprocess_aut_standard", &preprocess_aut_standard,
        py::arg("twa"), py::arg("io_spec"), py::arg("k_max"),
        "Preprocess automaton standard");

  m.def("preprocess_aut_surely_losing", &preprocess_aut_surely_losing,
        py::arg("twa"), py::arg("io_spec"), py::arg("k_max"),
        "Preprocess automaton surely losing");

  m.def("set_bool_thresh_no_bool_states", &set_bool_thresh_no_bool_states,
        py::arg("twa"), py::arg("k_max"),
        "Set boolean threshold without boolean states");

  m.def("set_bool_thresh_forward_saturation", &set_bool_thresh_forward_saturation,
        py::arg("twa"), py::arg("k_max"),
        "Set boolean threshold forward saturation");

}