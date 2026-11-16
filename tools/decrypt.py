import sys
def bitswap8(v,*bits): return sum(((v>>b)&1)<<(7-i) for i,b in enumerate(bits))
def decrypt(data,key,perm,start=0x3000):
    t=bytes(bitswap8(c^key,*perm) for c in range(256))
    d=bytearray(data); d[start:]=bytes(t[c] for c in d[start:]); return bytes(d)
if __name__=='__main__':
    src,dst,key=sys.argv[1],sys.argv[2],int(sys.argv[3],0); perm=[int(x) for x in sys.argv[4].split(',')]
    open(dst,'wb').write(decrypt(open(src,'rb').read(),key,perm))
