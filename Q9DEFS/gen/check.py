import re,sys
f=sys.argv[1] if len(sys.argv)>1 else 'q9sys.d'
sec=grp=None; names={}; vals={}; cnt={}
prob=0
for l in open(f):
    m=re.match(r'\* SECTION: (\d)',l)
    if m: sec=m.group(1); grp=None; continue
    m=re.match(r'\* -- (.*)',l)
    if m: grp=m.group(1); continue
    m=re.match(r'(\S+)\s+equ\s+(\S+)\s+\* (.*)',l)
    if not m: continue
    n,v,c=m.groups(); k=n.lower()
    cnt[sec]=cnt.get(sec,0)+1
    if k in names: print('DUP NAME',n,names[k]); prob+=1
    names[k]=n
    if not re.match(r'\$[0-9a-f]+$',v): continue
    if '[alias]' in c: continue
    val=int(v[1:],16)
    if val==0: continue
    key=sec if sec in '1234' else (sec,grp)
    d=vals.setdefault(key,{})
    if val in d: print('VALUE COLLISION',key,n,d[val],hex(val)); prob+=1
    d[val]=n
    if sec=='4' and len(v)!=5: print('ERR not 16 bit',n,v); prob+=1
print('names',len(names),'problems',prob)
