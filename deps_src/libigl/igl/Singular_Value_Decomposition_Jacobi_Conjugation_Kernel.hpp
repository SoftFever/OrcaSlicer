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
// Compute the Givens angle (and half-angle) 
//###########################################################

ENABLE_SCALAR_IMPLEMENTATION(Ssh.f=SS21.f*Sone_half.f;)                                   ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_mul_ps(VS21,Vone_half);)                                ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_mul_ps(VS21,Vone_half);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp5.f=SS11.f-SS22.f;)                                      ENABLE_SSE_IMPLEMENTATION(Vtmp5=_mm_sub_ps(VS11,VS22);)                                   ENABLE_AVX_IMPLEMENTATION(Vtmp5=_mm256_sub_ps(VS11,VS22);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ssh.f*Ssh.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vsh,Vsh);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vsh,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.ui=(Stmp2.f>=Stiny_number.f)?0xffffffff:0;)            ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_cmpge_ps(Vtmp2,Vtiny_number);)                        ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_cmp_ps(Vtmp2,Vtiny_number, _CMP_GE_OS);) //ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_cmpge_ps(Vtmp2,Vtiny_number);)
ENABLE_SCALAR_IMPLEMENTATION(Ssh.ui=Stmp1.ui&Ssh.ui;)                                     ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_and_ps(Vtmp1,Vsh);)                                     ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_and_ps(Vtmp1,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Sch.ui=Stmp1.ui&Stmp5.ui;)                                   ENABLE_SSE_IMPLEMENTATION(Vch=_mm_and_ps(Vtmp1,Vtmp5);)                                   ENABLE_AVX_IMPLEMENTATION(Vch=_mm256_blendv_ps(Vone,Vtmp5,Vtmp1);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.ui=~Stmp1.ui&Sone.ui;)                                 ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_andnot_ps(Vtmp1,Vone);)
ENABLE_SCALAR_IMPLEMENTATION(Sch.ui=Sch.ui|Stmp2.ui;)                                     ENABLE_SSE_IMPLEMENTATION(Vch=_mm_or_ps(Vch,Vtmp2);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Ssh.f*Ssh.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vsh,Vsh);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vsh,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Sch.f*Sch.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vch,Vch);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vch,Vch);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp3.f=Stmp1.f+Stmp2.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp3=_mm_add_ps(Vtmp1,Vtmp2);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp3=_mm256_add_ps(Vtmp1,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp4.f=rsqrt(Stmp3.f);)                                     ENABLE_SSE_IMPLEMENTATION(Vtmp4=_mm_rsqrt_ps(Vtmp3);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp4=_mm256_rsqrt_ps(Vtmp3);)

#ifdef USE_ACCURATE_RSQRT_IN_JACOBI_CONJUGATION
ENABLE_SCALAR_IMPLEMENTATION(Ss.f=Stmp4.f*Sone_half.f;)                                   ENABLE_SSE_IMPLEMENTATION(Vs=_mm_mul_ps(Vtmp4,Vone_half);)                                ENABLE_AVX_IMPLEMENTATION(Vs=_mm256_mul_ps(Vtmp4,Vone_half);)
ENABLE_SCALAR_IMPLEMENTATION(Sc.f=Stmp4.f*Ss.f;)                                          ENABLE_SSE_IMPLEMENTATION(Vc=_mm_mul_ps(Vtmp4,Vs);)                                       ENABLE_AVX_IMPLEMENTATION(Vc=_mm256_mul_ps(Vtmp4,Vs);)
ENABLE_SCALAR_IMPLEMENTATION(Sc.f=Stmp4.f*Sc.f;)                                          ENABLE_SSE_IMPLEMENTATION(Vc=_mm_mul_ps(Vtmp4,Vc);)                                       ENABLE_AVX_IMPLEMENTATION(Vc=_mm256_mul_ps(Vtmp4,Vc);)
ENABLE_SCALAR_IMPLEMENTATION(Sc.f=Stmp3.f*Sc.f;)                                          ENABLE_SSE_IMPLEMENTATION(Vc=_mm_mul_ps(Vtmp3,Vc);)                                       ENABLE_AVX_IMPLEMENTATION(Vc=_mm256_mul_ps(Vtmp3,Vc);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp4.f=Stmp4.f+Ss.f;)                                       ENABLE_SSE_IMPLEMENTATION(Vtmp4=_mm_add_ps(Vtmp4,Vs);)                                    ENABLE_AVX_IMPLEMENTATION(Vtmp4=_mm256_add_ps(Vtmp4,Vs);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp4.f=Stmp4.f-Sc.f;)                                       ENABLE_SSE_IMPLEMENTATION(Vtmp4=_mm_sub_ps(Vtmp4,Vc);)                                    ENABLE_AVX_IMPLEMENTATION(Vtmp4=_mm256_sub_ps(Vtmp4,Vc);)
#endif

ENABLE_SCALAR_IMPLEMENTATION(Ssh.f=Stmp4.f*Ssh.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_mul_ps(Vtmp4,Vsh);)                                     ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_mul_ps(Vtmp4,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Sch.f=Stmp4.f*Sch.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vch=_mm_mul_ps(Vtmp4,Vch);)                                     ENABLE_AVX_IMPLEMENTATION(Vch=_mm256_mul_ps(Vtmp4,Vch);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Sfour_gamma_squared.f*Stmp1.f;)                      ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vfour_gamma_squared,Vtmp1);)                   ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vfour_gamma_squared,Vtmp1);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.ui=(Stmp2.f<=Stmp1.f)?0xffffffff:0;)                   ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_cmple_ps(Vtmp2,Vtmp1);)                               ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_cmp_ps(Vtmp2,Vtmp1, _CMP_LE_OS);) //ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_cmple_ps(Vtmp2,Vtmp1);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp2.ui=Ssine_pi_over_eight.ui&Stmp1.ui;)                   ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_and_ps(Vsine_pi_over_eight,Vtmp1);)                   ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_blendv_ps(Vsh,Vsine_pi_over_eight,Vtmp1);)
ENABLE_SCALAR_IMPLEMENTATION(Ssh.ui=~Stmp1.ui&Ssh.ui;)                                    ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_andnot_ps(Vtmp1,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Ssh.ui=Ssh.ui|Stmp2.ui;)                                     ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_or_ps(Vsh,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.ui=Scosine_pi_over_eight.ui&Stmp1.ui;)                 ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_and_ps(Vcosine_pi_over_eight,Vtmp1);)                 ENABLE_AVX_IMPLEMENTATION(Vch=_mm256_blendv_ps(Vch,Vcosine_pi_over_eight,Vtmp1);)
ENABLE_SCALAR_IMPLEMENTATION(Sch.ui=~Stmp1.ui&Sch.ui;)                                    ENABLE_SSE_IMPLEMENTATION(Vch=_mm_andnot_ps(Vtmp1,Vch);)
ENABLE_SCALAR_IMPLEMENTATION(Sch.ui=Sch.ui|Stmp2.ui;)                                     ENABLE_SSE_IMPLEMENTATION(Vch=_mm_or_ps(Vch,Vtmp2);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Ssh.f*Ssh.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vsh,Vsh);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vsh,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Sch.f*Sch.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vch,Vch);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vch,Vch);)
ENABLE_SCALAR_IMPLEMENTATION(Sc.f=Stmp2.f-Stmp1.f;)                                       ENABLE_SSE_IMPLEMENTATION(Vc=_mm_sub_ps(Vtmp2,Vtmp1);)                                    ENABLE_AVX_IMPLEMENTATION(Vc=_mm256_sub_ps(Vtmp2,Vtmp1);)
ENABLE_SCALAR_IMPLEMENTATION(Ss.f=Sch.f*Ssh.f;)                                           ENABLE_SSE_IMPLEMENTATION(Vs=_mm_mul_ps(Vch,Vsh);)                                        ENABLE_AVX_IMPLEMENTATION(Vs=_mm256_mul_ps(Vch,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Ss.f=Ss.f+Ss.f;)                                             ENABLE_SSE_IMPLEMENTATION(Vs=_mm_add_ps(Vs,Vs);)                                          ENABLE_AVX_IMPLEMENTATION(Vs=_mm256_add_ps(Vs,Vs);)

//###########################################################
// Perform the actual Givens conjugation
//###########################################################

#ifndef USE_ACCURATE_RSQRT_IN_JACOBI_CONJUGATION
ENABLE_SCALAR_IMPLEMENTATION(Stmp3.f=Stmp1.f+Stmp2.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp3=_mm_add_ps(Vtmp1,Vtmp2);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp3=_mm256_add_ps(Vtmp1,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(SS33.f=SS33.f*Stmp3.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS33=_mm_mul_ps(VS33,Vtmp3);)                                   ENABLE_AVX_IMPLEMENTATION(VS33=_mm256_mul_ps(VS33,Vtmp3);)
ENABLE_SCALAR_IMPLEMENTATION(SS31.f=SS31.f*Stmp3.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS31=_mm_mul_ps(VS31,Vtmp3);)                                   ENABLE_AVX_IMPLEMENTATION(VS31=_mm256_mul_ps(VS31,Vtmp3);)
ENABLE_SCALAR_IMPLEMENTATION(SS32.f=SS32.f*Stmp3.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS32=_mm_mul_ps(VS32,Vtmp3);)                                   ENABLE_AVX_IMPLEMENTATION(VS32=_mm256_mul_ps(VS32,Vtmp3);)
ENABLE_SCALAR_IMPLEMENTATION(SS33.f=SS33.f*Stmp3.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS33=_mm_mul_ps(VS33,Vtmp3);)                                   ENABLE_AVX_IMPLEMENTATION(VS33=_mm256_mul_ps(VS33,Vtmp3);)
#endif

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Ss.f*SS31.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vs,VS31);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vs,VS31);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ss.f*SS32.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vs,VS32);)                                     ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vs,VS32);)
ENABLE_SCALAR_IMPLEMENTATION(SS31.f=Sc.f*SS31.f;)                                         ENABLE_SSE_IMPLEMENTATION(VS31=_mm_mul_ps(Vc,VS31);)                                      ENABLE_AVX_IMPLEMENTATION(VS31=_mm256_mul_ps(Vc,VS31);)
ENABLE_SCALAR_IMPLEMENTATION(SS32.f=Sc.f*SS32.f;)                                         ENABLE_SSE_IMPLEMENTATION(VS32=_mm_mul_ps(Vc,VS32);)                                      ENABLE_AVX_IMPLEMENTATION(VS32=_mm256_mul_ps(Vc,VS32);)
ENABLE_SCALAR_IMPLEMENTATION(SS31.f=Stmp2.f+SS31.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS31=_mm_add_ps(Vtmp2,VS31);)                                   ENABLE_AVX_IMPLEMENTATION(VS31=_mm256_add_ps(Vtmp2,VS31);)
ENABLE_SCALAR_IMPLEMENTATION(SS32.f=SS32.f-Stmp1.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS32=_mm_sub_ps(VS32,Vtmp1);)                                   ENABLE_AVX_IMPLEMENTATION(VS32=_mm256_sub_ps(VS32,Vtmp1);)

ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ss.f*Ss.f;)                                          ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vs,Vs);)                                       ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vs,Vs);)         
ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=SS22.f*Stmp2.f;)                                     ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(VS22,Vtmp2);)                                  ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(VS22,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp3.f=SS11.f*Stmp2.f;)                                     ENABLE_SSE_IMPLEMENTATION(Vtmp3=_mm_mul_ps(VS11,Vtmp2);)                                  ENABLE_AVX_IMPLEMENTATION(Vtmp3=_mm256_mul_ps(VS11,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp4.f=Sc.f*Sc.f;)                                          ENABLE_SSE_IMPLEMENTATION(Vtmp4=_mm_mul_ps(Vc,Vc);)                                       ENABLE_AVX_IMPLEMENTATION(Vtmp4=_mm256_mul_ps(Vc,Vc);)
ENABLE_SCALAR_IMPLEMENTATION(SS11.f=SS11.f*Stmp4.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS11=_mm_mul_ps(VS11,Vtmp4);)                                   ENABLE_AVX_IMPLEMENTATION(VS11=_mm256_mul_ps(VS11,Vtmp4);)
ENABLE_SCALAR_IMPLEMENTATION(SS22.f=SS22.f*Stmp4.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS22=_mm_mul_ps(VS22,Vtmp4);)                                   ENABLE_AVX_IMPLEMENTATION(VS22=_mm256_mul_ps(VS22,Vtmp4);)
ENABLE_SCALAR_IMPLEMENTATION(SS11.f=SS11.f+Stmp1.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS11=_mm_add_ps(VS11,Vtmp1);)                                   ENABLE_AVX_IMPLEMENTATION(VS11=_mm256_add_ps(VS11,Vtmp1);)
ENABLE_SCALAR_IMPLEMENTATION(SS22.f=SS22.f+Stmp3.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS22=_mm_add_ps(VS22,Vtmp3);)                                   ENABLE_AVX_IMPLEMENTATION(VS22=_mm256_add_ps(VS22,Vtmp3);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp4.f=Stmp4.f-Stmp2.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp4=_mm_sub_ps(Vtmp4,Vtmp2);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp4=_mm256_sub_ps(Vtmp4,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=SS21.f+SS21.f;)                                      ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_add_ps(VS21,VS21);)                                   ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_add_ps(VS21,VS21);)
ENABLE_SCALAR_IMPLEMENTATION(SS21.f=SS21.f*Stmp4.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS21=_mm_mul_ps(VS21,Vtmp4);)                                   ENABLE_AVX_IMPLEMENTATION(VS21=_mm256_mul_ps(VS21,Vtmp4);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp4.f=Sc.f*Ss.f;)                                          ENABLE_SSE_IMPLEMENTATION(Vtmp4=_mm_mul_ps(Vc,Vs);)                                       ENABLE_AVX_IMPLEMENTATION(Vtmp4=_mm256_mul_ps(Vc,Vs);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Stmp2.f*Stmp4.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vtmp2,Vtmp4);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vtmp2,Vtmp4);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp5.f=Stmp5.f*Stmp4.f;)                                    ENABLE_SSE_IMPLEMENTATION(Vtmp5=_mm_mul_ps(Vtmp5,Vtmp4);)                                 ENABLE_AVX_IMPLEMENTATION(Vtmp5=_mm256_mul_ps(Vtmp5,Vtmp4);)
ENABLE_SCALAR_IMPLEMENTATION(SS11.f=SS11.f+Stmp2.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS11=_mm_add_ps(VS11,Vtmp2);)                                   ENABLE_AVX_IMPLEMENTATION(VS11=_mm256_add_ps(VS11,Vtmp2);)
ENABLE_SCALAR_IMPLEMENTATION(SS21.f=SS21.f-Stmp5.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS21=_mm_sub_ps(VS21,Vtmp5);)                                   ENABLE_AVX_IMPLEMENTATION(VS21=_mm256_sub_ps(VS21,Vtmp5);)
ENABLE_SCALAR_IMPLEMENTATION(SS22.f=SS22.f-Stmp2.f;)                                      ENABLE_SSE_IMPLEMENTATION(VS22=_mm_sub_ps(VS22,Vtmp2);)                                   ENABLE_AVX_IMPLEMENTATION(VS22=_mm256_sub_ps(VS22,Vtmp2);)

//###########################################################
// Compute the cumulative rotation, in quaternion form
//###########################################################

ENABLE_SCALAR_IMPLEMENTATION(Stmp1.f=Ssh.f*Sqvvx.f;)                                      ENABLE_SSE_IMPLEMENTATION(Vtmp1=_mm_mul_ps(Vsh,Vqvvx);)                                   ENABLE_AVX_IMPLEMENTATION(Vtmp1=_mm256_mul_ps(Vsh,Vqvvx);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp2.f=Ssh.f*Sqvvy.f;)                                      ENABLE_SSE_IMPLEMENTATION(Vtmp2=_mm_mul_ps(Vsh,Vqvvy);)                                   ENABLE_AVX_IMPLEMENTATION(Vtmp2=_mm256_mul_ps(Vsh,Vqvvy);)
ENABLE_SCALAR_IMPLEMENTATION(Stmp3.f=Ssh.f*Sqvvz.f;)                                      ENABLE_SSE_IMPLEMENTATION(Vtmp3=_mm_mul_ps(Vsh,Vqvvz);)                                   ENABLE_AVX_IMPLEMENTATION(Vtmp3=_mm256_mul_ps(Vsh,Vqvvz);)
ENABLE_SCALAR_IMPLEMENTATION(Ssh.f=Ssh.f*Sqvs.f;)                                         ENABLE_SSE_IMPLEMENTATION(Vsh=_mm_mul_ps(Vsh,Vqvs);)                                      ENABLE_AVX_IMPLEMENTATION(Vsh=_mm256_mul_ps(Vsh,Vqvs);)

ENABLE_SCALAR_IMPLEMENTATION(Sqvs.f=Sch.f*Sqvs.f;)                                        ENABLE_SSE_IMPLEMENTATION(Vqvs=_mm_mul_ps(Vch,Vqvs);)                                     ENABLE_AVX_IMPLEMENTATION(Vqvs=_mm256_mul_ps(Vch,Vqvs);)
ENABLE_SCALAR_IMPLEMENTATION(Sqvvx.f=Sch.f*Sqvvx.f;)                                      ENABLE_SSE_IMPLEMENTATION(Vqvvx=_mm_mul_ps(Vch,Vqvvx);)                                   ENABLE_AVX_IMPLEMENTATION(Vqvvx=_mm256_mul_ps(Vch,Vqvvx);)
ENABLE_SCALAR_IMPLEMENTATION(Sqvvy.f=Sch.f*Sqvvy.f;)                                      ENABLE_SSE_IMPLEMENTATION(Vqvvy=_mm_mul_ps(Vch,Vqvvy);)                                   ENABLE_AVX_IMPLEMENTATION(Vqvvy=_mm256_mul_ps(Vch,Vqvvy);)
ENABLE_SCALAR_IMPLEMENTATION(Sqvvz.f=Sch.f*Sqvvz.f;)                                      ENABLE_SSE_IMPLEMENTATION(Vqvvz=_mm_mul_ps(Vch,Vqvvz);)                                   ENABLE_AVX_IMPLEMENTATION(Vqvvz=_mm256_mul_ps(Vch,Vqvvz);)

ENABLE_SCALAR_IMPLEMENTATION(SQVVZ.f=SQVVZ.f+Ssh.f;)                                      ENABLE_SSE_IMPLEMENTATION(VQVVZ=_mm_add_ps(VQVVZ,Vsh);)                                   ENABLE_AVX_IMPLEMENTATION(VQVVZ=_mm256_add_ps(VQVVZ,Vsh);)
ENABLE_SCALAR_IMPLEMENTATION(Sqvs.f=Sqvs.f-STMP3.f;)                                      ENABLE_SSE_IMPLEMENTATION(Vqvs=_mm_sub_ps(Vqvs,VTMP3);)                                   ENABLE_AVX_IMPLEMENTATION(Vqvs=_mm256_sub_ps(Vqvs,VTMP3);)
ENABLE_SCALAR_IMPLEMENTATION(SQVVX.f=SQVVX.f+STMP2.f;)                                    ENABLE_SSE_IMPLEMENTATION(VQVVX=_mm_add_ps(VQVVX,VTMP2);)                                 ENABLE_AVX_IMPLEMENTATION(VQVVX=_mm256_add_ps(VQVVX,VTMP2);)
ENABLE_SCALAR_IMPLEMENTATION(SQVVY.f=SQVVY.f-STMP1.f;)                                    ENABLE_SSE_IMPLEMENTATION(VQVVY=_mm_sub_ps(VQVVY,VTMP1);)                                 ENABLE_AVX_IMPLEMENTATION(VQVVY=_mm256_sub_ps(VQVVY,VTMP1);)
