#!/bin/bash
# Consolidated analyzer. Usage: analyze2.sh <run_csv> <tshift> <label> <cap_csv>
RUN="$1"; SH="$2"; LABEL="$3"; CAP="$4"
awk -F, -v sh="$SH" -v label="$LABEL" -v capfile="$CAP" '
BEGIN{
  while((getline line<capfile)>0){ split(line,a,","); capmph[a[1]+0]=a[2]+0 }
}
function mean(arr,n){ s=0; for(i=1;i<=n;i++) s+=arr[i]; return n>0? s/n:0 }
{
  t=$1+0; ti=t+sh; rpm=$2; rir=$12+0; thr=$5+0; mph=$16+0; flow=$21+0; rms=$22+0; cl=$11+0
  if (ti>=166 && ti<=168){ b1++; b1r[b1]=rpm+0; b1i[b1]=rir; b1f[b1]=flow; b1m[b1]=rms }
  if (ti>=252 && ti<=255){ b2++; b2r[b2]=rpm+0; b2i[b2]=rir; b2f[b2]=flow; b2m[b2]=rms }
  if (thr>=70 && mph>=80 && ti<=424){ w++; wr[w]=rpm+0; wi[w]=rir; wc[w]=cl; if(flow<0)wn++; if(rir>0)wrsum+=rpm/rir }
  # mph fidelity only where capture has valid (>0) reading at this input-t
  if (ti in capmph && capmph[ti]>0){ dd=mph-capmph[ti]; ad=dd<0?-dd:dd; fa+=ad; fn++; if(dd>fmx)fmx=dd; if(-dd>fmn)fmn=-dd }
}
END{
  np=0;nn=0; for(i=1;i<=b1;i++){ if(b1f[i]>=0)np++; else nn++ }
  printf "%s B1: mean_rpm=%.0f mean_roadrpm=%.0f ratio=%.3f posflow=%d negflow=%d meanrms=%.0f\n", label, mean(b1r,b1), mean(b1i,b1), (mean(b1i,b1)>0?mean(b1r,b1)/mean(b1i,b1):0), np, nn, mean(b1m,b1)
  np=0;nn=0; for(i=1;i<=b2;i++){ if(b2f[i]>=0)np++; else nn++ }
  printf "%s B2: mean_rpm=%.0f mean_roadrpm=%.0f ratio=%.3f posflow=%d negflow=%d meanrms=%.0f\n", label, mean(b2r,b2), mean(b2i,b2), (mean(b2i,b2)>0?mean(b2r,b2)/mean(b2i,b2):0), np, nn, mean(b2m,b2)
  printf "%s WOT(thr>=70,mph>=80,t<=424): n=%d pctneg=%.0f%% meanrpm=%.0f meanroadrpm=%.0f meanratio=%.3f meanclutch=%.3f meanrms=%.0f\n", label, w, (w>0?100.0*wn/w:0), mean(wr,w), mean(wi,w), (w>0?wrsum/w:0), mean(wc,w), 0
  printf "%s MPH_FIDELITY(cap>0): n=%d mean_abs=%.2f max_pos=%.2f max_neg=%.2f\n", label, fn, (fn>0?fa/fn:0), fmx, -fmn
}
' "$RUN"
