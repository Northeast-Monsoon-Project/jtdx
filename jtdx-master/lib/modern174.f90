! Optional BP-posterior guided OSD rescue for the FT8/FT4 (174,91) code.
! Based on the hybrid decoder strategy in WSJT-X decode174_91.f90; see
! MODERN_DECODER_PROVENANCE.txt. Traditional decoder entry points are unchanged.
module modern174
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  implicit none
  logical :: modern_ft8=.false., modern_ft4=.false.
contains
  subroutine modern174_decode(llr,apmask,isft4,nthr,message77,cw,nharderror,dmin,evidence)
    real, intent(in) :: llr(174)
    integer*1, intent(in) :: apmask(174)
    logical, intent(in) :: isft4
    integer, intent(in) :: nthr
    integer :: depth
    integer*1, intent(out) :: message77(77),cw(174)
    integer, intent(out) :: nharderror
    real, intent(out) :: dmin
    real, intent(in), optional :: evidence(174,3)
    ! Six bounded OSD attempts at most; no random state and no shared scratch.
    ! Additional searches never pin guessed callsigns or messages.
    message77=0;cw=0;nharderror=-1;dmin=0.0
    if(any(apmask.ne.0))return
    depth=3
    if(isft4)depth=2
    call modern174_rescue(llr,apmask,depth,isft4,nthr,message77,cw,nharderror,dmin,1.0,evidence)
    if(nharderror.lt.0) &
      call modern174_rescue(llr,apmask,depth,isft4,nthr,message77,cw,nharderror,dmin,0.8,evidence)
  end subroutine

  logical function modern174_valid(llr,apmask,cw)
    real, intent(in) :: llr(174)
    integer*1, intent(in) :: apmask(174),cw(174)
    integer*1 :: encoded(174),message(77),decoded(91)
    integer :: badcrc
    modern174_valid=.false.
    if(.not.all(ieee_is_finite(llr))) return
    if(any(cw.lt.0) .or. any(cw.gt.1) .or. all(cw.eq.0)) return
    decoded=cw(1:91)
    call chkcrc14a(decoded,badcrc)
    if(badcrc.ne.0) return
    message=cw(1:77)
    call encode174_91(message,encoded)
    if(any(encoded.ne.cw)) return
    if(any(apmask.eq.1 .and. real(2*cw-1)*llr.lt.0.0)) return
    modern174_valid=.true.
  end function

  subroutine modern174_rescue(llr,apmask,depth,isft4,nthr,message77,cw,nharderror,dmin,beta,evidence)
    real, intent(in) :: llr(174)
    integer*1, intent(in) :: apmask(174)
    integer, intent(in) :: depth,nthr
    logical, intent(in) :: isft4
    integer*1, intent(out) :: message77(77),cw(174)
    integer, intent(out) :: nharderror
    real, intent(out) :: dmin
    real, intent(in), optional :: evidence(174,3)
    real, intent(in), optional :: beta
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
          tov(i,j)=weight*2.0*y
        enddo
      enddo
    enddo
  end subroutine

  logical function modern174_evidence(llr,apmask,candidate,err,metric,evidence) result(adequate)
    real, intent(in) :: llr(174)
    integer*1, intent(in) :: apmask(174),candidate(174)
    integer, intent(out) :: err
    real, intent(out) :: metric
    real, intent(in), optional :: evidence(174,3)
    integer :: view
    real :: channel_cost
    adequate=.false.; err=-1; metric=0.
    if(.not.modern174_valid(llr,apmask,candidate)) return
    err=count(real(2*candidate-1)*llr.lt.0.0)
    metric=sum(abs(llr),mask=real(2*candidate-1)*llr.lt.0.0)
    ! Posterior LLRs guide the search; only original received channel metrics
    ! count as evidence. Correlated views are NOT independent votes.
    if(err+metric.ge.60.0) return
    adequate=err+metric.lt.50.0
    if(present(evidence)) then
      do view=1,3
        if(.not.all(ieee_is_finite(evidence(:,view)))) cycle
        if(maxval(abs(evidence(:,view))).le.1.e-6) cycle
        channel_cost=count(real(2*candidate-1)*evidence(:,view).lt.0.0) &
          +sum(abs(evidence(:,view)),mask=real(2*candidate-1)*evidence(:,view).lt.0.0)
        if(channel_cost.lt.50.0) adequate=.true.
      enddo
    endif
  end function
end module
