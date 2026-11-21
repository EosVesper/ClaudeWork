import cantera as ct, numpy as np, sys
sp=[s for s in ct.Species.list_from_file('nasa_gas.yaml') if set(s.composition)<= {'C','H','O'} and not s.name.endswith('+') and not s.name.endswith('-')]
gas=ct.Solution(thermo='ideal-gas', species=sp)
g0=9.80665
def run(name, ox, fuel, of, pc, eps, pa):
    # ox/fuel: (formula dict per mol, molar mass kg/mol, h J/mol)
    mo, mf = of/(1+of), 1/(1+of)
    elem={}
    for (comp,M,h),m in [(ox,mo),(fuel,mf)]:
        for e,n in comp.items(): elem[e]=elem.get(e,0)+n*m/M
    H=sum(h*m/M for (c,M,h),m in [(ox,mo),(fuel,mf)])  # J/kg
    # initial composition of products from element moles
    C,Hh,O=elem['C'],elem['H'],elem['O']
    orem=O-C; h2o=min(Hh/2,orem); h2=Hh/2-h2o; orem-=h2o
    co2=min(orem,C); o2=(orem-co2)/2
    guess={'H2O':h2o,'H2':h2,'CO':C-co2,'CO2':co2,'O2':o2}
    gas.TPX=3000,pc,{k:v for k,v in guess.items() if v>0}
    assert abs(gas.elemental_mass_fraction('C')-C*0.0120107)<2e-4, 'element balance'
    # fix elemental mass exactly via mass fractions of element-carrying species
    gas.HP=H,pc; gas.equilibrate('HP')
    Tc=gas.T; s0=gas.s; h0=gas.h
    def state(p, frozen=None):
        if frozen is None: gas.SP=s0,p; gas.equilibrate('SP')
        else: gas.TPY=Tc,pc,frozen; gas.SP=s0,p
        u=np.sqrt(max(2*(h0-gas.h),0)); return u, gas.density, gas.P
    Yc=gas.Y.copy()
    out={}
    for mode in ['eq','fc']:
        fr=None if mode=='eq' else Yc
        ps=np.geomspace(pc*0.45,pc*0.7,300); G=[]
        for p in ps:
            u,r,_=state(p,fr); G.append(r*u)
        i=int(np.argmax(G)); Gt=G[i]
        cstar=pc/Gt
        lo,hi=np.log(pc*1e-6),np.log(ps[i])
        for _ in range(80):
            m=(lo+hi)/2; u,r,_=state(np.exp(m),fr); e=Gt/(r*u)
            if e>eps: lo=m
            else: hi=m
        pe=np.exp((lo+hi)/2); u,r,_=state(pe,fr); Te=gas.T
        ispv=u+pe*eps/Gt; isp=u+(pe-pa)*eps/Gt
        out[mode]=dict(cstar=cstar,isp=isp/g0,ispVac=ispv/g0,pe=pe/1e3,Te=Te)
    print(f"{name}: Tc={Tc:.1f} K, h0={H/1e6:.4f} MJ/kg")
    for k,v in out.items(): print('  ',k,{a:round(b,2) for a,b in v.items()})
LOX=({'O':2},0.0319988,-12979.0); CH4=({'C':1,'H':4},0.0160425,-89233.0); RP1=({'C':1,'H':1.9423},0.0139761,-24717.7)
run('TQ-12B',LOX,CH4,2.9,14e6,20.5,101325)
run('TQ-15B',LOX,CH4,2.9,14e6,50,0)
run('YF-100K',LOX,RP1,2.6,18.7e6,35,101325)
