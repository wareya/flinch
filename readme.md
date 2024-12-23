# Flinch

Flinch is a fast\* super ultra-lightweight scripting language (**under 1000 lines of code**), designed to be as easy to implement as possible while still having enough functionality to be theoretically usable for "real programming".

Flinch is stack-based and concatenative (e.g. it looks like `5 4 +`, not `5 + 4`). A consequence of this is that no parsing is needed and expressions can be evaluated by the same machinery that's responsible for control flow. Rather than using blocks and structured branches, Flinch exposes labels and direct jumps (`goto`, `if_goto`), which makes it much easier to implement.

To make up for concatenative math code being hard to read, Flinch has an optional infix expression unrolling system (so things like `( 5 + 4 )` are legal Flinch code). This makes it much, much easier to write readable code, and only costs about 40 lines of space in the Flinch program loader.

\*: Roughly 1x to 2x the runtime of the Lua interpreter, measured compiling with clang 19.1.14 with various advanced compiler flags; see "Speed" section. Compiling against a faster malloc implementation like mimalloc is highly recommended; array-heavy code will run significantly faster.

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
 C++ Header              1         1219          994           47          178
===============================================================================
 Total                   1         1219          994           47          178
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

$ clang++ -g -ggdb -Wall -Wextra -pedantic -Wno-attributes -DUSE_MIMALLOC -static -lmimalloc-static main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000

$ hyperfine.exe "a.exe examples/pf.fl" "lua etc/pf.lua" "python etc/pf.py" "pf_c.exe" --warmup 3
Benchmark 1: a.exe examples/pf.fl
  Time (mean ± σ):     322.4 ms ±   8.3 ms    [User: 308.8 ms, System: 14.3 ms]
  Range (min … max):   315.1 ms … 338.1 ms    10 runs

Benchmark 2: lua etc/pf.lua
  Time (mean ± σ):     315.1 ms ±   4.3 ms    [User: 294.7 ms, System: 17.1 ms]
  Range (min … max):   310.1 ms … 322.7 ms    10 runs

Benchmark 3: python etc/pf.py
  Time (mean ± σ):     439.1 ms ±   9.7 ms    [User: 435.3 ms, System: 8.6 ms]
  Range (min … max):   429.5 ms … 460.7 ms    10 runs

Benchmark 4: pf_c.exe
  Time (mean ± σ):      16.3 ms ±   0.5 ms    [User: 12.9 ms, System: 4.7 ms]
  Range (min … max):    15.0 ms …  19.3 ms    157 runs

Summary
  pf_c.exe ran
   19.39 ± 0.70 times faster than lua etc/pf.lua
   19.83 ± 0.83 times faster than a.exe examples/pf.fl
   27.02 ± 1.08 times faster than python etc/pf.py

$ hyperfine.exe "a.exe examples/too_simple_2_shunting.fl" "lua etc/too_simple.lua" "python etc/too_simple.py" "too_simple_c.exe" --warmup 3
Benchmark 1: a.exe examples/too_simple_2_shunting.fl
  Time (mean ± σ):     117.4 ms ±   1.6 ms    [User: 114.0 ms, System: 5.3 ms]
  Range (min … max):   116.0 ms … 122.7 ms    24 runs

Benchmark 2: lua etc/too_simple.lua
  Time (mean ± σ):      68.9 ms ±   0.5 ms    [User: 65.8 ms, System: 3.9 ms]
  Range (min … max):    67.8 ms …  70.6 ms    40 runs

Benchmark 3: python etc/too_simple.py
  Time (mean ± σ):      1.304 s ±  0.013 s    [User: 1.295 s, System: 0.012 s]
  Range (min … max):    1.293 s …  1.337 s    10 runs

Benchmark 4: too_simple_c.exe
  Time (mean ± σ):      12.8 ms ±   0.6 ms    [User: 11.6 ms, System: 2.9 ms]
  Range (min … max):    11.7 ms …  15.0 ms    194 runs

Summary
  too_simple_c.exe ran
    5.40 ± 0.27 times faster than lua etc/too_simple.lua
    9.21 ± 0.47 times faster than a.exe examples/too_simple_2_shunting.fl
  102.20 ± 5.17 times faster than python etc/too_simple.py

# linux

$ clang++ -g -ggdb -Wall -Wextra -pedantic -Wno-attributes -DUSE_MIMALLOC -lmimalloc main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000

$ hyperfine "./a.out examples/too_simple_2_shunting.fl" "lua etc/too_simple.lua" "python3 etc/too_simple.py" "./too_simple_c.out" --warmup 3
Benchmark 1: ./a.out examples/too_simple_2_shunting.fl
  Time (mean ± σ):     128.5 ms ±   1.8 ms    [User: 114.8 ms, System: 0.3 ms]
  Range (min … max):   125.9 ms … 132.1 ms    23 runs

Benchmark 2: lua etc/too_simple.lua
  Time (mean ± σ):      77.2 ms ±   1.4 ms    [User: 68.6 ms, System: 0.7 ms]
  Range (min … max):    75.3 ms …  80.2 ms    39 runs

Benchmark 3: python3 etc/too_simple.py
  Time (mean ± σ):     515.5 ms ±  10.2 ms    [User: 462.4 ms, System: 2.2 ms]
  Range (min … max):   504.8 ms … 533.7 ms    10 runs

Benchmark 4: ./too_simple_c.out
  Time (mean ± σ):      12.1 ms ±   0.5 ms    [User: 9.9 ms, System: 0.5 ms]
  Range (min … max):    11.5 ms …  14.7 ms    236 runs

Summary
  ./too_simple_c.out ran
    6.37 ± 0.28 times faster than lua etc/too_simple.lua
   10.61 ± 0.46 times faster than ./a.out examples/too_simple_2_shunting.fl
   42.56 ± 1.93 times faster than python3 etc/too_simple.py
   
$ hyperfine "./a.out examples/pf.fl" "lua etc/pf.lua" "python3 etc/pf.py" "./pf_c.out" --warmup 3
Benchmark 1: ./a.out examples/pf.fl
  Time (mean ± σ):     303.1 ms ±   4.3 ms    [User: 267.0 ms, System: 6.7 ms]
  Range (min … max):   295.3 ms … 308.9 ms    10 runs

Benchmark 2: lua etc/pf.lua
  Time (mean ± σ):     290.0 ms ±   6.0 ms    [User: 257.2 ms, System: 5.4 ms]
  Range (min … max):   281.0 ms … 297.3 ms    10 runs

Benchmark 3: python3 etc/pf.py
  Time (mean ± σ):     299.5 ms ±   4.0 ms    [User: 264.6 ms, System: 4.4 ms]
  Range (min … max):   293.7 ms … 307.5 ms    10 runs

Benchmark 4: ./pf_c.out
  Time (mean ± σ):      11.8 ms ±   0.5 ms    [User: 9.5 ms, System: 0.6 ms]
  Range (min … max):    10.9 ms …  13.6 ms    244 runs

Summary
  ./pf_c.out ran
   24.60 ± 1.10 times faster than lua etc/pf.lua
   25.40 ± 1.06 times faster than python3 etc/pf.py
   25.71 ± 1.08 times faster than ./a.out examples/pf.fl
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

