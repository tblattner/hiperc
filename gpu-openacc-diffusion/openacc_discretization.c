/**********************************************************************************
 This file is part of Phase-field Accelerator Benchmarks, written by Trevor Keller
 and available from https://github.com/usnistgov/phasefield-accelerator-benchmarks.

 This software was developed at the National Institute of Standards and Technology
 by employees of the Federal Government in the course of their official duties.
 Pursuant to title 17 section 105 of the United States Code this software is not
 subject to copyright protection and is in the public domain. NIST assumes no
 responsibility whatsoever for the use of this software by other parties, and makes
 no guarantees, expressed or implied, about its quality, reliability, or any other
 characteristic. We would appreciate acknowledgement if the software is used.

 This software can be redistributed and/or modified freely provided that any
 derivative works bear some notice that they are derived from it, and any modified
 versions bear some notice that they have been modified.

 Questions/comments to Trevor Keller (trevor.keller@nist.gov)
 **********************************************************************************/

/**
 \file  openacc_discretization.c
 \brief Implementation of boundary condition functions with OpenACC threading
*/

#include <math.h>
#include <omp.h>
#include <openacc.h>
#include "discretization.h"
#include "numerics.h"

void compute_convolution(fp_t** conc_old, fp_t** conc_lap, fp_t** mask_lap,
                         int nx, int ny, int nm)
{
	/* OpenACC does not support nested accelerator functions */
}

void solve_diffusion_equation(fp_t** conc_old, fp_t** conc_new, fp_t** conc_lap,
                              fp_t** mask_lap, int nx, int ny, int nm,
                              fp_t bc[2][2], fp_t D, fp_t dt, fp_t* elapsed,
                              struct Stopwatch* sw)
{
	int i, j, mi, mj;
	fp_t value=0.;
	double start_time=0.;

	apply_boundary_conditions(conc_old, nx, ny, nm, bc);

	start_time = GetTimer();
	#pragma acc data copyin(conc_old[0:ny][0:nx], mask_lap[0:nm][0:nm]) create(conc_lap[0:ny][0:nx]) copyout(conc_new[0:ny][0:nx])
	{
		#pragma acc parallel
		{
			#pragma acc loop
			for (j = nm/2; j < ny-nm/2; j++) {
				#pragma acc loop
				for (i = nm/2; i < nx-nm/2; i++) {
					value = 0.;
					for (mj = -nm/2; mj < 1+nm/2; mj++) {
						for (mi = -nm/2; mi < 1+nm/2; mi++) {
							value += mask_lap[mj+nm/2][mi+nm/2] * conc_old[j+mj][i+mi];
						}
					}
					conc_lap[j][i] = value;
				}
			}
		}
		sw->conv += GetTimer() - start_time;

		start_time = GetTimer();
		#pragma acc parallel
		{
			#pragma acc loop
			for (j = nm/2; j < ny-nm/2; j++) {
				#pragma acc loop
				for (i = nm/2; i < nx-nm/2; i++) {
					conc_new[j][i] = conc_old[j][i] + dt * D * conc_lap[j][i];
				}
			}
		}
	}

	*elapsed += dt;
	sw->step += GetTimer() - start_time;
}

void check_solution(fp_t** conc_new, fp_t** conc_lap, int nx, int ny,
                    fp_t dx, fp_t dy, int nm, fp_t elapsed, fp_t D,
                    fp_t bc[2][2], fp_t* rss)
{
	/* OpenCL does not have a GPU-based erf() definition, using Maclaurin series approximation */

	fp_t sum=0.;

	#pragma acc data copyin(conc_new[0:ny][0:nx], bc[0:2][0:2]) copy(sum)
	{
		#pragma acc parallel reduction(+:sum)
		{
			int i, j;
			fp_t ca, cal, car, cn, poly_erf, r, z, z2;

			#pragma acc loop private(ca,cal,car,cn,i,j,r)
			for (j = nm/2; j < ny-nm/2; j++) {
				#pragma acc loop private(ca,cal,car,cn,i,j,r)
				for (i = nm/2; i < nx-nm/2; i++) {
					/* numerical solution */
					cn = conc_new[j][i];

					/* shortest distance to left-wall source */
					r = distance_point_to_segment(dx * (nm/2), dy * (nm/2),
					                              dx * (nm/2), dy * (ny/2),
					                              dx * i, dy * j);
					z = r / sqrt(4. * D * elapsed);
					z2 = z * z;
					poly_erf = (z < 1.5)
					         ? 2. * z * (1. + z2 * (-1./3 + z2 * (1./10 + z2 * (-1./42 + z2 / 216)))) / sqrt(M_PI)
					         : 1.;
					cal = bc[1][0] * (1. - poly_erf);

					/* shortest distance to right-wall source */
					r = distance_point_to_segment(dx * (nx-1-nm/2), dy * (ny/2),
					                              dx * (nx-1-nm/2), dy * (ny-1-nm/2),
					                              dx * i, dy * j);
					z = r / sqrt(4. * D * elapsed);
					z2 = z * z;
					poly_erf = (z < 1.5)
					         ? 2. * z * (1. + z2 * (-1./3 + z2 * (1./10 + z2 * (-1./42 + z2 / 216)))) / sqrt(M_PI)
					         : 1.;
					car = bc[1][0] * (1. - poly_erf);

					/* superposition of analytical solutions */
					ca = cal + car;

					/* residual sum of squares (RSS) */
					conc_lap[j][i] = (ca - cn) * (ca - cn) / (fp_t)((nx-1-nm/2) * (ny-1-nm/2));
				}
			}

			#pragma acc loop private(i,j)
			for (j = nm/2; j < ny-nm/2; j++) {
				#pragma acc loop private(i,j)
				for (i = nm/2; i < nx-nm/2; i++) {
					sum += conc_lap[j][i];
				}
			}
		}
	}

	*rss = sum;
}
