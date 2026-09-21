# msc::vector

A vector with the same functionalities and interface as a `std::vector` but without moving data on growth and a splice method that moves remaps pages instead of copying data.
This results in overall superior performance over a `std::vector`, especially when handling large amounts of data.
See [Limitations](#limitations) for when to not use it.

## Example usage
```cpp
#include <msc/vector.hpp>

msc::vector<int> a(1'000'000, 1);
msc::vector<int> b(1'000'000, 2);

for(std::size_t i = 0; i < a.size(); ++i){
  a.emplace_back(i);
  b.emplace_back(i);
}

int* p = a.data();
a.reserve(100'000'000);   // grows in place: p and every iterator into a stay valid
a.splice(std::move(b));   // b's pages are remapped onto the end of a; b is left empty
```

### Splicing

```cpp
dst.splice(std::move(src));                             // one vector
dst.splice(std::move(a), std::move(b), std::move(c));   // several at once
dst.splice_range(parts);   // a run-time number of them, e.g. std::vector<msc::vector<T>>
```

Every source is left empty and immediately reusable.

**Order is not preserved.** To land a source on a page boundary, the elements
at the end of `dst` that sit past its last boundary are moved behind the
spliced data. After splicing `{1,2,3}` onto `{a,b,c}` you get all six elements,
but not necessarily in that order.

### Iterator stability

Because the storage never moves, pointers, references and iterators stay valid
across `push_back`, `emplace_back`, `reserve`, `resize` and `shrink_to_fit`.
`std::vector` invalidates all of them whenever it reallocates.

The same property makes `v.push_back(v[0])` safe, which a reallocating vector
has to go out of its way to support.
`splice` may relocate the last few
elements of the destination, so treat iterators into it as invalidated.


## Performance

Moving the contents of one `vector<int>` onto the end of another:

| elements            | `msc::vector::splice` | `std::vector::append_range` |
|---------------------|----------------------:|----------------------------:|
| 2²⁷ (134 million)   |               0.10 ms |                      602 ms |
| 2²⁹ (537 million)   |               0.49 ms |                    2,234 ms |
| 2³¹ (2.1 billion)   |               0.50 ms |                   14,986 ms |

Filling an empty `vector<int>` with emplace_back:

| elements            | `msc::vector` | `std::vector` |
|---------------------|--------------:|--------------:|
| 2²⁷ (134 million)   |        0.34 s |        0.45 s |
| 2²⁹ (537 million)   |        1.37 s |        1.96 s |
| 2³¹ (2.1 billion)   |        5.58 s |       10.39 s |


## Getting it

### CMake FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(msc_vector
    GIT_REPOSITORY https://github.com/M-Scheinost/vector.git
    GIT_TAG        v1.0.0)
FetchContent_MakeAvailable(msc_vector)

target_link_libraries(your_target PRIVATE msc::vector)
```

Pulling the library in this way gives you the header and nothing else. The
tests, the benchmark and the developer compiler flags are only built when this
is the top-level project, so no googletest download and no `-Werror` are forced
on your build.

### Installed, with `find_package`

```bash
cmake -S . -B build -DMSC_VECTOR_BUILD_TESTS=OFF -DMSC_VECTOR_BUILD_BENCH=OFF \
      -DMSC_VECTOR_BUILD_EXAMPLE=OFF
cmake --install build --prefix /usr/local
```

```cmake
find_package(msc_vector 1.0 REQUIRED)
target_link_libraries(your_target PRIVATE msc::vector)
```

### Or just copy it

The library is the single header `include/msc/vector.hpp`.



## Limitations

Read these before adopting it.

**`splice` needs trivially relocatable element types**, meaning types that
still work after their bytes are moved to a new address: it relocates elements
by remapping pages, so their move constructors never run. Under libstdc++ that
rules out types which point into themselves: `std::string` holding short
strings, `std::list`, `std::map`, `std::set`, `std::unordered_map` and
`std::unordered_set`. Plain data, `std::unique_ptr`, `std::shared_ptr`,
`std::vector`, `std::deque` and `std::function` are fine. Every other operation
moves elements the way `std::vector` does and works with any type.

**Splice may change element order.** To land a source on a page boundary, the elements
at the end of `dst` that sit past its last page boundary are moved behind the
spliced data. After splicing `{1,2,3}` onto `{a,b,c}` you get all six elements,
but not necessarily in that order. If element order is important use the `insert`/`append`
methods. They work like `std::vectors` methods and never change order.

**The number of simultaneously held vectors is limited** Each vector reserves a virtual address range. Virtual addresses are limited, which limits vectors. Empty/Unused/Cleared vectors reserve their address ranges. Rule of thumb ~100 vectors are no problem and deconstruct vectors when they aren't needed anymore.

**4 KiB pages are assumed.** ARM64 might have different ones

**No allocator support.** The vector owns its mapping directly.


## Building the tests and benchmarks

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build
./build/bench
```

| option                     | default         | purpose                                 |
|----------------------------|-----------------|-----------------------------------------|
| `MSC_VECTOR_BUILD_TESTS`   | top-level only  | googletest suite (fetched on configure) |
| `MSC_VECTOR_BUILD_BENCH`   | top-level only  | `bench/bench.cpp`                       |
| `MSC_VECTOR_BUILD_EXAMPLE` | top-level only  | `main.cpp`                              |
| `MSC_VECTOR_INSTALL`       | top-level only  | install and package config rules        |
| `MSC_VECTOR_WERROR`        | `OFF`           | treat warnings as errors                |

No architecture flags are set. Pass your own through the usual variable, for
example `-DCMAKE_CXX_FLAGS="-march=native"`.

The benchmark reads hardware counters through `perf_event_open`. Where those
are unavailable it still reports timings, without the counter columns.

## License

MIT. See [LICENSE](LICENSE).

`bench/PerfEvent.hpp` is © 2018 Viktor Leis, MIT licensed, with local changes.
