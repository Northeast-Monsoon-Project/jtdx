! SPDX-License-Identifier: GPL-3.0-or-later
! A process-private adapter to the unmodified WSJT-X 3.0.2 codec modules.
module fm_upstream_bridge
  use iso_c_binding
  use q65_decode, only: q65_decoder
  implicit none
  type(q65_decoder), save :: qdecoder
  interface
    subroutine fm_upstream_decoded(message,snr,dt,freq,method,averaged) bind(C)
      import
      character(c_char) :: message(38)
      integer(c_int),value :: snr,method,averaged
      real(c_float),value :: dt,freq
    end subroutine
  end interface
contains
  subroutine copy_in(src,dst)
    character(c_char),intent(in) :: src(*)
    character(*),intent(out) :: dst
    integer i
    dst=' '
    do i=1,len(dst)
      if(src(i)==c_null_char)exit
      dst(i:i)=src(i)
    enddo
  end subroutine
  subroutine fm_upstream_init(path) bind(C)
    use prog_args
    use fftw3
    character(c_char),intent(in) :: path(*)
    integer npatience,nthreads,iret
    common/patience/npatience,nthreads
    npatience=0;nthreads=1
    iret=fftwf_init_threads()
    call fftwf_plan_with_nthreads(1)
    call copy_in(path,data_dir)
    temp_dir=data_dir
  end subroutine
  subroutine fm_upstream_close() bind(C)
    logical opened
    integer ios
    ! jplsubs keeps its direct-access ephemeris on unit12. Close it before
    ! Qt removes the private copy, including on Windows where it is locked.
    inquire(unit=12,opened=opened)
    if(opened)close(12,iostat=ios)
  end subroutine
  subroutine fm_upstream_q65(samples,period,submode,utc,rx,tol,lo,hi,effort,drift,progress, &
      average,clear_average,single_decode,eme,mycall0,hiscall0,hisgrid0,averages) bind(C)
    integer(c_short),intent(in) :: samples(*)
    integer(c_int),value :: period,submode,utc,rx,tol,lo,hi,effort,drift,progress,average,clear_average,single_decode
    real(c_float),value :: eme
    character(c_char),intent(in) :: mycall0(*),hiscall0(*),hisgrid0(*)
    integer(c_int),intent(out) :: averages
    character(12) mycall,hiscall
    character(6) hisgrid
    integer depth,nqf(20)
    logical clear
    call copy_in(mycall0,mycall);call copy_in(hiscall0,hiscall);call copy_in(hisgrid0,hisgrid)
    clear=clear_average/=0;depth=effort
    if(average/=0)depth=ior(depth,16)
    call qdecoder%decode(q65_result,samples,0,utc,period,submode,rx,tol,depth,lo,hi, &
      clear,single_decode/=0,.false.,drift,.true.,eme,mycall,hiscall,hisgrid,progress,0,.false.,averages,nqf)
  end subroutine
  subroutine q65_result(this,utc,sync,snr,dt,freq,decoded,method,nused,period)
    class(q65_decoder),intent(inout) :: this
    integer,intent(in) :: utc,snr,method,nused,period
    real,intent(in) :: sync,dt,freq
    character(37),intent(in) :: decoded
    character(c_char) message(38)
    integer i
    do i=1,37
      message(i)=decoded(i:i)
    enddo
    message(38)=c_null_char
    call fm_upstream_decoded(message,snr,dt,freq,method,nused)
  end subroutine
  subroutine fm_upstream_q65_encode(text,tones,sent) bind(C)
    character(c_char),intent(in) :: text(*)
    integer(c_int),intent(out) :: tones(85)
    character(c_char),intent(out) :: sent(38)
    character(37) msg,canonical
    integer i,i3,n3
    call copy_in(text,msg)
    call genq65(msg,0,canonical,tones,i3,n3)
    do i=1,37
      sent(i)=canonical(i:i)
    enddo
    sent(38)=c_null_char
  end subroutine
  subroutine fm_upstream_msk_encode(text,tones,sent,nsyms) bind(C)
    character(c_char),intent(in) :: text(*)
    integer(c_int),intent(out) :: tones(144),nsyms
    character(c_char),intent(out) :: sent(38)
    character(37) msg,canonical
    integer i,itype
    call copy_in(text,msg)
    tones=0
    call genmsk_128_90(msg,0,canonical,tones,itype)
    nsyms=144
    if(tones(41)==-40)nsyms=40
    if(itype<0)nsyms=0
    do i=1,37
      sent(i)=canonical(i:i)
    enddo
    sent(38)=c_null_char
  end subroutine
  subroutine fm_upstream_msk(samples,utc,tsec,rx,tol,depth,mycall0,hiscall0,shorts) bind(C)
    use prog_args, only: data_dir
    integer(c_short),intent(in) :: samples(7168)
    integer(c_int),value :: utc,rx,tol,depth,shorts
    real(c_float),value :: tsec
    character(c_char),intent(in) :: mycall0(*),hiscall0(*)
    character(12) mycall,hiscall
    character(80) line
    character(c_char) message(38)
    logical*1 short_messages,train,swl
    real(c_double) coeffs(5)
    integer i,snr,frequency,ios
    real dt
    call copy_in(mycall0,mycall);call copy_in(hiscall0,hiscall)
    coeffs=0;short_messages=shorts/=0;train=.false.;swl=.false.;line=' '
    call mskrtd(samples,utc,tsec,tol,rx,depth,mycall,hiscall,short_messages,train,coeffs,swl,data_dir,line)
    if(line(1:1)==char(0))return
    read(line(7:10),*,iostat=ios)snr
    if(ios/=0)return
    read(line(11:15),*,iostat=ios)dt
    if(ios/=0)return
    read(line(16:20),*,iostat=ios)frequency
    if(ios/=0)return
    do i=1,37
      message(i)=line(24+i:24+i)
    enddo
    message(38)=c_null_char
    call fm_upstream_decoded(message,snr,dt,real(frequency),0,1)
  end subroutine
  subroutine fm_upstream_astro(year,month,day,utc_hours,frequency,mygrid0,hisgrid0,path,values,integers) bind(C)
    integer(c_int),value :: year,month,day
    real(c_double),value :: utc_hours,frequency
    character(c_char),intent(in) :: mygrid0(*),hisgrid0(*),path(*)
    real(c_double),intent(out) :: values(20)
    integer(c_int),intent(out) :: integers(3)
    character(6) mygrid,hisgrid
    character(256) jpleph_file_name
    common/jplcom/jpleph_file_name
    call copy_in(mygrid0,mygrid);call copy_in(hisgrid0,hisgrid)
    call copy_in(path,jpleph_file_name)
    values=0;integers=0
    call astro0(year,month,day,utc_hours,frequency,mygrid,hisgrid,values(1),values(2), &
      values(3),values(4),values(5),values(6),integers(1),integers(2),integers(3), &
      values(7),values(8),values(9),values(10),values(11),values(12),values(13), &
      values(14),values(15),values(16),values(17),values(18),values(19),values(20))
  end subroutine
  subroutine fm_upstream_echo(samples,doppler,rit,auto_mode,averages,clear,frequency,width0,values,ints) bind(C)
    integer(c_short),intent(inout) :: samples(*)
    integer(c_int),value :: doppler,rit,auto_mode,averages,clear
    real(c_float),value :: frequency,width0
    real(c_float),intent(out) :: values(6)
    integer(c_int),intent(out) :: ints(2)
    integer nclearave,nsum,ndop,nfrit,nauto,navg,ndf,nqual
    real blue(4096),red(4096),fspread_self,fspread_dx,f1,width
    logical*1 disk,echocall
    character(6) txcall,rxcall
    common/echocom/nclearave,nsum,blue,red
    common/echocom2/fspread_self,fspread_dx
    nclearave=clear;ndop=doppler;nfrit=rit;nauto=auto_mode;navg=averages;ndf=0
    f1=frequency;width=width0;fspread_self=width0;fspread_dx=width0
    disk=.false.;echocall=.false.;txcall=' ';rxcall=' ';values=0;ints=0
    call avecho(samples,ndop,nfrit,nauto,ndf,navg,nqual,f1,values(1),values(2),values(3), &
      values(4),width,disk,echocall,txcall,rxcall)
    values(5)=width;ints(1)=nqual;ints(2)=nsum
  end subroutine
end module
