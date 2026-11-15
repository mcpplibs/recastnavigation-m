#!/usr/bin/env python3
"""The constants of port/DeterministicMath.cpp, printed as C++ hexadecimal floating literals.

pi/2 in three parts (28, 28 and 53 bits), 2/pi and pi in double, the first 256 bits of 2/pi (Payne-Hanek),
atan(k/8), and the Taylor coefficients of sin, cos and atan, each computed with mpmath at 600 bits and
rounded to double once (PIO2_1, PIO2_2 are exact: True). Run: python3 tools/port_constants.py
"""
import mpmath
mpmath.mp.prec = 600
from mpmath import mpf, pi, atan, floor, factorial
def hexd(v): return float(v).hex()
def trunc_bits(v, bits):
    # v positive: keep the leading `bits` significant bits
    e = int(mpmath.floor(mpmath.log(v, 2)))
    scale = mpf(2) ** (bits - 1 - e)
    return mpmath.floor(v * scale) / scale
p = pi / 2
p1 = trunc_bits(p, 28); p2 = trunc_bits(p - p1, 28); p3 = p - p1 - p2
print('PIO2_1', hexd(p1), float(p1) == p1)
print('PIO2_2', hexd(p2), float(p2) == p2)
print('PIO2_3', hexd(p3))
print('INV_PIO2', hexd(2 / pi))
print('PI', hexd(pi), 'PIO2', hexd(p), 'PIO4', hexd(pi/4))
t = 2 / pi
words = []
for i in range(4):
    t *= 2**64
    w = int(floor(t)); t -= w
    words.append('0x%016x' % w)
print('TWO_OVER_PI', words)
print('ATAN', [hexd(atan(mpf(k) / 8)) for k in range(9)])
print('SIN', [hexd((-1)**n / factorial(2*n+1)) for n in range(1, 9)])
print('COS', [hexd((-1)**n / factorial(2*n)) for n in range(1, 10)])
print('ATANS', [hexd(mpf((-1)**n) / (2*n+1)) for n in range(1, 8)])
