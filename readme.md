# Flinch

Flinch is a fast\* super ultra-lightweight scripting language (**under 1000 lines of code**), designed to be as easy to implement as possible while still having enough functionality to be theoretically usable for "real programming".

Flinch is stack-based and concatenative (e.g. it looks like `5 4 +`, not `5 + 4`). A consequence of this is that no parsing is needed and expressions can be evaluated by the same machinery that's responsible for control flow. Rather than using blocks and structured branches, Flinch exposes labels and direct jumps (`goto`, `if_goto`), which makes it much easier to implement.

To make up for concatenative math code being hard to read, Flinch has an optional infix expression unrolling system (so things like `( 5 + 4 )` are legal Flinch code). This makes it much, much easier to write readable code, and only costs about 40 lines of space in the Flinch program loader.

\*: Within 1.15x~1.5x the runtime of the Lua interpreter, measured compiling with clang 19.1.14 with various advanced compiler flags; see "Speed" section. On windows you're going to want to pass `-DUSE_MIMALLOC` during compilation and link in `mimalloc`, otherwise perf will be more like 1.5x~2.0x of Lua.

## Examples

Hello world:

```R
"Hello, world!" !printstr
# strings evaluate as arrays of numbers, so this would print a series of decimal ascii codes instead:
#"Hello, world!" !print
```

Pi calculation with shunting yard expressions:

```R
calc_pi^
    ( 0.0 -> $sum$ )
    ( -1.0 -> $flip$ )
    ( 0 -> $i$ ) # note: newly-declared variables contain 0 by default. this assignment is only for clarity
    
    :loopend goto loopstart:
        ( -1.0 *= $flip )
        ( flip / ( i * 2 - 1 ) += $sum )
    loopend: $i 10000000 :loopstart inc_goto_until
    
    ( sum * 4.0 ) return
^^
```

Currying with references to emulate a closure (prints 1 2 3 4 5):

```R
inc_and_print^
    $n$ -> # create a new var, then bind it to the top of the stack (which we hope is a reference)
    1 n += # increment the underlying value behind the reference
    n :: !print # print it (:: clones it first)
^^

make_fake_closure^
    $x$ # new variables contain the integer 0 by default
    [ $x ^inc_and_print ]
^^

bound_call^
    !dump
    call
^^

# .f calls a function immediately, ^f merely pushes its reference to the stack (to be later called with call)
.make_fake_closure $curried$ ->

curried .bound_call
curried .bound_call
curried .bound_call
curried .bound_call
curried .bound_call
````

## FAQ

#### Q: Why?

A: I was curious.

#### Q: This is disgusting though?

A: Yeah, but it's fully-featured (as far as being a dynamically-typed language without closures, blocks, or classes goes) and really, really, really short to implement.

#### Q: Does this have literally any practical uses at all?

A: Practically, no. But like if you're going to throw a scripting language into something and it needs to be security-audited for some reason, 1000k lines is pretty few.

#### Q: Is this memory safe?

A: Yes, as long as you don't compile with MEMORY_UNSAFE_REFERENCES defined.

## Shunting Yard

The shunting yard transformation (i.e. `( .... )` around expressions) has the following operator precedence table:


```
    6     @   @-  @+  @@
    5     *  /  %  <<  >>  &
    4     +  -  |  ^
    3     ==  <=  >=  !=  >  <
    2     and  or
    1     ->  +=  -=  *=  /=  %=
    0     ;
```

(`@+` is included despite being a three-operand operator because including it makes some expressions much easier to write; however, it is fragile and can produce buggy transformations. Be careful!)

(`;` is a no-op that can be used to control the reordering of subexpressions)

In shunting yard expressions, `((` temporarily disables and `))` re-enables the transformation. Shunting yard expressions inside of a `(( .... ))` will themselves still be transformed.

Shunting-yard-related parens (i.e. `(`, `((`, `)`, `))` parens) follow nesting rules.

## Source code size

`main.cpp` is an example of how to integrate `flinch.hpp` into a project. `builtins.hpp` is standard library functionality and is however long as you want it to be; you could delete everything from it if you wanted to. `flinch.hpp` is the actual language implementation, and at time of writing, is sized like:

```
$ tokei flinch.hpp
===============================================================================
 Language            Files        Lines         Code     Comments       Blanks
===============================================================================
 C++ Header              1         1213          995           45          173
===============================================================================
 Total                   1         1213          995           45          173
===============================================================================
```

(`cloc` gives the same numbers.)

An empty `builtins.hpp` is:
```c++
static void(* const builtins [])(vector<DynamicType> &) = { 0 };
static inline int builtins_lookup(const string & s) { throw runtime_error("Unknown built-in function: " + s); };
```

## Speed

Note: the "too simple" pi calculation benchmark here uses fewer iterations than the benchmark game website does

```
# Windows

$ clang++ -g -ggdb -Wall -Wextra -pedantic -Wno-attributes -static -lmimalloc-static -DUSE_MIMALLOC main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000

$ hyperfine.exe "a.exe examples/too_simple_2_shunting.fl" "lua etc/too_simple.lua" "python etc/too_simple.py" "too_simple_c.exe" --warmup 3
Benchmark 1: a.exe examples/too_simple_2_shunting.fl
  Time (mean ± σ):     117.8 ms ±   2.0 ms    [User: 116.2 ms, System: 3.9 ms]
  Range (min … max):   115.8 ms … 124.4 ms    24 runs

Benchmark 2: lua etc/too_simple.lua
  Time (mean ± σ):      81.0 ms ±   2.3 ms    [User: 76.7 ms, System: 4.3 ms]
  Range (min … max):    78.3 ms …  89.7 ms    35 runs

Benchmark 3: python etc/too_simple.py
  Time (mean ± σ):      1.315 s ±  0.033 s    [User: 1.304 s, System: 0.005 s]
  Range (min … max):    1.283 s …  1.394 s    10 runs

Benchmark 4: too_simple_c.exe
  Time (mean ± σ):      14.3 ms ±   1.1 ms    [User: 11.1 ms, System: 3.4 ms]
  Range (min … max):    12.1 ms …  16.8 ms    171 runs

Summary
  too_simple_c.exe ran
    5.66 ± 0.45 times faster than lua etc/too_simple.lua
    8.23 ± 0.63 times faster than a.exe examples/too_simple_2_shunting.fl
   91.93 ± 7.25 times faster than python etc/too_simple.py

$ hyperfine.exe "a.exe examples/pf.fl" "lua etc/pf.lua" "python etc/pf.py" "pf_c.exe" --warmup 3
Benchmark 1: a.exe examples/pf.fl
  Time (mean ± σ):     446.4 ms ±  14.4 ms    [User: 434.7 ms, System: 9.6 ms]
  Range (min … max):   435.7 ms … 484.3 ms    10 runs

Benchmark 2: lua etc/pf.lua
  Time (mean ± σ):     365.4 ms ±   8.2 ms    [User: 337.8 ms, System: 26.2 ms]
  Range (min … max):   354.1 ms … 378.3 ms    10 runs

Benchmark 3: python etc/pf.py
  Time (mean ± σ):     451.4 ms ±   6.5 ms    [User: 433.1 ms, System: 12.5 ms]
  Range (min … max):   443.9 ms … 462.8 ms    10 runs

Benchmark 4: pf_c.exe
  Time (mean ± σ):      17.1 ms ±   0.7 ms    [User: 13.9 ms, System: 4.4 ms]
  Range (min … max):    15.8 ms …  19.8 ms    148 runs

Summary
  pf_c.exe ran
   21.35 ± 1.02 times faster than lua etc/pf.lua
   26.08 ± 1.39 times faster than a.exe examples/pf.fl
   26.38 ± 1.18 times faster than python etc/pf.py

# linux

$ clang++ -g -ggdb -Wall -Wextra -pedantic -Wno-attributes main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000

$ hyperfine "./a.out examples/too_simple_2_shunting.fl" "lua etc/too_simple.lua" "python3 etc/too_simple.py" "./too_simple_c.out" --warmup 3
Benchmark 1: ./a.out examples/too_simple_2_shunting.fl
  Time (mean ± σ):     126.0 ms ±   3.3 ms    [User: 116.5 ms, System: 0.9 ms]
  Range (min … max):   122.8 ms … 138.5 ms    23 runs

Benchmark 2: lua etc/too_simple.lua
  Time (mean ± σ):      91.2 ms ±   3.3 ms    [User: 84.7 ms, System: 0.4 ms]
  Range (min … max):    87.7 ms … 102.5 ms    33 runs

Benchmark 3: python3 etc/too_simple.py
  Time (mean ± σ):     533.9 ms ±  22.2 ms    [User: 497.1 ms, System: 3.4 ms]
  Range (min … max):   514.4 ms … 593.7 ms    10 runs

Benchmark 4: ./too_simple_c.out
  Time (mean ± σ):      12.2 ms ±   0.9 ms    [User: 10.3 ms, System: 0.6 ms]
  Range (min … max):    11.2 ms …  14.7 ms    204 runs

Summary
  ./too_simple_c.out ran
    7.45 ± 0.60 times faster than lua etc/too_simple.lua
   10.30 ± 0.79 times faster than ./a.out examples/too_simple_2_shunting.fl
   43.62 ± 3.64 times faster than python3 etc/too_simple.py
   
$ hyperfine "./a.out examples/pf.fl" "lua etc/pf.lua" "python3 etc/pf.py" "./pf_c.out" --warmup 3
Benchmark 1: ./a.out examples/pf.fl
  Time (mean ± σ):     390.0 ms ±   5.0 ms    [User: 361.9 ms, System: 5.0 ms]
  Range (min … max):   384.0 ms … 401.5 ms    10 runs

Benchmark 2: lua etc/pf.lua
  Time (mean ± σ):     327.7 ms ±   6.5 ms    [User: 306.3 ms, System: 2.3 ms]
  Range (min … max):   320.4 ms … 340.4 ms    10 runs

Benchmark 3: python3 etc/pf.py
  Time (mean ± σ):     294.9 ms ±   8.4 ms    [User: 266.2 ms, System: 7.8 ms]
  Range (min … max):   286.5 ms … 312.8 ms    10 runs

Benchmark 4: ./pf_c.out
  Time (mean ± σ):      11.7 ms ±   0.5 ms    [User: 9.6 ms, System: 0.8 ms]
  Range (min … max):    10.7 ms …  13.5 ms    246 runs

Summary
  ./pf_c.out ran
   25.27 ± 1.37 times faster than python3 etc/pf.py
   28.08 ± 1.41 times faster than lua etc/pf.lua
   33.42 ± 1.61 times faster than ./a.out examples/pf.fl

```

Build-related info:

```
wareya@Toriaezu UCRT64 ~/dev/flinch
$ clang++ --version
clang version 19.1.4
Target: x86_64-w64-windows-gnu
Thread model: posix
InstalledDir: C:/msys64/ucrt64/bin
```

## License

CC0 (only applies to `main.cpp`, `builtins.hpp`, `flinch.hpp`, and the files under `examples`).

