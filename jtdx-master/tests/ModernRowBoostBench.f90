! Research-only matched-budget weight/row-boost screening.
! Variant 0 baseline; 1 beta .9; 2/3 four reliable rows boosted 1.5/2.0.
program modern_rowboost_bench
  use iso_fortran_env, only: int64
  use modern174_rowscreen, only: screen_decode
  use packjt77, only: pack77
  implicit none
  integer :: variant,mode,trials,i,j,k,scenario,level,i3,n3,errors,iterations,base_errors,attempts,good,wrong
  integer(int64) :: state
  integer*1 :: message(77),codeword(174),decoded(77),candidate(174),ap(174),base_message(77),base_word(174)
  real :: llr(174),noise(174),evidence(174,3),sigma,u1,u2,distance,t0,t1,elapsed
  logical :: isft4
  character(77) :: bits
  character(37) :: source
  character(32) :: arg
  character(37), parameter :: messages(8)=[character(37) :: &
    'CQ K1ABC FN20','CQ W9XYZ EN37','K1ABC W9XYZ -12','W9XYZ K1ABC R-09', &
    'K1ABC W9XYZ RR73','K1ABC W9XYZ 73','HELLO WORLD','CQ EA8/K1ABC']
  mode=8; trials=200; state=3702026_int64
  if(command_argument_count().ge.1) then
    call get_command_argument(1,arg); read(arg,*)mode
  endif
  if(command_argument_count().ge.2) then
    call get_command_argument(2,arg); read(arg,*)trials
  endif
  if(command_argument_count().ge.3) then
    call get_command_argument(3,arg); read(arg,*)state
  endif
  variant=0
  if(command_argument_count().ge.4) then
    call get_command_argument(4,arg); read(arg,*)variant
  endif
  isft4=mode.eq.4; ap=0; elapsed=0.; attempts=0; good=0; wrong=0
  do scenario=0,2
    do level=0,3
      if(scenario.eq.1.and.level.ne.0) cycle
      sigma=.85+.1*real(level)
      do i=1,trials
        source=messages(mod(i-1,size(messages))+1); i3=-1; n3=-1
        call pack77(source,i3,n3,bits,1); read(bits,'(77i1)')message
        call encode174_91(message,codeword)
        do j=1,174,2
          u1=max(uniform(),1.e-9); u2=uniform()
          noise(j)=sqrt(-2.*log(u1))*cos(6.283185307*u2)
          noise(j+1)=sqrt(-2.*log(u1))*sin(6.283185307*u2)
        enddo
        llr=2.*(real(2*codeword-1)+sigma*noise)/(sigma*sigma)
        if(scenario.eq.1) llr=2.*noise
        if(scenario.eq.2) then
          do j=1,3
            k=1+mod(23*j+7*i,174); llr(k)=-8.*real(2*codeword(k)-1)
          enddo
        endif
        call bpdecode174_91(llr,ap,30,base_message,base_word,base_errors,iterations)
        if(base_errors.ge.0.and.scenario.ne.1) cycle
        evidence(:,1)=llr*.9; evidence(:,2)=llr*1.1; evidence(:,3)=llr
        attempts=attempts+1
        call cpu_time(t0)
        if(mod(i,2).eq.0) then
          call screen_decode(variant,llr,ap,isft4,1,decoded,candidate,errors,distance,evidence)
        else
          call screen_decode(variant,llr,ap,isft4,1,decoded,candidate,errors,distance)
        endif
        call cpu_time(t1); elapsed=elapsed+t1-t0
        if(errors.ge.0) then
          if(scenario.ne.1.and.all(decoded.eq.message)) then
            good=good+1
          else
            wrong=wrong+1
          endif
        endif
        write(*,'(a,4i6,1x,f12.6,1x,77i1)') 'CASE ',scenario,level,i,errors,distance,decoded
      enddo
    enddo
  enddo
  write(*,'(a,3i8,1x,f12.6)') 'SUMMARY attempts/correct/wrong/CPU_s ',attempts,good,wrong,elapsed
contains
  real function uniform()
    state=modulo(48271_int64*state,2147483647_int64)
    uniform=real(state)/2147483647.
  end function
end program
