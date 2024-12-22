#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <string.h>

#define SIZE 2000
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
    struct _Node * left;
    struct _Node * right;
    int flag;
} Node;

typedef struct {
    int key;
    int value;
} PopRet;

void pushraw(Node ** heap, Node * val)
{
    if (!*heap)
        *heap = val;
    else if ((*heap)->key > val->key)
    {
        Node * temp = *heap;
        *heap = val;
        pushraw(heap, temp);
    }
    else
    {
        (*heap)->flag = ((*heap)->flag + 1) & 1;
        pushraw((*heap)->flag == 0 ? &((*heap)->left) : &((*heap)->right), val);
    }
}
void push(Node ** heap, int key, int value)
{
    Node * val = (Node*)calloc(sizeof(Node), 1);
    val->key = key;
    val->value = value;
    pushraw(heap, val);
}
PopRet pop(Node ** heap)
{
    PopRet ret;
    ret.key = (*heap)->key;
    ret.value = (*heap)->value;

    if ((*heap)->right != NULL)
        pushraw(&((*heap)->left), (*heap)->right);
    Node * temp = *heap;
    *heap = (*heap)->left; 
    free(temp);

    return ret;
}

int dijkstra(int *grid, int size)
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
    while (heap)
    {
        PopRet current = pop(&heap);

        if (current.value == end)
        {
            result = current.key;
            break;
        }

        if (current.key > distances[current.value]) continue;

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

    while (heap) pop(&heap); // free heap
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
    return 0;
}
