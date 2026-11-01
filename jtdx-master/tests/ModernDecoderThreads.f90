! Verify independent workers, including simultaneous first OSD initialization.
program test_modern_decoder_threads
  use modern174
  use ft8_mod1, only: first_osd,gen
  use omp_lib
  implicit none
  integer,parameter :: trials=96
  integer :: i,j,k,nhard,thread,failures,reference_error(trials),error(trials)
  integer*1 :: msg(77),cw(174),ap(174),decoded(77),outcw(174)
  integer*1 :: reference(77,trials),actual(77,trials)
  real :: llr(174,trials),u1,u2,noise,dmin
  integer :: seedsize
  integer,allocatable :: seed(:)
  call random_seed(size=seedsize);allocate(seed(seedsize));seed=918251;call random_seed(put=seed)
  ap=0
  do i=1,trials
    do j=1,77
      call random_number(u1);msg(j)=int(u1*2)
    enddo
    call encode174_91(msg,cw)
    do j=1,174
      call random_number(u1);call random_number(u2)
      noise=sqrt(-2*log(max(u1,1.e-10)))*cos(6.283185307*u2)
      llr(j,i)=2.*(real(2*cw(j)-1)+.95*noise)/(.95*.95)
    enddo
    call modern174_decode(llr(:,i),ap,.false.,1,decoded,outcw,nhard,dmin)
    reference(:,i)=decoded;reference_error(i)=nhard
  enddo
  do k=1,4
    first_osd=.true.;gen=-1
!$omp parallel do num_threads(8) private(i,thread,decoded,outcw,nhard,dmin)
    do i=1,trials
      thread=omp_get_thread_num()+1
      call modern174_decode(llr(:,i),ap,.false.,thread,decoded,outcw,nhard,dmin)
      actual(:,i)=decoded;error(i)=nhard
    enddo
!$omp end parallel do
    if(any(actual.ne.reference).or.any(error.ne.reference_error))error stop 'Concurrent decoder differs'
  enddo
  print *, 'Threaded decoder: 384 calls match serial results, 4 concurrent first initializations'
end program
