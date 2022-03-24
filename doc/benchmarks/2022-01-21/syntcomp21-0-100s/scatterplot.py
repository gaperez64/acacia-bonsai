#!/usr/bin/env python3


import itertools
import matplotlib.pyplot as plt
import pandas as pd


# Loading the data in a pandas data frame
df = pd.read_csv("strixvsab.csv")
print(df.shape[0])
print(df.head())

# Remove all entries without a valid output
df = df[(df.Strix != "TO") & (df.AB != "TO")]

# Sort the entries per time it took Strix to solve
types = {"Benchmark": "string", "Strix": "float32", "AB": "float32"}
df = df.sort_values(by="Strix").astype(types)
print(df.shape[0])
print(df.head())
print(df.dtypes)

# Plot the two lines
plt.plot(df.Strix, df.Strix, label="Strix")
plt.plot(df.Strix, df.AB, label="Acacia-bonsai",
         marker="x", linestyle="None")

# Show the plot and close everything after
plt.legend(loc="lower right")
# plt.yscale("log")
plt.ylabel("Time to solve by Acacia-bonsai")
plt.xlabel("Time to solve benchmarks by Strix")
plt.show()
plt.close()
