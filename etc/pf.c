#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#define SIZE 4002
uint32_t seed = 8381853;

uint32_t lcg()
{
    seed = (seed + 1) * 141853155;
    return seed;
}
void generate_grid(int *grid, int size)
{
    for (int i = 0; i < size * size; i++)
        grid[i] = lcg() > 1477603712;
    
    grid[size*2-2] = 1;
    grid[size * (size - 2) + 1] = 1;
    for (int n = 0; n < size; n++)
    {
        grid[n*size] = 0;
        grid[n*size + size - 1] = 0;
        grid[n] = 0;
        grid[n + size * (size - 1)] = 0;
    }
}
typedef struct _Node {
    int key;
    int value;
    struct _Node * n[2];
    int flag;
} Node;

typedef struct {
    int key;
    int value;
} PopRet;

size_t total_ops = 0;
void pushraw(Node ** heap, Node * val)
{
    total_ops += 1;
    if (!*heap)
        return (void)(*heap = val);
    else if ((*heap)->key > val->key)
    {
        Node * temp = *heap;
        *heap = val;
        val = temp;
    }
    
    pushraw(&(*heap)->n[(*heap)->flag], val);
    (*heap)->flag = !(*heap)->flag;
}

void push(Node ** heap, int key, int value)
{
    Node * val = (Node*)calloc(1, sizeof(Node));
    val->key = key;
    val->value = value;
    pushraw(heap, val);
}
PopRet pop(Node ** heap)
{
    PopRet ret;
    ret.key = (*heap)->key;
    ret.value = (*heap)->value;

    if ((*heap)->n[1] != NULL)
        pushraw(&((*heap)->n[0]), (*heap)->n[1]);
    
    Node * temp = *heap;
    *heap = (*heap)->n[0]; 
    free(temp);

    return ret;
}
int has_top(Node * f)
{
    return !!f;
}

int dijkstra(int * grid, int size)
{
    int start = size * (size - 2) + 1;
    int end = size * 2 - 2;

    int * distances = (int *)malloc(size * size * sizeof(int));
    for (int i = 0; i < size * size; i++)
        distances[i] = INT_MAX;
    distances[start] = 0;

    Node * heap = 0;
    
    push(&heap, 0, start);

    int result = -1;
    while (has_top(heap))
    {
        PopRet current = pop(&heap);

        if (current.value == end)
        {
            result = current.key;
            break;
        }

        if (current.key > distances[current.value])
            continue;

        int neighbors[4];
        neighbors[0] = current.value - 1;
        neighbors[1] = current.value + 1;
        neighbors[2] = current.value - size;
        neighbors[3] = current.value + size;
        
        
        for (int i = 0; i < 4; i++)
        {
            int neighbor = neighbors[i];
            if (grid[neighbor] == 0) continue;
            int new_dist = current.key + 1;

            if (new_dist < distances[neighbor])
            {
                distances[neighbor] = new_dist;
                push(&heap, new_dist, neighbor);
            }
        }
    }

    while (has_top(heap)) pop(&heap); // free heap
    free(distances);
    return result;
}

int main()
{
    int * grid = (int *)malloc(SIZE * SIZE * sizeof(int));

    generate_grid(grid, SIZE);

    int shortest_path_cost = dijkstra(grid, SIZE);
    printf("Shortest path cost from bottom-left to top-right: %d\n", shortest_path_cost);

    free(grid);
    printf("total ops: %zu\n", total_ops);
    return 0;
}
