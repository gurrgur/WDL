# bench_swell_window_app

CMake-built SWELL window/control benchmark app. It links against the `Swell`
target, creates real SWELL dialogs and controls, runs timed windowing/widget
benchmarks, and prints JSON to stdout.

## Build

```sh
cmake --build WDL/swell2/build --target bench_swell_window_app
```

Benchmarks are enabled by default. Disable them with:

```sh
cmake -S WDL/swell2 -B WDL/swell2/build -DSWELL2_BUILD_BENCHMARKS=OFF
```

## Run

```sh
WDL/swell2/build/bench_swell_window_app --duration-ms 1000 --pretty
WDL/swell2/build/bench_swell_window_app --bench resize_window --bench paint_update --duration-ms 2000
```

JSON includes target duration, actual elapsed time, iteration count, operation
count, ops/sec, ns/op, and paint/message counters.
