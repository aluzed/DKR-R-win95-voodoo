#!/usr/bin/env python3
"""Translates text into a sequence of physical keys for an AZERTY guest.

The host sends scancodes; the guest interprets them with ITS layout. The swapped
letters (A/Q, Z/W, M) and the whole digit row must therefore be sent from the
corresponding QWERTY position.
"""
import sys

SWAP = {'a':'q','q':'a','z':'w','w':'z','m':'semicolon'}
DIGIT = {c:f'shift+{c}' for c in '0123456789'}
PUNCT = {
    ':':'period', '.':'shift+comma', ',':'m', ';':'comma',
    '\\':'ISO_Level3_Shift+8', '/':'shift+period', '_':'8',
    '-':'6', ' ':'space', '=':'slash', '(':'shift+5', ')':'minus',
}

def keys(text):
    out=[]
    for ch in text:
        low=ch.lower()
        if ch in DIGIT: out.append(DIGIT[ch])
        elif low in SWAP:
            k=SWAP[low]; out.append(f'shift+{k}' if ch.isupper() else k)
        elif ch in PUNCT: out.append(PUNCT[ch])
        elif ch.isalpha(): out.append(f'shift+{low}' if ch.isupper() else low)
        else: out.append(ch)
    return out

if __name__ == '__main__':
    print(' '.join(keys(sys.argv[1])))
