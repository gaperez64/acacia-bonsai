#!/bin/sh

ZIP=acacia-bonsai-tacas23.zip

rm -fR ~/deps/python-graph/core/{build,dist,python_graph_core.egg-info}
rm -fR ~/deps/spot-2.11.2/_build
rm -fR ~/acacia-bonsai/build*

rm -f $ZIP
zip -r $ZIP packages deps acacia-bonsai bootstrap.sh
