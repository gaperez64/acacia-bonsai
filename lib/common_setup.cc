// -*- coding: utf-8 -*-
// Copyright (C) 2012-2020 Laboratoire de Recherche et Développement
// de l'Epita (LRDE).
//
// This file is part of Spot, a model checking library.
//
// Spot is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// Spot is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
// or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
// License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include "common_setup.hh"

// #include <error.h>
#include "error_msg.hh"
#include <cstdlib>
#include <unistd.h>
#include <iostream>
#include <signal.h>
#include <sys/wait.h>
#include <spot/misc/tmpfile.hh>

// This is called on normal exit (i.e., when leaving
// main or calling exit(), even via error()).
static void atexit_cleanup()
{
  spot::cleanup_tmpfiles();
}

#ifdef HAVE_SIGACTION
// This is called on abnormal exit, i.e. when the process is killed by
// some signal.
static void sig_handler(int sig)
{
  spot::cleanup_tmpfiles();
  // Send the signal again, this time to the default handler, so that
  // we return a meaningful error code.
  raise(sig);
}

static void setup_sig_handler()
{
  struct sigaction sa;
  sa.sa_handler = sig_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESETHAND;
  // Catch termination signals, so we can cleanup temporary files.
  sigaction(SIGALRM, &sa, nullptr);
  sigaction(SIGHUP, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);
  sigaction(SIGPIPE, &sa, nullptr);
  sigaction(SIGQUIT, &sa, nullptr);
  sigaction(SIGTERM, &sa, nullptr);
}
#else
# define setup_sig_handler() while (0);
#endif


static void bad_alloc_handler()
{
  std::set_new_handler(nullptr);
  std::cerr << "not enough memory\n";
  abort();
}

void
setup(char** argv)
{
  if (getenv("SPOT_OOM_ABORT"))
    std::set_new_handler(bad_alloc_handler);

  std::ios_base::sync_with_stdio(false);
  // Do not flush std::cout every time we read from std::cin, unless
  // we are reading from a terminal.  Note that we do flush regularly
  // in check_cout().
  if (!isatty(STDIN_FILENO))
    std::cin.tie(nullptr);

  // setup_default_output_format();
  setup_sig_handler();
  atexit(atexit_cleanup);
}

[[noreturn]] void handle_any_exception()
{
  try
    {
      throw;
    }
  catch (const std::exception& e)
    {
      error(2, 0, "%s", e.what());
    }
  SPOT_UNREACHABLE();
}

int protected_main(char** progname, std::function<int()> mainfun)
{
  try
    {
      setup(progname);
      return mainfun();
    }
  catch (...)
    {
      handle_any_exception();
    }
  SPOT_UNREACHABLE();
  return 2;
}
