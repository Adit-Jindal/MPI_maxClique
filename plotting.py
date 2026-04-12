import matplotlib.pyplot as plt

procs = []
times = []
speedups = []

with open("results.txt") as f:
    for line in f:
        if line.startswith("#"): continue
        p, t, s = line.split()
        procs.append(int(p))
        times.append(float(t))
        speedups.append(float(s))

# compute speedup
speedup = [speedups[0] / t for t in times]

# ---- Plot 1: Time vs Processes ----
plt.figure()
plt.plot(procs, times, marker='o')
plt.xlabel("Number of Processes")
plt.ylabel("Execution Time (s)")
plt.title("Execution Time vs Processes")
plt.grid()
plt.savefig("time_vs_procs.png")

# ---- Plot 2: Speedup vs Processes ----
plt.figure()
plt.plot(procs, speedup, marker='o', label="Actual Speedup")
plt.plot(procs, procs, linestyle='--', label="Ideal Speedup")
plt.xlabel("Number of Processes")
plt.ylabel("Speedup")
plt.title("Speedup vs Processes")
plt.legend()
plt.grid()
plt.savefig("speedup.png")

plt.show()