! Fluxmancer, GPLv3. Deterministic, offline GFSK decoder fixture.
! One independent PCM frame per process: no QSO/history leakage between trials.
module modern_waveform_params
  include 'jt9com.f90'
end module

program modern_waveform
  use, intrinsic :: iso_c_binding, only: c_sizeof
  use, intrinsic :: iso_fortran_env, only: int64
  use modern_waveform_params
  use prog_args, only: share_dir, shared_mode
  use ft8_mod1, only: dd8
  use ft4_mod1, only: dd4
  use FFTW3
  implicit none
  type(params_block) :: params
  integer :: mode,modern,threads,nsamples,nsym,nsps,nwave,nsignals,i,j,k,offset,iret,i3,n3
  integer :: tones(103),npatience,nthreads,cycles,pcm_unit,io_status,file_mode,file_samples
  integer(int64) :: rng,start,finish,rate
  integer*1 :: bits(77),legacy_word(174)
  integer, parameter :: test_gray(0:7)=[0,1,3,2,5,6,4,7]
  integer :: legacy_bit,legacy_symbol,legacy_index
  real :: snr,amp,freq,dtshift,cpu_start,cpu_finish,u1,u2,phase,pi
  real, allocatable :: pcm(:),wave(:)
  complex, allocatable :: cwave(:)
  character(512) :: arg
  character(32) :: scenario,message_set
  character(37) :: sent,message
  character(37), parameter :: messages(6)=[character(37) :: &
    'CQ K1ABC FN20','CQ W9XYZ EN37','CQ JA1AAA PM95', &
    'CQ DL1ABC JO31','CQ VK2ABC QF56','CQ PY2ABC GG66']
  character(37), parameter :: mixed_messages(6)=[character(37) :: &
    'K1ABC W9XYZ -15','W9XYZ K1ABC R-09','JA1AAA DL1ABC RR73', &
    'VK2ABC PY2ABC 73','HELLO WORLD','CQ PY2ABC GG66']
  character(37), parameter :: portable_messages(6)=[character(37) :: &
    'CQ W9XYZ/P EN37','K1ABC/P W9XYZ/P -15','K1ABC/P W9XYZ/P R-09', &
    'K1ABC/P W9XYZ/P RR73','K1ABC/P W9XYZ/P 73','CQ PY2ABC/R GG66']
  common/patience/npatience,nthreads

  if(command_argument_count().lt.7.or.command_argument_count().gt.10) then
    print *, 'Usage: modern_waveform MODE MODERN SEED SNR SCENARIO THREADS SHARE_DIR ', &
             '[cq|mixed|portable|rr73|rr73_legacy] [1|2|3 cycles] [write=PCM|read=PCM]'
    print *, 'MODE: 8 or 4; MODERN: 0 or 1; SCENARIO: awgn, impulse, tone, crowded, noise, noise_tone'
    stop 2
  endif
  call get_command_argument(1,arg);read(arg,*)mode
  call get_command_argument(2,arg);read(arg,*)modern
  call get_command_argument(3,arg);read(arg,*)rng
  call get_command_argument(4,arg);read(arg,*)snr
  call get_command_argument(5,scenario)
  call get_command_argument(6,arg);read(arg,*)threads
  call get_command_argument(7,share_dir)
  message_set='cq'
  if(command_argument_count().ge.8)call get_command_argument(8,message_set)
  cycles=1
  if(command_argument_count().ge.9) then
    call get_command_argument(9,arg); read(arg,*)cycles
    if(cycles.lt.1.or.cycles.gt.3)stop 2
  endif
  if(message_set.ne.'cq'.and.message_set.ne.'mixed'.and.message_set.ne.'portable'.and. &
     message_set.ne.'rr73'.and.message_set.ne.'rr73_legacy')stop 2
  if(message_set.eq.'rr73_legacy'.and.mode.ne.8)stop 2
  if((mode.ne.8.and.mode.ne.4).or.(modern.ne.0.and.modern.ne.1))stop 2
  if(threads.lt.1.or.threads.gt.24)stop 2
  select case(trim(scenario))
  case('awgn','impulse','tone','crowded','noise','noise_tone')
  case default
    stop 2
  end select
  rng=modulo(rng,2147483646_int64)+1
  pi=4.0*atan(1.0)
  if(mode.eq.8)then
    nsamples=180000;nsym=79;nsps=1920;nwave=151680
  else
    nsamples=73728;nsym=103;nsps=576;nwave=60480
  endif
  allocate(pcm(nsamples),wave(nwave),cwave(nwave))
  do i=1,nsamples,2
    u1=max(uniform(),1.e-9);u2=uniform()
    pcm(i)=sqrt(-2.*log(u1))*cos(2.*pi*u2)
    pcm(i+1)=sqrt(-2.*log(u1))*sin(2.*pi*u2)
  enddo
  nsignals=1
  if(scenario.eq.'crowded')nsignals=6
  if(scenario.eq.'noise'.or.scenario.eq.'noise_tone')nsignals=0
  freq=1500.0
  do j=1,nsignals
    if(nsignals.eq.1)then
      freq=1500.+4.*(uniform()-0.5)
      k=1+mod(int(rng),6)
    else
      freq=600.+350.*(j-1)+4.*(uniform()-0.5)
      k=j
    endif
    dtshift=0.3*(uniform()-0.5)
    message=messages(k)
    if(message_set.eq.'mixed')message=mixed_messages(k)
    if(message_set.eq.'portable')message=portable_messages(k)
    if(index(message_set,'rr73').eq.1)message='K1ABC W9XYZ RR73'
    if(mode.eq.8)then
      i3=-1;n3=-1
      call genft8(message,i3,n3,1,sent,bits,tones(1:79))
      if(message_set.eq.'rr73_legacy') then
        ! Independent reproduction of the old JTDX grid/RR73 wire collision.
        ! Do not call the receiver's compatibility helper to generate truth.
        do legacy_bit=60,74
          bits(legacy_bit)=ibits(32373,74-legacy_bit,1)
        enddo
        call encode174_91(bits,legacy_word)
        legacy_symbol=7
        do legacy_bit=1,174,3
          legacy_symbol=legacy_symbol+1
          if(legacy_symbol.eq.37) legacy_symbol=44
          legacy_index=4*legacy_word(legacy_bit)+2*legacy_word(legacy_bit+1)+legacy_word(legacy_bit+2)
          tones(legacy_symbol)=test_gray(legacy_index)
        enddo
      endif
      call gen_ft8wave(tones(1:79),nsym,nsps,2.0,12000.0,freq,cwave,wave,0,nwave)
      offset=nint((0.5+dtshift)*12000.)
    else
      call genft4(message,0,1,sent,bits,tones)
      call gen_ft4wave(tones,nsym,nsps,12000.0,freq,cwave,wave,0,nwave)
      offset=nint((0.5+dtshift)*12000.)-nsps
    endif
    ! Official simulator convention: SNR in 2500 Hz, real noise variance 1.
    amp=sqrt(2.0*2500.0/6000.0)*10.0**(snr/20.0)
    if(nsignals.gt.1.and.mod(j,2).eq.0)amp=amp*10.0**(5.0/20.0)
    pcm(offset+1:offset+nwave)=pcm(offset+1:offset+nwave)+amp*wave
    write(*,'(a,a)')'@@TRUTH ',trim(sent)
  enddo
  if(scenario.eq.'impulse')then
    ! Ten 2-ms interference bursts. This is a stress model, not an ITU channel claim.
    do j=1,10
      offset=1+int(uniform()*real(nsamples-25))
      do i=offset,offset+23
        pcm(i)=pcm(i)+12.0*(2.*uniform()-1.)
      enddo
    enddo
  endif
  if(scenario.eq.'tone'.or.scenario.eq.'noise_tone')then
    phase=2.*pi*uniform()
    do i=1,nsamples
      pcm(i)=pcm(i)+0.35*cos(2.*pi*(freq+3.125)*real(i-1)/12000.+phase)
    enddo
  endif
  ! Match the PCM magnitude used by the official simulations; keep quantization
  ! identical in paired traditional/modern processes.
  pcm=real(nint(100.*pcm))
  ! Test-only interchange: a frozen older encoder can supply the exact PCM
  ! to the current decoder, independently of subsequent wire-format fixes.
  if(command_argument_count().ge.10) then
    call get_command_argument(10,arg)
    if(index(arg,'write=').eq.1) then
      open(newunit=pcm_unit,file=trim(arg(7:)),access='stream',form='unformatted',status='replace',iostat=io_status)
      if(io_status.ne.0) stop 4
      write(pcm_unit) mode,nsamples,pcm
      close(pcm_unit)
    elseif(index(arg,'read=').eq.1) then
      open(newunit=pcm_unit,file=trim(arg(6:)),access='stream',form='unformatted',status='old',iostat=io_status)
      if(io_status.ne.0) stop 4
      read(pcm_unit,iostat=io_status) file_mode,file_samples
      if(io_status.ne.0) stop 4
      if(file_mode.ne.mode.or.file_samples.ne.nsamples) stop 4
      read(pcm_unit,iostat=io_status) pcm
      if(io_status.ne.0) stop 4
      close(pcm_unit)
    else
      stop 2
    endif
  endif
  if(mode.eq.8)then
    dd8=pcm
  else
    dd4=pcm
  endif
  params=transfer(repeat(achar(0),int(c_sizeof(params))),params)
  params%mycall=' ';params%mybcall=' ';params%hiscall=' ';params%hisbcall=' ';params%hisgrid=' '
  params%nutc=1                    ! simulated-file time; disables previous-period averaging
  params%nmode=mode;params%ntxmode=mode;params%nmodernft8=modern;params%nmodernft4=modern
  params%nfa=200;params%nfb=3000;params%nfqso=1500;params%nftx=1500;params%napwid=50
  params%ntrperiod=15
  if(mode.eq.4)params%ntrperiod=7
  params%nft8cycles=cycles;params%nft8swlcycles=cycles;params%nft4depth=3;params%ndepth=3
  params%ncandthin=100;params%nmt=threads;params%nft8rxfsens=1
  params%newdat=.true.;params%ndiskdat=.true.;params%nstophint=.true.
  params%lmycallstd=.true.;params%lhiscallstd=.true.;params%lmodechanged=.true.;params%lbandchanged=.true.
  params%lhideft8dupes=.true.
  if(mode.eq.8.and.(message_set.eq.'portable'.or.index(message_set,'rr73').eq.1)) then
    params%mycall=transfer('K1ABC/P     ',params%mycall); params%mybcall=transfer('K1ABC       ',params%mybcall)
    params%hiscall=transfer('W9XYZ/P     ',params%hiscall); params%hisbcall=transfer('W9XYZ       ',params%hisbcall)
    params%hisgrid=transfer('EN37  ',params%hisgrid); params%nQSOProgress=3; params%nlasttx=3; params%lapmyc=.true.
    params%nstophint=.false.
    if(index(message_set,'rr73').eq.1) then
      params%mycall=transfer('K1ABC       ',params%mycall); params%hiscall=transfer('W9XYZ       ',params%hiscall)
    endif
  endif
  shared_mode=.false.;npatience=0;nthreads=1
  iret=fftwf_init_threads()
  if(iret.eq.0)stop 3
  call fftwf_plan_with_nthreads(1)
  call cpu_time(cpu_start);call system_clock(start,rate)
  call multimode_decoder(params)
  call system_clock(finish);call cpu_time(cpu_finish)
  write(*,'(a,f12.6,a,f12.6)')'@@TIMING wall=',real(finish-start)/real(rate),' cpu=',cpu_finish-cpu_start
contains
  real function uniform()
    ! Park-Miller in int64: reproducible independent of runtime/compiler seed implementations.
    rng=modulo(rng*48271_int64,2147483647_int64)
    uniform=real(rng,kind=8)/2147483647.d0
  end function
end program
