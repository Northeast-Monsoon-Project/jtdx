! Offline regression for the actual shared FT8 result/history interfaces.
program ft8_worker_state
  use omp_lib
  use ft8_mod1
  use ft8_shared_state
  implicit none
  integer :: width,workers,worker,low,high,last,i,j,published
  character(37) :: messages(10),message
  real :: freqs(10),dts(10)
  logical :: states(10),accepted
  logical(1) :: hint_decoded
  integer :: saved_tones(56,79),saved_data(56,58),saved_cq(25,58),tones(79),i3,n3
  integer*1 :: message_bits(77)
  character(37) :: saved_messages(56),sent,hint
  real :: symbols(0:7,79)
  type(lastrxmsg_struct) snapshot(1)

  do width=1,2801,50
    do workers=1,min(24,width)
      last=199
      do worker=1,workers
        call ft8_worker_band(200,199+width,worker,workers,low,high)
        if(low.ne.last+1.or.high.lt.low) stop 1
        last=high
      enddo
      if(last.ne.199+width) stop 2
    enddo
  enddo

  ndecodes=0; allmessages=''; allsnrs=0; allfreq=0.; published=0
!$omp parallel do num_threads(8) private(accepted) reduction(+:published)
  do i=1,1000
    call ft8_publish_decode('CQ K1ABC FN20',-12,1500.,0.,'K1ABC',.true.,8,.false.,.true.,.false.,accepted)
    if(accepted) published=published+1
  enddo
!$omp end parallel do
  if(published.ne.1.or.ndecodes.ne.1) stop 3
  if(.not.ft8_decode_seen('CQ K1ABC FN20',1500.)) stop 4
  if(ft8_decode_seen('CQ K1ABC FN20',1600.)) stop 5
  call ft8_publish_decode('CQ K1ABC FN20',-10,1500.,0.,'K1ABC',.false.,1,.false.,.true.,.false.,accepted)
  if(.not.accepted.or.ndecodes.ne.2) stop 6 ! preserve single-worker stronger update
  call ft8_publish_decode('CQ K1ABC FN20',-10,1800.,0.,'K1ABC',.false.,8,.false.,.true.,.false.,accepted)
  if(.not.accepted.or.ndecodes.ne.3) stop 7 ! same text on a distinct frequency

  ! More than 130 hints must leave both guard caches and the count intact.
  do j=1,2
    nmsg=0; even%lstate=.false.; odd%lstate=.false.
    oddcopy%msg='guard odd'; evencopy%msg='guard even'
!$omp parallel do num_threads(8) private(messages,freqs,dts,states)
    do i=1,40
      messages='CQ K1ABC FN20'; freqs=real(i); dts=real(i); states=.true.
      call ft8_append_history(j.eq.1,messages,freqs,dts,states)
    enddo
!$omp end parallel do
    if(nmsg.ne.130) stop 8
    if(j.eq.1.and..not.all(even%lstate)) stop 9
    if(j.eq.2.and..not.all(odd%lstate)) stop 10
    if(any(oddcopy%msg.ne.'guard odd').or.any(evencopy%msg.ne.'guard even')) stop 11
  enddo

  hiscall='W9XYZ'; mycall='K1ABC'; lqsomsgdcd=.false.
  call ft8_record_qso('000000',0.,.false.)
!$omp parallel do num_threads(8) private(message,snapshot,last)
  do i=1,10000
    write(message,'(i6.6)') i
    call ft8_record_qso(message,real(i),.true.)
    call ft8_snapshot_history(snapshot)
    read(snapshot(1)%lastmsg,*) last
    if(snapshot(1)%xdt.ne.real(last).or..not.snapshot(1)%lstate) stop 12
  enddo
!$omp end parallel do
  if(.not.ft8_qso_decoded()) stop 13
  hiscall=''; call ft8_prepare_qso_history(0,.false.)
  if(lastrxmsg(1)%lstate) stop 14

  if(ft8_message_has_call('CQ W1ABCD FN20','W1ABC')) stop 17
  if(ft8_message_has_call('CQ K1ABC/R FN20','K1ABC')) stop 18
  if(ft8_message_has_call('CQ K1ABC/P FN20','K1ABC/R')) stop 19
  if(.not.ft8_message_has_call('  CQ   <k1abc/r>  FN20','K1ABC/R',2,2)) stop 20
  if(ft8_message_has_call('<K1ABC> W1ABC -12','W1ABC',1,1)) stop 21
  if(.not.ft8_message_has_call('<K1ABC> W1ABC -12','W1ABC',2,2)) stop 22
  mycall='K1ABC'; hiscall='W1ABC'; lasthcall='W1ABCD'
  lastrxmsg(1)%lstate=.true.; lastrxmsg(1)%lastmsg='K1ABC W1ABCD -12'
  incall%msg=''; evencopy%lstate=.false.; oddcopy%lstate=.false.
  call ft8_prepare_qso_history(0,.false.)
  if(lastrxmsg(1)%lstate) stop 23
  incall(1)%msg='K1ABC/R W1ABC -12'
  incall(2)%msg='K1ABC W1ABCD -12'
  incall(3)%msg='K1ABC W1ABC/P -12'
  call ft8_prepare_qso_history(0,.false.)
  if(lastrxmsg(1)%lstate) stop 24
  incall(4)%msg='<K1ABC> <W1ABC> -10'; incall(4)%xdt=.25
  call ft8_prepare_qso_history(0,.false.)
  if(.not.lastrxmsg(1)%lstate.or.lastrxmsg(1)%xdt.ne.0.25) stop 25
  lastrxmsg(1)%lstate=.false.; incall%msg=''
  evencopy(1)%lstate=.true.; evencopy(1)%msg='CQ W1ABCD FN20'
  evencopy(2)%lstate=.true.; evencopy(2)%msg='CQ W1ABC/P FN20'
  call ft8_prepare_qso_history(0,.false.)
  if(lastrxmsg(1)%lstate) stop 26
  evencopy(3)%lstate=.true.; evencopy(3)%msg='CQ DX <W1ABC> FN20'; evencopy(3)%dt=.5
  call ft8_prepare_qso_history(0,.false.)
  if(.not.lastrxmsg(1)%lstate.or.lastrxmsg(1)%xdt.ne.0.5) stop 27

  ! The grid-hint path formerly passed a scalar to genft8sd's 77-byte output,
  ! then rewrote shared tones. Exercise that exact branch with concurrent calls.
  mycall='K1ABC'; hiscall='W9XYZ'; mybcall=mycall; hisbcall=hiscall
  lhound=.false.; nlasttx=2
  call tone8(.true._1,.true._1)
  call ft8_record_qso('K1ABC W9XYZ FN20',0.,.false.)
  saved_tones=itone56; saved_data=idtone56; saved_messages=msg
  idtone25=0; saved_cq=idtone25
!$omp parallel do num_threads(8) private(hint_decoded,tones,i3,n3,message_bits,sent,hint,symbols,j)
  do i=1,32
    symbols=1.
    call ft8s(symbols,2.,tones,hint,hint_decoded,1,.false._1)
    i3=-1; n3=-1; hint='CQ K1ABC FN20'
    if(mod(i,2).eq.0) hint='CQ W9XYZ EN37'
    call genft8sd(hint,i3,n3,sent,message_bits,tones)
    do j=1,79
      symbols(tones(j),j)=20.
    enddo
    call ft8mfcq(symbols,tones,sent,hint,hint_decoded)
  enddo
!$omp end parallel do
  if(any(itone56.ne.saved_tones).or.any(idtone56.ne.saved_data).or.any(msg.ne.saved_messages)) stop 15
  if(any(idtone25.ne.saved_cq)) stop 16
  print *, 'FT8 worker partitions, atomic publication and bounded/coherent history passed'
end program
