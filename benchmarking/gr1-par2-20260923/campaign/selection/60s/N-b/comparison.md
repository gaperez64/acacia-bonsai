# N-b at 60 s, eligible subset (91 IDs)

| Series | Solved | PAR-2 total (s) | PAR-2 mean (s) |
|---|---:|---:|---:|
| N-b | 67 | 3285.664955 | 36.106208 |
| B epoch 1 (derived from 120 s) | 35 | 6778.125800 | 74.484899 |
| B epoch 2 (derived from 120 s) | 35 | 6778.312177 | 74.486947 |

N versus B epoch 1: gain=32, loss=0, same=59, verdict-conflict=0
B epoch 2 versus epoch 1: gain=0, loss=0, same=91, verdict-conflict=0

Route winners: lifting=64, B=3, fallback-nonanswer=21, missing-record=3

Per-ID results and gains/losses: `comparison.tsv`. Missing records can occur when the outer scope kills the wrapper before its atomic write.

B 60 s rows are derived by censoring the archived uniform 120 s epochs; they are not 60 s solver runs.
