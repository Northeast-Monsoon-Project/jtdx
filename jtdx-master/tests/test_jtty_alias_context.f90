! SPDX-License-Identifier: GPL-3.0-or-later
! Fluxmancer regression: live, review and standalone alias history must be isolated.
program test_jtty_alias_context
  use iso_c_binding, only: c_int, c_int16_t, c_int64_t, c_float, c_double, c_char
  use iso_fortran_env, only: real64
  use jtty_receive_context
  use jtty_mdec, only: MAX_ALIAS_REFERENCES, ALIAS_FRAME_SYMBOLS, UPDATE_COMPLETE, &
       UPDATE_RECEPTION_ENDED, nalias_refs, next_alias_ref, alias_ref_f1, &
       alias_ref_tsync, alias_ref_tones, remember_alias_reference, reset_decode_search_state
  use jtty_fec, only: is13, TOTAL_K
  use jtty_mod, only: MAX_FRAMES
  implicit none

  integer, parameter :: nsps=384, rate=12000, capacity=32
  character(len=*), parameter :: wanted='CQ DL3WDG JN68'
  integer(c_int16_t) :: pcm(16*rate)
  integer :: tones(MAX_FRAMES*(size(is13)+TOTAL_K)),symbols,n,i,processed
  integer(c_int) :: live,review
  real, allocatable :: wave(:)
  complex, allocatable :: analytic(:)
  real :: saved_frequency(MAX_ALIAS_REFERENCES)
  real(real64) :: saved_time(MAX_ALIAS_REFERENCES)
  integer :: saved_tones(ALIAS_FRAME_SYMBOLS,MAX_ALIAS_REFERENCES),saved_count,saved_next
  character(len=80) :: message

  message=wanted
  call genjtty(message,tones,symbols)
  n=symbols*nsps
  allocate(wave(n),analytic(n))
  call gen_jttywave(tones,symbols,nsps,2.0,real(rate),1500.0,analytic,wave,0,n)
  pcm=0
  do i=1,n
     pcm(2400+i)=int(nint(25000.0*wave(i)),c_int16_t)
  enddo

  ! Public standalone decoder state already contains a previous, unrelated frame.
  call reset_decode_search_state()
  call remember_alias_reference(1000.0,42.0_real64,tones(1:ALIAS_FRAME_SYMBOLS))
  saved_count=nalias_refs
  saved_next=next_alias_ref
  saved_frequency=alias_ref_f1
  saved_time=alias_ref_tsync
  saved_tones=alias_ref_tones

  live=jtty_rx_create()
  review=jtty_rx_create()
  call expect(live.ne.0 .and. review.ne.0 .and. live.ne.review,'two reception contexts are available')
  call jtty_rx_begin(live,1_c_int64_t,0_c_int64_t,0_c_int64_t,nsps)
  call jtty_rx_begin(review,2_c_int64_t,0_c_int64_t,0_c_int64_t,nsps)
  call expect_ambient_history('starting contexts')

  ! Interleave real decoding in the same DSP module, as live reception/manual review do.
  do
     processed=decode_step(live)
     call expect_ambient_history('live decoding')
     if(processed.eq.0) exit
     i=decode_step(review)
     call expect_ambient_history('interleaved review')
  enddo
  call expect_message(live)
  call jtty_rx_end(live,UPDATE_RECEPTION_ENDED)
  call expect_ambient_history('ending another context')
  do while(decode_step(review).gt.0)
     call expect_ambient_history('remaining review decoding')
  enddo
  call expect_message(review)
  call jtty_rx_end(review,UPDATE_RECEPTION_ENDED)
  call expect_ambient_history('ending review')

  ! A reused handle starts with empty alias history and can decode the same recording again.
  call jtty_rx_begin(live,3_c_int64_t,0_c_int64_t,0_c_int64_t,nsps)
  do while(decode_step(live).gt.0)
     call expect_ambient_history('restarted decoding')
  enddo
  call expect_message(live)
  call jtty_rx_destroy(live)
  call jtty_rx_destroy(review)
  call expect_ambient_history('destroying contexts')
  print *, 'test_jtty_alias_context: all checks passed'

contains

  integer function decode_step(handle) result(steps)
    integer(c_int), intent(in) :: handle
    steps=jtty_rx_process(handle,pcm,size(pcm),0_c_int64_t,int(size(pcm),c_int64_t), &
         1,1000,2000,1500.0_c_float,100.0_c_float)
    call expect(steps.ge.0,'decoding retains its required PCM history')
  end function decode_step

  subroutine expect_message(handle)
    integer(c_int), intent(in) :: handle
    character(kind=c_char) :: text(80*capacity)
    integer(c_int64_t) :: ids(capacity)
    real(c_float) :: frequencies(capacity)
    real(c_double) :: starts(capacity),latest(capacity)
    integer(c_int) :: terminals(capacity),snrs(capacity)
    integer :: count,event,j
    character(len=80) :: decoded
    logical :: found

    count=jtty_rx_take_updates(handle,capacity,text,ids,frequencies,starts,latest,terminals,snrs)
    found=.false.
    do event=1,count
       do j=1,80
          decoded(j:j)=text((event-1)*80+j)
       enddo
       if(terminals(event).eq.UPDATE_COMPLETE .and. trim(decoded).eq.wanted) found=.true.
    enddo
    call expect(found,'each context publishes the complete message independently')
  end subroutine expect_message

  subroutine expect_ambient_history(action)
    character(len=*), intent(in) :: action
    call expect(nalias_refs.eq.saved_count .and. next_alias_ref.eq.saved_next .and. &
         all(alias_ref_f1.eq.saved_frequency) .and. all(alias_ref_tsync.eq.saved_time) .and. &
         all(alias_ref_tones.eq.saved_tones),trim(action)//' preserves standalone alias references')
  end subroutine expect_ambient_history

  subroutine expect(condition,message)
    logical, intent(in) :: condition
    character(len=*), intent(in) :: message
    if(.not.condition) then
       print *, message
       error stop 1
    endif
  end subroutine expect
end program test_jtty_alias_context
