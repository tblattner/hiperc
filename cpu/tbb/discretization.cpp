/*
	File: discretization.c
	Role: implementation of discretized mathematical operations with TBB threading

	Questions/comments to trevor.keller@nist.gov
	Bugs/requests to https://github.com/tkphd/accelerator-testing
*/

#include <math.h>
#include <tbb/tbb.h>
#include <tbb/task_scheduler_init.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range2d.h>

#include "diffusion.h"

void set_threads(int n)
{
	tbb::task_scheduler_init init(n);
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
	const int tbb_bs = 16;

	tbb::parallel_for(tbb::blocked_range2d<int>(nm/2, ny-nm/2, tbb_bs, nm/2, nx-nm/2, tbb_bs),
		[=](const tbb::blocked_range2d<int>& r) {
			for (int j = r.cols().begin(); j != r.cols().end(); j++) {
				for (int i = r.rows().begin(); i != r.rows().end(); i++) {
					double value = 0.0;
					for (int mj = -nm/2; mj < nm/2+1; mj++) {
						for (int mi = -nm/2; mi < nm/2+1; mi++) {
							value += M[mj+nm/2][mi+nm/2] * A[j+mj][i+mi];
						}
					}
					C[j][i] = value;
				}
			}
		}
	);
}

void solve_diffusion_equation(double** A, double** B, double** C,
                              int nx, int ny, int nm, double D, double dt, double* elapsed)
{
	const int tbb_bs = 16;

	tbb::parallel_for(tbb::blocked_range2d<int>(nm/2, ny-nm/2, tbb_bs, nm/2, nx-nm/2, tbb_bs),
		[=](const tbb::blocked_range2d<int>& r) {
			for (int j = r.cols().begin(); j != r.cols().end(); j++) {
				for (int i = r.rows().begin(); i != r.rows().end(); i++) {
					B[j][i] = A[j][i] + dt * D * C[j][i];
				}
			}
		}
	);

	*elapsed += dt;
}

void analytical_value(double x, double t, double D, double chi, double* c)
{
	*c = chi * (1.0 - erf(x / sqrt(4.0 * D * t)));
}

class ResidualSumOfSquares2D {
	double** my_A;
	int my_nx;
	int my_ny;
	double my_dx;
	double my_dy;
	int my_nm;
	double my_elapsed;
	double my_D;
	double my_c;

	public:
		double my_rss;

		/* constructors */
		ResidualSumOfSquares2D(double** A, int nx, int ny, double dx, double dy, int nm, double elapsed, double D, double c)
		                      : my_A(A), my_nx(nx), my_ny(ny), my_dx(dx), my_dy(dy), my_nm(nm), my_elapsed(elapsed), my_D(D), my_c(c), my_rss(0.0) {}
		ResidualSumOfSquares2D(ResidualSumOfSquares2D& a, tbb::split)
		                      : my_A(a.my_A), my_nx(a.my_nx), my_ny(a.my_ny), my_dx(a.my_dx), my_dy(a.my_dy), my_nm(a.my_nm), my_elapsed(a.my_elapsed), my_D(a.my_D), my_c(a.my_c), my_rss(0.0) {}

		/* modifier */
		void operator()(const tbb::blocked_range2d<int>& r)
		{
			double** A = my_A;
			int nx = my_nx;
			int ny = my_ny;
			double dx = my_dx;
			double dy = my_dy;
			int nm = my_nm;
			double elapsed = my_elapsed;
			double D = my_D;
			double c = my_c;
			double sum = my_rss;

			for (int j = r.cols().begin(); j != r.cols().end(); j++) {
				for (int i = r.rows().begin(); i != r.rows().end(); i++) {
					double x, cal, car, ca, cn;

					/* numerical solution */
					cn = A[j][i];

					/* shortest distance to left-wall source */
					x = (j < ny/2) ?
					    dx * (i - nm/2) :
					    sqrt(dx*dx * (i - nm/2) * (i - nm/2) + dy*dy * (j - ny/2) * (j - ny/2));
					analytical_value(x, elapsed, D, c, &cal);

					/* shortest distance to right-wall source */
					x = (j >= ny/2) ?
					    dx * (nx-nm/2-1 - i) :
					    sqrt(dx*dx * (nx-nm/2-1 - i)*(nx-nm/2-1 - i) + dy*dy * (ny/2 - j)*(ny/2 - j));
					analytical_value(x, elapsed, D, c, &car);

					/* superposition of analytical solutions */
					ca = cal + car;

					/* residual sum of squares (RSS) */
					sum += (ca - cn) * (ca - cn) / (double)((nx-nm/2-1) * (ny-nm/2-1));
				}
			}
			my_rss = sum;
		}

		/* reduction */
		void join(const ResidualSumOfSquares2D& a)
		{
			my_rss += a.my_rss;
		}
};

void check_solution(double** A, int nx, int ny, double dx, double dy, int nm, double elapsed, double D, double bc[2][2], double* rss)
{
	const int tbb_bs = 16;

	ResidualSumOfSquares2D R(A, nx, ny, dx, dy, nm, elapsed, D, bc[1][0]);

	tbb::parallel_reduce(tbb::blocked_range2d<int>(nm/2, ny-nm/2, tbb_bs, nm/2, nx-nm/2, tbb_bs), R);

	*rss = R.my_rss;
}
