! Candidate-family comparison on IDENTICAL soft-bit samples. This is a research
! executable, not an application decoder. See MODERN_DECODER_PROVENANCE.txt.
program compare_candidates
  use modern174
  use modern174_candidates
  use packjt77, only: pack77,unpack77
  implicit none
  integer,parameter:: variants=6
  integer*1 :: msg(77),cw(174),decoded(77),outcw(174),ap(174)
  real :: noise(174),llr(174),clipped(174),ordered(174),sigma,t0,t1,dmin,u1,u2,cap,tmp
  real :: elapsed(0:variants),worst(0:variants)
  integer :: ntrials,seedarg,seedsize,ns,t,j,i,v,nhard,niter,i3,n3,scenario
  integer :: good(0:variants),bad(0:variants)
  integer,allocatable:: seed(:)
  logical :: ok,baseok
  character(77):: c77
  character(37):: message,outmsg
  character(32):: arg
  character(37),parameter:: messages(8)=[character(37)::'CQ K1ABC FN42','CQ W9XYZ EN37', &
    'K1ABC W9XYZ -12','W9XYZ K1ABC R-09','K1ABC W9XYZ RR73','TNX 73 GL','CQ EA8/K1ABC','K1ABC W9XYZ RRR']
  seedarg=375429;ntrials=1000
  if(command_argument_count().gt.0)then
    call get_command_argument(1,arg);read(arg,*)seedarg
    call get_command_argument(2,arg);read(arg,*)ntrials
  endif
  call random_seed(size=seedsize);allocate(seed(seedsize));seed=seedarg;call random_seed(put=seed)
  ap=0
  print *, 'variants: 0 traditional; 1 layered SPA; 2 layered NMS; 3 posterior beta1;'
  print *, '4 posterior beta.8; 5 bounded ensemble; 6 median-capped ensemble'
  print *, 'case sigma variant N correct wrong CPU_s max_call_s'
  do scenario=0,2
    do ns=0,3
      if(scenario.gt.0.and.ns.gt.0)cycle
      sigma=.85+.10*real(ns)
      good=0;bad=0;elapsed=0.;worst=0.
      do t=1,ntrials
        message=messages(mod(t-1,size(messages))+1);i3=-1;n3=-1
        call pack77(message,i3,n3,c77,1);read(c77,'(77i1)')msg
        call encode174_91(msg,cw)
        do j=1,174,2
          call random_number(u1);call random_number(u2)
          noise(j)=sqrt(-2.*log(max(1.e-10,u1)))*cos(6.283185307*u2)
          noise(j+1)=sqrt(-2.*log(max(1.e-10,u1)))*sin(6.283185307*u2)
        enddo
        llr=2.*(real(2*cw-1)+sigma*noise)/(sigma*sigma)
        if(scenario.eq.1)llr=2.*noise
        if(scenario.eq.2)then
          do j=1,3
            llr(1+mod(23*j+7*t,174))=-8.*real(2*cw(1+mod(23*j+7*t,174))-1)
          enddo
        endif
        call cpu_time(t0)
        call bpdecode174_91(llr,ap,30,decoded,outcw,nhard,niter);dmin=0.
        if(nhard.lt.0)call osd174_91(llr,ap,3,decoded,outcw,nhard,dmin,1)
        call cpu_time(t1);elapsed(0)=elapsed(0)+t1-t0;worst(0)=max(worst(0),t1-t0)
        baseok=nhard.ge.0.and.nhard+dmin.lt.60.
        if(baseok)then
          write(c77,'(77i1)')decoded;call unpack77(c77,1,outmsg,ok,1);baseok=ok
        endif
        if(baseok)then
          if(all(decoded.eq.msg).and.scenario.ne.1)then
            good=good+1
          else
            bad=bad+1
          endif
          cycle
        endif
        do v=1,variants
          call cpu_time(t0)
          select case(v)
          case(1,2)
            call modern174_layered(llr,ap,v,decoded,outcw,nhard,dmin)
          case(3)
            call modern174_rescue(llr,ap,2,.false.,1,decoded,outcw,nhard,dmin,1.)
          case(4)
            call modern174_rescue(llr,ap,2,.false.,1,decoded,outcw,nhard,dmin,.8)
          case(5)
            call modern174_rescue(llr,ap,2,.false.,1,decoded,outcw,nhard,dmin,1.)
            if(nhard.lt.0)call modern174_rescue(llr,ap,2,.false.,1,decoded,outcw,nhard,dmin,.8)
          case(6)
            ordered=abs(llr)
            do i=2,174
              tmp=ordered(i);j=i-1
              do while(j.ge.1)
                if(ordered(j).le.tmp)exit
                ordered(j+1)=ordered(j);j=j-1
              enddo
              ordered(j+1)=tmp
            enddo
            cap=3.5*(ordered(87)+ordered(88))*.5
            clipped=sign(min(abs(llr),cap),llr)
            call modern174_rescue(clipped,ap,2,.false.,1,decoded,outcw,nhard,dmin,1.)
            if(nhard.lt.0)call modern174_rescue(clipped,ap,2,.false.,1,decoded,outcw,nhard,dmin,.8)
          end select
          if(nhard.ge.0)then
            nhard=count(real(2*outcw-1)*llr.lt.0.)
            dmin=sum(abs(llr),mask=real(2*outcw-1)*llr.lt.0.)
            if(nhard+dmin.ge.50.)nhard=-1
          endif
          call cpu_time(t1);elapsed(v)=elapsed(v)+t1-t0;worst(v)=max(worst(v),t1-t0)
          if(nhard.ge.0)then
            write(c77,'(77i1)')decoded;call unpack77(c77,1,outmsg,ok,1)
            if(ok)then
              if(all(decoded.eq.msg).and.scenario.ne.1)then
                good(v)=good(v)+1
              else
                bad(v)=bad(v)+1
              endif
            endif
          endif
        enddo
      enddo
      do v=0,variants
        write(*,'(i3,f6.2,i4,3i8,2f12.6)')scenario,sigma,v,ntrials,good(v),bad(v),elapsed(v),worst(v)
      enddo
    enddo
  enddo
end program
