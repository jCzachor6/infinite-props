#!/usr/bin/env python3
"""Reproduce independent density vectors and 90-digit conditional-CDF checks.

Run from any directory. Expected bits never use the native/WASM runtime.
"""
import json, math, struct
from decimal import Decimal, localcontext
from pathlib import Path

def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def bits(x):return struct.unpack('<I',struct.pack('<f',x))[0]
def hash32(x):
 x^=x>>16;x=x*0x7feb352d&0xffffffff;x^=x>>15;x=x*0x846ca68b&0xffffffff;return x^(x>>16)
def inverse_hash(x):
 x^=x>>16;x=x*pow(0x846ca68b,-1,2**32)&0xffffffff;x^=(x>>15)^(x>>30);x=x*pow(0x7feb352d,-1,2**32)&0xffffffff;return x^(x>>16)
def numeric(s):return float(f32(float(s))) if any(c in s.lower() for c in '.e') else float(int(s))
def source_number(text):
 # The established literal grammar rejects strtof underflow; construct the
 # smallest subnormal through exact binary32 multiplication instead.
 return '(1.1754943508222875e-38 * 1.1920928955078125e-7)' if text=='1.401298464324817e-45' else text

def draw(points,lo,hi,u,decimal=False):
 convert=Decimal.from_float if decimal else float
 ps=[(convert(x),convert(y)) for x,y in points];lo,hi,u=map(convert,(lo,hi,u));zero=convert(0);one=convert(1);half=one/convert(2)
 if lo==hi:return lo
 segments=[];mass=zero
 for (a,p0),(b,q0) in zip(ps,ps[1:]):
  left,right=max(lo,a),min(hi,b)
  if left>=right:continue
  def height(x):
   if x==a:return p0
   if x==b:return q0
   t=(x-a)/(b-a);return p0*(one-t)+q0*t
  p,q=height(left),height(right);length=right-left;area=length*((p+q)*half)
  mass+=area;segments.append((left,length,p,q,area))
 target=u*mass;prefix=zero
 for left,length,p,q,area in segments:
  if area>zero and target<prefix+area:
   z=target-prefix
   if z==zero:offset=zero
   else:
    slope=(q-p)/length
    if slope==zero:offset=z/p
    else:
     A=p*p;B=(convert(2)*slope)*z;R=A+B
     if R<zero:
      assert -R<=convert(2**-49)*(abs(A)+abs(B));R=zero
     root=R.sqrt() if decimal else math.sqrt(R)
     offset=(convert(2)*z)/(p+root)
   return min(left+length,max(left,left+min(length,max(zero,offset))))
  prefix+=area
 raise ValueError('missing bounded area')
def cdf_exact(points,lo,hi,x):
 D=Decimal.from_float;lo,hi,x=map(D,(lo,hi,x));mass=Decimal(0);below=Decimal(0)
 for (a,p),(b,q) in zip(points,points[1:]):
  a,p,b,q=map(D,(a,p,b,q));left,right=max(lo,a),min(hi,b)
  if left>=right:continue
  def h(t):return p+(q-p)*(t-a)/(b-a)
  mass+=(right-left)*(h(left)+h(right))/2
  end=min(right,x)
  if end>left:below+=(end-left)*(h(left)+h(end))/2
 return below/mass
rows=[
 ('uniform','0.0','1.0',[('0','1'),('1','1')]),
 ('ascending','0.0','1.0',[('0','0'),('1','2')]),
 ('descending','0.0','1.0',[('0','2'),('1','0')]),
 ('clipped-rise','1.0','3.0',[('0','0'),('4','2')]),
 ('clipped-fall','1.0','3.0',[('0','2'),('4','0')]),
 ('plateau','0.0','4.0',[('0','0'),('1','1'),('3','1'),('4','0')]),
 ('zeros','0.0','4.0',[('0','0'),('1','0'),('2','1'),('3','0'),('4','0')]),
 ('tiny-width','0.0','1.401298464324817e-45',[('0.0','1.0'),('1.401298464324817e-45','1.0')]),
 ('tiny-area','0.0','1.401298464324817e-45',[('0.0','1.401298464324817e-45'),('1.401298464324817e-45','1.401298464324817e-45')]),
 ('tiny-triangle','0.0','1.401298464324817e-45',[('0.0','0.0'),('1.401298464324817e-45','1.401298464324817e-45')]),
 ('near-flat','0.0','1.0',[('0.0','1.0000000031710769e-30'),('1.0','1.0000000972106250e-30')]),
 ('extreme-flat','-3.4028234663852886e38','3.4028234663852886e38',[('-3.4028234663852886e38','3.4028234663852886e38'),('3.4028234663852886e38','3.4028234663852886e38')]),
 ('mixed-exact-keys','16777216.0','16777220.0',[('16777217','1'),('16777218.0','1'),('16777219','1')]),
 ('example-height','14.0','26.0',[('14.0','0.0'),('18.0','1.0'),('22.0','0.6'),('26.0','0.0')]),
 ('equal-zero-support','0.5','0.5',[('0','0'),('1','0'),('2','1')]),
]
raws=[0,1,0x3fffffff,0x40000000,0x7fffffff,0x80000000,0xbfffffff,0xc0000000,0xfffffffe,0xffffffff]
seeds=[0,7,26,*[((inverse_hash(h)-0x9e3779b9)&0xffffffff) for h in raws]]
cases=[]
maximum_error=Decimal(0)
with localcontext() as context:
 context.prec=90
 for name,lower,upper,pairs in rows:
  points=[(numeric(x),numeric(y)) for x,y in pairs];lo,hi=numeric(lower),numeric(upper)
  case={'name':name,'expression':f'{source_number(lower)} to {source_number(upper)} with density '+'{'+','.join(f'{source_number(x)}:{source_number(y)}' for x,y in pairs)+'}','rows':[]}
  for seed in seeds:
   state=(seed+0x9e3779b9)&0xffffffff;raw=hash32(state);u=(raw+.5)*2**-32
   actual=draw(points,lo,hi,u);precise=draw(points,lo,hi,u,True)
   # Compare the prescribed binary64 operation order to independent
   # high-precision mathematics; cancellation can change ideal-real f32 bits.
   if lo!=hi:
    error=abs(cdf_exact(points,lo,hi,actual)-Decimal.from_float(u))
    condition=Decimal.from_float(max(1.0,max(abs(lo),abs(hi))/(hi-lo)))
    assert error<(Decimal(2)**-42)*condition,(name,seed,error,condition)
    maximum_error=max(maximum_error,error)
   mathematical_bits=bits(float(precise))
   case['rows'].append({'seed':seed,'raw_hash':raw,'value_bits':bits(actual),'mathematical_value_bits':mathematical_bits,'state_bits':[bits(state&65535),bits(state>>16)]})
  cases.append(case)
fixture={'note':'Independent Python binary64 reference of spec 0.6 density integration; cross-checked against independent 90-digit Decimal integrals/CDFs within 2^-42 times max(1,abs(coordinate)/span), accounting for binary64 coordinate quantization; operation-order and ideal-real f32 bits are recorded separately. No expected values come from the C++/WASM runtime.','maximum_cdf_error':str(maximum_error),'cases':cases}
(Path(__file__).resolve().parents[1]/'tests/fixtures/density-v0.6-vectors.json').write_text(json.dumps(fixture,indent=2)+'\n')
# Independently pin the visible example after its weighted material draw.
state=26;values=[]
for i in range(4):
 state=(state+0x9e3779b9)&0xffffffff;u=(hash32(state)+.5)*2**-32
 if i:values.append(f32(draw([(numeric(x),numeric(y)) for x,y in rows[-2][3]],14,26,u)))
print('Pinned',len(cases),'cases /',sum(len(c['rows']) for c in cases),'vectors; raw edge seeds:',[hex(s) for s in seeds[3:]])
print('Example heights',values,'bits',[hex(bits(x)) for x in values],'state',hex(state),'halves bits',[hex(bits(state&65535)),hex(bits(state>>16))])
