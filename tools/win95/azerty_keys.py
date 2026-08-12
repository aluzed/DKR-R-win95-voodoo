#!/usr/bin/env python3
"""Traduit un texte en sequence de touches physiques pour un invite AZERTY.

L'hote envoie des scancodes ; l'invite les interprete avec SA disposition. Les
lettres croisees (A/Q, Z/W, M) et toute la rangee des chiffres doivent donc etre
envoyees depuis la position QWERTY correspondante.
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
