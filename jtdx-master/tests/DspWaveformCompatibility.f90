! Compare real PCM against the unchanged WSJT-X 3.0.2 waveform generator.
! Sequential rate/BT changes specifically exercise cache invalidation.
program test_dsp_waveform_compatibility
  implicit none
  integer tones(79),i,k,nsps,nwave
  real rate,bt,dummy(1)
  real,allocatable :: actual(:),expected(:)
  complex unused(1)
  integer,parameter :: samples(4)=[1920,7680,1920,1920]
  real,parameter :: rates(4)=[12000.,48000.,12000.,12000.]
  real,parameter :: widths(4)=[2.,2.,2.,2.04]
  do i=1,79
    tones(i)=mod(i*5+3,8)
  enddo
  do k=1,4
    nsps=samples(k);rate=rates(k);bt=widths(k);nwave=79*nsps
    allocate(actual(nwave),expected(nwave))
    call gen_ft8wave(tones,79,nsps,bt,rate,1500.,unused,actual,0,nwave)
    call reference_gen_ft8wave(tones,79,nsps,bt,rate,1500.,unused,expected,0,nwave)
    if(maxval(abs(actual-expected)).gt.1.e-6)then
      print *, 'Waveform mismatch for case ',k,' error ',maxval(abs(actual-expected))
      error stop 1
    endif
    deallocate(actual,expected)
  enddo
  print *, 'WSJT-X 3.0.2 waveform compatibility PASS'
end program
