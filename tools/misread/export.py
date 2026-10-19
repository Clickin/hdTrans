"""export.py MODEL.tsv [OUT.cpp]: writes the misread model as the C++ table of
src/core/word/misread_model_gen.cpp (default OUT) and copies it to tools/misread/model.tsv."""
import os, shutil, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import features as fx

HERE = os.path.dirname(os.path.abspath(__file__))


def literal(s):
    return '"' + ''.join('\\' + c if c in '"\\' else (f'\\{ord(c):03o}' if ord(c) < 0x20 else c) for c in s) + '"'


def main(argv):
    if not 1 <= len(argv) <= 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    weights, bias, threshold, keep = fx.load_model(argv[0])
    out = argv[1] if len(argv) == 2 else os.path.join(HERE, '..', '..', 'src', 'core', 'word', 'misread_model_gen.cpp')
    rows = sorted(weights.items(), key=lambda kv: kv[0].encode('utf-8'))
    with open(out, 'w', encoding='utf-8') as o:
        o.write('// Generated table; do not edit.\n#include "core/word/misread.h"\n\nnamespace hdTrans::word {\n\n'
                'namespace {\n\nconstexpr MisreadWeight kWeights[] = {\n')
        for f, w in rows or [('', 0)]:
            o.write(f'    {{{literal(f)}, {w}}},\n')
        o.write(f'}};\n\nconstexpr MisreadModel kModel{{kWeights, {bias}, {threshold}, {keep}}};\n\n}}  // namespace\n\n'
                'const MisreadModel& BuiltinMisreadModel() { return kModel; }\n\n}  // namespace hdTrans::word\n')
    if os.path.abspath(argv[0]) != os.path.abspath(os.path.join(HERE, 'model.tsv')):
        shutil.copyfile(argv[0], os.path.join(HERE, 'model.tsv'))
    print(f'{len(rows)} weights, bias {bias}, threshold {threshold}, keep {keep}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
