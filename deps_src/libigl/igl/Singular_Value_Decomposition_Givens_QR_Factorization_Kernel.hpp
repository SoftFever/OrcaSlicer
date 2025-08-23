//#####################################################################
// Copyright (c) 2010-2011, Eftychios Sifakis.
//
// Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
//   * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or
//     other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
// BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
// SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//#####################################################################

//###########################################################
// Compute the Givens half-angle, construct the Givens quaternion and the rotation sine/cosine (for the full angle)
//###########################################################

#ifdef _WIN32
  #undef max
  #undef min
#endif

ENABLE_SCALAR_IMPLEMENTATION(Ssh.f=SANPIVOT.f*SANPIVOT.f;)                                ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_mul_ps(VANPIVOT,VANPIVOT);)                             ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_mul_ps(VANPIVOT,VANPIVOT);)
ENABLE_SCALAR_IMPLEMENTATION(Ssh.ui=(Ssh.f>=Ssmall_number.f)?0xffffffff:0;)               ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_cmpge_ps(Vsh,Vsmall_number);)                           ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_cmp_ps(Vsh,Vsmall_number, _CMP_GE_OS);) //ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_cmpge_ps(Vsh,Vsmall_number);)
ENABLE_SCALAR_IMPLEMENTATION(Ssh.ui=Ssh.ui&SANPIVOT.ui;)                                  ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_and_ps(Vsh,VANPIVOT);)                                  ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_and_ps(Vsh,VANPIVOT);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp5.f=0.;)                                                 ENABLE_SSE_IMPLEMENTATION(Vtmp5=_mm_xor_ps(Vtmp5,Vtmp5);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp5=_mm256_xor_ps(Vtmp5,Vtmp5);)
ENABLE_SCALAR_IMPLEMENTATION(Sch.f=Stmp5.f-SAPIVOT.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vch=_mm_sub_ps(Vtmp5,VAPIVOT);)                                 ENABLE_AVX_IMPLEMENTATION(Vch=_mm256_sub_ps(Vtmp5,VAPIVOT);)
ENABLE_SCALAR_IMPLEMENTATION(Sch.f=std::max(Sch.f,SAPIVOT.f);)                            ENABLE_SSE_IMPLEMENTATION(Vch=_mm_max_ps(Vch,VAPIVOT);)                                   ENABLE_AVX_IMPLEMENTATION(Vch=_mm256_max_ps(Vch,VAPIVOT);)
ENABLE_SCALAR_IMPLEMENTATION(Sch.f=std::max(Sch.f,Ssmall_number.f);)                      ENABLE_SSE_IMPLEMENTATION(Vch=_mm_max_ps(Vch,Vsmall_number);)                             ENABLE_AVX_IMPLEMENTATION(Vch=_mm256_max_ps(Vch,Vsmall_number);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp5.ui=(SAPIVOT.f>=Stmp5.f)?0xffffffff:0;)                 ENABLE_SSE_IMPLEMENTATION(Vtmp5=_mm_cmpge_ps(VAPIVOT,Vtmp5);)                             ENABLE_AVX_IMPLEMENTATION(Vtmp5=_mm256_cmp_ps(VAPIVOT,Vtmp5, _CMP_GE_OS);) //ENABLE_AVX_IMPLEMENTATION(Vtmp5=_mm256_cmpge_ps(VAPIVOT,Vtmp5);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Sch.f*Sch.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vch,Vch);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vch,Vch);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ssh.f*Ssh.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vsh,Vsh);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vsh,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Stmp1.f+Stmp2.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_add_ps(Vtmp1,Vtmp2);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_add_ps(Vtmp1,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=rsqrt(Stmp2.f);)                                     ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_rsqrt_ps(Vtmp2);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_rsqrt_ps(Vtmp2);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp4.f=Stmp1.f*Sone_half.f;)                                ENABLE_SSE_IMPLEMENTATION(Vtmp4=_mm_mul_ps(Vtmp1,Vone_half);)                             ENABLE_AVX_IMPLEMENTATION(Vtmp4=_mm256_mul_ps(Vtmp1,Vone_half);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp3.f=Stmp1.f*Stmp4.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp3=_mm_mul_ps(Vtmp1,Vtmp4);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp3=_mm256_mul_ps(Vtmp1,Vtmp4);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp3.f=Stmp1.f*Stmp3.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp3=_mm_mul_ps(Vtmp1,Vtmp3);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp3=_mm256_mul_ps(Vtmp1,Vtmp3);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp3.f=Stmp2.f*Stmp3.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp3=_mm_mul_ps(Vtmp2,Vtmp3);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp3=_mm256_mul_ps(Vtmp2,Vtmp3);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Stmp1.f+Stmp4.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_add_ps(Vtmp1,Vtmp4);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_add_ps(Vtmp1,Vtmp4);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Stmp1.f-Stmp3.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_sub_ps(Vtmp1,Vtmp3);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_sub_ps(Vtmp1,Vtmp3);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Stmp1.f*Stmp2.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vtmp1,Vtmp2);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vtmp1,Vtmp2);)

ENABLE_SCALAR_IMPLEMENTATION(Sch.f=Sch.f+Stmp1.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vch=_mm_add_ps(Vch,Vtmp1);)                                     ENABLE_AVX_IMPLEMENTATION(Vch=_mm256_add_ps(Vch,Vtmp1);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.ui=~Stmp5.ui&Ssh.ui;)                                  ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_andnot_ps(Vtmp5,Vsh);)                                ENABLE_AVX_IMPLEMENTATION(Vtmp1=Vch;)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.ui=~Stmp5.ui&Sch.ui;)                                  ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_andnot_ps(Vtmp5,Vch);)                                ENABLE_AVX_IMPLEMENTATION(Vch=_mm256_blendv_ps(Vsh,Vch,Vtmp5);)
ENABLE_SCALAR_IMPLEMENTATION(Sch.ui=Stmp5.ui&Sch.ui;)                                     ENABLE_SSE_IMPLEMENTATION(Vch=_mm_and_ps(Vtmp5,Vch);)                                     ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_blendv_ps(Vtmp1,Vsh,Vtmp5);)
ENABLE_SCALAR_IMPLEMENTATION(Ssh.ui=Stmp5.ui&Ssh.ui;)                                     ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_and_ps(Vtmp5,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Sch.ui=Sch.ui|Stmp1.ui;)                                     ENABLE_SSE_IMPLEMENTATION(Vch=_mm_or_ps(Vch,Vtmp1);)
ENABLE_SCALAR_IMPLEMENTATION(Ssh.ui=Ssh.ui|Stmp2.ui;)                                     ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_or_ps(Vsh,Vtmp2);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Sch.f*Sch.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vch,Vch);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vch,Vch);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ssh.f*Ssh.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vsh,Vsh);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vsh,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Stmp1.f+Stmp2.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_add_ps(Vtmp1,Vtmp2);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_add_ps(Vtmp1,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=rsqrt(Stmp2.f);)                                     ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_rsqrt_ps(Vtmp2);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_rsqrt_ps(Vtmp2);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp4.f=Stmp1.f*Sone_half.f;)                                ENABLE_SSE_IMPLEMENTATION(Vtmp4=_mm_mul_ps(Vtmp1,Vone_half);)                             ENABLE_AVX_IMPLEMENTATION(Vtmp4=_mm256_mul_ps(Vtmp1,Vone_half);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp3.f=Stmp1.f*Stmp4.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp3=_mm_mul_ps(Vtmp1,Vtmp4);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp3=_mm256_mul_ps(Vtmp1,Vtmp4);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp3.f=Stmp1.f*Stmp3.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp3=_mm_mul_ps(Vtmp1,Vtmp3);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp3=_mm256_mul_ps(Vtmp1,Vtmp3);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp3.f=Stmp2.f*Stmp3.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp3=_mm_mul_ps(Vtmp2,Vtmp3);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp3=_mm256_mul_ps(Vtmp2,Vtmp3);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Stmp1.f+Stmp4.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_add_ps(Vtmp1,Vtmp4);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_add_ps(Vtmp1,Vtmp4);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Stmp1.f-Stmp3.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_sub_ps(Vtmp1,Vtmp3);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_sub_ps(Vtmp1,Vtmp3);)

ENABLE_SCALAR_IMPLEMENTATION(Sch.f=Sch.f*Stmp1.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vch=_mm_mul_ps(Vch,Vtmp1);)                                     ENABLE_AVX_IMPLEMENTATION(Vch=_mm256_mul_ps(Vch,Vtmp1);)
ENABLE_SCALAR_IMPLEMENTATION(Ssh.f=Ssh.f*Stmp1.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_mul_ps(Vsh,Vtmp1);)                                     ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_mul_ps(Vsh,Vtmp1);)

ENABLE_SCALAR_IMPLEMENTATION(Sc.f=Sch.f*Sch.f;)                                           ENABLE_SSE_IMPLEMENTATION(Vc=_mm_mul_ps(Vch,Vch);)                                        ENABLE_AVX_IMPLEMENTATION(Vc=_mm256_mul_ps(Vch,Vch);)ENABLE_SCALAR_IMPLEMENTATION(Ss.f=Ssh.f*Ssh.f;)                                           ENABLE_SSE_IMPLEMENTATION(Vs=_mm_mul_ps(Vsh,Vsh);)                                        ENABLE_AVX_IMPLEMENTATION(Vs=_mm256_mul_ps(Vsh,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Sc.f=Sc.f-Ss.f;)                                             ENABLE_SSE_IMPLEMENTATION(Vc=_mm_sub_ps(Vc,Vs);)                                          ENABLE_AVX_IMPLEMENTATION(Vc=_mm256_sub_ps(Vc,Vs);)
ENABLE_SCALAR_IMPLEMENTATION(Ss.f=Ssh.f*Sch.f;)                                           ENABLE_SSE_IMPLEMENTATION(Vs=_mm_mul_ps(Vsh,Vch);)                                        ENABLE_AVX_IMPLEMENTATION(Vs=_mm256_mul_ps(Vsh,Vch);)
ENABLE_SCALAR_IMPLEMENTATION(Ss.f=Ss.f+Ss.f;)                                             ENABLE_SSE_IMPLEMENTATION(Vs=_mm_add_ps(Vs,Vs);)                                          ENABLE_AVX_IMPLEMENTATION(Vs=_mm256_add_ps(Vs,Vs);)

//###########################################################
// Rotate matrix A
//###########################################################

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Ss.f*SA11.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vs,VA11);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vs,VA11);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ss.f*SA21.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vs,VA21);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vs,VA21);)
ENABLE_SCALAR_IMPLEMENTATION(SA11.f=Sc.f*SA11.f;)                                         ENABLE_SSE_IMPLEMENTATION(VA11=_mm_mul_ps(Vc,VA11);)                                      ENABLE_AVX_IMPLEMENTATION(VA11=_mm256_mul_ps(Vc,VA11);)
ENABLE_SCALAR_IMPLEMENTATION(SA21.f=Sc.f*SA21.f;)                                         ENABLE_SSE_IMPLEMENTATION(VA21=_mm_mul_ps(Vc,VA21);)                                      ENABLE_AVX_IMPLEMENTATION(VA21=_mm256_mul_ps(Vc,VA21);)
ENABLE_SCALAR_IMPLEMENTATION(SA11.f=SA11.f+Stmp2.f;)                                      ENABLE_SSE_IMPLEMENTATION(VA11=_mm_add_ps(VA11,Vtmp2);)                                   ENABLE_AVX_IMPLEMENTATION(VA11=_mm256_add_ps(VA11,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(SA21.f=SA21.f-Stmp1.f;)                                      ENABLE_SSE_IMPLEMENTATION(VA21=_mm_sub_ps(VA21,Vtmp1);)                                   ENABLE_AVX_IMPLEMENTATION(VA21=_mm256_sub_ps(VA21,Vtmp1);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Ss.f*SA12.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vs,VA12);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vs,VA12);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ss.f*SA22.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vs,VA22);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vs,VA22);)
ENABLE_SCALAR_IMPLEMENTATION(SA12.f=Sc.f*SA12.f;)                                         ENABLE_SSE_IMPLEMENTATION(VA12=_mm_mul_ps(Vc,VA12);)                                      ENABLE_AVX_IMPLEMENTATION(VA12=_mm256_mul_ps(Vc,VA12);)
ENABLE_SCALAR_IMPLEMENTATION(SA22.f=Sc.f*SA22.f;)                                         ENABLE_SSE_IMPLEMENTATION(VA22=_mm_mul_ps(Vc,VA22);)                                      ENABLE_AVX_IMPLEMENTATION(VA22=_mm256_mul_ps(Vc,VA22);)
ENABLE_SCALAR_IMPLEMENTATION(SA12.f=SA12.f+Stmp2.f;)                                      ENABLE_SSE_IMPLEMENTATION(VA12=_mm_add_ps(VA12,Vtmp2);)                                   ENABLE_AVX_IMPLEMENTATION(VA12=_mm256_add_ps(VA12,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(SA22.f=SA22.f-Stmp1.f;)                                      ENABLE_SSE_IMPLEMENTATION(VA22=_mm_sub_ps(VA22,Vtmp1);)                                   ENABLE_AVX_IMPLEMENTATION(VA22=_mm256_sub_ps(VA22,Vtmp1);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Ss.f*SA13.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vs,VA13);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vs,VA13);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ss.f*SA23.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vs,VA23);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vs,VA23);)
ENABLE_SCALAR_IMPLEMENTATION(SA13.f=Sc.f*SA13.f;)                                         ENABLE_SSE_IMPLEMENTATION(VA13=_mm_mul_ps(Vc,VA13);)                                      ENABLE_AVX_IMPLEMENTATION(VA13=_mm256_mul_ps(Vc,VA13);)
ENABLE_SCALAR_IMPLEMENTATION(SA23.f=Sc.f*SA23.f;)                                         ENABLE_SSE_IMPLEMENTATION(VA23=_mm_mul_ps(Vc,VA23);)                                      ENABLE_AVX_IMPLEMENTATION(VA23=_mm256_mul_ps(Vc,VA23);)
ENABLE_SCALAR_IMPLEMENTATION(SA13.f=SA13.f+Stmp2.f;)                                      ENABLE_SSE_IMPLEMENTATION(VA13=_mm_add_ps(VA13,Vtmp2);)                                   ENABLE_AVX_IMPLEMENTATION(VA13=_mm256_add_ps(VA13,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(SA23.f=SA23.f-Stmp1.f;)                                      ENABLE_SSE_IMPLEMENTATION(VA23=_mm_sub_ps(VA23,Vtmp1);)                                   ENABLE_AVX_IMPLEMENTATION(VA23=_mm256_sub_ps(VA23,Vtmp1);)

//###########################################################
// Update matrix U
//###########################################################

#ifdef COMPUTE_U_AS_MATRIX
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Ss.f*SU11.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vs,VU11);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vs,VU11);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ss.f*SU12.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vs,VU12);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vs,VU12);)
ENABLE_SCALAR_IMPLEMENTATION(SU11.f=Sc.f*SU11.f;)                                         ENABLE_SSE_IMPLEMENTATION(VU11=_mm_mul_ps(Vc,VU11);)                                      ENABLE_AVX_IMPLEMENTATION(VU11=_mm256_mul_ps(Vc,VU11);)
ENABLE_SCALAR_IMPLEMENTATION(SU12.f=Sc.f*SU12.f;)                                         ENABLE_SSE_IMPLEMENTATION(VU12=_mm_mul_ps(Vc,VU12);)                                      ENABLE_AVX_IMPLEMENTATION(VU12=_mm256_mul_ps(Vc,VU12);)
ENABLE_SCALAR_IMPLEMENTATION(SU11.f=SU11.f+Stmp2.f;)                                      ENABLE_SSE_IMPLEMENTATION(VU11=_mm_add_ps(VU11,Vtmp2);)                                   ENABLE_AVX_IMPLEMENTATION(VU11=_mm256_add_ps(VU11,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(SU12.f=SU12.f-Stmp1.f;)                                      ENABLE_SSE_IMPLEMENTATION(VU12=_mm_sub_ps(VU12,Vtmp1);)                                   ENABLE_AVX_IMPLEMENTATION(VU12=_mm256_sub_ps(VU12,Vtmp1);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Ss.f*SU21.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vs,VU21);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vs,VU21);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ss.f*SU22.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vs,VU22);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vs,VU22);)
ENABLE_SCALAR_IMPLEMENTATION(SU21.f=Sc.f*SU21.f;)                                         ENABLE_SSE_IMPLEMENTATION(VU21=_mm_mul_ps(Vc,VU21);)                                      ENABLE_AVX_IMPLEMENTATION(VU21=_mm256_mul_ps(Vc,VU21);)
ENABLE_SCALAR_IMPLEMENTATION(SU22.f=Sc.f*SU22.f;)                                         ENABLE_SSE_IMPLEMENTATION(VU22=_mm_mul_ps(Vc,VU22);)                                      ENABLE_AVX_IMPLEMENTATION(VU22=_mm256_mul_ps(Vc,VU22);)
ENABLE_SCALAR_IMPLEMENTATION(SU21.f=SU21.f+Stmp2.f;)                                      ENABLE_SSE_IMPLEMENTATION(VU21=_mm_add_ps(VU21,Vtmp2);)                                   ENABLE_AVX_IMPLEMENTATION(VU21=_mm256_add_ps(VU21,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(SU22.f=SU22.f-Stmp1.f;)                                      ENABLE_SSE_IMPLEMENTATION(VU22=_mm_sub_ps(VU22,Vtmp1);)                                   ENABLE_AVX_IMPLEMENTATION(VU22=_mm256_sub_ps(VU22,Vtmp1);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Ss.f*SU31.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vs,VU31);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vs,VU31);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ss.f*SU32.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vs,VU32);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vs,VU32);)
ENABLE_SCALAR_IMPLEMENTATION(SU31.f=Sc.f*SU31.f;)                                         ENABLE_SSE_IMPLEMENTATION(VU31=_mm_mul_ps(Vc,VU31);)                                      ENABLE_AVX_IMPLEMENTATION(VU31=_mm256_mul_ps(Vc,VU31);)
ENABLE_SCALAR_IMPLEMENTATION(SU32.f=Sc.f*SU32.f;)                                         ENABLE_SSE_IMPLEMENTATION(VU32=_mm_mul_ps(Vc,VU32);)                                      ENABLE_AVX_IMPLEMENTATION(VU32=_mm256_mul_ps(Vc,VU32);)
ENABLE_SCALAR_IMPLEMENTATION(SU31.f=SU31.f+Stmp2.f;)                                      ENABLE_SSE_IMPLEMENTATION(VU31=_mm_add_ps(VU31,Vtmp2);)                                   ENABLE_AVX_IMPLEMENTATION(VU31=_mm256_add_ps(VU31,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(SU32.f=SU32.f-Stmp1.f;)                                      ENABLE_SSE_IMPLEMENTATION(VU32=_mm_sub_ps(VU32,Vtmp1);)                                   ENABLE_AVX_IMPLEMENTATION(VU32=_mm256_sub_ps(VU32,Vtmp1);)
#endif
