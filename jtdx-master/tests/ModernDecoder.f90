! Deterministic comparison at the soft-bit interface, NOT an audio SNR test.
! Run without arguments for a bounded regression; seed/trials enable a longer
! held-out benchmark: fm_modern_decoder 837284 1500.
program test_modern_decoder
  use modern174
  use ft8_mod1, only: gen
  use packjt77, only: pack77, unpack77
  use, intrinsic :: ieee_arithmetic, only: ieee_value, ieee_quiet_nan, ieee_positive_inf
  implicit none
  integer*1 :: msg(77),cw(174),decoded(77),outcw(174),ap(174),altered(174),plain(77)
  integer*1,parameter :: rvec(77)=[0,1,0,0,1,0,1,0,0,1,0,1,1,1,1,0,1,0,0,0,1,0,0,1,1,0,1,1,0, &
    1,0,0,1,0,1,1,0,0,0,0,1,0,0,0,1,0,1,0,0,1,1,1,1,0,0,1,0,1, &
    0,1,0,1,0,1,1,0,1,1,1,1,1,0,0,0,1,0,1]
  real :: noise(174),llr(174),sigma,t0,t1,t2,oldtime,extra,maxextra,dmin,u1,u2
  integer :: ntrials,seedarg,seedsize,mode,scenario,ns,t,j,nhard,niter,i3,n3,ngood,modern_good
  integer :: nwrong,modern_wrong,total_rescued,nnoise,oldhard
  real :: oldmetric
  integer,allocatable :: seed(:)
  logical :: oldok,modernok,unpackok,isft4,benchmark
  character(77) :: c77
  character(37) :: message,outmsg
  character(32) :: arg
  character(37),parameter :: messages(8)=[character(37)::'CQ K1ABC FN42','CQ W9XYZ EN37', &
    'K1ABC W9XYZ -12','W9XYZ K1ABC R-09','K1ABC W9XYZ RR73','TNX 73 GL','CQ EA8/K1ABC','K1ABC W9XYZ RRR']
  ntrials=200;seedarg=582901;benchmark=command_argument_count().gt.0
  if(benchmark)then
    call get_command_argument(1,arg);read(arg,*)seedarg
    call get_command_argument(2,arg);read(arg,*)ntrials
  endif
  if(ntrials.lt.1.or.ntrials.gt.100000)error stop 'Invalid trial count'
  call random_seed(size=seedsize);allocate(seed(seedsize));seed=seedarg
  call random_seed(put=seed)
  ap=0;msg=0;msg(1:4)=1
  call encode174_91(msg,cw)
  llr=5.*real(2*cw-1)
  if(.not.modern174_valid(llr,ap,cw))error stop 'Valid codeword rejected'
  altered=cw;altered(174)=1-altered(174)
  if(modern174_valid(llr,ap,altered))error stop 'Invalid parity accepted'
  altered=cw;altered(78)=1-altered(78)
  if(modern174_valid(llr,ap,altered))error stop 'Invalid CRC accepted'
  ! A valid LDPC word can still have an invalid CRC. In the old OSD interface
  ! negating a zero hard-error count cannot mark this case as failed.
  call osd174_91(llr,ap,0,decoded,outcw,nhard,dmin,1)
  if(nhard.lt.0.or.any(outcw.ne.cw))error stop 'Valid traditional FT8 word changed'
  call osd4_174_91(llr,ap,0,decoded,outcw,nhard,dmin)
  if(nhard.lt.0.or.any(outcw.ne.cw))error stop 'Valid traditional FT4 word changed'
  outcw=cw;outcw(78)=1-outcw(78);altered=0
  do j=1,91
    if(outcw(j).eq.1)altered=ieor(altered,gen(j,:))
  enddo
  llr=5.*real(2*altered-1)
  if(modern174_valid(llr,ap,altered))error stop 'LDPC-valid but CRC-invalid word accepted'
  call osd174_91(llr,ap,0,decoded,outcw,nhard,dmin,1)
  if(nhard.ge.0)error stop 'Traditional FT8 accepted zero-distance invalid CRC'
  call osd4_174_91(llr,ap,0,decoded,outcw,nhard,dmin)
  if(nhard.ge.0)error stop 'Traditional FT4 accepted zero-distance invalid CRC'
  llr=5.*real(2*cw-1)
  altered=0
  if(modern174_valid(llr,ap,altered))error stop 'All-zero word accepted'
  ap(1)=1;llr(1)=-llr(1)
  if(modern174_valid(llr,ap,cw))error stop 'Contradictory AP accepted'
  call modern174_decode(llr,ap,.false.,1,decoded,outcw,nhard,dmin)
  if(nhard.ge.0)error stop 'Extra rescue must not search with AP guesses'
  ap=0;llr=0
  call modern174_decode(llr,ap,.false.,1,decoded,outcw,nhard,dmin)
  if(nhard.ge.0)error stop 'Zero evidence decoded'
  llr(1)=ieee_value(1.,ieee_quiet_nan)
  call modern174_decode(llr,ap,.false.,1,decoded,outcw,nhard,dmin)
  if(nhard.ge.0)error stop 'NaN evidence decoded'
  llr(1)=ieee_value(1.,ieee_positive_inf)
  call modern174_decode(llr,ap,.true.,1,decoded,outcw,nhard,dmin)
  if(nhard.ge.0)error stop 'Infinite evidence decoded'
  total_rescued=0
  write(*,'(a)') 'mode case sigma trials old_correct modern_correct old_wrong modern_wrong old_s extra_s max_extra_s'
  do mode=1,2
    isft4=mode.eq.2
    do scenario=0,2
      do ns=0,3
        if(scenario.gt.0.and.ns.gt.0)cycle
        sigma=0.85+real(ns)*0.10
        ngood=0;modern_good=0;nwrong=0;modern_wrong=0;oldtime=0;extra=0;maxextra=0
        do t=1,ntrials
          message=messages(mod(t-1,size(messages))+1);i3=-1;n3=-1
          call pack77(message,i3,n3,c77,1);read(c77,'(77i1)')msg
          if(isft4)msg=mod(msg+rvec,2)
          call encode174_91(msg,cw)
          do j=1,174,2
            call random_number(u1);call random_number(u2)
            noise(j)=sqrt(-2.*log(max(1.e-10,u1)))*cos(6.283185307*u2)
            noise(j+1)=sqrt(-2.*log(max(1.e-10,u1)))*sin(6.283185307*u2)
          enddo
          llr=2.*(real(2*cw-1)+sigma*noise)/(sigma*sigma)
          if(scenario.eq.1)llr=2.*noise
          ! Impulsive soft-bit corruption is a stress test, not an RF model.
          if(scenario.eq.2)then
            do j=1,8
              llr(1+mod(23*j+7*t,174))=-8.*real(2*cw(1+mod(23*j+7*t,174))-1)
            enddo
          endif
          call cpu_time(t0)
          call bpdecode174_91(llr,ap,merge(40,30,isft4),decoded,outcw,nhard,niter)
          dmin=0.
          if(nhard.lt.0)then
            if(isft4)then
              call osd4_174_91(llr,ap,3,decoded,outcw,nhard,dmin)
            else
              call osd174_91(llr,ap,3,decoded,outcw,nhard,dmin,1)
            endif
          endif
          call cpu_time(t1);oldtime=oldtime+t1-t0
          oldok=nhard.ge.0
          if(.not.isft4)oldok=oldok.and.nhard+dmin.lt.60.
          if(oldok)then
            plain=decoded
            if(isft4)plain=mod(plain+rvec,2)
            write(c77,'(77i1)')plain;call unpack77(c77,1,outmsg,unpackok,1)
            oldok=unpackok
          endif
          modernok=oldok
          if(modernok)modernok=modern174_valid(llr,ap,outcw)
          if(oldok)then
            if(all(decoded.eq.msg).and.scenario.ne.1)then
              ngood=ngood+1
              if(modernok)modern_good=modern_good+1
            else
              nwrong=nwrong+1
              if(modernok)modern_wrong=modern_wrong+1
            endif
          endif
          if(.not.modernok)then
            call cpu_time(t1)
            call modern174_decode(llr,ap,isft4,1,decoded,outcw,nhard,dmin)
            call cpu_time(t2);extra=extra+t2-t1;maxextra=max(maxextra,t2-t1)
            if(nhard.ge.0)then
              plain=decoded
              if(isft4)plain=mod(plain+rvec,2)
              write(c77,'(77i1)')plain;call unpack77(c77,1,outmsg,unpackok,1)
              if(unpackok)then
                if(all(decoded.eq.msg).and.scenario.ne.1)then
                  modern_good=modern_good+1
                else
                  modern_wrong=modern_wrong+1
                endif
              endif
            endif
          endif
        enddo
        write(*,'(2i3,f6.2,5i8,3f11.6)')mode,scenario,sigma,ntrials,ngood,modern_good,nwrong,modern_wrong, &
          oldtime,extra,maxextra
        total_rescued=total_rescued+modern_good-ngood
        if(.not.benchmark)then
          if(modern_good.lt.ngood)error stop 'Regression in correct codeword recovery'
          if(modern_wrong.gt.nwrong)error stop 'Additional false candidates in regression corpus'
        endif
      enddo
    enddo
  enddo
  if(total_rescued.le.0)error stop 'No additional correct codewords recovered'
  print *, 'Additional correct codewords:',total_rescued
end program
