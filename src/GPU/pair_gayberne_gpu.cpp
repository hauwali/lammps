/* ----------------------------------------------------------------------
   LAMMPS - Large-scale Atomic/Molecular Massively Parallel Simulator
   http://lammps.sandia.gov, Sandia National Laboratories
   Steve Plimpton, sjplimp@sandia.gov

   Copyright (2003) Sandia Corporation.  Under the terms of Contract
   DE-AC04-94AL85000 with Sandia Corporation, the U.S. Government retains
   certain rights in this software.  This software is distributed under 
   the GNU General Public License.

   See the README file in the top-level LAMMPS directory.
------------------------------------------------------------------------- */

/* ----------------------------------------------------------------------
   Contributing author: Mike Brown (SNL)
------------------------------------------------------------------------- */

#include "lmptype.h"
#include "math.h"
#include "stdio.h"
#include "stdlib.h"
#include "pair_gayberne_gpu.h"
#include "math_extra.h"
#include "atom.h"
#include "atom_vec.h"
#include "comm.h"
#include "force.h"
#include "neighbor.h"
#include "neigh_list.h"
#include "integrate.h"
#include "memory.h"
#include "error.h"
#include "neigh_request.h"
#include "universe.h"
#include "domain.h"
#include "update.h"
#include "string.h"
#include "gpu_extra.h"

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

// External functions from cuda library for atom decomposition

int gb_gpu_init(const int ntypes, const double gamma, const double upsilon,
		const double mu, double **shape, double **well, double **cutsq,
		double **sigma, double **epsilon, double *host_lshape,
		int **form, double **host_lj1, double **host_lj2,
		double **host_lj3, double **host_lj4, double **offset,
		double *special_lj, const int nlocal, const int nall,
		const int max_nbors, const double cell_size,
		int &gpu_mode, FILE *screen);
void gb_gpu_clear();
int ** gb_gpu_compute_n(const int ago, const int inum, const int nall,
			double **host_x, int *host_type, double *sublo,
			double *subhi, const bool eflag, const bool vflag,
			const bool eatom, const bool vatom, int &host_start,
			int **ilist, int **jnum, const double cpu_time,
			bool &success, double **host_quat);
int * gb_gpu_compute(const int ago, const int inum, const int nall,
		     double **host_x, int *host_type, int *ilist, int *numj,
		     int **firstneigh, const bool eflag, const bool vflag,
		     const bool eatom, const bool vatom, int &host_start,
		     const double cpu_time, bool &success, double **host_quat);
double gb_gpu_bytes();

using namespace LAMMPS_NS;

enum{SPHERE_SPHERE,SPHERE_ELLIPSE,ELLIPSE_SPHERE,ELLIPSE_ELLIPSE};

/* ---------------------------------------------------------------------- */

PairGayBerneGPU::PairGayBerneGPU(LAMMPS *lmp) : PairGayBerne(lmp),
                                                gpu_mode(GPU_PAIR)
{
}

/* ----------------------------------------------------------------------
   free all arrays
------------------------------------------------------------------------- */

PairGayBerneGPU::~PairGayBerneGPU()
{
  gb_gpu_clear();
  cpu_time = 0.0;
}

/* ---------------------------------------------------------------------- */

void PairGayBerneGPU::compute(int eflag, int vflag)
{
  if (eflag || vflag) ev_setup(eflag,vflag);
  else evflag = vflag_fdotr = 0;

  int nall = atom->nlocal + atom->nghost;
  int inum, host_start;

  bool success = true;
  int *ilist, *numneigh, **firstneigh;  
  if (gpu_mode == GPU_NEIGH) {
    inum = atom->nlocal;
    firstneigh = gb_gpu_compute_n(neighbor->ago, inum, nall, atom->x,
				  atom->type, domain->sublo, domain->subhi,
				  eflag, vflag, eflag_atom, vflag_atom,
				  host_start, &ilist, &numneigh, cpu_time,
				  success, atom->quat);
  } else {
    inum = list->inum;
    ilist = list->ilist;
    numneigh = list->numneigh;
    firstneigh = list->firstneigh;
    olist = gb_gpu_compute(neighbor->ago, inum, nall, atom->x, atom->type,
			   ilist, numneigh, firstneigh, eflag, vflag,
			   eflag_atom, vflag_atom, host_start,
			   cpu_time, success, atom->quat);
  }
  if (!success)
    error->one("Out of memory on GPGPU");

  if (host_start < inum) {
    cpu_time = MPI_Wtime();
    cpu_compute(host_start, inum, eflag, vflag, ilist, numneigh, firstneigh);
    cpu_time = MPI_Wtime() - cpu_time;
  }
}

/* ----------------------------------------------------------------------
   init specific to this pair style
------------------------------------------------------------------------- */

void PairGayBerneGPU::init_style()
{
  if (force->newton_pair) 
    error->all("Cannot use newton pair with GPU Gay-Berne pair style");
  if (!atom->ellipsoid_flag)
    error->all("Pair gayberne requires atom style ellipsoid");

  // per-type shape precalculations
  // require that atom shapes are identical within each type
  // if shape = 0 for point particle, set shape = 1 as required by Gay-Berne

  for (int i = 1; i <= atom->ntypes; i++) {
    if (!atom->shape_consistency(i,shape1[i][0],shape1[i][1],shape1[i][2]))
      error->all("Pair gayberne requires atoms with same type have same shape");
    if (shape1[i][0] == 0.0)
      shape1[i][0] = shape1[i][1] = shape1[i][2] = 1.0;
    shape2[i][0] = shape1[i][0]*shape1[i][0];
    shape2[i][1] = shape1[i][1]*shape1[i][1];
    shape2[i][2] = shape1[i][2]*shape1[i][2];
    lshape[i] = (shape1[i][0]*shape1[i][1]+shape1[i][2]*shape1[i][2]) * 
      sqrt(shape1[i][0]*shape1[i][1]);
  }

  // Repeat cutsq calculation because done after call to init_style
  double maxcut = -1.0;
  double cut;
  for (int i = 1; i <= atom->ntypes; i++) {
    for (int j = i; j <= atom->ntypes; j++) {
      if (setflag[i][j] != 0 || (setflag[i][i] != 0 && setflag[j][j] != 0)) {
        cut = init_one(i,j);
        cut *= cut;
        if (cut > maxcut)
          maxcut = cut;
        cutsq[i][j] = cutsq[j][i] = cut;
      } else
        cutsq[i][j] = cutsq[j][i] = 0.0;
    }
  }

  double cell_size = sqrt(maxcut) + neighbor->skin;

  int success = gb_gpu_init(atom->ntypes+1, gamma, upsilon, mu, 
			    shape2, well, cutsq, sigma, epsilon, lshape, form,
			    lj1, lj2, lj3, lj4, offset, force->special_lj, 
			    atom->nlocal, atom->nlocal+atom->nghost, 300, 
			    cell_size, gpu_mode, screen);
  GPU_EXTRA::check_flag(success,error,world);

  if (gpu_mode != GPU_NEIGH) {
    int irequest = neighbor->request(this);
    neighbor->requests[irequest]->half = 0;
    neighbor->requests[irequest]->full = 1;
  }
}

/* ---------------------------------------------------------------------- */

double PairGayBerneGPU::memory_usage()
{
  double bytes = Pair::memory_usage();
  return bytes + gb_gpu_bytes();
}

/* ---------------------------------------------------------------------- */

void PairGayBerneGPU::cpu_compute(int start, int inum, int eflag, int vflag,
				  int *ilist, int *numneigh, int **firstneigh)
{
  int i,j,ii,jj,jnum,itype,jtype;
  double evdwl,one_eng,rsq,r2inv,r6inv,forcelj,factor_lj;
  double fforce[3],ttor[3],rtor[3],r12[3];
  double a1[3][3],b1[3][3],g1[3][3],a2[3][3],b2[3][3],g2[3][3],temp[3][3];
  int *jlist;

  double **x = atom->x;
  double **f = atom->f;
  double **quat = atom->quat;
  double **tor = atom->torque;
  int *type = atom->type;
  int nlocal = atom->nlocal;
  double *special_lj = force->special_lj;

  // loop over neighbors of my atoms

  for (ii = start; ii < inum; ii++) {
    i = ilist[ii];
    itype = type[i];

    if (form[itype][itype] == ELLIPSE_ELLIPSE) {
      MathExtra::quat_to_mat_trans(quat[i],a1);
      MathExtra::diag_times3(well[itype],a1,temp);
      MathExtra::transpose_times3(a1,temp,b1);
      MathExtra::diag_times3(shape2[itype],a1,temp);
      MathExtra::transpose_times3(a1,temp,g1);
    }

    jlist = firstneigh[i];
    jnum = numneigh[i];

    for (jj = 0; jj < jnum; jj++) {
      j = jlist[jj];
      factor_lj = special_lj[sbmask(j)];
      j &= NEIGHMASK;

      // r12 = center to center vector

      r12[0] = x[j][0]-x[i][0];
      r12[1] = x[j][1]-x[i][1];
      r12[2] = x[j][2]-x[i][2];
      rsq = MathExtra::dot3(r12,r12);
      jtype = type[j];

      // compute if less than cutoff

      if (rsq < cutsq[itype][jtype]) {

	switch (form[itype][jtype]) {
	case SPHERE_SPHERE:
	  r2inv = 1.0/rsq;
	  r6inv = r2inv*r2inv*r2inv;
	  forcelj = r6inv * (lj1[itype][jtype]*r6inv - lj2[itype][jtype]);
	  forcelj *= -r2inv;
	  if (eflag) one_eng = 
	    r6inv*(r6inv*lj3[itype][jtype]-lj4[itype][jtype]) -
	    offset[itype][jtype];
	  fforce[0] = r12[0]*forcelj;
	  fforce[1] = r12[1]*forcelj;
	  fforce[2] = r12[2]*forcelj;
	  ttor[0] = ttor[1] = ttor[2] = 0.0;
	  rtor[0] = rtor[1] = rtor[2] = 0.0;
	  break;

        case SPHERE_ELLIPSE:
	  MathExtra::quat_to_mat_trans(quat[j],a2);
	  MathExtra::diag_times3(well[jtype],a2,temp);
	  MathExtra::transpose_times3(a2,temp,b2);
	  MathExtra::diag_times3(shape2[jtype],a2,temp);
	  MathExtra::transpose_times3(a2,temp,g2);
	  one_eng = gayberne_lj(j,i,a2,b2,g2,r12,rsq,fforce,rtor);
	  ttor[0] = ttor[1] = ttor[2] = 0.0;
	  break;

        case ELLIPSE_SPHERE:
	  one_eng = gayberne_lj(i,j,a1,b1,g1,r12,rsq,fforce,ttor);
	  rtor[0] = rtor[1] = rtor[2] = 0.0;
	  break;

	default:
	  MathExtra::quat_to_mat_trans(quat[j],a2);
	  MathExtra::diag_times3(well[jtype],a2,temp);
	  MathExtra::transpose_times3(a2,temp,b2);
	  MathExtra::diag_times3(shape2[jtype],a2,temp);
	  MathExtra::transpose_times3(a2,temp,g2);
	  one_eng = gayberne_analytic(i,j,a1,a2,b1,b2,g1,g2,r12,rsq,
				      fforce,ttor,rtor);
	  break;
	}

        fforce[0] *= factor_lj;
	fforce[1] *= factor_lj;
	fforce[2] *= factor_lj;
        ttor[0] *= factor_lj;
	ttor[1] *= factor_lj;
	ttor[2] *= factor_lj;

        f[i][0] += fforce[0];
	f[i][1] += fforce[1];
	f[i][2] += fforce[2];
        tor[i][0] += ttor[0];
	tor[i][1] += ttor[1];
	tor[i][2] += ttor[2];

        if (eflag) evdwl = factor_lj*one_eng;

	if (evflag) ev_tally_xyz_full(i,evdwl,0.0,fforce[0],fforce[1],fforce[2],
				      -r12[0],-r12[1],-r12[2]);
      }
    }
  }
}
