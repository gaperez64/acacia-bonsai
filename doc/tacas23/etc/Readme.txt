# -*- Org -*-

This is the Readme.txt file for Acacia-Bonsai's Artifact for TACAS'23, written
November 2022.

Artifact link:           https://doi.org/10.5281/zenodo.7296659
Repository URL:          https://github.com/gaperez64/acacia-bonsai/
License:                 GPLv3

Additional requirements: Internet connection to install dependencies
Experiment Runtime:      Compilation: 1 hour, Benchmarking: 9 hours

* At a glimpse: suggested process

In the coming sections of this README, all of the following commands are
explained and justified.  We sum up the process we suggest the reviewer follows
here for convenience.

  $ cd ~; unzip acacia-bonsai-tacas23.zip

  $ sudo ./bootstrap.sh
  [type password (tacas23) and wait for completion]

  $ cd acacia-bonsai

  $ ./self-benchmark.sh -b ab/syntcomp21/0s-20s -t 1
  [wait 8 hours for completion]

  $ for tool in ab strix ltlsynt aca+; do \
      ./self-benchmark.sh -c best -b $tool/syntcomp21/0s-20s -t 1; \
      mv _bm-logs/best.json _bm-logs/$tool.json; \
    done
  [wait 1 hour for completion]

  $ mkdir mkplottable

  $ for f in _bm-logs/*.json; do \
      ../doc/benchmarks/meson-to-mkplot.sh ${f/json/} $f > mkplottable/$f; \
    done

  $ ~/mkplot/mkplot.py --lloc='upper left' --ymin=1e-2 --ylog -b pdf --save-to plot.pdf mkplottable/*.json
    [~/acacia-bonsai/_bm-logs/plot.pdf contains a plot of the benchmarking of
     the different options of Acacia-Bonsai and other tools.]


The last command creates plots based on a set of benchmarks.  To plot
Acacia-Bonsai against other tools, use, instead of mkplottable/*.json,
mkplottable/{ab,strix,ltlsynt,aca+}.json .


* Installing the artifact and the project's dependencies

Extract the artifact in the VM.  Throughout this document, we assume that the
artifact was extracted in ~ (i.e., /home/tacas23).

  $ cd ~
  $ unzip acacia-bonsai-tacas23.zip

Then run the helper script bootstrap.sh which will install dependencies and run
tiny tests on Acacia-Bonsai and the other software against which Acacia-Bonsai
is benchmarked:

  $ sudo ./bootstrap.sh

The ~/deps/ folder contains the dependencies that were installed:
- Spot 2.11.2 (https://spot.lrde.epita.fr/).  The bootstrap script installs this
version.  If Spot weren't found during the build of Acacia-Bonsai, the build
system would have downloaded this version of Spot and compiled it.  To avoid
this compilation to happen with every build of Acacia-Bonsai, we elected to
install it.
- Strix 21.0.0 (https://github.com/meyerphi/strix).  This is another LTL
synthesizer.
- Python-Graph (https://github.com/Shoobx/python-graph).  This is a Python
package for graphs that is used by Acacia+.

The zip file also contains mkplot (https://github.com/alexeyignatiev/mkplot), a
tool to produce cactus plots.

* Running & benchmarking Acacia-Bonsai

Acacia-Bonsai relies on the Meson build system <https://mesonbuild.com/>.  To
compile and run Acacia-Bonsai, the simplest route is:

  $ cd ~/acacia-bonsai/
  $ meson build
  $ cd build
  $ meson compile
  $ src/acacia-bonsai –help
  Usage: acacia-bonsai [OPTION...]
  Verify realizability for LTL specification.
  
    -f, --formula=STRING       process the formula STRING
    [...]  
   Input options:
    -i, --ins=PROPS            comma-separated list of uncontrollable (a.k.a.
                               input) atomic propositions
    -o, --outs=PROPS           comma-separated list of controllable (a.k.a.
                               output) atomic propositions
    [...]

To check if a formula is realizable, one can simply run, for instance:

  $ cd ~/acacia-bonsai/build/
  $ src/acacia-bonsai -f '((G (F (req))) -> (G (F (grant))))' --ins req --outs grant
  REALIZABLE

The source tree contains a copy of all the LTL files used in the SyntComp21
<http://syntcomp.org/> competition, with a description of which symbols are
inputs and outputs in a .part file.  As it can be cumbersome to extract the
inputs/outputs manually, a script is provided to do this automatically.  For
instance:

  $ cd ~/acacia-bonsai/build/
  $ tests/check-real-correct.sh -a -F ../tests/ltl/syntcomp21/lilydemo08.ltl
  Running Acacia Bonsai...
  src/acacia-bonsai -c BOTH -F ../tests/ltl/syntcomp21/lilydemo08.ltl --ins req --outs grant
  REALIZABLE

(The -a flag indicates which LTL synthesizer is used.)

The file self-benchmark.sh, at the root of the project, wraps about 25 different
compilation options in a convenient script that builds, compiles, and benchmarks
them:

  $ cd ~/acacia-bonsai/
  $ ./self-benchmark.sh –help
  usage: ./self-benchmark.sh [-hplBCR] [-b BENCHMARK] [-c CONF[,CONF,...]]
    -h: Print this message.
    -p: Do not build/compile/benchmark, instead, print the CXXFLAGS.
    -l: Do not build/compile/benchmark, instead, list configurations.
    -B: Do not build.
    -C: Do not compile.
    -R: Do not benchmark.
    -b BENCHMARK: Run a specific benchmark suite (default: ab/syntcomp21/crit).
    -t TIMEOUT: Use timeout factor TIMEOUT (default: 1.7).  Actual time is multiplied by 10.
    -c CONF,...: Only consider configurations listed.

Running the script with no argument builds, compiles, and benchmarks the 25
compilation options.  By default, the test suite is a subset of tests that are
hard for most tools.  Logs and benchmarking results are stored in the folder
_bm-logs.

To experiment with the version of Acacia-Bonsai with best performances, one can
use the "best" configuration:

  $ ./self-benchmark.sh -c best

To see which CXXFLAGS were used, the option '-p' can be passed to
self-benchmark.sh.

* Benchmarks appearing in the paper and replication

** Comparing the 25 configurations of Acacia-Bonsai

The Meson build file at acacia-bonsai/tests/meson.build contains the list of all
test & benchmark suites available.  A suite simply is a set of LTL files.  For
all of our benchmarking, we used the test suite containing all the 940 instances
of SyntComp21.  To replicate the benchmark, one can use:

  $ ./self-benchmark.sh -b ab/syntcomp21/all -t 2

We do NOT recommend that the reviewer starts this many tests as this can take up
to 130 hours.  Instead, they can obtain similar results by benchmarking only the
ab/syntcomp21/0s-20s test suite and putting a time out at 10s:

  $ ./self-benchmark.sh -b ab/syntcomp21/0s-20s -t 1

This test suite contains about 90 instances.  With that time limit and the 25
configurations being tested, all the benchmarks should run in at most 7 hours.
Compiling the 25 configurations, which is also done by the script, should take
at most 1 hour.

** Comparing Acacia-Bonsai with Strix, ltlsynt, and Acacia+

To compare Acacia-Bonsai with Strix, ltlsynt, and Acacia+, the same test suite
was used.  The test script allows testing for the different tools by varying the
'ab' prefix of the test suite:

  $ rm -f build_best/benchmarked; ./self-benchmark.sh -b ab/syntcomp21/all -t 6 -c best
  [... runs Acacia-Bonsai]
  $ cp _bm-logs/best.json acacia-bonsai.json
  
  $ rm -f build_best/benchmarked; ./self-benchmark.sh -b strix/syntcomp21/all -t 6 -c best
  [... runs Strix]
  $ cp _bm-logs/best.json strix.json
  
  $ rm -f build_best/benchmarked; ./self-benchmark.sh -b ltlsynt/syntcomp21/all -t 6 -c best
  [... runs ltlsynt]
  $ cp _bm-logs/best.json ltlsynt.json
  
  $ rm -f build_best/benchmarked; ./self-benchmark.sh -b aca+/syntcomp21/all -t 6 -c best
  [... runs Acacia+]
  $ cp _bm-logs/best.json aca+.json
  
Similarly, if the reviewer wishes to execute the benchmarks for the other tools,
we encourage them to replace 'syntcomp21/all' with 'syntcomp21/0s-20s' and
'-t 6' with '-t 1' to obtain reasonable running times (approx. 1 hour for all
tools).
  
** Generating the plots

Once a few JSON files have been produced in _bm-logs/, one can convert the files
to a format that mkplot understands.  To convert one JSON from the test output
to the mkplot format, one can use:

  $ doc/benchmarks/meson-to-mkplot.sh 'Title of Plot' testlog.json > mkplottable.json

The files used to create the plots in the paper can be found in the folder:

   acacia-bonsai/doc/benchmarks/2022-10-11/

Scatter plots are then generated using, for instance:

  $ ~/mkplot/mkplot.py --lloc='upper left' --ymin=1e-2 --ylog -b pdf --save-to plot.pdf ~/acacia-bonsai/doc/benchmarks/2022-10-11/syntcomp21-timeout-60s/*.json
