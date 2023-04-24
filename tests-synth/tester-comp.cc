#include <iostream>

int main (int argc, char** argv) {
  if (argc != 2) return -1; // pass one TLSF file
  std::string fname = argv[1];
  std::cout << "Testing fname = " << fname << std::endl;
  int ret = std::system (("../tests-synth/process-comp.sh \"" + fname +"\"").c_str());
  std::cout << "Retcode: " << ret << std::endl;
  return ret == 0 ? 0 : -1; // -1 -> FAIL
}
