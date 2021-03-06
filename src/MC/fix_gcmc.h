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

#ifdef FIX_CLASS

FixStyle(gcmc,FixGCMC)

#else

#ifndef LMP_FIX_GCMC_H
#define LMP_FIX_GCMC_H

#include "stdio.h"
#include "fix.h"

namespace LAMMPS_NS {

class FixGCMC : public Fix {
 public:
  FixGCMC(class LAMMPS *, int, char **);
  ~FixGCMC();
  int setmask();
  void init();
  void pre_exchange();
  void attempt_atomic_translation();
  void attempt_atomic_deletion();
  void attempt_atomic_insertion();
  void attempt_molecule_translation();
  void attempt_molecule_rotation();
  void attempt_molecule_deletion();
  void attempt_molecule_insertion();
  double energy(int, int, tagint, double *);
  void attempt_atomic_translation_full();
  void attempt_atomic_deletion_full();
  void attempt_atomic_insertion_full();
  void attempt_molecule_translation_full();
  void attempt_molecule_rotation_full();
  void attempt_molecule_deletion_full();
  void attempt_molecule_insertion_full();
  double energy_full();
  int pick_random_gas_atom();
  tagint pick_random_gas_molecule();
  void toggle_intramolecular(int);
  double molecule_energy(tagint);
  void update_gas_atoms_list();
  double compute_vector(int);
  double memory_usage();
  void write_restart(FILE *);
  void restart(char *);

 private:
  int molecule_group,molecule_group_bit;
  int molecule_group_inversebit;
  int exclusion_group,exclusion_group_bit;
  int ngcmc_type,nevery,seed;
  int ncycles,nexchanges,nmcmoves;
  int ngas;                 // # of gas atoms on all procs
  int ngas_local;           // # of gas atoms on this proc
  int ngas_before;          // # of gas atoms on procs < this proc
  int mode;                 // ATOM or MOLECULE
  int regionflag;           // 0 = anywhere in box, 1 = specific region
  int iregion;              // gcmc region
  char *idregion;           // gcmc region id
  bool pressure_flag;       // true if user specified reservoir pressure
  bool charge_flag;         // true if user specified atomic charge
  bool full_flag;           // true if doing full system energy calculations
 
  int natoms_per_molecule;  // number of atoms in each gas molecule

  int groupbitall;          // group bitmask for inserted atoms
  int ngroups;              // number of group-ids for inserted atoms
  char** groupstrings;      // list of group-ids for inserted atoms
  int ngrouptypes;          // number of type-based group-ids for inserted atoms
  char** grouptypestrings;  // list of type-based group-ids for inserted atoms
  int* grouptypebits;       // list of type-based group bitmasks 
  int* grouptypes;          // list of type-based group types 
  double ntranslation_attempts;
  double ntranslation_successes;
  double nrotation_attempts;
  double nrotation_successes;
  double ndeletion_attempts;
  double ndeletion_successes;
  double ninsertion_attempts;
  double ninsertion_successes;

  int gcmc_nmax;
  int max_region_attempts;
  double gas_mass;
  double reservoir_temperature;
  double chemical_potential;
  double displace;
  double max_rotation_angle;
  double beta,zz,sigma,volume;
  double pressure,fugacity_coeff,charge;
  double xlo,xhi,ylo,yhi,zlo,zhi;
  double region_xlo,region_xhi,region_ylo,region_yhi,region_zlo,region_zhi;
  double region_volume;
  double energy_stored;
  double *sublo,*subhi;
  int *local_gas_list;
  double **cutsq;
  double **atom_coord;
  imageint imagetmp;

  double energy_intra;

  class Pair *pair;

  class RanPark *random_equal;
  class RanPark *random_unequal;
  
  class Atom *model_atom;

  class Molecule **onemols;
  int imol,nmol;
  double **coords;
  imageint *imageflags;
  class Fix *fixshake;
  int shakeflag;
  char *idshake;
  
  class Compute *c_pe;

  void options(int, char **);
};

}

#endif
#endif

/* ERROR/WARNING messages:

E: Illegal ... command

Self-explanatory.  Check the input script syntax and compare to the
documentation for the command.  You can use -echo screen as a
command-line option when running LAMMPS to see the offending line.

E: Fix gcmc does not (yet) work with atom_style template

Self-explanatory.

E: Fix gcmc region does not support a bounding box

Not all regions represent bounded volumes.  You cannot use
such a region with the fix gcmc command.

E: Fix gcmc region cannot be dynamic

Only static regions can be used with fix gcmc.

E: Fix gcmc region extends outside simulation box

Self-explanatory.

E: Fix gcmc molecule must have coordinates

The defined molecule does not specify coordinates.

E: Fix gcmc molecule must have atom types

The defined molecule does not specify atom types.

E: Invalid atom type in fix gcmc mol command

Self-explanatory.

E: Fix gcmc molecule template ID must be same as atom_style template ID

When using atom_style template, you cannot insert molecules that are
not in that template.

E: Cannot use fix gcmc shake and not molecule

Self-explanatory.

E: Molecule template ID for fix gcmc does not exist

Self-explanatory.

W: Molecule template for fix gcmc has multiple molecules

The fix gcmc command will only create molecules of a single type,
i.e. the first molecule in the template.

E: Region ID for fix gcmc does not exist

Self-explanatory.

W: Fix gcmc using full_energy option

Fix gcmc has automatically turned on the full_energy option since it
is required for systems like the one specified by the user. User input
included one or more of the following: kspace, triclinic, a hybrid
pair style, an eam pair style, or no "single" function for the pair
style.

E: Invalid atom type in fix gcmc command

The atom type specified in the gcmc command does not exist.

E: Fix gcmc cannot exchange individual atoms belonging to a molecule

This is an error since you should not delete only one atom of a
molecule.  The user has specified atomic (non-molecular) gas
exchanges, but an atom belonging to a molecule could be deleted.

E: All mol IDs should be set for fix gcmc group atoms

The molecule flag is on, yet not all molecule ids in the fix group
have been set to non-zero positive values by the user. This is an
error since all atoms in the fix gcmc group are eligible for deletion,
rotation, and translation and therefore must have valid molecule ids.

E: Fix gcmc molecule command requires that atoms have molecule attributes

Should not choose the gcmc molecule feature if no molecules are being
simulated. The general molecule flag is off, but gcmc's molecule flag
is on.

E: Fix gcmc shake fix does not exist

Self-explanatory.

E: Fix gcmc and fix shake not using same molecule template ID

Self-explanatory.

E: Cannot use fix gcmc in a 2d simulation

Fix gcmc is set up to run in 3d only. No 2d simulations with fix gcmc
are allowed.

E: Could not find fix gcmc exclusion group ID

Self-explanatory.

E: Could not find fix gcmc rotation group ID

Self-explanatory.

E: Illegal fix gcmc gas mass <= 0

The computed mass of the designated gas molecule or atom type was less 
than or equal to zero.

E: Cannot do gcmc on atoms in atom_modify first group

This is a restriction due to the way atoms are organized in a list to
enable the atom_modify first command.

E: Fix gcmc ran out of available molecule IDs

See the setting for tagint in the src/lmptype.h file.

E: Fix gcmc ran out of available atom IDs

See the setting for tagint in the src/lmptype.h file.

E: Too many total atoms

See the setting for bigint in the src/lmptype.h file.

*/
