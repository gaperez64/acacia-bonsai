# Acacia-Bonsai evidence archive

This tag exists only to carry release assets: deterministic, checksum-verified archives of
benchmarking campaigns and frozen measured binaries. It is not a software release and is not an
ancestor of any branch. It deliberately contains no workflows, so publishing it cannot trigger
image or package publication.

The authoritative list of archives, their SHA-256 digests and the conclusions they support is
`benchmarking/evidence-index.tsv` in the main repository. Retrieve and verify an archive with
`scripts/acacia-evidence.py fetch`.
