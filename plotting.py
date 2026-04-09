import matplotlib.pyplot as plt

# hardcoded sequential time
SEQ_TIME = 120.0  # <-- change this

procs = []
times = []

with open("results.txt") as f:
    for line in f:
        if line.startswith("#"): continue
        p, t = line.split()
        procs.append(int(p))
        times.append(float(t))

# compute speedup
speedup = [SEQ_TIME / t for t in times]

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