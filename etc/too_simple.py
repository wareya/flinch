#!/usr/bin/env python

def main():
    sumval = 0.0
    flip = -1.0
    for i in range(1, 10000001):
        flip = -flip
        sumval += flip / ((i << 1) - 1)
    print(f"{sumval * 4.0:.16f}")

if __name__ == "__main__":
    #import dis
    #dis.dis(main)
    main()
