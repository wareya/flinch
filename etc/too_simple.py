#!/usr/bin/env python

def main():
    sum = 0.0
    flip = -1.0
    for i in range(1, 10000001):
        flip *= -1.0
        sum += flip / (2 * i - 1)
    print(f"{sum * 4.0:.16f}")

if __name__ == "__main__":
    main()
