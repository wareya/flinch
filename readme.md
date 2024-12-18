# Flinch

Flinch is a super ultra-lightweight scripting language, faster than python but slower than lua, designed to be as easy to implement as possible while still having enough functionality to be theoretically usable for "real programming".

Flinch is stack-based and concatenative (e.g. it looks like `5 4 +`, not `5 + 4`). A consequence of this is that no parsing is needed and expressions can be evaluated by the same machinery that's responsible for control flow. Rather than using blocks and structured branches, Flinch exposes labels and direct jumps (`goto`, `if_goto`), which makes it much easier to implement.

## Examples

Hello world:

```R
"Hello, world!"& !printstr
```

Pi calculation with shunting yard expressions (Flinch has an optional infix expression unrolling system):

```R
calc_pi^
    ( 0.0 -> $sum$ )
    ( -1.0 -> $flip$ )
    ( 0 -> $i$ )
    
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

## License

CC0 (only applies to `flinch.cpp` and the `*.fl` files).

