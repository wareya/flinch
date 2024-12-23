--size = 2000
size = 503
--size = 202
seed = 8381853

function lcg()
    seed = ((seed + 1) * 141853155) & 0xFFFFFFFF
    return seed
end

function generate_grid(grid, size)
    for i = 1, size * size do
        grid[i] = lcg() > 1477603712 and 1 or 0
    end

    grid[size * 2 - 1] = 1
    grid[size * (size - 2) + 2] = 1
    for n = 0, (size-1) do
        grid[n * size + 1] = 0
        grid[n * size + size] = 0
        grid[n + 1] = 0
        grid[n + size * (size - 1) + 1] = 0
    end
end

function get_neighbors(index, size)
    return {
        index - 1,
        index + 1,
        index - size,
        index + size
    }
end

function table.shallow_copy(t)
    local t2 = {}
    for k, v in pairs(t) do
        t2[k] = v
    end
    return t2
end

function pushraw(heap, val)
    if #(heap[1]) == 0 then
        heap[1] = val
    elseif heap[1][1] > val[1] then
        local a = heap[1]
        heap[1] = val
        pushraw(heap, a)
    else
        heap[1][5] = (heap[1][5] + 1) % 2
        local n = {heap[1][3 + heap[1][5]]}
        pushraw(n, val)
        heap[1][3 + heap[1][5]] = n[1]
    end
end

function push(heap, val)
    pushraw(heap, {val[1], val[2], {}, {}, 0})
end

function pop(heap)
    local top = heap[1]
    if #{heap[1][4]} > 0 then
        local n = {heap[1][3]}
        pushraw(n, heap[1][4])
        heap[1][3] = n[1]
    end
    heap[1] = heap[1][3]
    return top[1], top[2]
end

function dijkstra(grid, size)
    local start = size * (size - 2) + 2
    local end_ = size * 2 - 1

    local distances = {}
    for _ = 1, size * size do
        table.insert(distances, math.huge)
    end
    distances[start] = 0

    local heap = {{}}
    push(heap, {0, start})

    while #(heap[1]) > 0 do
        local current_distance, current_index = pop(heap)

        if current_index == end_ then
            return current_distance
        end

        if current_distance > distances[current_index] then
            goto continue_
        end

        local neighbors = get_neighbors(current_index, size)
        for i = 1, #neighbors do
            local neighbor = neighbors[i]
            if grid[neighbor] == 0 then
                goto continue_2
            end
            local new_dist = current_distance + 1

            if new_dist < distances[neighbor] then
                distances[neighbor] = new_dist
                push(heap, {new_dist, neighbor})
            end
            ::continue_2::
        end

        ::continue_::
    end

    return -1
end

function main()
    local grid = {}
    for _ = 1, size * size do
        table.insert(grid, 0)
    end
    generate_grid(grid, size)
    --print(table.concat(grid, " "))

    local shortest_path_cost = dijkstra(grid, size)
    print("Shortest path cost from bottom-left to top-right:", shortest_path_cost)
end

main()
