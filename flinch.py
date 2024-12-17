#!/usr/bin/env python

import sys

with open(sys.argv[1], "r", encoding="utf-8") as f:
    text = f.read()

line = 0
i = 0
i2 = 0
def next():
    global line
    global i
    global i2
    while i < len(text) and text[i] in " \t\r\n#":
        if text[i] != "#":
            if text[i] in "\r\n":
                line += 1
            i += 1
            continue
        while i < len(text) and not text[i] in "\r\n":
            i += 1
        while i < len(text) and text[i] in "\r\n":
            line += 1
            i += 1
    i2 = i
    while i2 < len(text) and not text[i2] in " \t\r\n#":
        i2 += 1
    ret = text[i:i2]
    i = i2
    return ret

program = []
lines = []

while i < len(text):
    program.append(next())
    lines.append(line + 1)

class Func:
    __slots__ = ["loc", "len", "name"]
    def __init__(self, loc, _len, name):
        self.loc = loc
        self.len = _len
        self.name = name

class Var:
    __slots__ = ["loc", "name"]
    def __init__(self, loc, name):
        self.loc = loc
        self.name = name

class Label:
    __slots__ = ["loc"]
    def __init__(self, loc):
        self.loc = loc

# interpreter

call_stack = []
fstack = []
funcs = {}
varstack = [{}]
labels = {}
evalstack = [[]]

def valpush(x):
    return evalstack[-1].append(x)
def valpop():
    return evalstack[-1].pop()

def push_sentinel():
    return evalstack.append([])
def pop_sentinel():
    return evalstack.pop()

def isint(x):
    try: x = int(x); return True
    except: return False

def isfloat(x):
    try: x = float(x); return True
    except: return False

import re
def isname(s):
    return bool(re.match(r"[a-zA-Z_][a-zA-Z0-9_]*", s))

i = 0
try:
    while program[i] != "":
        token = program[i]
        if token != "^^" and token[-1] == "^" and len(token) >= 2:
            assert(len(call_stack) == 0)
            start = i
            name = token[:-1]
            labels[name] = {}
            while token != "^^":
                if token[-1] == ":" and len(token) >= 2:
                    labels[name][token[:-1]] = Label(i)
                i += 1
                token = program[i]
            funcs[name] = Func(start, i - start, name)
        elif token != "^^" and token[0] == "^" and len(token) >= 2:
            valpush(funcs[token[1:]])
        elif token == "^^":
            fstack.pop()
            i, is_eval = call_stack.pop()
            if is_eval:
                valpush(0)
        elif token == "return":
            val = valpop()
            fstack.pop()
            i, is_eval = call_stack.pop()
            if is_eval:
                valpush(val)
        elif token[0] == "$" and token[-1] == "$" and len(token) >= 3:
            varstack[-1][token[1:-1]] = 0
            valpush(Var(varstack[-1], token[1:-1]))
        elif token[0] == "$" and len(token) >= 2:
            valpush(Var(varstack[-1] if token[1:] in varstack[-1] else varstack[0], token[1:]))
        elif token[-1] == "$" and len(token) >= 2:
            varstack[-1][token[:-1]] = 0
        elif token[0] == ":" and len(token) >= 2:
            valpush(labels[fstack[-1]][token[1:]])
        elif token[-1] == ":" and len(token) >= 2:
            pass # already done when skipping over function
        elif token == "call" or token == "call_eval":
            f = valpop()
            assert(type(f) is Func)
            call_stack.append((i, token == "call_eval"))
            fstack.append(f.name)
            varstack.append({})
            i = f.loc
        elif token == "<-":
            var = valpop()
            val = valpop()
            assert(type(var) is Var)
            var.loc[var.name] = val
        elif token == "<<<":
            push_sentinel()
        elif token == ">>>":
            pop_sentinel()
        elif token == "if_goto":
            dest = valpop()
            val = valpop()
            if val:
                i = dest.loc
        elif token == "goto":
            dest = valpop()
            i = dest.loc
        elif token in "+-*/%":
            b = valpop()
            a = valpop()
            if token == "+": valpush(a + b)
            elif token == "-": valpush(a - b)
            elif token == "*": valpush(a * b)
            elif token == "/":
                if type(a) is int and type(b) is int:
                    valpush(a // b)
                else:
                    valpush(a / b)
            elif token == "%": valpush(a % b)
        elif token in ["<="]:
            b = valpop()
            a = valpop()
            if token == "<=": valpush(a <= b)
        else:
            if isint(token):
                valpush(int(token))
            elif isfloat(token):
                valpush(float(token))
            elif isname(token):
                loc = varstack[-1] if token in varstack[-1] else varstack[0]
                valpush(loc[token])
            else:
                print("TODO:", token)
                assert(0)
        
        i += 1
except Exception as e:
    print(f"error on line {lines[i]}")
    raise e

print("done!")

print(evalstack[0])

