! Evaluation-only layered alternatives; not linked into the application.
module modern174_candidates
  use modern174, only: modern174_valid
  use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
  implicit none
contains
  subroutine modern174_layered(llr,apmask,algorithm,message77,cw,nharderror,dmin)
    real, intent(in) :: llr(174)
    integer*1, intent(in) :: apmask(174)
    integer, intent(in) :: algorithm
    integer*1, intent(out) :: message77(77),cw(174)
    integer, intent(out) :: nharderror
    real, intent(out) :: dmin
    integer, parameter :: n=174,k=91,m=83
    integer :: nrw(m),ncw,Nm(7,m),Mn(3,n),i,j,l,bit,iter
    real :: z(n),messages(7,m),q(7),r(7),tt(7),p,y
    integer*1 :: candidate(n)
    include 'ft8v2/ldpc_174_91_c_reordered_parity.f90'
    message77=0;cw=0;nharderror=-1;dmin=0.0
    if(.not.all(ieee_is_finite(llr)) .or. maxval(abs(llr)).le.1.e-6) return
    z=llr;messages=0.0
    do iter=1,30
      do j=1,m
        do i=1,nrw(j)
          bit=Nm(i,j)
          if(apmask(bit).eq.1) then
            q(i)=llr(bit)
          else
            q(i)=z(bit)-messages(i,j)
          endif
        enddo
        if(algorithm.eq.1) then
          tt(1:nrw(j))=tanh(-q(1:nrw(j))/2.0)
          do i=1,nrw(j)
            p=1.0
            do l=1,nrw(j)
              if(l.ne.i)p=p*tt(l)
            enddo
            call platanh(-p,y)
            r(i)=2.0*y
          enddo
        else
          do i=1,nrw(j)
            p=-1.0;y=1.e6
            do l=1,nrw(j)
              if(l.eq.i) cycle
              p=p*sign(1.0,-q(l));y=min(y,abs(q(l)))
            enddo
            r(i)=0.8*p*y
          enddo
        endif
        do i=1,nrw(j)
          bit=Nm(i,j)
          if(apmask(bit).ne.1)z(bit)=q(i)+r(i)
          messages(i,j)=r(i)
        enddo
      enddo
      candidate=0
      where(z.gt.0.0) candidate=1
      do j=1,m
        if(mod(sum(candidate(Nm(1:nrw(j),j))),2).ne.0) exit
      enddo
      if(j.le.m) cycle
      if(.not.modern174_valid(llr,apmask,candidate)) cycle
      nharderror=count(real(2*candidate-1)*llr.lt.0.0)
      dmin=sum(abs(llr),mask=real(2*candidate-1)*llr.lt.0.0)
      if(nharderror+dmin.ge.60.0) then
        nharderror=-1;cycle
      endif
      cw=candidate;message77=candidate(1:77)
      return
    enddo
  end subroutine

end module
