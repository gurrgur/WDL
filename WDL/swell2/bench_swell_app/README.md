# bench_swell_app

CMake-built SWELL benchmark app. It links against the `Swell` target, runs
offscreen memory-HDC drawing benchmarks for fixed durations, and prints JSON to
stdout.

## Build

```sh
cmake --build WDL/swell2/build --target bench_swell_app
```

Benchmarks are enabled by default. Disable them with:

```sh
cmake -S WDL/swell2 -B WDL/swell2/build -DSWELL2_BUILD_BENCHMARKS=OFF
```

## Run

```sh
WDL/swell2/build/bench_swell_app --duration-ms 1000 --pretty
WDL/swell2/build/bench_swell_app --bench text_draw --bench bitblt --duration-ms 2000
```

JSON includes target duration, actual elapsed time, iteration count, operation
count, ops/sec, and ns/op per benchmark.
