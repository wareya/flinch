#!/usr/bin/env lua

function main()
    local sum = 0.0
    local flip = -1.0
    for i = 1, 100000000 do
        flip = -flip
        --sum = sum + flip / (2 * i - 1)
        sum = sum + flip / ((i << 1) - 1)
    end
    print(string.format("%.16f", sum * 4.0))
end

main()
