"""Dump a Pure Data patch as readable text.

Usage: python tools/pd2txt.py <patch.pd> <out.txt>

Every canvas (subpatch) is listed with numbered objects, prefixed by their
@x,y position, then its connections as "src:outlet [obj] -> dst:inlet [obj]".
Pd orders a subpatch's inlets/outlets by x position, so use the coordinates
to map subpatch ports. GEM/Pd semantics notes: Documentation/PD114-ANALYSIS.md.
"""
import sys, re
def parse(path):
    txt = open(path, encoding='latin-1').read()
    recs = re.split(r'(?<!\\);\s*\n', txt)
    stack = []; root = None
    for r in recs:
        r = r.replace('\n', ' ').strip()
        if not r: continue
        if r.startswith('#N canvas'):
            parts = r.split()
            name = parts[6] if len(parts) > 7 else 'ROOT'
            c = {'name': name, 'objs': [], 'conns': [], 'children': [], 'hdr': r}
            stack.append(c)
            if root is None: root = c
        elif r.startswith('#X restore'):
            c = stack.pop(); parent = stack[-1]
            p = r.split()
            body = ' '.join(p[4:])
            parent['objs'].append('@%s,%s [%s]  (subpatch)' % (p[2], p[3], body))
            parent['children'].append(c); c['label'] = body
        elif r.startswith('#X connect'):
            p = r.split()
            stack[-1]['conns'].append(tuple(int(x) for x in p[2:6]))
        elif r.startswith('#X coords'):
            stack[-1]['coords'] = r
        elif r.startswith(('#X obj', '#X msg', '#X floatatom', '#X symbolatom', '#X text', '#X array', '#X listbox')):
            p = r.split(' ', 4)
            kind = p[1]; body = p[4] if len(p) > 4 else ''
            pos = '@%s,%s ' % (p[2], p[3])
            if kind == 'obj': s = '[' + body + ']'
            elif kind == 'msg': s = 'msg(' + body + ')'
            elif kind == 'text': s = '## ' + body
            else: s = kind + ' ' + body
            stack[-1]['objs'].append(pos + s)
        elif r.startswith('#A'):
            stack[-1]['objs'].append('  #A ' + r[:200])
    return root
def short(s, n=60):
    s = re.sub(r'\s+', ' ', s)
    s = re.sub(r'^@\S+ ', '', s)
    return s if len(s) < n else s[:n-3] + '...'
def dump(c, path, f):
    f.write('\n' + '='*70 + '\nCANVAS ' + path + '   ' + c['hdr'] + ('  ' + c.get('coords','') ) + '\n')
    for i, o in enumerate(c['objs']):
        f.write('  %3d %s\n' % (i, re.sub(r'\s+',' ',o)))
    f.write('  -- connections\n')
    for a, ao, b, bi in c['conns']:
        oa = c['objs'][a] if a < len(c['objs']) else '?'
        ob = c['objs'][b] if b < len(c['objs']) else '?'
        f.write('  %3d:%d %-50s -> %3d:%d %s\n' % (a, ao, short(oa, 50), b, bi, short(ob, 60)))
    for ch in c['children']:
        dump(ch, path + '/' + ch.get('label', ch['name']), f)
root = parse(sys.argv[1])
with open(sys.argv[2], 'w', encoding='utf-8') as f: dump(root, 'ROOT', f)
