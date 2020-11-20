#include "basic_antichain.hh"
#include <iostream>

std::ostream& operator<<(std::ostream& os, const basic_antichain::vector& v)
{
  os << "{ ";
  for (auto el : v)
    os << el << " ";
  os << "}";
  return os;
}

std::ostream& operator<<(std::ostream& os, const basic_antichain::set& f)
{
  for (auto el : f.vector_set)
    os << el << std::endl;

  return os;
}
