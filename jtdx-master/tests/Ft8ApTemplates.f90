! Actual AP templates must pin the same 77-bit Type1/2 message as the encoder.
program ft8_ap_templates
  use ft8_mod1
  use packjt77, only: pack77
  use ft8_shared_state, only: ft8_prefer_legacy_rr73,ft8_apply_rr73_variant
  implicit none
  character(12), parameter :: mine(6)=[character(12):: &
    'K1ABC','K1ABC/R','K1ABC/P','K1ABC','K1ABC/P','K1ABC/R']
  character(12), parameter :: theirs(6)=[character(12):: &
    'W9XYZ','W9XYZ/R','W9XYZ/P','W9XYZ/P','W9XYZ','W9XYZ/P']
  integer, parameter :: types(8)=[2,3,4,5,6,31,35,36]
  character(37) :: source
  character(77) :: bits
  integer*1 :: message(77),word(174),decoded(77),decoded_word(174),mask(174)
  integer :: config,which,aptype,i3,n3,errors,iterations
  real :: llr(174),before(174)
  logical :: ft8_ap_mask_valid
  lhound=.false.; hisgrid4='EN37'
  ! Normal multimode initialization performs this binary-to-bipolar conversion
  ! in cwfilter before constructing any AP mask.
  mrrr=2*mrrr-1; m73=2*m73-1; mrr73=2*mrr73-1
  do config=1,size(mine)
    mycall=mine(config); hiscall=theirs(config); mybcall='K1ABC'; hisbcall='W9XYZ'
    call fillhash(1,.false.)
    call ft8apset(.true._1,.true._1,1)
    if(config.eq.6) then
      if(apsym(1).ne.99) error stop 'Mixed /R and /P must not enable a wrong AP template'
      mask=0; mask(1:58)=1; llr=0.; llr(1:58)=12.*apsym
      if(ft8_ap_mask_valid(12.,mask,llr)) error stop 'Invalid AP cache entered decoder'
      cycle
    endif
    if(any(abs(apsym).ne.1).or.any(abs(apcqsym).ne.1)) error stop 'Standard /P AP template rejected'
    do which=1,size(types)
      aptype=types(which)
      source=trim(mycall)//' '//trim(hiscall)//' -12'
      if(aptype.eq.4) source=trim(mycall)//' '//trim(hiscall)//' RRR'
      if(aptype.eq.5) source=trim(mycall)//' '//trim(hiscall)//' 73'
      if(aptype.eq.6) source=trim(mycall)//' '//trim(hiscall)//' RR73'
      if(aptype.eq.31) source='CQ '//trim(hiscall)//' EN37'
      if(aptype.eq.35) source='N0XYZ '//trim(hiscall)//' 73'
      if(aptype.eq.36) source='N0XYZ '//trim(hiscall)//' RR73'
      i3=-1; n3=-1
      call pack77(source,i3,n3,bits,1); read(bits,'(77i1)') message
      if(i3.ne.1.and.i3.ne.2) error stop 'Expected a Type1/2 fixture'
      call encode174_91(message,word)
      llr=2.*real(2*word-1); mask=0
      ! Construct the same existing payload/tail masks as ft8b, including its
      ! Type1 constants. The common finalizer must provide the true type bits.
      select case(aptype)
      case(2)
        mask(1:29)=1; llr(1:29)=12.*apsym(1:29)
      case(3)
        mask(1:58)=1; llr(1:58)=12.*apsym
      case(4:6)
        mask(1:77)=1; llr(1:58)=12.*apsym
        if(aptype.eq.4) llr(59:77)=12.*mrrr
        if(aptype.eq.5) llr(59:77)=12.*m73
        if(aptype.eq.6) llr(59:77)=12.*mrr73
      case(31)
        mask(1:77)=1; llr(1:77)=12.*apcqsym
      case(35:36)
        mask(30:77)=1; llr(30:58)=12.*apsym(30:58)
        if(aptype.eq.35) llr(59:77)=12.*m73
        if(aptype.eq.36) llr(59:77)=12.*mrr73
      end select
      mask(75:77)=1; llr(75:77)=12.*[-1,-1,1]; before=llr
      call ft8_apply_ap_type(aptype,.true._1,.true._1,12.,mask,llr)
      if(any(abs(llr).ne.12. .and. mask.eq.1)) error stop 'AP bit was not fully pinned'
      if(.not.ft8_ap_mask_valid(12.,mask,llr)) error stop 'Valid AP cache was rejected'
      if(any(real(2*word-1)*llr.lt.0.)) then
        print *, config,aptype,source
        error stop 'AP payload or ending/type bits disagree with wire encoding'
      endif
      if(any(llr(:74).ne.before(:74)).or.any(llr(78:).ne.before(78:))) error stop 'AP changed unrelated bits'
      call bpdecode174_91(llr,mask,30,decoded,decoded_word,errors,iterations)
      if(errors.lt.0.or.any(decoded.ne.message)) error stop 'AP BP decoding did not recover the exact message'
      if(i3.eq.2) then
        ! Wrong /R type bits must not be accepted as the desired /P message.
        call bpdecode174_91(before,mask,30,decoded,decoded_word,errors,iterations)
        if(errors.ge.0.and.all(decoded.eq.message)) error stop 'Type2 negative control did not exercise fixed bits'
      endif
    enddo
    call check_rr73_variants()
  enddo
  ! Portable my-call-only AP must retain Type2 even when DX Call is empty.
  mycall='K1ABC/P'; hiscall=''; call fillhash(1,.false.)
  call ft8apset(.true._1,.false._1,1)
  source='K1ABC/P W9XYZ -12'; i3=-1; n3=-1
  call pack77(source,i3,n3,bits,1); read(bits,'(77i1)') message
  call encode174_91(message,word)
  llr=2.*real(2*word-1); mask=0; mask(1:29)=1; mask(75:77)=1
  llr(1:29)=12.*apsym(1:29); llr(75:77)=12.*[-1,-1,1]
  call ft8_apply_ap_type(2,.true._1,.false._1,12.,mask,llr)
  if(.not.ft8_ap_mask_valid(12.,mask,llr).or.any(real(2*word-1)*llr.lt.0.)) &
    error stop 'Portable my-call-only AP lost its Type2 bits'
  call bpdecode174_91(llr,mask,30,decoded,decoded_word,errors,iterations)
  if(errors.lt.0.or.any(decoded.ne.message)) error stop 'Portable my-call-only BP failed'
  ! Same DX call, changed grid: rebuild CQ bits and preserve the new identity.
  mycall='K1ABC/P'; hiscall='W9XYZ/P'; hisgrid4='EN37'
  call fillhash(1,.false.); call ft8apset(.true._1,.true._1,1)
  hisgrid4='FN20'; call ft8apset(.true._1,.true._1,1)
  source='CQ W9XYZ/P FN20'; i3=-1; n3=-1
  call pack77(source,i3,n3,bits,1); read(bits,'(77i1)') message
  if(any(apcqsym.ne.2*message-1)) error stop 'CQ AP retained a previous grid'
  ! An unsupported /P + hashed pair must not inherit the old standard pair.
  hiscall='W9XYZ/AB'; call fillhash(1,.false.)
  call ft8apset(.true._1,.false._1,1)
  mask=0; mask(1:58)=1; llr=0.; llr(1:58)=12.*apsym
  if(ft8_ap_mask_valid(12.,mask,llr)) error stop 'Old station AP reused after call change'
  llr=0.; mask=1
  if(ft8_ap_mask_valid(12.,mask,llr)) error stop 'Zero AP template accepted'
  llr=1.; mask=1; before=llr
  call ft8_apply_ap_type(1,.true._1,.true._1,12.,mask,llr)
  if(any(llr.ne.before)) error stop 'Generic CQ mask must retain its own type hypothesis'
  call ft8_apply_ap_type(42,.false._1,.true._1,12.,mask,llr)
  if(any(llr.ne.before)) error stop 'Type4 mask changed'
  lhound=.true.
  call ft8_apply_ap_type(3,.true._1,.true._1,12.,mask,llr)
  if(any(llr.ne.before)) error stop 'Fox base-call mask changed'
  print *, 'Standard, /R and /P AP templates, RR73/73 and exact BP recovery passed'
contains
  subroutine check_rr73_variants()
    real :: spectrum(0:7,79),selected(174)
    integer :: k,j,variant
    integer*1 :: legacy_message(77),legacy_word(174)
    call tone8(.true._1,.true._1)
    if(.not.legacy_rr73_valid) error stop 'Missing legacy RR73 receive cache'
    source=trim(mycall)//' '//trim(hiscall)//' RR73'; i3=-1; n3=-1
    call pack77(source,i3,n3,bits,1); read(bits,'(77i1)') message
    legacy_message=message
    do k=60,74
      legacy_message(k)=ibits(legacy_rr73_field,74-k,1)
    enddo
    call encode174_91(legacy_message,legacy_word)
    do variant=0,1
      spectrum=1.
      do k=1,79
        j=itone56(55,k)
        if(variant.eq.1) j=legacy_rr73_tones(k)
        spectrum(j,k)=10.
      enddo
      if(ft8_prefer_legacy_rr73(spectrum).neqv.(variant.eq.1)) error stop 'RR73 original-energy selection failed'
      mask=1
      ! llr has 174 elements; set payload separately and use actual parity.
      call encode174_91(message,word)
      llr=2.*real(2*word-1); llr(1:77)=12.*real(2*message-1)
      if(variant.eq.1) llr(78:)=2.*real(2*legacy_word(78:)-1)
      call ft8_apply_rr73_variant(6,variant.eq.1,12.,mask,llr)
      if(variant.eq.1) then
        if(any(llr(1:77)*real(2*legacy_message-1).le.0.)) error stop 'Legacy RR73 AP pins disagree'
      else
        if(any(llr(1:77)*real(2*message-1).le.0.)) error stop 'Standard RR73 AP pins changed'
      endif
      mask(78:)=0
      call bpdecode174_91(llr,mask,30,decoded,decoded_word,errors,iterations)
      if(errors.lt.0) error stop 'RR73 variant BP failed'
      if(variant.eq.1.and.any(decoded.ne.legacy_message)) error stop 'Legacy RR73 BP payload changed'
      if(variant.eq.0.and.any(decoded.ne.message)) error stop 'Standard RR73 BP payload changed'
      selected=llr
      call ft8_apply_rr73_variant(36,.true.,12.,mask,llr)
      if(any(llr.ne.selected)) error stop 'Unknown-recipient AP gained legacy hypothesis'
    enddo
    spectrum=1.
    if(ft8_prefer_legacy_rr73(spectrum)) error stop 'Uninformative spectrum must prefer standard encoding'
  end subroutine
end program
