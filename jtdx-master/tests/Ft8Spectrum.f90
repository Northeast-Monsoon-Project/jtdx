! Validate the imported pure kernels and concurrent FFTW plan publication.
program ft8_spectrum
  use iso_fortran_env, only: real64
  use iso_c_binding, only: c_int
  use FFTW3, only: fftwf_init_threads,fftwf_plan_with_nthreads
  use ft8_symbol_spectrum, only: ft8_symbol_spectra
  use ieee_arithmetic, only: ieee_is_finite
  implicit none
  complex :: symbol(32),reversed_input(32),forward(0:7),reversed(0:7),reference(32),inverse(32)
  complex(real64) :: basis(0:7,32),expected(0:7)
  real(real64) :: angle,scale,error,worst
  integer :: i,k,trial,npatience,nthreads,init_result
  interface
    integer(c_int) function fftwf_planner_nthreads() bind(C,name='fftwf_planner_nthreads')
      import c_int
    end function
  end interface
  common/patience/npatience,nthreads
  npatience=0; nthreads=1; worst=0.
  init_result=fftwf_init_threads()
  if(init_result.eq.0) stop 7
  ! Reproduce a preceding JT9/JT10 big FFT leaving a multithreaded planner.
  call fftwf_plan_with_nthreads(4)
  if(fftwf_planner_nthreads().ne.4) stop 8
  do i=1,32
    do k=0,7
      angle=-8.*atan(1._real64)*k*real(i-1,real64)/32.
      basis(k,i)=cmplx(cos(angle),sin(angle),real64)
    enddo
  enddo
!$omp parallel do num_threads(8) private(symbol,reversed_input,forward,reversed,reference,inverse, &
!$omp& expected,scale,error,i,k) reduction(max:worst)
  do trial=1,4000
    do i=1,32
      symbol(i)=cmplx(sin(real(17*i+trial)*.713),cos(real(11*i+trial)*.417))
    enddo
    if(mod(trial,17).eq.0) symbol=0. ! zero/silent symbol
    if(mod(trial,31).eq.0) symbol=cmplx(1.,.5) ! DC
    call ft8_symbol_spectra(symbol,forward,reversed)
    scale=max(1._real64,sum(abs(cmplx(symbol,kind=real64))))
    do k=0,7
      expected(k)=sum(cmplx(symbol,kind=real64)*basis(k,:))
    enddo
    error=maxval(abs(cmplx(forward,kind=real64)-expected))/scale
    if(error.gt.1.e-6_real64) stop 1
    worst=max(worst,error)
    reference=symbol
    call four2a(reference,32,1,-1,1)
    if(maxval(abs(reference(1:8)-forward))/scale.gt.1.e-6) stop 2
    reversed_input=conjg(symbol(32:1:-1))
    call four2a(reversed_input,32,1,-1,1)
    if(maxval(abs(reversed_input(1:8)-reversed))/scale.gt.1.e-6) stop 3
    inverse=reference
    call four2a(inverse,32,1,1,1)
    if(maxval(abs(inverse/32.-symbol))/scale.gt.1.e-6) stop 4
    if(any(.not.ieee_is_finite(real(forward))).or.any(.not.ieee_is_finite(aimag(reversed)))) stop 5
  enddo
!$omp end parallel do
  if(fftwf_planner_nthreads().ne.1) stop 9
  call four2a(reference,-1,1,1,1)
  ! Destroy/recreate verifies that the shared cache is invalidated as well.
  symbol=1.; reference=symbol
  call four2a(reference,32,1,-1,1)
  if(abs(reference(1)-32.).gt.1.e-5.or.maxval(abs(reference(2:))).gt.1.e-5) stop 6
  call four2a(reference,-1,1,1,1)
  print '(a,es12.4)', 'FT8 spectrum/FFTW concurrency passed; max normalized error=',worst
end program

! Interpose only the legacy Fortran planning wrapper in this test executable.
! The production four2a calls this name; forwarding to FFTW's C API checks the
! planner's real thread count at creation, not merely the numerical FFT result.
subroutine sfftw_plan_dft_1d(plan,length,input,output,direction,flags)
  use iso_c_binding, only: c_int,c_intptr_t,c_ptr,c_float_complex
  use FFTW3, only: fftwf_plan_dft_1d
  implicit none
  integer(c_intptr_t), intent(out) :: plan
  integer(c_int), intent(in) :: length,direction,flags
  complex(c_float_complex), intent(inout) :: input(*),output(*)
  type(c_ptr) :: created
  interface
    integer(c_int) function fftwf_planner_nthreads() bind(C,name='fftwf_planner_nthreads')
      import c_int
    end function
  end interface
  if(fftwf_planner_nthreads().ne.1) error stop 'Generic FFT inherited nested worker threads'
  created=fftwf_plan_dft_1d(length,input,output,direction,flags)
  plan=transfer(created,plan)
end subroutine
