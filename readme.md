# Flinch

Flinch is a super ultra-lightweight scripting language (**under 1000 lines of code**), faster than python 3 but slower than lua\*, designed to be as easy to implement as possible while still having enough functionality to be theoretically usable for "real programming".

Flinch is stack-based and concatenative (e.g. it looks like `5 4 +`, not `5 + 4`). A consequence of this is that no parsing is needed and expressions can be evaluated by the same machinery that's responsible for control flow. Rather than using blocks and structured branches, Flinch exposes labels and direct jumps (`goto`, `if_goto`), which makes it much easier to implement.

To make up for concatenative math code being hard to read, Flinch has an optional infix expression unrolling system (so things like `( 5 + 4 )` are legal Flinch code). This makes it much, much easier to write readable code, and only costs about 40 lines of space in the Flinch program loader.

\*: Measured compiled with `clang++ -O3`, not GCC, running microbenchmarks: \~25% the runtime of python 3, ~4x the runtime of lua, so logarithmically half way bettween python and lua.

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

^calc_pi call_eval

$ret$ ->
ret !print
ret
```

Pi calculation with only RPN/concatenation:

```R
calc_pi^
    0.0 $sum$ ->
    -1.0 $flip$ ->
    1 $i$ ->
    
    :loopend goto
    
    loopstart:
    -1.0 $flip *=
    flip i 2 * 1 - / $sum +=
    1 $i +=
    
    loopend:
    i 10000000 <= :loopstart if_goto
    
    4.0 sum * return
^^

^calc_pi call_eval

$ret$ ->
ret !print
ret
````

## FAQ

#### Q: Why?

A: I was curious.

#### Q: This is disgusting though?

A: Yeah, but it's fully-featured (as far as being a dynamically-typed language without closures, blocks, or classes goes) and really, really, really short to implement.

#### Q: Does this have literally any practical uses at all?

A: Practically, no. But like if you're going to throw a scripting language into something and it needs to be security-audited for some reason, 1000k lines is pretty few.

#### Q: Is this memory safe?

A: If you compile with MEMORY_SAFE_REFERENCES defined, yes! However on microbenchmarks this is more than 100% slower (more than 2x the runtime), so it's left as an option instead of being mandatory.

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

In shunting yard expressions, `((` temporarily disables and `))` re-enables the transformation. Shunting yard expressions inside of a `(( .... ))` will themselves be transformed as well, just not stuff outside of them but still inside of the `(( .... ))`.

Shunting-yard-related parens (i.e. `(`, `((`, `)`, `))` parens) follow nesting rules.

## Source code size

`main.cpp` is an example of how to integrate `flinch.hpp` into a project. `builtins.hpp` is standard library functionality and is however long as you want it to be; you could delete everything from it if you wanted to. `flinch.hpp` is the actual language implementation, and at time of writing, is sized like:

```
$ tokei flinch.hpp
===============================================================================
 Language            Files        Lines         Code     Comments       Blanks
===============================================================================
 C++ Header              1         1197          996           19          182
===============================================================================
 Total                   1         1197          996           19          182
===============================================================================
```

(`cloc` gives the same numbers.)

An empty `builtins.hpp` is:
```c++
static void(* const builtins [])(vector<DynamicType> &) = { 0 };
static inline int builtins_lookup(const string & s) { throw runtime_error("Unknown built-in function: " + s); };
```

## Speed

Using the "too simple" pi calculation benchmark (with fewer iterations than the benchmark game does):

```
$ hyperfine "a.exe examples/too_simple_2_shunting.fl" "lua etc/too_simple.lua" "python etc/too_simple.py" --warmup 2
Benchmark 1: a.exe examples/too_simple_2_shunting.fl
  Time (mean ± σ):     332.6 ms ±   3.4 ms    [User: 328.4 ms, System: 5.8 ms]
  Range (min … max):   330.0 ms … 340.0 ms    10 runs

Benchmark 2: lua etc/too_simple.lua
  Time (mean ± σ):      78.0 ms ±   2.6 ms    [User: 72.2 ms, System: 3.1 ms]
  Range (min … max):    76.5 ms …  92.7 ms    37 runs

  Warning: Statistical outliers were detected. Consider re-running this benchmark on a quiet system without any interferences from other program
s. It might help to use the '--warmup' or '--prepare' options.

Benchmark 3: python etc/too_simple.py
  Time (mean ± σ):      1.281 s ±  0.041 s    [User: 1.274 s, System: 0.010 s]
  Range (min … max):    1.261 s …  1.397 s    10 runs

  Warning: Statistical outliers were detected. Consider re-running this benchmark on a quiet system without any interferences from other program
s. It might help to use the '--warmup' or '--prepare' options.

Summary
  lua etc/too_simple.lua ran
    4.26 ± 0.15 times faster than a.exe examples/too_simple_2_shunting.fl
   16.42 ± 0.76 times faster than python etc/too_simple.py
```

## License

CC0 (only applies to `main.cpp`, `builtins.hpp`, `flinch.hpp`, and the files under `examples`).

