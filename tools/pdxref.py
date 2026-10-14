"""Cross-reference send/receive names in a Pure Data patch.

Usage: python tools/pdxref.py <patch.pd> <out.txt>

Lists each name with its senders (S) and receivers (R), including the send/
receive arguments of IEM GUI widgets and "; name ..." message boxes, and
flags names with no sender or no receiver.
"""
import sys, re, collections
# Build send/receive cross reference from a Pd file
def parse(path):
    txt = open(path, encoding='latin-1').read()
    recs = re.split(r'(?<!\\);\s*\n', txt)
    stack = []
    out = []
    for r in recs:
        r = r.replace('\n', ' ').strip()
        if not r: continue
        if r.startswith('#N canvas'):
            parts = r.split()
            name = parts[6] if len(parts) > 7 else 'ROOT'
            stack.append([name, 0])
            continue
        if r.startswith('#X restore'):
            stack.pop()
            stack[-1][1] += 1
            continue
        if not r.startswith(('#X obj', '#X msg', '#X floatatom', '#X symbolatom', '#X text', '#X listbox')):
            continue
        path_ = '/'.join(s[0] for s in stack)
        idx = stack[-1][1]
        stack[-1][1] += 1
        p = r.split()
        kind = p[1]
        if kind == 'obj' and len(p) > 4:
            cls = p[4]
            a = p[5:]
            if cls in ('s', 'send', 's~', 'throw~') and a:
                out.append(('S', a[0], path_, idx, cls))
            elif cls in ('r', 'receive', 'r~', 'catch~') and a:
                out.append(('R', a[0], path_, idx, cls))
            elif cls in ('tgl', 'bng', 'nbx', 'hsl', 'vsl', 'hradio', 'vradio', 'hdl', 'vdl', 'cnv', 'vu', 'knob'):
                # snd/rcv positions
                pos = {'tgl': (2, 3), 'bng': (4, 5), 'nbx': (6, 7), 'hsl': (6, 7), 'vsl': (6, 7),
                       'hradio': (4, 5), 'vradio': (4, 5), 'hdl': (4, 5), 'vdl': (4, 5), 'cnv': (3, 4), 'vu': (None, 2)}.get(cls)
                if pos:
                    si, ri = pos
                    if si is not None and len(a) > si and a[si] != 'empty':
                        out.append(('S', a[si], path_, idx, cls + '(gui)'))
                    if ri is not None and len(a) > ri and a[ri] != 'empty':
                        out.append(('R', a[ri], path_, idx, cls + '(gui)'))
        elif kind == 'msg':
            body = ' '.join(p[4:])
            for m in re.finditer(r'\\;\s*(\S+)', body):
                out.append(('S', m.group(1), path_, idx, 'msg;'))
        elif kind in ('floatatom', 'symbolatom', 'listbox'):
            a = p[2:]
            # floatatom x y w min max flag label rcv snd
            if len(a) >= 9:
                if a[7] not in ('-', 'empty'): out.append(('R', a[7], path_, idx, kind))
                if a[8] not in ('-', 'empty'): out.append(('S', a[8], path_, idx, kind))
    return out
out = parse(sys.argv[1])
d = collections.defaultdict(lambda: {'S': [], 'R': []})
for k, n, p, i, c in out:
    d[n][k].append('%s#%d(%s)' % (p.replace('ROOT/', '').replace('ROOT', '/'), i, c))
with open(sys.argv[2], 'w', encoding='utf-8') as f:
    for n in sorted(d, key=str.lower):
        s = d[n]['S']; r = d[n]['R']
        flag = ''
        if not s: flag = '  !!NO SENDER'
        if not r: flag = '  !!NO RECEIVER'
        f.write('%s%s\n   S: %s\n   R: %s\n' % (n, flag, '; '.join(s), '; '.join(r)))
