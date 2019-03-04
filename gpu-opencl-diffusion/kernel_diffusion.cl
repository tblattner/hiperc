/**********************************************************************************
 HiPerC: High Performance Computing Strategies for Boundary Value Problems
 Written by Trevor Keller and available from https://github.com/usnistgov/hiperc
 **********************************************************************************/

/**
 \brief Enable double-precision floats
*/
#if defined(cl_khr_fp64)  // Khronos extension available?
	#pragma OPENCL EXTENSION cl_khr_fp64 : enable
#elif defined(cl_amd_fp64)  // AMD extension available?
	#pragma OPENCL EXTENSION cl_amd_fp64 : enable
#endif

#include "numerics.h"

/**
 \brief Diffusion equation kernel for execution on the GPU

 This function accesses 1D data rather than the 2D array representation of the
 scalar composition field
*/
__kernel void diffusion_kernel(__global fp_t* d_conc_old,
                               __global fp_t* d_conc_new,
                               __global fp_t* d_conc_lap,
                               const int nx,
                               const int ny,
                               const int nm,
                               const fp_t D,
                               const fp_t dt)
{
	int col, idx, row;

	/* determine indices on which to operate */
	col = get_global_id(0);
	row = get_global_id(1);
	idx = row * nx + col;

	/* explicit Euler solution to the equation of motion */
	if (row < ny && col < nx) {
		d_conc_new[idx] = d_conc_old[idx] + dt * D * d_conc_lap[idx];
	}

	/* wait for all threads to finish writing */
	barrier(CLK_GLOBAL_MEM_FENCE);
}
