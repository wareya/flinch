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
 C++ Header              1         1223          996           48          179
===============================================================================
 Total                   1         1223          996           48          179
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
  Time (mean ± σ):     310.8 ms ±   4.1 ms    [User: 306.6 ms, System: 4.5 ms]
  Range (min … max):   305.5 ms … 318.3 ms    10 runs

Benchmark 2: lua etc/pf.lua
  Time (mean ± σ):     313.9 ms ±   3.7 ms    [User: 298.8 ms, System: 12.1 ms]
  Range (min … max):   309.0 ms … 322.3 ms    10 runs

Benchmark 3: python etc/pf.py
  Time (mean ± σ):     398.7 ms ±  20.7 ms    [User: 386.3 ms, System: 13.8 ms]
  Range (min … max):   364.4 ms … 432.6 ms    10 runs

Benchmark 4: pf_c.exe
  Time (mean ± σ):      15.8 ms ±   0.5 ms    [User: 11.8 ms, System: 4.9 ms]
  Range (min … max):    14.7 ms …  18.3 ms    161 runs

Summary
  pf_c.exe ran
   19.72 ± 0.67 times faster than a.exe examples/pf.fl
   19.91 ± 0.66 times faster than lua etc/pf.lua
   25.29 ± 1.53 times faster than python etc/pf.py

$ hyperfine.exe "a.exe examples/too_simple_2_shunting.fl" "lua etc/too_simple.lua" "python etc/too_simple.py" "too_simple_c.exe" --warmup 3
Benchmark 1: a.exe examples/too_simple_2_shunting.fl
  Time (mean ± σ):     118.3 ms ±   1.1 ms    [User: 113.0 ms, System: 4.5 ms]
  Range (min … max):   116.9 ms … 121.9 ms    24 runs

Benchmark 2: lua etc/too_simple.lua
  Time (mean ± σ):      69.3 ms ±   0.6 ms    [User: 64.5 ms, System: 3.7 ms]
  Range (min … max):    68.1 ms …  71.0 ms    40 runs

Benchmark 3: python etc/too_simple.py
  Time (mean ± σ):     600.7 ms ±  30.9 ms    [User: 588.8 ms, System: 15.7 ms]
  Range (min … max):   581.8 ms … 685.7 ms    10 runs

Benchmark 4: too_simple_c.exe
  Time (mean ± σ):      13.0 ms ±   0.7 ms    [User: 10.3 ms, System: 3.5 ms]
  Range (min … max):    11.9 ms …  16.2 ms    173 runs

Summary
  too_simple_c.exe ran
    5.32 ± 0.31 times faster than lua etc/too_simple.lua
    9.09 ± 0.53 times faster than a.exe examples/too_simple_2_shunting.fl
   46.15 ± 3.57 times faster than python etc/too_simple.py

# linux

$ clang++ -g -ggdb -Wall -Wextra -pedantic -Wno-attributes -DUSE_MIMALLOC -lmimalloc main.cpp -O3 -frandom-seed=constant_seed -fuse-ld=lld -flto -mllvm -inline-threshold=10000
   
$ hyperfine "./a.out examples/pf.fl" "lua etc/pf.lua" "python3 etc/pf.py" "./pf_c.out" --warmup 3
Benchmark 1: ./a.out examples/pf.fl
  Time (mean ± σ):     297.2 ms ±   4.6 ms    [User: 261.2 ms, System: 7.2 ms]
  Range (min … max):   292.5 ms … 308.2 ms    10 runs

Benchmark 2: lua etc/pf.lua
  Time (mean ± σ):     288.7 ms ±   3.7 ms    [User: 256.4 ms, System: 5.1 ms]
  Range (min … max):   282.6 ms … 294.0 ms    10 runs

Benchmark 3: python3 etc/pf.py
  Time (mean ± σ):     299.0 ms ±   3.7 ms    [User: 262.3 ms, System: 5.9 ms]
  Range (min … max):   290.5 ms … 303.0 ms    10 runs

Benchmark 4: ./pf_c.out
  Time (mean ± σ):      11.9 ms ±   0.5 ms    [User: 9.4 ms, System: 0.8 ms]
  Range (min … max):    11.1 ms …  14.0 ms    252 runs

Summary
  ./pf_c.out ran
   24.20 ± 0.99 times faster than lua etc/pf.lua
   24.91 ± 1.04 times faster than ./a.out examples/pf.fl
   25.06 ± 1.02 times faster than python3 etc/pf.py

$ hyperfine "./a.out examples/too_simple_2_shunting.fl" "lua etc/too_simple.lua" "python3 etc/too_simple.py" "./too_simple_c.out" --warmup 3
Benchmark 1: ./a.out examples/too_simple_2_shunting.fl
  Time (mean ± σ):     128.6 ms ±   0.8 ms    [User: 114.4 ms, System: 0.9 ms]
  Range (min … max):   127.8 ms … 130.2 ms    23 runs

Benchmark 2: lua etc/too_simple.lua
  Time (mean ± σ):      75.9 ms ±   0.6 ms    [User: 67.5 ms, System: 0.6 ms]
  Range (min … max):    74.9 ms …  77.3 ms    39 runs

Benchmark 3: python3 etc/too_simple.py
  Time (mean ± σ):     516.1 ms ±   6.5 ms    [User: 462.2 ms, System: 3.0 ms]
  Range (min … max):   507.7 ms … 527.3 ms    10 runs

Benchmark 4: ./too_simple_c.out
  Time (mean ± σ):      12.1 ms ±   0.4 ms    [User: 9.9 ms, System: 0.5 ms]
  Range (min … max):    11.7 ms …  14.6 ms    236 runs

  Warning: Statistical outliers were detected. Consider re-running this benchmark on a quiet system without any interferences from other programs. It might help to use the '--warmup' or '--prepare' options.

Summary
  ./too_simple_c.out ran
    6.27 ± 0.20 times faster than lua etc/too_simple.lua
   10.63 ± 0.33 times faster than ./a.out examples/too_simple_2_shunting.fl
   42.64 ± 1.42 times faster than python3 etc/too_simple.py
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

