# Flinch

Flinch is a super ultra-lightweight scripting language (**under 1000 lines of code**), faster than python 3 but slower than lua\*, designed to be as easy to implement as possible while still having enough functionality to be theoretically usable for "real programming".

Flinch is stack-based and concatenative (e.g. it looks like `5 4 +`, not `5 + 4`). A consequence of this is that no parsing is needed and expressions can be evaluated by the same machinery that's responsible for control flow. Rather than using blocks and structured branches, Flinch exposes labels and direct jumps (`goto`, `if_goto`), which makes it much easier to implement.

To make up for concatenative math code being hard to read, Flinch has an optional infix expression unrolling system (so things like `( 5 + 4 )` are legal Flinch code). This makes it much, much easier to write readable code, and only costs about 40 lines of space in the Flinch program loader.

\*: Measured compiling with clang 19.1.14 on Windows with various advanced compiler flags; see "Speed" section

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
$ clang++ -g -ggdb -Wall -Wextra -pedantic -Wno-attributes main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000

$ hyperfine.exe "a.exe examples/too_simple_2_shunting.fl" "lua etc/too_simple.lua" "python etc/too_simple.py" "too_simple_c.exe" --warmup 3
Benchmark 1: a.exe examples/too_simple_2_shunting.fl
  Time (mean ± σ):     117.0 ms ±   2.2 ms    [User: 112.0 ms, System: 8.6 ms]
  Range (min … max):   113.8 ms … 121.5 ms    24 runs

Benchmark 2: lua etc/too_simple.lua
  Time (mean ± σ):      77.8 ms ±   1.3 ms    [User: 72.9 ms, System: 4.8 ms]
  Range (min … max):    76.3 ms …  81.3 ms    37 runs

Benchmark 3: python etc/too_simple.py
  Time (mean ± σ):      1.281 s ±  0.020 s    [User: 1.271 s, System: 0.009 s]
  Range (min … max):    1.261 s …  1.314 s    10 runs

Benchmark 4: too_simple_c.exe
  Time (mean ± σ):      13.3 ms ±   0.8 ms    [User: 10.4 ms, System: 4.2 ms]
  Range (min … max):    11.9 ms …  16.3 ms    164 runs

Summary
  too_simple_c.exe ran
    5.87 ± 0.37 times faster than lua etc/too_simple.lua
    8.83 ± 0.57 times faster than a.exe examples/too_simple_2_shunting.fl
   96.65 ± 6.09 times faster than python etc/too_simple.py

$ hyperfine.exe "a.exe examples/pf.fl" "lua etc/pf.lua" "python etc/pf.py" "pf_c.exe" --warmup 3
Benchmark 1: a.exe examples/pf.fl
  Time (mean ± σ):     664.5 ms ±   4.4 ms    [User: 657.8 ms, System: 1.5 ms]
  Range (min … max):   659.4 ms … 673.0 ms    10 runs

Benchmark 2: lua etc/pf.lua
  Time (mean ± σ):     341.5 ms ±   4.6 ms    [User: 323.4 ms, System: 16.7 ms]
  Range (min … max):   335.6 ms … 350.6 ms    10 runs

Benchmark 3: python etc/pf.py
  Time (mean ± σ):     430.3 ms ±   3.9 ms    [User: 418.8 ms, System: 8.8 ms]
  Range (min … max):   425.8 ms … 439.1 ms    10 runs

Benchmark 4: pf_c.exe
  Time (mean ± σ):      15.7 ms ±   0.4 ms    [User: 12.4 ms, System: 3.6 ms]
  Range (min … max):    15.1 ms …  16.9 ms    156 runs

Summary
  pf_c.exe ran
   21.72 ± 0.62 times faster than lua etc/pf.lua
   27.37 ± 0.73 times faster than python etc/pf.py
   42.28 ± 1.09 times faster than a.exe examples/pf.fl
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

