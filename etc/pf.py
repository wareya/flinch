SIZE = 2000
#SIZE = 503
#SIZE = 202
seed = 8381853

def lcg():
    global seed
    seed = ((seed + 1) * 141853155) & 0xFFFFFFFF
    return seed

def generate_grid(grid, size):
    for i in range(size * size):
        grid[i] = 1 if lcg() > 1477603712 else 0
    
    grid[size*2-2] = 1
    grid[size * (size - 2) + 1] = 1
    for n in range(size):
        grid[n*size] = 0
        grid[n*size + size - 1] = 0
        grid[n] = 0
        grid[n + size * (size - 1)] = 0

def get_neighbors(index, size):
    return [ index - 1,
             index + 1,
             index - size,
             index + size ]

def pushraw(heap, val):
    if heap == []:
        heap[:] = val
    elif heap[0] > val[0]:
        a = heap[:]
        heap[:] = val
        pushraw(heap, a)
    elif heap[2] == []:
        heap[2] = val
    elif heap[3] == []:
        heap[3] = val
    else:
        heap[4] = (heap[4]+1)&1
        pushraw(heap[2+heap[4]], val)

def push(heap, val):
    pushraw(heap, [val[0], val[1], [], [], 0])

def pop(heap):
    ret = [heap[0], heap[1]]
    if heap[3] != []:
        pushraw(heap[2], heap[3])
    heap[:] = heap[2][:]
    return ret

def dijkstra(grid, size):
    start = size * (size - 2) + 1
    end = size * 2 - 2

    distances = [float('inf')] * (size * size)
    distances[start] = 0

    heap = []
    push(heap, [0, start])

    while heap:
        current_distance, current_index = pop(heap)

        if current_index == end:
            return current_distance

        if current_distance > distances[current_index]:
            continue

        neighbors = get_neighbors(current_index, size)
        for i in range(4):
            neighbor = neighbors[i]
            if grid[neighbor] == 0:
                continue
            new_dist = current_distance + 1

            if new_dist < distances[neighbor]:
                distances[neighbor] = new_dist
                push(heap, [new_dist, neighbor])

    return -1

def main():
    grid = [0] * (SIZE * SIZE)
    generate_grid(grid, SIZE)
    #print(" ".join([str(x) for x in grid]))
    #print(grid)

    shortest_path_cost = dijkstra(grid, SIZE)
    print(f"Shortest path cost from bottom-left to top-right: {shortest_path_cost}")

if __name__ == "__main__":
    main()