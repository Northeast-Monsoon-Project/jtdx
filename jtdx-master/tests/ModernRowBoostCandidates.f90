! Research only. No application links this module. See docs/FT8_V370_VALIDATION.md.
module modern174_rowscreen
  use modern174, only: modern174_rescue,modern174_evidence
  use ieee_arithmetic, only: ieee_is_finite
  implicit none
contains
  subroutine screen_decode(variant,llr,apmask,isft4,nthr,message77,cw,nharderror,dmin,evidence)
    integer, intent(in) :: variant,nthr
    real, intent(in) :: llr(174)
    integer*1, intent(in) :: apmask(174)
    logical, intent(in) :: isft4
    integer*1, intent(out) :: message77(77),cw(174)
    integer, intent(out) :: nharderror
    real, intent(out) :: dmin
    real, optional, intent(in) :: evidence(174,3)
    integer :: depth
    depth=3; if(isft4) depth=2
    call modern174_rescue(llr,apmask,depth,isft4,nthr,message77,cw,nharderror,dmin,1.,evidence)
    if(nharderror.ge.0) return
    select case(variant)
    case(0)
      call modern174_rescue(llr,apmask,depth,isft4,nthr,message77,cw,nharderror,dmin,.8,evidence)
    case(1)
      call modern174_rescue(llr,apmask,depth,isft4,nthr,message77,cw,nharderror,dmin,.9,evidence)
    case(2)
      call rowboost_rescue(llr,apmask,depth,isft4,nthr,message77,cw,nharderror,dmin,.8,evidence,1.5)
    case(3)
      call rowboost_rescue(llr,apmask,depth,isft4,nthr,message77,cw,nharderror,dmin,.8,evidence,2.)
    end select
  end subroutine
  subroutine rowboost_rescue(llr,apmask,depth,isft4,nthr,message77,cw,nharderror,dmin,beta,evidence,boost)
    real, intent(in) :: llr(174)
    integer*1, intent(in) :: apmask(174)
    integer, intent(in) :: depth,nthr
    logical, intent(in) :: isft4
    integer*1, intent(out) :: message77(77),cw(174)
    integer, intent(out) :: nharderror
    real, intent(out) :: dmin
    real, intent(in), optional :: evidence(174,3)
    real, intent(in), optional :: beta
    real, intent(in) :: boost
    real :: row_weight(83),anchor_score(83)
    integer :: anchor,boosted,hard(174)
    integer, parameter :: n=174,k=91,m=83
    integer :: nrw(m),ncw,Nm(7,m),Mn(3,n)
    real :: tov(3,n),toc(7,m),tt(7,m),zn(n),zsum(n),guided(n),p,y,metric,weight
    integer :: i,j,kk,ibj,ichk,iter,err
    integer*1 :: candidate(n),decoded(77)
    include 'ft8v2/ldpc_174_91_c_reordered_parity.f90'
    message77=0;cw=0;nharderror=-1;dmin=0.0
    weight=1.0
    if(present(beta)) weight=beta
    if(.not.all(ieee_is_finite(llr)) .or. maxval(abs(llr)).le.1.e-6) return
    row_weight=weight
    hard=0; where(llr.gt.0.) hard=1
    ! Hypothesis inspired by RBE (arXiv:2608.09560), not its trained row
    ! selection: boost four initially satisfied, most reliable checks.
    do j=1,m
      anchor_score(j)=-1.
      if(mod(sum(hard(Nm(:nrw(j),j))),2).eq.0) &
        anchor_score(j)=minval(abs(llr(Nm(:nrw(j),j))))
    enddo
    do boosted=1,4
      anchor=maxloc(anchor_score,dim=1)
      if(anchor_score(anchor).lt.0.) exit
      row_weight(anchor)=weight*boost
      anchor_score(anchor)=-1.
    enddo
    tov=0.0;zsum=0.0
    ! Accumulate the channel and the first three BP posterior estimates. This
    ! changes the reliability ordering explored by OSD, not the transmitted code.
    do iter=0,3
      do i=1,n
        zn(i)=llr(i)
        if(apmask(i).ne.1) zn(i)=zn(i)+sum(tov(:,i))
      enddo
      zsum=zsum+zn
      ! Try each posterior as soon as it is available. Successful early OSD
      ! attempts avoid unused BP updates, preserving the previous candidate
      ! order, search budget and original-channel acceptance criteria.
      if(iter.gt.0) then
        guided=zsum/real(iter+1)
        if(isft4) then
          call osd4_174_91(guided,apmask,depth,decoded,candidate,err,metric)
        else
          call osd174_91(guided,apmask,depth,decoded,candidate,err,metric,nthr)
        endif
        if(err.ge.0) then
          if(modern174_evidence(llr,apmask,candidate,err,metric,evidence)) then
            message77=decoded; cw=candidate; nharderror=err; dmin=metric
            return
          endif
        endif
      endif
      if(iter.eq.3) exit
      do j=1,m
        do i=1,nrw(j)
          ibj=Nm(i,j);toc(i,j)=zn(ibj)
          do kk=1,ncw
            if(Mn(kk,ibj).eq.j) toc(i,j)=toc(i,j)-tov(kk,ibj)
          enddo
        enddo
        tt(1:nrw(j),j)=tanh(-toc(1:nrw(j),j)/2.0)
      enddo
      do j=1,n
        do i=1,ncw
          ichk=Mn(i,j)
          p=product(tt(1:nrw(ichk),ichk),mask=Nm(1:nrw(ichk),ichk).ne.j)
          call platanh(-p,y)
          tov(i,j)=row_weight(ichk)*2.0*y
        enddo
      enddo
    enddo
  end subroutine

end module
