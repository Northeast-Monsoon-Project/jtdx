! Fluxmancer, GPLv3. Synchronization boundary for JTDX FT8 workers.
module ft8_shared_state
  use ft8_mod1
  use decode_buffers, only: store_decode
  implicit none
  private
  public :: ft8_worker_band, ft8_prepare_qso_history, ft8_qso_decoded, ft8_mark_qso_decoded
  public :: ft8_snapshot_history, ft8_record_qso, ft8_publish_decode, ft8_decode_seen, ft8_append_history
  public :: ft8_message_has_call
  public :: ft8_prepare_legacy_rr73, ft8_prefer_legacy_rr73, ft8_apply_rr73_variant
contains
  ! Build this receive-only alternative once, before the worker team starts.
  ! The transmitter always uses the standard RR73 field (32403).
  subroutine ft8_prepare_legacy_rr73(message)
    integer*1, intent(in) :: message(77)
    integer*1 :: bits(77),codeword(174)
    integer :: i,j,k,kind,tail
    legacy_rr73_valid=.false.
    kind=4*message(75)+2*message(76)+message(77)
    tail=0
    do i=60,74
      tail=2*tail+message(i)
    enddo
    if((kind.ne.1.and.kind.ne.2).or.tail.ne.32403.or.message(59).ne.0) return
    bits=message
    do i=60,74
      bits(i)=ibits(legacy_rr73_field,74-i,1)
    enddo
    call encode174_91(bits,codeword)
    legacy_rr73_tones(1:7)=icos7
    legacy_rr73_tones(37:43)=icos7
    legacy_rr73_tones(73:79)=icos7
    k=7
    do j=1,58
      i=3*j-2; k=k+1
      if(j.eq.30) k=k+7
      legacy_rr73_tones(k)=graymap(4*codeword(i)+2*codeword(i+1)+codeword(i+2))
    enddo
    legacy_rr73_valid=.true.
  end subroutine

  ! Keep the existing RR73 candidate and AP budget: select its wire variant
  ! using original symbol energy, then apply the existing acceptance tests.
  logical function ft8_prefer_legacy_rr73(spectrum) result(legacy)
    use, intrinsic :: ieee_arithmetic, only: ieee_is_finite
    real, intent(in) :: spectrum(0:7,79)
    real :: score
    integer :: k
    legacy=.false.
    if(.not.legacy_rr73_valid.or.lhound) return
    score=0.
    do k=8,72
      if(k.ge.37.and.k.le.43) cycle
      score=score+spectrum(legacy_rr73_tones(k),k)-spectrum(itone56(55,k),k)
    enddo
    if(ieee_is_finite(score)) legacy=score.gt.0.
  end function

  subroutine ft8_apply_rr73_variant(iaptype,legacy,apmag,apmask,llr)
    integer, intent(in) :: iaptype
    logical, intent(in) :: legacy
    real, intent(in) :: apmag
    integer*1, intent(in) :: apmask(174)
    real, intent(inout) :: llr(174)
    integer :: i
    if(iaptype.ne.6.or..not.legacy.or..not.legacy_rr73_valid.or.lhound) return
    if(any(apmask(59:77).ne.1)) return
    llr(59)=-apmag
    do i=60,74
      llr(i)=apmag*(2*ibits(legacy_rr73_field,74-i,1)-1)
    enddo
  end subroutine

  ! Match complete space-delimited tokens. Angle brackets annotate a hash;
  ! /R, /P and other suffixes remain part of the station's identity.
  pure logical function ft8_message_has_call(message,call,first,last) result(found)
    character(*), intent(in) :: message,call
    integer, optional, intent(in) :: first,last
    integer :: begin,finish,position,low,high,length
    character(len(call)) :: wanted
    character(len(message)) :: token
    found=.false.; wanted=normalized_call(call)
    if(len_trim(wanted).eq.0) return
    low=1; high=huge(1)
    if(present(first)) low=first
    if(present(last)) high=last
    begin=1; position=0; length=len_trim(message)
    do while(begin.le.length)
      if(message(begin:begin).eq.' '.or.message(begin:begin).eq.achar(9)) then
        begin=begin+1
        cycle
      endif
      finish=begin
      do while(finish.le.length)
        if(message(finish:finish).eq.' '.or.message(finish:finish).eq.achar(9)) exit
        finish=finish+1
      enddo
      position=position+1
      if(position.gt.high) return
      if(position.ge.low) then
        token=normalized_call(message(begin:finish-1))
        if(token.eq.wanted) then
          found=.true.
          return
        endif
      endif
      begin=finish+1
    enddo
  end function

  pure function normalized_call(call) result(normalized)
    character(*), intent(in) :: call
    character(len(call)) :: normalized
    integer :: i,length,letter
    normalized=adjustl(call); length=len_trim(normalized)
    if(length.ge.2) then
      if(normalized(1:1).eq.'<'.and.normalized(length:length).eq.'>') &
        normalized=normalized(2:length-1)
    endif
    do i=1,len_trim(normalized)
      letter=iachar(normalized(i:i))
      if(letter.ge.iachar('a').and.letter.le.iachar('z')) normalized(i:i)=achar(letter-32)
    enddo
  end function

  pure subroutine ft8_worker_band(low,high,worker,workers,first,last)
    integer, intent(in) :: low,high,worker,workers
    integer, intent(out) :: first,last
    integer :: width
    width=max(0,high-low+1)
    first=low+(worker-1)*width/workers
    last=low+worker*width/workers-1
  end subroutine

  ! Called once, before the parallel region. Never invalidate another worker's
  ! newly decoded QSO while that worker is still publishing its history.
  subroutine ft8_prepare_qso_history(nsec,stophint)
    integer, intent(in) :: nsec
    logical, intent(in) :: stophint
    integer :: i
    if(hiscall.eq.'') then
      lastrxmsg(1)%lstate=.false.
    else if(lastrxmsg(1)%lstate .and. lasthcall.ne.hiscall .and. &
            .not.ft8_message_has_call(lastrxmsg(1)%lastmsg,hiscall)) then
      lastrxmsg(1)%lstate=.false.
    endif
    if(lastrxmsg(1)%lstate.or.stophint.or.hiscall.eq.'') return
    do i=1,size(incall)
      if(incall(i)%msg(1:1).eq.' ') exit
      if(ft8_message_has_call(incall(i)%msg,mycall,1,1).and. &
          ft8_message_has_call(incall(i)%msg,hiscall,2,2)) then
        call ft8_record_qso(incall(i)%msg,incall(i)%xdt,.false.)
        return
      endif
    enddo
    if(nsec.eq.0.or.nsec.eq.30) then
      do i=1,size(evencopy)
        if(.not.evencopy(i)%lstate) cycle
        if(ft8_message_has_call(evencopy(i)%msg,hiscall,2)) then
          call ft8_record_qso(evencopy(i)%msg,evencopy(i)%dt,.false.)
          return
        endif
      enddo
    elseif(nsec.eq.15.or.nsec.eq.45) then
      do i=1,size(oddcopy)
        if(.not.oddcopy(i)%lstate) cycle
        if(ft8_message_has_call(oddcopy(i)%msg,hiscall,2)) then
          call ft8_record_qso(oddcopy(i)%msg,oddcopy(i)%dt,.false.)
          return
        endif
      enddo
    endif
  end subroutine

  logical function ft8_qso_decoded() result(decoded)
!$omp atomic read
    decoded=lqsomsgdcd
  end function

  subroutine ft8_mark_qso_decoded()
!$omp atomic write
    lqsomsgdcd=.true.
  end subroutine

  subroutine ft8_snapshot_history(last,even_dt,odd_dt)
    type(lastrxmsg_struct), intent(out) :: last(1)
    type(callsigndteven_struct), optional, intent(out) :: even_dt(150)
    type(callsigndtodd_struct), optional, intent(out) :: odd_dt(150)
!$omp critical(ft8_results)
    last=lastrxmsg
    if(present(even_dt)) even_dt=calldteven
    if(present(odd_dt)) odd_dt=calldtodd
!$omp end critical(ft8_results)
  end subroutine

  subroutine ft8_record_qso(message,dt,decoded)
    character(*), intent(in) :: message
    real, intent(in) :: dt
    logical, intent(in) :: decoded
!$omp critical(ft8_results)
    lasthcall=hiscall
    lastrxmsg(1)%lastmsg=message
    lastrxmsg(1)%xdt=dt
    lastrxmsg(1)%lstate=.true.
!$omp end critical(ft8_results)
    if(decoded) call ft8_mark_qso_decoded()
  end subroutine

  logical function ft8_decode_seen(message,freq) result(seen)
    character(*), intent(in) :: message
    real, intent(in) :: freq
    integer :: i
    seen=.false.
!$omp critical(ft8_results)
    do i=1,ndecodes
      if(allmessages(i).eq.message.and.abs(allfreq(i)-freq).lt.45.) seen=.true.
    enddo
!$omp end critical(ft8_results)
  end function

  ! Duplicate check and publication form one transaction. Different slices can
  ! find the same candidate at a boundary, so separate locks are insufficient.
  subroutine ft8_publish_decode(message,snr,freq,dt,call2,hide_dupes,workers,hidden,even_interval,odd_interval,accepted)
    character(*), intent(in) :: message,call2
    integer, intent(in) :: snr,workers
    real, intent(in) :: freq,dt
    logical, intent(in) :: hide_dupes,hidden,even_interval,odd_interval
    logical, intent(out) :: accepted
    integer :: i
    logical :: duplicate,nearby
    duplicate=len_trim(message).eq.0
!$omp critical(ft8_results)
    do i=1,ndecodes
      if(allmessages(i).ne.message) cycle
      nearby=abs(allfreq(i)-freq).lt.45.
      if(hide_dupes) then
        duplicate=duplicate.or.snr.le.allsnrs(i).or.nearby
      else
        duplicate=duplicate.or.(nearby.and.(snr.le.allsnrs(i).or.workers.ne.1))
      endif
    enddo
    accepted=.not.duplicate
    if(accepted) then
      call store_decode(ndecodes,allmessages,allsnrs,allfreq,message,snr,freq)
      if(.not.hidden.and.len_trim(call2).gt.0) then
        if(even_interval) then
          calldteven(150:2:-1)=calldteven(149:1:-1)
          calldteven(1)%call2=call2; calldteven(1)%dt=dt
        elseif(odd_interval) then
          calldtodd(150:2:-1)=calldtodd(149:1:-1)
          calldtodd(1)%call2=call2; calldtodd(1)%dt=dt
        endif
      endif
    endif
!$omp end critical(ft8_results)
  end subroutine

  ! This history is a bounded hint cache, not the emitted decode stream. Full
  ! caches must not overwrite adjacent module state when many workers decode.
  subroutine ft8_append_history(even_interval,messages,freqs,dts,states)
    logical, intent(in) :: even_interval,states(:)
    character(*), intent(in) :: messages(:)
    real, intent(in) :: freqs(:),dts(:)
    integer :: count,first,last
!$omp critical(ft8_history_append)
    count=min(size(messages),size(freqs),size(dts),size(states),max(0,size(even)-nmsg))
    first=nmsg+1; last=nmsg+count
    if(even_interval) then
      even(first:last)%msg=messages(:count); even(first:last)%freq=freqs(:count)
      even(first:last)%dt=dts(:count); even(first:last)%lstate=states(:count)
    else
      odd(first:last)%msg=messages(:count); odd(first:last)%freq=freqs(:count)
      odd(first:last)%dt=dts(:count); odd(first:last)%lstate=states(:count)
    endif
    nmsg=last
!$omp end critical(ft8_history_append)
  end subroutine
end module ft8_shared_state
