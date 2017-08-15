/*
	File: discretization.c
	Role: implementation of discretized mathematical operations with OpenMP threading

	Questions/comments to trevor.keller@nist.gov
	Bugs/requests to https://github.com/tkphd/accelerator-testing
*/

#include <math.h>
#include <omp.h>

#include "diffusion.h"

void set_threads(int n)
{
	omp_set_num_threads(n);
}

void five_point_Laplacian_stencil(double dx, double dy, double** M)
{
	M[0][1] =  1. / (dy * dy); /* up */
	M[1][0] =  1. / (dx * dx); /* left */
	M[1][1] = -2. * (dx*dx + dy*dy) / (dx*dx * dy*dy); /* middle */
	M[1][2] =  1. / (dx * dx); /* right */
	M[2][1] =  1. / (dy * dy); /* down */
}

void set_mask(double dx, double dy, int nm, double** M)
{
	five_point_Laplacian_stencil(dx, dy, M);
}

void compute_convolution(double** A, double** C, double** M, int nx, int ny, int nm)
{
	#pragma omp parallel
	{
		int i, j, mi, mj;
		double value;

		#pragma omp for collapse(2)
		for (j = nm/2; j < ny-nm/2; j++) {
			for (i = nm/2; i < nx-nm/2; i++) {
				value = 0.0;
				for (mj = -nm/2; mj < nm/2+1; mj++) {
					for (mi = -nm/2; mi < nm/2+1; mi++) {
						value += M[mj+nm/2][mi+nm/2] * A[j+mj][i+mi];
					}
				}
				C[j][i] = value;
			}
		}
	}
}

void solve_diffusion_equation(double** A, double** B, double** C,
                              int nx, int ny, int nm, double D, double dt, double* elapsed)
{
	#pragma omp parallel
	{
		int i, j;

		#pragma omp for collapse(2)
		for (j = nm/2; j < ny-nm/2; j++)
			for (i = nm/2; i < nx-nm/2; i++)
				B[j][i] = A[j][i] + dt * D * C[j][i];
	}

	*elapsed += dt;
}

void analytical_value(double x, double t, double D, double bc[2][2], double* c)
{
	*c = bc[1][0] * (1.0 - erf(x / sqrt(4.0 * D * t)));
}

void check_solution(double** A,
                    int nx, int ny, double dx, double dy, int nm,
                    double elapsed, double D, double bc[2][2], double* rss)
{
	double sum=0.;
	#pragma omp parallel reduction(+:sum)
	{
		int i, j;
		double r, cal, car, ca, cn, trss;

		#pragma omp for collapse(2)
		for (j = nm/2; j < ny-nm/2; j++) {
			for (i = nm/2; i < nx-nm/2; i++) {
				/* numerical solution */
				cn = A[j][i];

				/* shortest distance to left-wall source */
				r = (j < ny/2) ? dx * (i - nm/2) : sqrt(dx*dx * (i - nm/2) * (i - nm/2) + dy*dy * (j - ny/2) * (j - ny/2));
				analytical_value(r, elapsed, D, bc, &cal);

				/* shortest distance to right-wall source */
				r = (j >= ny/2) ? dx * (nx-nm/2-1 - i) : sqrt(dx*dx * (nx-nm/2-1 - i)*(nx-nm/2-1 - i) + dy*dy * (ny/2 - j)*(ny/2 - j));
				analytical_value(r, elapsed, D, bc, &car);

				/* superposition of analytical solutions */
				ca = cal + car;

				/* residual sum of squares (RSS) */
				trss = (ca - cn) * (ca - cn) / (double)((nx-nm/2-1) * (ny-nm/2-1));
				sum += trss;
			}
		}
	}

	*rss = sum;
}
