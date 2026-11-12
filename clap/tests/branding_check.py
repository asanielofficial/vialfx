import glob, re, sys
bad = 0
for f in glob.glob(sys.argv[1] + '/*'):
    data = open(f, 'rb').read()
    hits = re.findall(rb'(?i)vital\.audio|account\.vital|store\.vital|forum\.vital|vital audio|tytel', data)
    print(f, hits[:10])
    bad += bool(hits)
sys.exit(1 if bad else 0)
