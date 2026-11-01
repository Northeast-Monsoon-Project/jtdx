subroutine fm_ft2_ldpc(llr,keff,maxosd,ndeep,apmask,message91,cw,ntype,nharderror,dmin)
  implicit none
  real llr(174),dmin
  integer keff,maxosd,ndeep,ntype,nharderror,niterations
  integer*1 apmask(174),message91(91),message77(77),cw(174)
  message91=0; message77=0; cw=0; ntype=0; dmin=0.0
  call bpdecode174_91(llr,apmask,40,message77,cw,nharderror,niterations)
  if(nharderror.lt.0 .and. maxosd.ge.0) then
    call osd4_174_91(llr,apmask,min(3,ndeep),message77,cw,nharderror,dmin)
    ntype=1
  endif
  if(nharderror.ge.0) message91(1:77)=message77
end subroutine fm_ft2_ldpc
