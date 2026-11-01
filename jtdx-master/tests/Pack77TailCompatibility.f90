! Bit-level Type1/2 fixes from WSJT-X v3.3.0-beta5 grammar.
! No radio, audio device, clock change or private callsign log is used.
program test_pack77_tail_compatibility
  use packjt77, only: pack77,unpack77
  use packjt77sd, only: pack77sd,unpack77sd
  implicit none
  character(37) :: message,decoded
  character(77) :: bits,hint_bits,legacy
  integer :: i3,n3,hint_i3,hint_n3,tail,ack,kind,i
  logical :: ok
  character(37),parameter :: mixed(4)=[character(37):: &
    'K1ABC/R W9XYZ/P FN42','K1ABC/P W9XYZ/R FN42', &
    'K1ABC/R W9XYZ/P RR73','K1ABC/P W9XYZ/R -15']

  call check('K1ABC W9XYZ RR73',32403,0,1)
  call check('K1ABC/R W9XYZ/R RR73',32403,0,1)
  call check('K1ABC/P W9XYZ/P RR73',32403,0,2)
  call check('K1ABC W9XYZ RRR',32402,0,1)
  call check('K1ABC W9XYZ 73',32404,0,1)
  ! The same text is a valid geographical grid in CQ and R+locator forms.
  call check('CQ K1ABC RR73',32373,0,1)
  call check('CQ DX K1ABC RR73',32373,0,1)
  call check('CQ K1ABC/P RR73',32373,0,2)
  call check('K1ABC W9XYZ R RR73',32373,1,1)

  ! Accept older on-air packets using locator encoding for an RR73 signoff.
  ! The transmitter fix must not remove receive compatibility.
  message='K1ABC W9XYZ RR73';i3=-1;n3=-1
  call pack77(message,i3,n3,bits,1)
  legacy=bits;write(legacy(60:74),'(b15.15)')32373
  call unpack77(legacy,0,decoded,ok,1)
  if(.not.ok.or.decoded.ne.message)error stop 'Legacy RR73 receive compatibility'
  call unpack77sd(legacy,decoded,ok)
  if(.not.ok.or.decoded.ne.message)error stop 'Legacy hint RR73 receive compatibility'

  do i=1,size(mixed)
    message=mixed(i);i3=-1;n3=-1
    call pack77(message,i3,n3,bits,1)
    if(i3.ne.0.or.n3.ne.0)error stop 'Mixed suffixes silently rewritten as standard calls'
    call unpack77(bits,0,decoded,ok,1)
    if(.not.ok.or.trim(decoded).ne.message(:13))error stop 'Mixed suffix fallback changed'
    call pack77sd(message,hint_i3,hint_n3,hint_bits)
    if(hint_i3.ne.0.or.hint_n3.ne.0.or.hint_bits.ne.bits)error stop 'Hint mixed suffix mismatch'
  enddo
  print *, 'Type1/2 RR73 tail, locator, suffix and hint compatibility PASS'
contains
  subroutine check(source,expected_tail,expected_ack,expected_type)
    character(*),intent(in) :: source
    integer,intent(in) :: expected_tail,expected_ack,expected_type
    message=source;i3=-1;n3=-1
    call pack77(message,i3,n3,bits,1)
    read(bits(59:77),'(b1,b15,b3)')ack,tail,kind
    if(tail.ne.expected_tail.or.ack.ne.expected_ack.or.kind.ne.expected_type) then
      print *, source,': actual ack/tail/type ',ack,tail,kind
      error stop 'Wrong Type1/2 wire encoding'
    endif
    call unpack77(bits,0,decoded,ok,1)
    if(.not.ok.or.decoded.ne.message)error stop 'Type1/2 round-trip failed'
    call pack77sd(message,hint_i3,hint_n3,hint_bits)
    if(hint_bits.ne.bits)error stop 'Hint templates differ from on-air encoder'
    call unpack77sd(hint_bits,decoded,ok)
    if(.not.ok.or.decoded.ne.message)error stop 'Hint template round-trip failed'
  end subroutine
end program
