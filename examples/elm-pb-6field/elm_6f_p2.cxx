/*******************************************************************************
 * High-Beta Flute-Reduced MHD with 5-field of (N_i, T_e, T_i, U, Psi)
 * Basically the same as Hazeltine-Meiss but different normalisations.
 * diffusion_par can open the parallel thermal conductivity
 * T. Xia
 *******************************************************************************/

#include "bout.hxx"
#include "initialprofiles.hxx"
#include "invert_laplace.hxx"
#include "invert_parderiv.hxx"
#include "interpolation.hxx"
#include "derivs.hxx"
#include <math.h>
#include "sourcex.hxx"
#include <boutmain.hxx>
#include <msg_stack.hxx>


BoutReal n0_height, n0_ave, n0_width, n0_center, n0_bottom_x; //the total height, average width and center of profile of N0
BoutReal Tconst; //the ampitude of congstant temperature

BoutReal laplace_alpha; //test the effect of first order term of invert Laplace function
BoutReal Tau_ie; //the ratio of Ti0/Te0

// 2D inital profiles
Field2D  J0, P0; // Current and pressure
Vector2D b0xcv; // Curvature term
Field2D phi0;   // When diamagnetic terms used

Field2D N0,Ti0,Te0,Ne0;  // number density and temperature
Field2D Pi0, Pe0;
Field2D q95;
BoutReal q95_input;
bool n0_fake_prof, T0_fake_prof;
BoutReal Zi; // charge number of ion

// B field vectors
Vector2D B0vec; // B0 field vector

// V0 field vectors
Vector2D V0vec; // V0 field vector in convection
Vector2D V0eff; // effective V0 field vector in Ohm's law

// 3D evolving variables
Field3D U, Psi, P, Pi, Pe;
Field3D Ni, Te, Ti, Ne;
Field3D Vipar, Vepar;

// Derived 3D variables
Field3D Jpar, phi; // Parallel current, electric potential

Field3D Jpar2; //  Delp2 of Parallel current
Field3D tmpA2; // Grad2_par2new of Parallel vector potential
Field3D tmpN2, tmpTi2, tmpTe2, tmpVp2; // Grad2_par2new of Parallel density

// Constraint
Field3D C_phi;

// Parameters
BoutReal density; // Number density [m^-3]
BoutReal Bbar, Lbar, Tbar, Va; // Normalisation constants
BoutReal Nbar, Tibar, Tebar;
BoutReal dia_fact; // Multiply diamagnetic term by this

BoutReal diffusion_par;   //Parallel thermal conductivity
BoutReal diffusion_perp;  //Perpendicular thermal conductivity (>0 open)
BoutReal diffusion_n4, diffusion_ti4, diffusion_te4;   //M: 4th Parallel density diffusion
BoutReal diffusion_v4;
BoutReal diffusion_u4;   //xqx: parallel hyper-viscous diffusion for vorticity

BoutReal heating_P;  // heating power in pressure
BoutReal hp_width;  // heating profile radial width in pressure
BoutReal hp_length;  // heating radial domain in pressure
BoutReal sink_P;     // sink in pressure
BoutReal sp_width;   // sink profile radial width in pressure
BoutReal sp_length;  // sink radial domain in pressure

BoutReal sink_Ul;     // left edge sink in vorticity
BoutReal su_widthl;   // left edge sink profile radial width in vorticity
BoutReal su_lengthl;  // left edge sink radial domain in vorticity

BoutReal sink_Ur;     // right edge sink in vorticity
BoutReal su_widthr;   // right edge sink profile radial width in vorticity
BoutReal su_lengthr;  // right edge sink radial domain in vorticity

BoutReal viscos_par;  // Parallel viscosity
BoutReal viscos_perp; // Perpendicular viscosity
BoutReal hyperviscos; // Hyper-viscosity (radial)
Field3D hyper_mu_x; // Hyper-viscosity coefficient

Field3D Dperp2Phi0, Dperp2Phi, GradPhi02, GradPhi2; //Temporary variables for gyroviscous
Field3D GradparPhi02, GradparPhi2, GradcPhi, GradcparPhi;
Field3D Dperp2Pi0, Dperp2Pi, bracketPhi0P, bracketPhiP0, bracketPhiP;

BoutReal Psipara1, Upara0, Upara1;  // Temporary normalization constants for all the equations
BoutReal Upara2, Upara3, Nipara1;
BoutReal Tipara1, Tipara2;
BoutReal Tepara1, Tepara2, Tepara3, Tepara4;
BoutReal Vepara, Vipara;
BoutReal Low_limit; //To limit the negative value of total density and temperatures

Field3D Te_tmp, Ti_tmp, N_tmp; //to avoid the negative value of total value
BoutReal gamma_i_BC, gamma_e_BC; //sheath energy transmission factors
int Sheath_width;
Field3D c_se, Jpar_sh, q_se, q_si;  //variables for sheath boundary conditions

// options
bool include_curvature, include_jpar0, compress0;
bool evolve_pressure, continuity, gyroviscous;
bool BScurrent;
Field3D Jpar_BS0;

BoutReal vacuum_pressure;
BoutReal vacuum_trans; // Transition width
Field3D vac_mask;

int phi_flags, apar_flags;
bool nonlinear;
bool evolve_jpar; 
BoutReal g; // Only if compressible
bool phi_curv;

// Poisson brackets: b0 x Grad(f) dot Grad(g) / B = [f, g]
// Method to use: BRACKET_ARAKAWA, BRACKET_STD or BRACKET_SIMPLE               
BRACKET_METHOD bm_exb, bm_mag; // Bracket method for advection terms 
int bracket_method_exb, bracket_method_mag;

bool diamag;
bool energy_flux, energy_exch; // energy flux term
bool diamag_phi0;   // Include the diamagnetic equilibrium phi0
bool thermal_force;  // Include the thermal flux term in Ohm's law
bool eHall;
BoutReal AA; // ion mass in units of the proton mass; AA=Mi/Mp

BoutReal Vt0; // equilibrium toroidal flow normalized to Alfven velocity
BoutReal Vp0; // equilibrium poloidal flow normalized to Alfven velocity

bool nogradparj;
bool filter_z;
int filter_z_mode;
int low_pass_z;
int zonal_flow;
int zonal_field;
int zonal_bkgd;
bool relax_j_vac;
BoutReal relax_j_tconst; // Time-constant for j relax
Field3D Psitarget;   // The (moving) target to relax to

bool smooth_j_x;  // Smooth Jpar in the x direction
BoutReal filter_nl;

int jpar_bndry_width; // Zero jpar in a boundary region

bool parallel_lr_diff; // Use left and right shifted stencils for parallel differences

bool parallel_lagrange; // Use (semi-) Lagrangian method for parallel derivatives
bool parallel_project;  // Use Apar to project field-lines



//********************


Field3D Xip_x, Xip_z;     // Displacement of y+1 (in cell index space)

Field3D Xim_x, Xim_z;     // Displacement of y-1 (in cell index space)

bool phi_constraint; // Solver for phi using a solver constraint 

BoutReal vac_lund, core_lund;       // Lundquist number S = (Tau_R / Tau_A). -ve -> infty
BoutReal vac_resist,  core_resist;  // The resistivities (just 1 / S)
Field3D eta;                    // Resistivity profile (1 / S)
bool spitzer_resist;  // Use Spitzer formula for resistivity

Field3D eta_spitzer;     // Resistivity profile (kg*m^3 / S / C^2)
Field3D nu_i;            // Ion collision frequency profile (1 / S)
Field3D nu_e;            // Electron collision frequency profile (1 / S)
Field3D vth_i;           // Ion Thermal Velocity profile (M / S)
Field3D vth_e;           // Electron Thermal Velocity profile (M / S)
Field3D kappa_par_i;     // Ion Thermal Conductivity profile (kg&M / S^2)
Field3D kappa_par_e;     // Electron Thermal Conductivity profile (kg*M / S^2)
Field2D omega_ci, omega_ce;  //cyclotron frequency
Field3D kappa_perp_i;     // Ion perpendicular Thermal Conductivity profile (kg&M / S^2)
Field3D kappa_perp_e;     // Electron perpendicular Thermal Conductivity profile (kg*M / S^2)

BoutReal hyperresist;    // Hyper-resistivity coefficient (in core only)
BoutReal ehyperviscos;   // electron Hyper-viscosity coefficient
Field3D hyper_eta_x; // Radial resistivity profile
Field3D hyper_eta_z; // Toroidal resistivity profile

int damp_width;     // Width of inner damped region
BoutReal damp_t_const;  // Timescale of damping

// Metric coefficients
Field2D Rxy, Bpxy, Btxy, B0, hthe;
Field2D I; // Shear factor
BoutReal  LnLambda; // ln(Lambda)
//Field3D LnLambda;

const BoutReal PI = 3.14159265;
const BoutReal MU0 = 4.0e-7*PI;
const BoutReal Mi = 2.0*1.6726e-27; // Ion mass
const BoutReal KB = 1.38065e-23;     // Boltamann constant
const BoutReal ee = 1.602e-19;       // ln(Lambda)
const BoutReal eV_K = 11605.0;         // 1eV = 11605K

//const BoutReal Low_limit = 1.e-10;   // limit of the profile to prevent minus total value

// Communication objects
FieldGroup comms;

//Functions for sheath boundary conditions
void SBC_Dirichlet(Field3D &var, const Field3D &value);
void SBC_Gradpar(Field3D &var, const Field3D &value);
void SBC_yup_eq(Field3D &var, const Field3D &value);
void SBC_ydown_eq(Field3D &var, const Field3D &value);
void SBC_yup_Grad_par(Field3D &var, const Field3D &value);
void SBC_ydown_Grad_par(Field3D &var, const Field3D &value);

void advect_tracer(const Field3D &p,  // phi (input)
		   const Field3D &delta_x, const Field3D &delta_z, // Current location (input) 
		   Field3D &F_dx, Field3D &F_dz); // Time-derivative of location

const Field3D Grad2_par2new(const Field3D &f); //for 4th order diffusion

const Field2D N0tanh(BoutReal n0_height, BoutReal n0_ave, BoutReal n0_width, BoutReal n0_center, BoutReal n0_bottom_x);

const Field3D field_larger(const Field3D &f, const BoutReal limit);

const Field2D Invert_laplace2(const Field2D &f, int flags);

const Field3D BS_ft(const int index);
const Field3D F31(const Field3D input);
const Field3D F32ee(const Field3D input);
const Field3D F32ei(const Field3D input);

const Field2D Invert_laplace2(const Field2D &f, int flags)
{
  Field3D f_tmp, result_tmp;
  Field2D result;
  f_tmp.allocate();
  result_tmp.allocate();
  result.allocate();
  
  f_tmp = f;
  
  result_tmp = invert_laplace(f_tmp, flags, NULL);
  mesh->communicate(result_tmp);
  result_tmp = smooth_x(result_tmp);
  result_tmp = nl_filter_y(result_tmp, 1);

  for(int jx=0;jx<mesh->ngx;jx++)
    for(int jy=0;jy<mesh->ngy;jy++)
      result[jx][jy] = result_tmp[jx][jy][0];

  return(result);
}

const Field3D field_larger(const Field3D &f, const BoutReal limit)
{
  Field3D result;
  result.allocate();

//  #pragma omp parallel for
  for(int jx=0;jx<mesh->ngx;jx++)
    for(int jy=0;jy<mesh->ngy;jy++)
      for(int jz=0;jz<mesh->ngz;jz++)
      {
        if(f[jx][jy][jz] >= limit)
	  result[jx][jy][jz] = f[jx][jy][jz];
	else
	  result[jx][jy][jz] = limit;
      }
  mesh->communicate(result);
  return(result);
}

const Field3D Grad2_par2new(const Field3D &f)
{
  /*
   * This function implements d2/dy2 where y is the poloidal coordinate theta
   */


#ifdef CHECK
  int msg_pos = msg_stack.push("Grad2_par2new( Field3D )");
#endif



  Field3D result = D2DY2(f);
  

#ifdef TRACK
  result.name = "Grad2_par2new("+f.name+")";
#endif
#ifdef CHECK
  msg_stack.pop(msg_pos);
#endif
  return result;
}

const Field2D N0tanh(BoutReal n0_height, BoutReal n0_ave, BoutReal n0_width, BoutReal n0_center, BoutReal n0_bottom_x)
{
  Field2D result;
  result.allocate();

  BoutReal Grid_NX, Grid_NXlimit;    //the grid number on x, and the 
  mesh->get(Grid_NX, "NX");
  Grid_NXlimit = n0_bottom_x * Grid_NX;

  for(int jx=0;jx<mesh->ngx;jx++)
    {
      BoutReal mgx = mesh->GlobalX(jx);
      BoutReal xgrid_num = Grid_NXlimit/Grid_NX;
      if (mgx > xgrid_num)
	mgx = xgrid_num;
      BoutReal rlx = mgx - n0_center;
      BoutReal temp = exp(rlx/n0_width);
      BoutReal dampr = ((temp - 1.0 / temp) / (temp + 1.0 / temp));
      for(int jy=0;jy<mesh->ngy;jy++)
	result[jx][jy] = 0.5*(1.0 - dampr) * n0_height + n0_ave;  
    }
  
  mesh->communicate(result);

  return result;
}
  
  
int physics_init(bool restarting)
{
  bool noshear;
  
  output.write("Solving high-beta flute reduced equations\n");
  output.write("\tFile    : %s\n", __FILE__);
  output.write("\tCompiled: %s at %s\n", __DATE__, __TIME__);

  //////////////////////////////////////////////////////////////
  // Load data from the grid

  // Load 2D profiles
  mesh->get(J0, "Jpar0");    // A / m^2
//  mesh->get(P0, "pressure"); // Pascals
  mesh->get(P0, "pres2"); // Pascals



  // Load curvature term
  b0xcv.covariant = false; // Read contravariant components
  mesh->get(b0xcv, "bxcv"); // mixed units x: T y: m^-2 z: m^-2

  // Load metrics
  if(mesh->get(Rxy,  "Rxy")) { // m
    output.write("Error: Cannot read Rxy from grid\n");
    return 1;
  }
  if(mesh->get(Bpxy, "Bpxy")) { // T
    output.write("Error: Cannot read Bpxy from grid\n");
    return 1;
  }
  mesh->get(Btxy, "Btxy"); // T
  mesh->get(B0,   "Bxy");  // T
  mesh->get(hthe, "hthe"); // m
  mesh->get(I,    "sinty");// m^-2 T^-1

  //////////////////////////////////////////////////////////////
  // Read parameters from the options file
  // 
  // Options.get ( NAME,    VARIABLE,    DEFAULT VALUE)
  //
  // or if NAME = "VARIABLE" then just
  //
  // OPTION(VARIABLE, DEFAULT VALUE) 
  //
  // Prints out what values are assigned
  /////////////////////////////////////////////////////////////

  Options *globalOptions = Options::getRoot();
  Options *options = globalOptions->getSection("highbeta");


  OPTION(options, n0_fake_prof,    false);   //use the hyperbolic profile of n0. If both  n0_fake_prof and T0_fake_prof are false, use the profiles from grid file 
  OPTION(options, n0_height,         0.4);   //the total height of profile of N0, in percentage of Ni_x
  OPTION(options, n0_ave,           0.01);   //the center or average of N0, in percentage of Ni_x
  OPTION(options, n0_width,          0.1);   //the width of the gradient of N0,in percentage of x
  OPTION(options, n0_center,       0.633);   //the grid number of the center of N0, in percentage of x
  OPTION(options, n0_bottom_x,      0.81);  //the start of flat region of N0 on SOL side, in percentage of x 
  OPTION(options, T0_fake_prof,    false); 
  OPTION(options, Tconst,           -1.0);   //the amplitude of constant temperature, in percentage

  OPTION(options, laplace_alpha,     1.0);   //test parameter for the cross term of invert Lapalace
  OPTION(options, Low_limit,         1.0e-10); //limit the negative value of total quantities
  OPTION(options, q95_input,         5.0);   //input q95 as a constant, if <0 use profile from grid  
  
  OPTION(options, gamma_i_BC,        -1.0);  //sheath energy transmission factor for ion
  OPTION(options, gamma_e_BC,        -1.0);  //sheath energy transmission factor for electron
  OPTION(options, Sheath_width,      1);     //Sheath boundary width in grid number

  OPTION(options, density,           1.0e19); // Number density [m^-3]
  OPTION(options, Zi,                1);      // ion charge number
  OPTION(options, continuity,        false);  // use continuity equation

  OPTION(options, evolve_jpar,       false);  // If true, evolve J raher than Psi
  OPTION(options, phi_constraint,    false);  // Use solver constraint for phi

  // Effects to include/exclude
  OPTION(options, include_curvature, true);
  OPTION(options, include_jpar0,     true);
  OPTION(options, evolve_pressure,   true);
  
  OPTION(options, compress0,         false);
  OPTION(options, gyroviscous,       false);
  OPTION(options, nonlinear,         false);

  OPTION(options, BScurrent,         false);

  //  int bracket_method;
  OPTION(options, bracket_method_exb, 0);
  switch(bracket_method_exb) {
  case 0: {
    bm_exb = BRACKET_STD;
    output << "\tBrackets for ExB: default differencing\n";
    break;
  }
  case 1: {
    bm_exb = BRACKET_SIMPLE;
    output << "\tBrackets for ExB: simplified operator\n";
    break;
  }
  case 2: {
    bm_exb = BRACKET_ARAKAWA;
    output << "\tBrackets for ExB: Arakawa scheme\n";
    break;
  }
  case 3: {
    bm_exb = BRACKET_CTU;
    output << "\tBrackets for ExB: Corner Transport Upwind method\n";
    break;
  }
  default:
    output << "ERROR: Invalid choice of bracket method. Must be 0 - 3\n";
    return 1;
  }

  //  int bracket_method;
  OPTION(options, bracket_method_mag, 2);
  switch(bracket_method_mag) {
  case 0: {
    bm_mag = BRACKET_STD;
    output << "\tBrackets: default differencing\n";
    break;
  }
  case 1: {
    bm_mag = BRACKET_SIMPLE;
    output << "\tBrackets: simplified operator\n";
    break;
  }
  case 2: {
    bm_mag = BRACKET_ARAKAWA;
    output << "\tBrackets: Arakawa scheme\n";
    break;
  }
  case 3: {
    bm_mag = BRACKET_CTU;
    output << "\tBrackets: Corner Transport Upwind method\n";
    break;
  }
  default:
    output << "ERROR: Invalid choice of bracket method. Must be 0 - 3\n";
    return 1;
  }

  OPTION(options, eHall,             false);  // electron Hall or electron parallel pressue gradient effects? 
  OPTION(options, thermal_force,     false);  // Thermal flux in Ohm's Law
  OPTION(options, AA,                1.0);    // ion mass in units of proton mass

  OPTION(options, diamag,            false);  // Diamagnetic effects? 
  OPTION(options, energy_flux,       false);  // energy flux
  OPTION(options, energy_exch,       false);  // energy exchange
  OPTION(options, diamag_phi0,       diamag); // Include equilibrium phi0
  OPTION(options, dia_fact,          1.0);    // Scale diamagnetic effects by this factor

  OPTION(options, noshear,           false);

  OPTION(options, relax_j_vac,       false); // Relax vacuum current to zero
  OPTION(options, relax_j_tconst,    0.1);
  
  // Toroidal filtering
  OPTION(options, filter_z,          false);  // Filter a single n
  OPTION(options, filter_z_mode,     1);
  OPTION(options, low_pass_z,       -1);      // Low-pass filter
  OPTION(options, zonal_flow,       -1);      // zonal flow filter
  OPTION(options, zonal_field,      -1);      // zonal field filter
  OPTION(options, zonal_bkgd,       -1);      // zonal background P filter

  OPTION(options, filter_nl,        -1);      // zonal background P filter

  // Radial smoothing
  OPTION(options, smooth_j_x,       false);  // Smooth Jpar in x

  // Jpar boundary region
  OPTION(options, jpar_bndry_width, -1);

  // Parallel differencing
  OPTION(options, parallel_lr_diff, false);
  OPTION(options, parallel_lagrange, false); // Use a (semi-) Lagrangian method for Grad_parP
  OPTION(options, parallel_project, false);

  // Vacuum region control
  OPTION(options, vacuum_pressure,   0.02);   // Fraction of peak pressure
  OPTION(options, vacuum_trans,      0.005);  // Transition width in pressure
  
  // Resistivity and hyper-resistivity options
  OPTION(options, vac_lund,          0.0);    // Lundquist number in vacuum region
  OPTION(options, core_lund,         0.0);    // Lundquist number in core region
  OPTION(options, hyperresist,       -1.0);
  OPTION(options, ehyperviscos,      -1.0);
  OPTION(options, spitzer_resist,    false);  // Use Spitzer resistivity

  // Inner boundary damping
  OPTION(options, damp_width,        0);
  OPTION(options, damp_t_const,      0.1);

  // Viscosity and hyper-viscosity
  OPTION(options, viscos_par,        -1.0);  // Parallel viscosity
  OPTION(options, viscos_perp,       -1.0);  // Perpendicular viscosity
  OPTION(options, hyperviscos,       -1.0);  // Radial hyperviscosity
  
  OPTION(options, diffusion_par,        -1.0);  // Parallel temperature diffusion
  OPTION(options, diffusion_perp,       -1.0);  // Perpendicular temperature diffusion
  OPTION(options, diffusion_n4,        -1.0);  // M: 4th Parallel density diffusion
  OPTION(options, diffusion_ti4,        -1.0);  // M: 4th Parallel ion temperature diffusion
  OPTION(options, diffusion_te4,        -1.0);  // M: 4th Parallel electron temperature diffusion
  OPTION(options, diffusion_v4,        -1.0);  // M: 4th Parallel ion parallel velocity diffusion
  OPTION(options, diffusion_u4,        -1.0);   //xqx: parallel hyper-viscous diffusion for vorticity

  // heating factor in pressure
  OPTION(options, heating_P,        -1.0);  //  heating power in pressure
  OPTION(options, hp_width,         0.1);  //  the percentage of radial grid points for heating profile radial width in pressure
  OPTION(options, hp_length,        0.04);  //  the percentage of radial grid points for heating profile radial domain in pressure

  // sink factor in pressure
  OPTION(options, sink_P,           -1.0);  //  sink in pressure
  OPTION(options, sp_width,         0.05);  //  the percentage of radial grid points for sink profile radial width in pressure
  OPTION(options, sp_length,        0.04);  //  the percentage of radial grid points for sink profile radial domain in pressure


  // left edge sink factor in vorticity
  OPTION(options, sink_Ul,           -1.0);  //  left edge sink in vorticity
  OPTION(options, su_widthl,         0.06);  //  the percentage of left edge radial grid points for sink profile radial width in vorticity
  OPTION(options, su_lengthl,        0.15);  //  the percentage of left edge radial grid points for sink profile radial domain in vorticity

  // right edge sink factor in vorticity
  OPTION(options, sink_Ur,           -1.0);  //  right edge sink in vorticity
  OPTION(options, su_widthr,         0.06);  //  the percentage of right edge radial grid points for sink profile radial width in vorticity
  OPTION(options, su_lengthr,        0.15);  //  the percentage of right edge radial grid points for sink profile radial domain in vorticity

  // Compressional terms
  OPTION(options, phi_curv,          true);
  options->get("gamma",             g,                 5.0/3.0);
  
  // Field inversion flags
  OPTION(options, phi_flags,         0);
  OPTION(options, apar_flags,        0);



  if(!include_curvature)
    b0xcv = 0.0;
  
  if(!include_jpar0)
    J0 = 0.0;

  if(noshear) {
    if(include_curvature)
      b0xcv.z += I*b0xcv.x;
    mesh->ShiftXderivs = false;
    I = 0.0;
  }
  
  //////////////////////////////////////////////////////////////
  // SHIFTED RADIAL COORDINATES

  if(mesh->ShiftXderivs) {
    if(mesh->IncIntShear) {
      // BOUT-06 style, using d/dx = d/dpsi + I * d/dz
      mesh->IntShiftTorsion = I;
      
    }else {
      // Dimits style, using local coordinate system
      if(include_curvature)
	b0xcv.z += I*b0xcv.x;
      I = 0.0;  // I disappears from metric
    }
  }

  //////////////////////////////////////////////////////////////
  // NORMALISE QUANTITIES
  
  if(mesh->get(Bbar, "bmag")) // Typical magnetic field
    Bbar = 1.0;
  if(mesh->get(Lbar, "rmag")) // Typical length scale
    Lbar = 1.0;

  if(mesh->get(Tibar, "Ti_x")) // Typical ion temperature scale
    Tibar = 1.0;

  if(mesh->get(Tebar, "Te_x")) // Typical electron temperature scale
    Tebar = 1.0;

  if(mesh->get(Nbar, "Nixexp")) // Typical ion density scale
    Nbar = 1.0;
  Nbar *= 1.e20/density;

  Tau_ie = Tibar/Tebar;

  Va = sqrt(Bbar*Bbar / (MU0*Mi*Nbar*density));

  Tbar = Lbar / Va;

  output.write("Normalisations: Bbar = %e T   Lbar = %e m\n", Bbar, Lbar);
  output.write("                Va = %e m/s   Tbar = %e s\n", Va, Tbar);
  output.write("                Nbar = %e * %e m^-3\n",Nbar,density);
  output.write("Tibar = %e eV   Tebar = %e eV    Ti/Te = %e\n", Tibar, Tebar, Tau_ie);
  output.write("    Resistivity\n");


  if(thermal_force || eHall)
    {
      Psipara1 = KB*Tebar*eV_K/ee/Bbar/Lbar/Va;
      output.write("                Psipara1 = %e   AA = %e \n", Psipara1, AA);
    }

  Upara0 = KB * Tebar*eV_K / (Zi * ee * Bbar * Va * Lbar);
  Upara1 = KB*Tebar*eV_K/Mi/Va/Va;
  output.write("vorticity cinstant: Upara0 = %e     Upara1 = %e\n", Upara0, Upara1);
  
  if(gyroviscous)
    {
      Upara2 = KB*Tibar*eV_K / (Zi*ee*Bbar*Lbar*Va);
      Upara3 = 1.0;
      output.write("Upara2 = %e     Upara3 = %e\n", Upara2, Upara3);
    }
  
  if ((diamag && continuity) || energy_flux)
    {
      Nipara1 = KB * Tibar*eV_K/ (Zi*ee*Bbar*Lbar*Va);
      Tipara2 = Nipara1;
      Tepara2 = KB * Tebar*eV_K / (ee * Bbar * Lbar * Va);
      Tepara3 = Bbar / (ee * MU0 * Nbar * density * Lbar * Va);
      output.write("Nipara1 = %e     Tipara2 = %e\n", Nipara1, Tipara2);    
      output.write("Tepara2 = %e     Tepara3 = %e\n", Tepara2, Tepara3);
    }
  
  if (energy_exch)
    {
      Tepara4 = Bbar * Bbar / (MU0 * KB * Nbar*density * Tebar*eV_K);
      output.write("energy exchange constant:   Tepara4 = %e\n", Tepara4);
    }

  if (compress0)
    {
      output.write("Including compression (Vipar) effects\n");
      Vipara = MU0 * KB * Nbar*density * Tebar*eV_K / (Bbar*Bbar);
      Vepara = Bbar/(MU0*Zi*ee*Nbar*density*Lbar*Va);
      output.write("Normalized constant for Vipar :   Vipara = %e\n", Vipara);
      output.write("Normalized constant for Vepar :   Vepara = %e\n", Vepara);
    }

  if (diffusion_par >0.0 || diffusion_perp > 0.0)
    {
      Tipara1 = 2.0/ 3.0 / (Lbar * Va);
      Tepara1 = Tipara1 / Zi;
    }

  if(vac_lund > 0.0) {
    output.write("        Vacuum  Tau_R = %e s   eta = %e Ohm m\n", vac_lund * Tbar, 
		 MU0 * Lbar * Lbar / (vac_lund * Tbar));
    vac_resist = 1. / vac_lund;
  }else {
    output.write("        Vacuum  - Zero resistivity -\n");
    vac_resist = 0.0;
  }
  if(core_lund > 0.0) {
    output.write("        Core    Tau_R = %e s   eta = %e Ohm m\n", core_lund * Tbar,
		 MU0 * Lbar * Lbar / (core_lund * Tbar));
    core_resist = 1. / core_lund;
  }else {
    output.write("        Core    - Zero resistivity -\n");
    core_resist = 0.0;
  }

  if(hyperresist > 0.0) {
    output.write("    Hyper-resistivity coefficient: %e\n", hyperresist);
    dump.add(hyper_eta_x, "hyper_eta_x", 1);
    dump.add(hyper_eta_z, "hyper_eta_z", 1);
  }

  if(ehyperviscos > 0.0) {
    output.write("    electron Hyper-viscosity coefficient: %e\n", ehyperviscos);
  }

  if(hyperviscos > 0.0) {
    output.write("    Hyper-viscosity coefficient: %e\n", hyperviscos);
    dump.add(hyper_mu_x, "hyper_mu_x", 1);
  }

  if(diffusion_par > 0.0) {
    output.write("    diffusion_par: %e\n", diffusion_par);
    dump.add(diffusion_par, "diffusion_par", 0);
  }

  if(diffusion_perp > 0.0) {
    output.write("    diffusion_perp: %e\n", diffusion_perp);
    dump.add(diffusion_perp, "diffusion_perp", 0);
  }

  //M: 4th order diffusion of p
  if(diffusion_n4 > 0.0) {
    output.write("    diffusion_n4: %e\n", diffusion_n4);
    dump.add(diffusion_n4, "diffusion_n4", 0);
  }

  //M: 4th order diffusion of Ti
  if(diffusion_ti4 > 0.0) {
    output.write("    diffusion_ti4: %e\n", diffusion_ti4);
    dump.add(diffusion_ti4, "diffusion_ti4", 0);
  }
  
  //M: 4th order diffusion of Te
  if(diffusion_te4 > 0.0) {
    output.write("    diffusion_te4: %e\n", diffusion_te4);
    dump.add(diffusion_te4, "diffusion_te4", 0);
  }

  //M: 4th order diffusion of Vipar
  if(diffusion_v4 > 0.0) {
    output.write("    diffusion_v4: %e\n", diffusion_v4);
    dump.add(diffusion_v4, "diffusion_v4", 0);
  }
  
  //xqx: parallel hyper-viscous diffusion for vorticity
  if(diffusion_u4 > 0.0) {
    output.write("    diffusion_u4: %e\n", diffusion_u4);
    dump.add(diffusion_u4, "diffusion_u4", 0);
  }

  if(sink_P > 0.0) {
    output.write("    sink_P(rate): %e\n", sink_P);
    dump.add(sink_P, "sink_P", 1);

    output.write("    sp_width(%): %e\n",sp_width);
    dump.add(sp_width, "sp_width", 1);

    output.write("    sp_length(%): %e\n",sp_length);
    dump.add(sp_length, "sp_length", 1);
  }


  if (compress0 && (diffusion_par > 0.0) && (gamma_i_BC > 0.0) && (gamma_e_BC > 0.0) )
    {
      output.write("Sheath Boundary conditions applied.");
      dump.add(c_se, "c_se", 1);
      dump.add(q_si, "q_si", 1);
      dump.add(q_se, "q_se", 1);
      dump.add(Jpar_sh, "Jpar_sh", 1);
    }

  J0 = MU0*Lbar * J0 / B0;
  P0 = P0/(KB * (Tibar+Tebar)*eV_K /2. * Nbar*density);

  b0xcv.x /= Bbar;
  b0xcv.y *= Lbar*Lbar;
  b0xcv.z *= Lbar*Lbar;

  Rxy  /= Lbar;
  Bpxy /= Bbar;
  Btxy /= Bbar;
  B0   /= Bbar;
  hthe /= Lbar;
  mesh->dx   /= Lbar*Lbar*Bbar;
  I    *= Lbar*Lbar*Bbar;

  if( (!T0_fake_prof) && n0_fake_prof )
    {
      N0 = N0tanh(n0_height*Nbar, n0_ave*Nbar, n0_width, n0_center, n0_bottom_x);

      Ti0 = P0/N0/2.0;
      Te0 = Ti0;
    }
  else if (T0_fake_prof)
    {
      Ti0 = Tconst;
      Te0 = Ti0;
      N0 = P0/(Ti0+Te0);
    }
  else
    {
      if(mesh->get(N0,  "Niexp")) { // N_i0                                          
	output.write("Error: Cannot read Ni0 from grid\n");
	return 1;
	} 
      
      if(mesh->get(Ti0,  "Tiexp")) { // T_i0                                         
	output.write("Error: Cannot read Ti0 from grid\n");
	return 1;
      }

      if(mesh->get(Te0,  "Teexp")) { // T_e0  
	output.write("Error: Cannot read Te0 from grid\n");
	return 1;
	}
      N0 /= Nbar;
      Ti0 /= Tibar;
      Te0 /= Tebar;
    }

  Ne0 = Zi * N0; // quasi-neutral condition
  Pi0 = N0 * Ti0;
  Pe0 = Ne0 * Te0;

  nu_e.setLocation(CELL_YLOW);  
  nu_e.setBoundary("kappa");     
  if (spitzer_resist)
    {
      eta_spitzer.setLocation(CELL_YLOW); 
      eta_spitzer.setBoundary("kappa");
    }
  if (diffusion_par > 0.0 || diffusion_perp > 0.0)
    {
      nu_i.setLocation(CELL_YLOW);
      nu_i.setBoundary("kappa");
      vth_i.setLocation(CELL_YLOW);      
      vth_e.setLocation(CELL_YLOW);      
      vth_i.setBoundary("kappa");
      vth_e.setBoundary("kappa");
      kappa_par_i.setLocation(CELL_YLOW);
      kappa_par_e.setLocation(CELL_YLOW);
      kappa_par_i.setBoundary("kappa");
      kappa_par_e.setBoundary("kappa");
      kappa_perp_i.setLocation(CELL_YLOW);
      kappa_perp_e.setLocation(CELL_YLOW);
      kappa_perp_i.setBoundary("kappa");
      kappa_perp_e.setBoundary("kappa");
    }

  if (gyroviscous)
    {
      Dperp2Phi0.setLocation(CELL_CENTRE);
      Dperp2Phi0.setBoundary("phi");
      Dperp2Phi.setLocation(CELL_CENTRE);
      Dperp2Phi.setBoundary("phi");
      GradPhi02.setLocation(CELL_CENTRE);
      GradPhi02.setBoundary("phi");
      //GradparPhi02.setLocation(CELL_CENTRE);
      //GradparPhi02.setBoundary("phi");
      GradcPhi.setLocation(CELL_CENTRE);
      GradcPhi.setBoundary("phi");
      //GradcparPhi.setLocation(CELL_CENTRE);
      //GradcparPhi.setBoundary("phi");
      Dperp2Pi0.setLocation(CELL_CENTRE);
      Dperp2Pi0.setBoundary("P");
      Dperp2Pi.setLocation(CELL_CENTRE);
      Dperp2Pi.setBoundary("P");
      bracketPhi0P.setLocation(CELL_CENTRE);
      bracketPhi0P.setBoundary("P");
      bracketPhiP0.setLocation(CELL_CENTRE);
      bracketPhiP0.setBoundary("P");
      if (nonlinear)
	{
	  GradPhi2.setLocation(CELL_CENTRE);
	  GradPhi2.setBoundary("phi");
	  //GradparPhi2.setLocation(CELL_CENTRE);
	  //GradparPhi2.setBoundary("phi");
	  bracketPhiP.setLocation(CELL_CENTRE);
	  bracketPhiP.setBoundary("P");
	}
    }

  BoutReal pnorm = max(P0, true); // Maximum over all processors
  
  vacuum_pressure *= pnorm; // Get pressure from fraction
  vacuum_trans *= pnorm;

  // Transitions from 0 in core to 1 in vacuum
  vac_mask = (1.0 - tanh( (P0 - vacuum_pressure) / vacuum_trans )) / 2.0;

  if (q95_input >0 )
    q95 = q95_input;   //use a constant for test

  LnLambda = 24.0 - log(pow(Zi*Nbar*density/1.e6, 0.5) * pow(Tebar, -1.0));       //xia: ln Lambda
  output.write("\tlog Lambda: %e\n", LnLambda);

  nu_e = 2.91e-6*LnLambda*((N0)*Nbar*density/1.e6)*(((Te0)*Tebar)^(-1.5)); // nu_e in 1/S. 
  output.write("\telectron collision rate: %e -> %e [1/s]\n", min(nu_e), max(nu_e));
  //nu_e.applyBoundary();
  //mesh->communicate(nu_e);

  if (diffusion_par >0.0 || diffusion_perp > 0.0)
    {
    
      output.write("\tion thermal noramlized constant: Tipara1 = %e\n",Tipara1); 
      output.write("\telectron normalized thermal constant: Tepara1 = %e\n",Tepara1);
      //xqx addition, begin
      // Use Spitzer thermal conductivities 
      nu_i = 4.80e-8*(Zi*Zi*Zi*Zi/sqrt(AA))*LnLambda*((N0)*Nbar*density/1.e6)*(((Ti0)*Tibar)^(-1.5)); // nu_i in 1/S. 
      //output.write("\tCoulomb Logarithm: %e \n", max(LnLambda));
      output.write("\tion collision rate: %e -> %e [1/s]\n", min(nu_i), max(nu_i));

      //nu_i.applyBoundary();
      //mesh->communicate(nu_i);
  
      vth_i = 9.79e3*sqrt((Ti0)*Tibar/AA); // vth_i in m/S. 
      output.write("\tion thermal velocity: %e -> %e [m/s]\n", min(vth_i), max(vth_i));
      //vth_i.applyBoundary();
      //mesh->communicate(vth_i);
      vth_e = 4.19e5*sqrt((Te0)*Tebar);    // vth_e in m/S. 
      output.write("\telectron thermal velocity: %e -> %e [m/s]\n", min(vth_e), max(vth_e));
      //vth_e.applyBoundary();
      //mesh->communicate(vth_e);
    }

  if (diffusion_par >0.0)
    {
      kappa_par_i=3.9*vth_i*vth_i/nu_i;// * 1.e4;
      kappa_par_e=3.2*vth_e*vth_e/nu_e;// * 1.e4;

      output.write("\tion thermal conductivity: %e -> %e [m^2/s]\n", min(kappa_par_i), max(kappa_par_i));
      output.write("\telectron thermal conductivity: %e -> %e [m^2/s]\n", min(kappa_par_e), max(kappa_par_e));
      
      output.write("\tnormalized ion thermal conductivity: %e -> %e \n", min(kappa_par_i*Tipara1), max(kappa_par_i*Tipara1));
      output.write("\tnormalized electron thermal conductivity: %e -> %e \n", min(kappa_par_e*Tepara1), max(kappa_par_e*Tepara1));
      
      Field3D kappa_par_i_fl, kappa_par_e_fl;

      kappa_par_i_fl = vth_i * (q95 * Lbar);// * 1.e2;
      kappa_par_e_fl = vth_e * (q95 * Lbar);// * 1.e2;

      kappa_par_i *= kappa_par_i_fl / (kappa_par_i + kappa_par_i_fl);
      kappa_par_i *= Tipara1*N0;
      output.write("\tUsed normalized ion thermal conductivity: %e -> %e \n", min(kappa_par_i), max(kappa_par_i));
      //kappa_par_i.applyBoundary();
      //mesh->communicate(kappa_par_i);
      kappa_par_e *= kappa_par_e_fl / (kappa_par_e + kappa_par_e_fl);
      kappa_par_e *= Tepara1*N0/Zi;
      output.write("\tUsed normalized electron thermal conductivity: %e -> %e \n", min(kappa_par_e), max(kappa_par_e));
      //kappa_par_e.applyBoundary();
      //mesh->communicate(kappa_par_e);
      
      dump.add(kappa_par_i, "kappa_par_i", 1);
      dump.add(kappa_par_e, "kappa_par_e", 1);
    }

  if (diffusion_perp >0.0)
    {
      omega_ci = Zi*ee*Bbar*B0/Mi;
      omega_ce = 1836.0*ee*Bbar*B0/Mi;
      
      kappa_perp_i=2.0*vth_i*vth_i*nu_i/(omega_ci*omega_ci);// * 1.e4;
      kappa_perp_e=4.7*vth_e*vth_e*nu_e/(omega_ce*omega_ce);// * 1.e4;

      output.write("\tion perp thermal conductivity: %e -> %e [m^2/s]\n", min(kappa_perp_i), max(kappa_perp_i));
      output.write("\telectron perp thermal conductivity: %e -> %e [m^2/s]\n", min(kappa_perp_e), max(kappa_perp_e));
      
      output.write("\tnormalized perp ion thermal conductivity: %e -> %e \n", min(kappa_perp_i*Tipara1), max(kappa_perp_i*Tipara1));
      output.write("\tnormalized perp electron thermal conductivity: %e -> %e \n", min(kappa_perp_e*Tepara1), max(kappa_perp_e*Tepara1));
      
      Field3D kappa_perp_i_fl, kappa_perp_e_fl;

      kappa_perp_i_fl = vth_i * (q95 * Lbar);// * 1.e4;
      kappa_perp_e_fl = vth_e * (q95 * Lbar);// * 1.e4;

      kappa_perp_i *= kappa_perp_i_fl / (kappa_perp_i + kappa_perp_i_fl);
      kappa_perp_i *= Tipara1;
      output.write("\tUsed normalized ion perp thermal conductivity: %e -> %e \n", min(kappa_perp_i), max(kappa_perp_i));
      //kappa_perp_i.applyBoundary();
      //mesh->communicate(kappa_perp_i);
      kappa_perp_e *= kappa_perp_e_fl / (kappa_perp_e + kappa_perp_e_fl);
      kappa_perp_e *= Tepara1;
      output.write("\tUsed normalized electron perp thermal conductivity: %e -> %e \n", min(kappa_perp_e), max(kappa_perp_e));
      //kappa_perp_e.applyBoundary();
      //mesh->communicate(kappa_perp_e);
      
      dump.add(kappa_perp_i, "kappa_perp_i", 1);
      dump.add(kappa_perp_e, "kappa_perp_e", 1);
    }
      
  if(spitzer_resist) {
    // Use Spitzer resistivity 
    output.write("");
    output.write("\tSpizter parameters");
    //output.write("\tTemperature: %e -> %e [eV]\n", min(Te), max(Te));
    eta_spitzer = 0.51*1.03e-4*Zi*LnLambda*((Te0*Tebar)^(-1.5)); // eta in Ohm-m. NOTE: ln(Lambda) = 20
    output.write("\tSpitzer resistivity: %e -> %e [Ohm m]\n", min(eta_spitzer), max(eta_spitzer));
    eta_spitzer /= MU0 * Va * Lbar;
    //eta_spitzer.applyBoundary();
    //mesh->communicate(eta_spitzer);
    output.write("\t -> Lundquist %e -> %e\n", 1.0/max(eta_spitzer), 1.0/min(eta_spitzer));
    dump.add(eta_spitzer, "eta_spitzer", 1);
  }
  else {
    // transition from 0 for large P0 to resistivity for small P0
    eta = core_resist + (vac_resist - core_resist) * vac_mask;
    eta_spitzer = 0.;
    dump.add(eta, "eta", 0);
  }

  if (BScurrent)
    {
      Field3D L31, L32, L34;
      Field3D f31, f32ee, f32ei, f34, ft;
      Field3D nu_estar, nu_istar, BSal0, BSal;
      nu_estar = 100.*nu_e/(vth_e * q95*Lbar);
      nu_istar = 100.*nu_i/(vth_i * q95*Lbar);
      output.write("Bootstrap current is included: \n");
      output.write("Normalized electron collisionality: nu_e* = %e\n", max(nu_estar));
      output.write("Normalized ion collisionality: nu_i* = %e\n", max(nu_istar));
      ft = BS_ft(100);
      output.write("modified collisional trapped particle fraction: ft = %e\n", max(ft));
      f31 = ft / (1.+(1.-0.1*ft)*sqrt(nu_estar) + 0.5*(1.-ft)*nu_estar/Zi);
      f32ee = ft / (1.+0.26*(1.-ft)*sqrt(nu_estar) + 0.18*(1.-0.37*ft)*nu_estar/sqrt(Zi));
      f32ei = ft / (1.+(1.+0.6*ft)*sqrt(nu_estar) + 0.85*(1.-0.37*ft)*nu_estar*(1.+Zi));
      f34 = ft / (1.+(1.-0.1*ft)*sqrt(nu_estar) + 0.5*(1.-0.5*ft)*nu_estar/Zi);

      L31 = F31(f31) ;
      L32 = F32ee(f32ee)+F32ei(f32ei) ;
      L34 = F31(f34) ;

      BSal0 = - (1.17*(1.-ft))/(1.-0.22*ft-0.19*ft*ft);
      BSal = (BSal0+0.25*(1-ft*ft)*sqrt(nu_istar))/(1.+0.5*sqrt(nu_istar)) + 0.31*nu_istar*nu_istar*ft*ft*ft*ft*ft*ft;
      BSal *= 1./(1.+0.15*nu_istar*nu_istar*ft*ft*ft*ft*ft*ft);

      Jpar_BS0 = L31* DDX(P0)/Pe0 + L32*DDX(Te0)/Te0 + L34*DDX(Ti0)/(Zi*Te0)*BSal;
      Jpar_BS0 *= Field3D( -Rxy*Btxy*Pe0/(B0*B0)*(MU0*KB*Nbar*density*Tebar*eV_K)/(Bbar*Bbar) );

      dump.add(Jpar_BS0, "jpar_BS0", 0);
      dump.add(L31, "L31", 0);
      dump.add(L32, "L32", 0);
      dump.add(L34, "L34", 0);
    }
	
  /**************** CALCULATE METRICS ******************/

  mesh->g11 = (Rxy*Bpxy)^2;
  mesh->g22 = 1.0 / (hthe^2);
  mesh->g33 = (I^2)*mesh->g11 + (B0^2)/mesh->g11;
  mesh->g12 = 0.0;
  mesh->g13 = -I*mesh->g11;
  mesh->g23 = -Btxy/(hthe*Bpxy*Rxy);
  
  mesh->J = hthe / Bpxy;
  mesh->Bxy = B0;
  
  mesh->g_11 = 1.0/mesh->g11 + ((I*Rxy)^2);
  mesh->g_22 = (B0*hthe/Bpxy)^2;
  mesh->g_33 = Rxy*Rxy;
  mesh->g_12 = Btxy*hthe*I*Rxy/Bpxy;
  mesh->g_13 = I*Rxy*Rxy;
  mesh->g_23 = Btxy*hthe*Rxy/Bpxy;
  
  mesh->geometry(); // Calculate quantities from metric tensor

  // Set B field vector
  
  B0vec.covariant = false;
  B0vec.x = 0.;
  B0vec.y = Bpxy / hthe;
  B0vec.z = 0.;

  // Set V0vec field vector
  
  V0vec.covariant = false;
  V0vec.x = 0.;
  V0vec.y = Vp0 / hthe;
  V0vec.z = Vt0 / Rxy;

  // Set V0eff field vector

  V0eff.covariant = false;
  V0eff.x = 0.;
  V0eff.y = -(Btxy/(B0*B0))*(Vp0*Btxy-Vt0*Bpxy) / hthe;
  V0eff.z =  (Bpxy/(B0*B0))*(Vp0*Btxy-Vt0*Bpxy) / Rxy;

  /**************** SET VARIABLE LOCATIONS *************/

  P.setLocation(CELL_CENTRE);
  U.setLocation(CELL_CENTRE);
  phi.setLocation(CELL_CENTRE);
  Psi.setLocation(CELL_YLOW);
  Jpar.setLocation(CELL_YLOW);

  Ni.setLocation(CELL_YLOW);
  Ti.setLocation(CELL_CENTRE);
  Te.setLocation(CELL_CENTRE);

  Vipar.setLocation(CELL_YLOW);
  Vepar.setLocation(CELL_YLOW);
  Pi.setLocation(CELL_CENTRE);
  Pe.setLocation(CELL_CENTRE);

  N_tmp.setLocation(CELL_CENTRE);
  if (nonlinear)
    {
      Ti_tmp.setLocation(CELL_CENTRE);
      Te_tmp.setLocation(CELL_CENTRE);
    }  

  Pe.setBoundary("P");
  Pi.setBoundary("P");

  /**************** SET EVOLVING VARIABLES *************/

  // Tell BOUT which variables to evolve
  SOLVE_FOR(U);
  SOLVE_FOR(Ni);
  SOLVE_FOR(Ti);
  SOLVE_FOR(Te);


  output.write("Solving for Psi, Differentiating to get jpar\n");
  SOLVE_FOR(Psi);
  dump.add(Jpar, "jpar", 1);

  dump.add(P, "P", 1);
  
  if(parallel_lagrange) {
    // Evolving the distortion of the flux surfaces (Ideal-MHD only!)
    
    bout_solve(Xip_x, "Xip_x");
    bout_solve(Xip_z, "Xip_z");
    
    bout_solve(Xim_x, "Xim_x");
    bout_solve(Xim_z, "Xim_z");
  }
  
  if(parallel_project) {
    // Add Xi to the dump file
    dump.add(Xip_x, "Xip_x", 1);
    dump.add(Xip_z, "Xip_z", 1);
    
    dump.add(Xim_x, "Xim_x", 1);
    dump.add(Xim_z, "Xim_z", 1);
  }

  if(compress0) {
    SOLVE_FOR(Vipar);
  }

  if(phi_constraint) {
    // Implicit Phi solve using IDA
    
    if(!bout_constrain(phi, C_phi, "phi")) {
      output.write("ERROR: Cannot constrain. Run again with phi_constraint=false\n");
      bout_error("Aborting.\n");
    }
    
  }else {
    // Phi solved in RHS (explicitly)
    dump.add(phi, "phi", 1);

  }

  // Diamagnetic phi0
  if(diamag && diamag_phi0) {
    // Stationary equilibrium plasma. ExB velocity balances diamagnetic drift
    phi0 = -Upara0*Pi0/B0/N0;
    //Field2D pth;
    //pth = - Upara0/N0 * ( Delp2(Pi0) - Grad(N0)*Grad(Pi0)/N0 );
    //pth.setBoundary("Psi");
    //phi0 = Invert_laplace2(pth, phi_flags)/B0;
    SAVE_ONCE(phi0);
  }

  // Add some equilibrium quantities and normalisations
  // everything needed to recover physical units
  SAVE_ONCE2(J0, P0);
  SAVE_ONCE4(density, Lbar, Bbar, Tbar);
  SAVE_ONCE3(Tibar, Tebar, Nbar);
  SAVE_ONCE2(Va, B0);
  SAVE_ONCE3(Ti0, Te0, N0);

  /////////////// CHECK VACUUM ///////////////////////
  // In vacuum region, initial vorticity should equal zero
  
  if(!restarting) {
    // Only if not restarting: Check initial perturbation

    // Set U to zero where P0 < vacuum_pressure
    U = where(P0 - vacuum_pressure, U, 0.0);

    //    Field2D lap_temp = 0.0;
    Field2D logn0 = laplace_alpha * N0;
    Field3D ubyn;
    Field3D Ntemp;
    Ntemp = N0;
    ubyn = U*B0/Ntemp;
    // Phi should be consistent with U
    if(laplace_alpha <= 0.0)
      phi = invert_laplace(ubyn, phi_flags, NULL)/B0;
    else
      phi = invert_laplace(ubyn, phi_flags, NULL, &logn0, NULL)/B0;
  }

  /************** SETUP COMMUNICATIONS **************/
  
  comms.add(U);
  comms.add(Psi);
  comms.add(phi);
  comms.add(Ni);
  comms.add(Ti);
  comms.add(Te);

  if (compress0)
    {
      comms.add(Vipar);
      Vepar.setBoundary("Vipar");
    }

  if (diffusion_u4 > 0.0)
    tmpA2.setBoundary("J");

  if (diffusion_n4 > 0.0)
    tmpN2.setBoundary("Ni");

  if (diffusion_ti4 > 0.0)
    tmpTi2.setBoundary("Ti");
 
  if (diffusion_te4 > 0.0)
    tmpTe2.setBoundary("Te");

  if (diffusion_v4 > 0.0)
    tmpVp2.setBoundary("Vipar");

  phi.setBoundary("phi"); // Set boundary conditions

  P.setBoundary("P");
  Jpar.setBoundary("J");
  Jpar2.setBoundary("J");

  return 0;
}

// Parallel gradient along perturbed field-line
const Field3D Grad_parP(const Field3D &f, CELL_LOC loc = CELL_DEFAULT)
{
  Field3D result;
  
  if(parallel_lagrange || parallel_project) {
    // Moving stencil locations
    
    Field3D fp, fm; // Interpolated on + and - y locations
    
    fp = interpolate(f, Xip_x, Xip_z);
    fm = interpolate(f, Xim_x, Xim_z);
    
    result.allocate();
    for(int i=0;i<mesh->ngx;i++)
      for(int j=1;j<mesh->ngy-1;j++)
	for(int k=0;k<mesh->ngz-1;k++) {
	  result[i][j][k] = (fp[i][j+1][k] - fm[i][j-1][k])/(2.*mesh->dy[i][j]*sqrt(mesh->g_22[i][j]));
	}
  }else {
    if(parallel_lr_diff) {
      // Use left/right biased stencils. NOTE: First order only!
      if(loc == CELL_YLOW) {
	result = Grad_par_CtoL(f);
      }else
	result = Grad_par_LtoC(f);
    }else
      result = Grad_par(f, loc);
    
    if(nonlinear) {
       result -= bracket(Psi, f, bm_mag)*B0;
    }
  }

  return result;
}

bool first_run = true; // For printing out some diagnostics first time around

int physics_run(BoutReal t)
{
 
  // Inversion
  Pi = Ni*Ti0 + N0 * Ti;
  if(nonlinear)
    Pi +=  Ni*Ti;
  mesh->communicate(Pi);

  Pe = Zi * (Ni*Te0 + N0 * Te);
  if(nonlinear)
    Pe +=  Zi * Ni * Te;
  mesh->communicate(Pe);

  P = Tau_ie*Pi + Pe;
  mesh->communicate(P);

  BoutReal N_tmp1;
  N_tmp1 = Low_limit;
  N_tmp = field_larger(N0+Ni,  N_tmp1);

  BoutReal Te_tmp1, Ti_tmp1;
  Te_tmp1 = Low_limit;
  Ti_tmp1 = Low_limit;
      
  Ti_tmp = field_larger(Ti0+Ti, Ti_tmp1);
  Te_tmp = field_larger(Te0+Te, Te_tmp1);
  
  //output.write("I see you 1! \n");//xia
    
  // Transitions from 0 in core to 1 in vacuum
  if(nonlinear) 
    {
      vac_mask = (1.0 - tanh( ((P0 + P) - vacuum_pressure) / vacuum_trans )) / 2.0;
      // Update resistivity
      if(spitzer_resist) {
      // Use Spitzer formula
	eta_spitzer = 0.51*1.03e-4*Zi*LnLambda*((Te_tmp*Tebar)^(-1.5)); // eta in Ohm-m. ln(Lambda) = 20
	eta_spitzer /= MU0 *Va * Lbar;
	//eta_spitzer.applyBoundary();
	//mesh->communicate(eta_spitzer);
      }
      else {
	eta = core_resist + (vac_resist - core_resist) * vac_mask;
      }

      nu_e = 2.91e-6*LnLambda*(N_tmp*Nbar*density/1.e6)*((Te_tmp*Tebar)^(-1.5)); // nu_e in 1/S.
      //nu_e.applyBoundary();
      //mesh->communicate(nu_e);

      if (diffusion_par >0.0 || diffusion_perp > 0.0)
	{

	  //xqx addition, begin
	  // Use Spitzer thermal conductivities
	  
	  nu_i = 4.80e-8*(Zi*Zi*Zi*Zi/sqrt(AA))*LnLambda*(N_tmp*Nbar*density/1.e6)*((Ti_tmp*Tibar)^(-1.5)); // nu_i in 1/S.
	  //nu_i.applyBoundary();
	  //mesh->communicate(nu_i);	  

	  vth_i = 9.79e3*sqrt(Ti_tmp*Tibar/AA); // vth_i in m/S.
	  //vth_i.applyBoundary();
	  //mesh->communicate(vth_i);
	  vth_e = 4.19e5*sqrt(Te_tmp*Tebar);    // vth_e in m/S.
	  //vth_e.applyBoundary();
	  //mesh->communicate(vth_e);
	}
	  
      if (diffusion_par >0.0)
	{
	  kappa_par_i=3.9*vth_i*vth_i/nu_i;// * 1.e4;
	  kappa_par_e=3.2*vth_e*vth_e/nu_e;// * 1.e4;
	  
	  Field3D kappa_par_i_fl, kappa_par_e_fl;

	  kappa_par_i_fl = vth_i * (q95 * Lbar);// * 1.e2;
	  kappa_par_e_fl = vth_e * (q95 * Lbar);// * 1.e2;
	  
	  kappa_par_i *= kappa_par_i_fl / (kappa_par_i + kappa_par_i_fl);
	  kappa_par_i *= Tipara1*N_tmp;
	  //kappa_par_i.applyBoundary();
	  //mesh->communicate(kappa_par_i);
	  kappa_par_e *= kappa_par_e_fl / (kappa_par_e + kappa_par_e_fl);
	  kappa_par_e *= Tepara1*N_tmp/Zi;
	  //kappa_par_e.applyBoundary();
	  //mesh->communicate(kappa_par_e);
	}

      if (diffusion_perp >0.0)
	{

	  kappa_perp_i=2.0*vth_i*vth_i*nu_i/(omega_ci*omega_ci);// * 1.e4;
	  kappa_perp_e=4.7*vth_e*vth_e*nu_e/(omega_ce*omega_ce);// * 1.e4;

	  Field3D kappa_perp_i_fl, kappa_perp_e_fl;

	  kappa_perp_i_fl = vth_i * (q95 * Lbar);// * 1.e4;
	  kappa_perp_e_fl = vth_e * (q95 * Lbar);// * 1.e4;
	  
	  kappa_perp_i *= kappa_perp_i_fl / (kappa_perp_i + kappa_perp_i_fl);
	  kappa_perp_i *= Tipara1;
	  //kappa_perp_i.applyBoundary();
	  //mesh->communicate(kappa_perp_i);
	  kappa_perp_e *= kappa_perp_e_fl / (kappa_perp_e + kappa_perp_e_fl);
	  kappa_perp_e *= Tepara1;
	  //kappa_perp_e.applyBoundary();
	  //mesh->communicate(kappa_perp_e);
	}
    }

  //  Field2D lap_temp=0.0;
  Field2D logn0 = laplace_alpha * N0;
  Field3D ubyn = U*B0/N0;
  if (diamag)
    ubyn -= Upara0/N0 * Delp2(Pi);
  // Invert laplacian for phi
  if(laplace_alpha <= 0.0)
    phi = invert_laplace(ubyn, phi_flags, NULL)/B0;
  else
    phi = invert_laplace(ubyn, phi_flags, NULL, &logn0, NULL)/B0;
    
    // Apply a boundary condition on phi for target plates
  //phi.applyBoundary();
    //  }
  // Perform communications
  mesh->communicate(comms);
  //output.write("I see you 2! \n");//xia

  Jpar = -Delp2(Psi);
  
  Jpar.applyBoundary();

  if (compress0 && (diffusion_par > 0.0) && (gamma_i_BC > 0.0) && (gamma_e_BC > 0.0) )
    {
      
      c_se = sqrt( abs(Tau_ie*Ti_tmp+Te_tmp) ) - sqrt( abs(Tau_ie*Ti0+Te0) );
      c_se*= sqrt(KB*Tebar*eV_K / Mi )/Va;
      
      if (nonlinear)
	{
	  //phi_sh = log( (Jpar/(N_tmp*ee - c_se*Va) * 2.*sqrt(PI)/( vth_e*Va ) * KB*Te/ee;
	  //phi_sh *= c_se - vth_e * exp(-ee*Bbar*B0*(phi0+phi)*Va*Lbar /(KB*Tebar*Te_tmp));
	  /*phi_sh = 1.0 - ( 2.0*sqrt(PI)/(vth_e*Va) * ( Jpar/(ee*N_tmp*Nbar*density) - c_se*Va ) );
	  phi_sh *= KB * Te*Tebar*eV_K / ee;
	  phi_sh /= Lbar*Va*B0*Bbar; */
	  Jpar_sh = N_tmp*Nbar*density*ee;
	  Jpar_sh *= c_se*Va - vth_e/(2.0*sqrt(PI)) * exp( - ee*(phi*Va*Lbar*B0*Bbar)/(KB*Te_tmp*Tebar*eV_K) );
	  Jpar_sh *= MU0*Lbar/(B0*Bbar);
	}
      else
	{
	  Jpar_sh = N0*Nbar*density*ee;
	  Jpar_sh *= c_se*Va - vth_e/(2.0*sqrt(PI)) * exp( - ee*(phi*Va*Lbar*B0*Bbar)/(KB*Te0*Tebar*eV_K) );
	  Jpar_sh *= MU0*Lbar/(B0*Bbar);
	}

      q_se = -gamma_e_BC * Pe * c_se / kappa_par_e * (Nbar*density*KB);
      q_si = -gamma_i_BC * Pi * c_se / kappa_par_i * (Nbar*density*KB);      

      //sbc--xia
      SBC_Dirichlet(Jpar, Jpar_sh);
    }
  mesh->communicate(Jpar);
  
  if(jpar_bndry_width > 0) {
      // Zero j in boundary regions. Prevents vorticity drive
      // at the boundary
      
    for(int i=0;i<jpar_bndry_width;i++)
      for(int j=0;j<mesh->ngy;j++)
	for(int k=0;k<mesh->ngz-1;k++) {
	  if(mesh->firstX())
	    Jpar[i][j][k] = 0.0;
	  if(mesh->lastX())
	    Jpar[mesh->ngx-1-i][j][k] = 0.0;
	}
  }

    // Smooth j in x
  if(smooth_j_x)
    Jpar = smooth_x(Jpar);

  if (compress0)
    {
      if (first_run)
	Vipar = 0.0;
      if (nonlinear)
	Vepar = Vipar - B0 * (Jpar) / N_tmp * Vepara;
      else	
	Vepar = Vipar - B0 * (Jpar) / N0 * Vepara;
      Vepar.applyBoundary();
      mesh->communicate(Vepar);
    }

    //xqx begin
    // Get Delp2(J) from J
  Jpar2 = -Delp2(Jpar);

  Jpar2.applyBoundary();
  mesh->communicate(Jpar2);

  if(jpar_bndry_width > 0) 
    {
      // Zero jpar2 in boundary regions. Prevents vorticity drive
      // at the boundary
      
    for(int i=0;i<jpar_bndry_width;i++)
      for(int j=0;j<mesh->ngy;j++)
	for(int k=0;k<mesh->ngz-1;k++) {
	  if(mesh->firstX())
	    Jpar2[i][j][k] = 0.0;
	  if(mesh->lastX())
	    Jpar2[mesh->ngx-1-i][j][k] = 0.0;
	}
    }
  //output.write("I see you 3! \n");//xia

  if (compress0 && (diffusion_par > 0.0) && (gamma_i_BC > 0.0) && (gamma_e_BC > 0.0) )
    {
      SBC_Dirichlet(Vipar, c_se);
      SBC_Gradpar(U, 0.0);
      SBC_Gradpar(Ni, 0.0);
      SBC_Gradpar(Ti, q_si);
      SBC_Gradpar(Te, q_se);
    }

  ////////////////////////////////////////////////////
  // Parallel electric field

  ddt(Psi) = 0.0;

  if (spitzer_resist)
    ddt(Psi) = -Grad_parP(B0*phi, CELL_CENTRE) / B0 - eta_spitzer*Jpar;
  else    
    ddt(Psi) = -Grad_parP(B0*phi, CELL_CENTRE) / B0 - eta*Jpar;

  if(diamag){
    ddt(Psi) -= bracket(B0*phi0, Psi, bm_exb);   // Equilibrium flow
  }

  if(thermal_force) 
    {
      // grad_par(T_e) correction 
      ddt(Psi) += 0.71 * Psipara1 * Grad_parP(Te, CELL_YLOW) / B0;
      ddt(Psi) -= 0.71 * Psipara1 * bracket(Psi, Te0, bm_mag);
    }

  if(eHall) 
    {
      ddt(Psi) +=  Psipara1 * Grad_parP(Pe, CELL_YLOW) / B0 / N0;
      ddt(Psi) -=  Psipara1 * bracket(Psi, Pe0, bm_mag) / N0;
    }

    // Hyper-resistivity
  if(hyperresist > 0.0) {
    ddt(Psi) += hyperresist * Delp2(Jpar);
  }
  //output.write("I see you 4! \n");//xia
  ////////////////////////////////////////////////////
  // Vorticity equation

  ddt(U) = 0.0;

  ddt(U) = -(B0^2) * bracket(Psi, J0, bm_mag)*B0; // Grad j term
 
  ddt(U) += 2.0* Upara1 * b0xcv*Grad(P);  // curvature term

  ddt(U) += (B0^2)*Grad_parP(Jpar, CELL_CENTRE); // b dot grad j

  
  if(diamag)
    ddt(U) -= bracket(B0*phi0, U, bm_exb);   // Equilibrium flow


  if(nonlinear) 
    {
      ddt(U) -= bracket(B0*phi, U, bm_exb);    // Advection
      /*if (compress0)
	//ddt(U) -= Vipar*Grad_par(U);
	ddt(U) -= Vpar_Grad_par(Vipar, U);*/
    }
  
    //xqx: parallel hyper-viscous diffusion for vector potential
  if(diffusion_u4 > 0.0)
    {
      tmpA2 = Grad2_par2new(Psi);
      mesh->communicate(tmpA2);
      tmpA2.applyBoundary();
      ddt(U) -= diffusion_u4 * Grad2_par2new(tmpA2);
    }
  
  if(gyroviscous)
    {

      Dperp2Phi0 = Field3D(Delp2(B0*phi0));
      Dperp2Phi0.applyBoundary();
      mesh->communicate(Dperp2Phi0);

      Dperp2Phi = Delp2(B0*phi);
      Dperp2Phi.applyBoundary();
      mesh->communicate(Dperp2Phi);

      /*      GradPhi02 = Field3D( Grad(B0*phi0)*Grad(B0*phi0) / (B0*B0) );
      GradPhi02.applyBoundary();
      mesh->communicate(GradPhi02);

      GradparPhi02 = Field3D( Grad_par(B0*phi0)*Grad_par(B0*phi0) / (B0*B0) );
      GradparPhi02.applyBoundary();
      mesh->communicate(GradparPhi02);

      GradcPhi = Grad(B0*phi0)*Grad(B0*phi) / (B0*B0);
      GradcPhi.applyBoundary();
      mesh->communicate(GradcPhi);

      GradcparPhi = Grad_par(B0*phi0)*Grad_par(B0*phi) / (B0*B0);
      GradcparPhi.applyBoundary();
      mesh->communicate(GradcparPhi);*/

      GradPhi02 = Field3D( Grad_perp(B0*phi0)*Grad_perp(B0*phi0) / (B0*B0) );
      GradPhi02.applyBoundary();
      mesh->communicate(GradPhi02);

      GradcPhi = Grad_perp(B0*phi0)*Grad_perp(B0*phi) / (B0*B0);
      GradcPhi.applyBoundary();
      mesh->communicate(GradcPhi);

      Dperp2Pi0 = Field3D(Delp2(Pi0));
      Dperp2Pi0.applyBoundary();
      mesh->communicate(Dperp2Pi0);

      Dperp2Pi = Delp2(Pi);
      Dperp2Pi.applyBoundary();
      mesh->communicate(Dperp2Pi);

      bracketPhi0P = bracket(B0*phi0, Pi, bm_exb);
      bracketPhi0P.applyBoundary();
      mesh->communicate(bracketPhi0P);

      bracketPhiP0 = bracket(B0*phi, Pi0, bm_exb);
      bracketPhiP0.applyBoundary();
      mesh->communicate(bracketPhiP0);

      ddt(U) -= 0.5*Upara2*bracket(Pi, Dperp2Phi0, bm_exb)/B0;
      ddt(U) -= 0.5*Upara2*bracket(Pi0, Dperp2Phi, bm_exb)/B0;
      ddt(U) += 0.5*Upara3*B0*bracket(Ni, GradPhi02, bm_exb);
      ddt(U) += Upara3*B0*bracket(N0, GradcPhi, bm_exb);
      //ddt(U) -= 0.5*Upara3*B0*bracket(Ni, GradparPhi02, bm_exb);
      //ddt(U) -= Upara3*B0*bracket(N0, GradcparPhi, bm_exb);
      ddt(U) += 0.5*Upara2*bracket(B0*phi, Dperp2Pi0, bm_exb)/B0;
      ddt(U) += 0.5*Upara2*bracket(B0*phi0, Dperp2Pi, bm_exb)/B0;
      ddt(U) -= 0.5*Upara2*Delp2(bracketPhi0P)/B0;
      ddt(U) -= 0.5*Upara2*Delp2(bracketPhiP0)/B0;
      
      if (nonlinear)
	{

	  /*GradPhi2 = Grad(B0*phi)*Grad(B0*phi) / (B0*B0);
	  GradPhi2.applyBoundary();
	  mesh->communicate(GradPhi2);

	  GradparPhi2 = Grad_par(B0*phi)*Grad_par(B0*phi) / (B0*B0);
	  GradparPhi2.applyBoundary();
	  mesh->communicate(GradparPhi2);*/

	  GradPhi2 = Grad_perp(B0*phi)*Grad_perp(B0*phi) / (B0*B0);
	  GradPhi2.applyBoundary();
	  mesh->communicate(GradPhi2);

	  bracketPhiP = bracket(B0*phi, Pi, bm_exb);
	  bracketPhiP.applyBoundary();
	  mesh->communicate(bracketPhiP);
	  
	  ddt(U) -= 0.5*Upara2*bracket(Pi, Dperp2Phi, bm_exb)/B0;
	  ddt(U) += 0.5*Upara3*B0*bracket(N0, GradPhi2, bm_exb);
	  ddt(U) += Upara3*B0*bracket(Ni, GradcPhi, bm_exb);
	  //ddt(U) -= Upara3*B0*bracket(Ni, GradcparPhi, bm_exb);
	  ddt(U) += 0.5*Upara2*bracket(B0*phi, Dperp2Pi, bm_exb)/B0;
	  ddt(U) -= 0.5*Upara2*Delp2(bracketPhiP)/B0;
	}
    }

  // Viscosity terms 
  if(viscos_par > 0.0)
    ddt(U) += viscos_par * Grad2_par2(U); // Parallel viscosity
  
  if(hyperviscos > 0.0) {
    // Calculate coefficient.
    
    hyper_mu_x = hyperviscos * mesh->g_11*SQ(mesh->dx) * abs(mesh->g11*D2DX2(U)) / (abs(U) + 1e-3);
    hyper_mu_x.applyBoundary("dirichlet"); // Set to zero on all boundaries
    
    ddt(U) += hyper_mu_x * mesh->g11*D2DX2(U);
    
    if(first_run) { // Print out maximum values of viscosity used on this processor
      output.write("   Hyper-viscosity values:\n");
      output.write("      Max mu_x = %e, Max_DC mu_x = %e\n", max(hyper_mu_x), max(hyper_mu_x.DC()));
    }
  }


  // left edge sink terms 
  if(sink_Ul > 0.0){
    ddt(U) -=  sink_Ul*sink_tanhxl(P0,U,su_widthl,su_lengthl); // core sink
  }

  // right edge sink terms 
  if(sink_Ur > 0.0){
    ddt(U) -=  sink_Ur*sink_tanhxr(P0,U,su_widthr,su_lengthr); //  sol sink
  }

  //output.write("I see you 5! \n");//xia

  ///////////////////////////////////////////////
  // number density equation

  ddt(Ni) = 0.0;

  ddt(Ni) -=  bracket(B0*phi, N0, bm_exb);

  if (continuity)  
    {
      ddt(Ni) -= 2.0 * N0/B0 *  b0xcv*Grad(phi*B0);
      if (diamag)
	{
	  //ddt(Ni) -= 2.0 * Ni/B0 *  b0xcv*Grad(phi0*B0);
	  ddt(Ni) -= 2.0 * Nipara1 * b0xcv*Grad(Pi) / B0;
	}
      if (nonlinear)
	{
	  ddt(Ni) -= 2.0 * Ni/B0 * b0xcv*Grad(phi*B0);
	}
    }

  if(diamag)
    ddt(Ni) -= bracket(B0*phi0, Ni, bm_exb);   // Equilibrium flow

  if(nonlinear) {
    ddt(Ni) -= bracket(B0*phi, Ni, bm_exb);    // Advection  
  }

  if (compress0)
    {
      //ddt(Ni) -= Vipar * Grad_parP(N0, CELL_YLOW);
      //ddt(Ni) -= Vpar_Grad_par(Vipar, N0);

      if (continuity)
	{
	  ddt(Ni) -= N0 * B0 * Grad_parP(Vipar/B0, CELL_CENTRE);
	}
      if (nonlinear)
	{
	  //ddt(Ni) -= Vipar * Grad_par(Ni, CELL_YLOW);

	  //ddt(Ni) -= Vpar_Grad_par(Vipar, Ni);
	  //ddt(Ni) += Vipar * bracket(Psi, N0, bm_mag)*B0;

	  if (continuity)
	    ddt(Ni) -= Ni * B0 * Grad_par(Vipar/B0, CELL_CENTRE);
	}
    }


  //M: 4th order Parallel diffusion terms 
  if(diffusion_n4 > 0.0){
    tmpN2 = Grad2_par2new(Ni);
    mesh->communicate(tmpN2);
    tmpN2.applyBoundary();
    ddt(Ni) -= diffusion_n4 * Grad2_par2new(tmpN2);}


  //output.write("I see you 6! \n");//xia

  ///////////////////////////////////////////////                                // ion temperature equation                                                    

  ddt(Ti) = 0.0;

  ddt(Ti) -= bracket(B0*phi, Ti0, bm_exb);

  if (continuity)
    {
      ddt(Ti) -= 4.0/3.0 * Ti0/B0 * b0xcv*Grad(phi*B0);
      if (diamag)
	ddt(Ti) -= 4.0/3.0 * Tipara2 * Ti0/N0 * b0xcv*Grad(Pi) / B0;
      if (nonlinear)
	{
	  ddt(Ti) -= 4.0/3.0 * Ti/B0 * b0xcv*Grad(phi*B0);
	  if (diamag)
	    ddt(Ti) -= 4.0/3.0 * Tipara2 * Ti/N0 * b0xcv*Grad(Pi) / B0;
	}
    }

  if (energy_flux)
    {
      //ddt(Ti) -= 10.0/3.0 * Tipara2 * Ti0/B0 * b0xcv*Grad(Ti);
      ddt(Ti) -= 10.0/3.0 * Tipara2/B0 * V_dot_Grad(Ti0*b0xcv, Ti);
      ddt(Ti) -= 10.0/3.0 * Tipara2 * Ti/B0 * b0xcv*Grad(Ti0);
      if (nonlinear)
	//ddt(Ti) -= 10.0/3.0 * Tipara2 * Ti/B0 * b0xcv*Grad(Ti);
	ddt(Ti) -= 10.0/3.0 * Tipara2/B0 * V_dot_Grad(Ti*b0xcv, Ti);	
    }

  if(diamag)
    ddt(Ti) -= bracket(phi0*B0, Ti, bm_exb);   // Equilibrium flow

  if(nonlinear) {
    ddt(Ti) -= bracket(phi*B0, Ti, bm_exb);    // Advection  
  }

  if (compress0)
    {
      //ddt(Ti) -= Vipar * Grad_parP(Ti0, CELL_YLOW);
      //ddt(Ti) -= Vpar_Grad_par(Vipar, Ti0);

      if (continuity)
	{
	  ddt(Ti) -= 2.0/3.0 * Ti0 * B0 * Grad_parP(Vipar/B0, CELL_CENTRE);
	}
      if (nonlinear)
	{
	  //ddt(Ti) -= Vipar * Grad_par(Ti, CELL_YLOW);

	  //ddt(Ti) -= Vpar_Grad_par(Vipar, Ti);
	  //ddt(Ti) += Vipar * bracket(Psi, Ti0, bm_mag)*B0;

	  if (continuity)
	    ddt(Ti) -= 2.0/3.0 * Ti * B0 * Grad_par(Vipar/B0, CELL_CENTRE);
	}
    }

  if (energy_exch)
    {
      ddt(Ti) += 2.0 * Zi * Tbar * nu_e / 1836.0 * (Te-Ti);
    }

  if(diffusion_par > 0.0)
    {
      ddt(Ti) += kappa_par_i * Grad2_par2(Ti)/N0; // Parallel diffusion
      ddt(Ti) += Grad_par(kappa_par_i, CELL_CENTRE) * Grad_par(Ti, CELL_YLOW)/N0;
    }

  if(diffusion_perp > 0.0)
    {
      ddt(Ti) += kappa_perp_i * Delp2(Ti); // Parallel diffusion
      ddt(Ti) += Grad_perp(kappa_perp_i) * Grad_perp(Ti);
    }

  //M: 4th order Parallel diffusion terms 
  if(diffusion_ti4 > 0.0){
    tmpTi2 = Grad2_par2new(Ti);
    mesh->communicate(tmpTi2);
    tmpTi2.applyBoundary();
    ddt(Ti) -= diffusion_ti4 * Grad2_par2new(tmpTi2);}

  //output.write("I see you 7! \n");//xia
  ///////////////////////////////////////////////                                // electron temperature equation                                                    
  ddt(Te) = 0.0;

  ddt(Te) -= bracket(B0*phi, Te0, bm_exb);

  if (continuity)
    {
      ddt(Te) -= 4.0/3.0 * Te0/B0 *  b0xcv*Grad(phi*B0);
      if (diamag)
	ddt(Te) += 4.0/3.0 * Tepara2  * Te0/Ne0 *  b0xcv*Grad(Pe) / B0;
      if (nonlinear)
	{
	  ddt(Te) -= 4.0/3.0 * Te/B0 * b0xcv*Grad(phi*B0);
	  if (diamag)
	    ddt(Te) += 4.0/3.0 * Tepara2 * Te/N0 * b0xcv*Grad(Pe) / B0;
	}
    }

  if (energy_flux)
    {
      //ddt(Te) += 10.0/3.0 * Tepara2 * Te0/B0 * b0xcv*Grad(Te); 
      ddt(Te) -= 10.0/3.0 * Tepara2/B0 * V_dot_Grad(-Te0*b0xcv, Te);
      ddt(Te) += 10.0/3.0 * Tepara2 * Te/B0 * b0xcv*Grad(Te0);
      if (thermal_force)
	{
	  ddt(Te) += 0.71 * 2.0/3.0 * Tepara3 * Te0 * B0 / Ne0 * Grad_parP(Jpar, CELL_CENTRE);
	  ddt(Te) -= 0.71 * 2.0/3.0 * Tepara3 * Te0 * B0 / Ne0 * bracket(Psi, J0, bm_mag) * B0;
	  ddt(Te) += 0.71 * 2.0/3.0 * Tepara3 * Te * B0 / Ne0 * Grad_parP(J0, CELL_CENTRE);	

	}
      if (nonlinear)
	{
	  //ddt(Te) += 10.0/3.0 * Tepara2 * Te/B0 * b0xcv*Grad(Te);
	  ddt(Te) -= 10.0/3.0 * Tepara2/B0 * V_dot_Grad(-Te*b0xcv, Te);
	  if (thermal_force)
	    ddt(Te) += 0.71 * 2.0/3.0 * Tepara3 * Te * B0 / Ne0 * Grad_par(Jpar, CELL_CENTRE);
	}
    }

  if(diamag)
    ddt(Te) -= bracket(B0*phi0, Te, bm_exb);   // Equilibrium flow

  if(nonlinear) {
    ddt(Te) -= bracket(B0*phi, Te, bm_exb);    // Advection
  }

  if (compress0)
    {
      //ddt(Te) -= Vepar * Grad_parP(Te0, CELL_YLOW);
      //ddt(Te) -= Vpar_Grad_par(Vepar, Te0);

      if (continuity)
	{
	  ddt(Te) -= 2.0/3.0 * Te0 * B0 * Grad_parP(Vepar/B0, CELL_CENTRE);
	}
      if (nonlinear)
	{
	  //ddt(Te) -= Vepar * Grad_par(Te, CELL_YLOW);

	  //ddt(Te) -= Vpar_Grad_par(Vepar, Te);
	  //ddt(Te) += Vepar * bracket(Psi, Te0, bm_mag)*B0;

	  if (continuity)
	    ddt(Te) -= 2.0/3.0 * Te * B0 * Grad_par(Vepar/B0, CELL_CENTRE);
	}
    }

  if (energy_exch)
    {
      ddt(Te) -= 2.0 * Tbar * nu_e / 1836.0 * (Te-Ti);
      if (spitzer_resist)
        ddt(Te) += 4.0/3.0 * Tepara4 * eta_spitzer * B0*B0 * J0 * Jpar / Ne0;
      else
	ddt(Te) += 4.0/3.0 * Tepara4 * eta * B0*B0 * J0 * Jpar / Ne0;
      if (nonlinear)
	{
	  if (spitzer_resist)
	    ddt(Te) += 2.0/3.0 * Tepara4 * eta_spitzer * B0*B0 * Jpar * Jpar / Ne0;
	  else
	    ddt(Te) += 2.0/3.0 * Tepara4 * eta * B0*B0 * Jpar * Jpar / Ne0;
	}
    }


  if(diffusion_par > 0.0)
    {
      ddt(Te) += kappa_par_e * Grad2_par2(Te)/N0; // Parallel diffusion
      ddt(Te) += Grad_par(kappa_par_e, CELL_CENTRE) * Grad_par(Te, CELL_YLOW)/N0;
    }

  if(diffusion_perp > 0.0)
    {
      ddt(Te) += kappa_perp_e * Delp2(Te); // Parallel diffusion
      ddt(Te) += Grad_perp(kappa_perp_e) * Grad_perp(Te);
    }

  if(diffusion_te4 > 0.0){
    tmpTe2 = Grad2_par2new(Te);
    mesh->communicate(tmpTe2);
    tmpTe2.applyBoundary();
    ddt(Te) -= diffusion_te4 * Grad2_par2new(tmpTe2);}

  //output.write("I see you 8! \n");//xia

  //////////////////////////////////////////////////////////////////////
  if (compress0)   //parallel velocity equation
    {
      ddt(Vipar) = 0.0;
      
      ddt(Vipar) -= Vipara * Grad_parP(P, CELL_YLOW) / N0;
      ddt(Vipar) += Vipara * bracket(Psi, P0, bm_mag) * B0 / N0;

      if (diamag)
	ddt(Vipar) -= bracket(B0*phi0, Vipar, bm_exb); 

      if (nonlinear)
	{
	  ddt(Vipar) -= bracket(B0*phi, Vipar, bm_exb);

	  //ddt(Vipar) -= Vipar * Grad_par(Vipar, CELL_CENTRE);
	  //ddt(Vipar) -= Vpar_Grad_par(Vipar, Vipar);
	}

      //xqx: parallel hyper-viscous diffusion for vector potential
      if(diffusion_v4 > 0.0)
	{
	  tmpVp2 = Grad2_par2new(Vipar);
	  mesh->communicate(tmpVp2);
	  tmpVp2.applyBoundary();
	  ddt(Vipar) -= diffusion_v4 * Grad2_par2new(tmpVp2);
	}

    }

  ///////////////////////////////////////////////////////////////////////

  if(filter_z) {
    // Filter out all except filter_z_mode
    
 
    ddt(Psi) = filter(ddt(Psi), filter_z_mode);
    
    ddt(U) = filter(ddt(U), filter_z_mode);

    ddt(Ni) = filter(ddt(Ni), filter_z_mode);

    ddt(Ti) = filter(ddt(Ti), filter_z_mode);

    ddt(Te) = filter(ddt(Te), filter_z_mode);

    if(compress0) 
      {
	ddt(Vipar) = filter(ddt(Vipar), filter_z_mode);
      }

  }

  //output.write("I see you 9! \n");//xia

  ///////////////////////////////////////////////////////////////////////

  if(low_pass_z > 0) {
    //Low-pass filter, keeping n up to low_pass_z
    ddt(Psi) = lowPass(ddt(Psi), low_pass_z, zonal_field);

    ddt(U) = lowPass(ddt(U), low_pass_z, zonal_flow);

    ddt(Ti) = lowPass(ddt(Ti), low_pass_z, zonal_bkgd);
    ddt(Te) = lowPass(ddt(Te), low_pass_z, zonal_bkgd);
    ddt(Ni) = lowPass(ddt(Ni), low_pass_z, zonal_bkgd);

    if(compress0) 
      {
	ddt(Vipar) = lowPass(ddt(Vipar), low_pass_z, zonal_bkgd);
      }
  }

  if(damp_width > 0) {
    for(int i=0;i<damp_width;i++) {
      for(int j=0;j<mesh->ngy;j++)
	for(int k=0;k<mesh->ngz;k++) {
	  if(mesh->firstX())
	    ddt(U)[i][j][k] -= U[i][j][k] / damp_t_const;
	  if(mesh->lastX())
	    ddt(U)[mesh->ngx-1-i][j][k] -= U[mesh->ngx-1-i][j][k] / damp_t_const;
	}
    }
  }

  if (filter_nl >0)
    {
      ddt(Ni) = nl_filter( ddt(Ni), filter_nl);
    }
  //output.write("I see you 10! \n");//xia

  first_run = false;

  return 0;
}

/****************BOUNDARY FUNCTIONS*****************************/
// Sheath Boundary Conditions on Phi
// Linearized
void SBC_Dirichlet(Field3D &var, const Field3D &value) //let the boundary equall to the value next to the boundary
{
  SBC_yup_eq(var, value);
  SBC_ydown_eq(var, value);
}

void SBC_Gradpar(Field3D &var, const Field3D &value)
{
  SBC_yup_Grad_par(var, value);
  SBC_ydown_Grad_par(var, value);
}

// Boundary to specified Field3D object
void SBC_yup_eq(Field3D &var, const Field3D &value)
{

  RangeIter* xrup = mesh->iterateBndryUpperY();

  for(xrup->first(); !xrup->isDone(); xrup->next())
    for(int jy=mesh->yend+1-Sheath_width; jy<mesh->ngy; jy++)
      for(int jz=0; jz<mesh->ngz; jz++) 
	{
	  var[xrup->ind][jy][jz] = value[xrup->ind][jy][jz];
	}
}

void SBC_ydown_eq(Field3D &var, const Field3D &value)
{

  RangeIter* xrdn = mesh->iterateBndryLowerY();

  for(xrdn->first(); !xrdn->isDone(); xrdn->next())
    for(int jy=mesh->ystart-1+Sheath_width; jy>=0; jy--)
      for(int jz=0; jz<mesh->ngz; jz++) 
	{
	  var[xrdn->ind][jy][jz] = value[xrdn->ind][jy][jz];
	}
}      

// Boundary gradient to specified Field3D object
void SBC_yup_Grad_par(Field3D &var, const Field3D &value)
{

  RangeIter* xrup = mesh->iterateBndryUpperY();

  for(xrup->first(); !xrup->isDone(); xrup->next())
    for(int jy=mesh->yend+1-Sheath_width; jy<mesh->ngy; jy++)
      for(int jz=0; jz<mesh->ngz; jz++) 
	{
	  var[xrup->ind][jy][jz] = var[xrup->ind][jy-1][jz] + mesh->dy[xrup->ind][jy]*sqrt(mesh->g_22[xrup->ind][jy])*value[xrup->ind][jy][jz];
	}
}

void SBC_ydown_Grad_par(Field3D &var, const Field3D &value)
{

  RangeIter* xrdn = mesh->iterateBndryLowerY();

  for(xrdn->first(); !xrdn->isDone(); xrdn->next())
    for(int jy=mesh->ystart-1+Sheath_width; jy>=0; jy--)
      for(int jz=0; jz<mesh->ngz; jz++) 
	{
	  var[xrdn->ind][jy][jz] = var[xrdn->ind][jy+1][jz] - mesh->dy[xrdn->ind][jy]*sqrt(mesh->g_22[xrdn->ind][jy])*value[xrdn->ind][jy][jz];
	}
}      

const Field3D BS_ft(const int index)
{
  Field3D result, result1;
  result.allocate();
  result1.allocate();
  result1=0.;
  
  BoutReal xlam, dxlam;
  dxlam = 1./Bbar/index;
  xlam = 0.;

  for(int i=0; i<index; i++)
    {
      result1 += xlam*dxlam/sqrt(1.-xlam*B0);
      xlam += dxlam;
    }
  result = 1.- 0.75*B0*B0 * result1;

  return(result);
}

const Field3D F31(const Field3D input)
{
  Field3D result;
  result.allocate();

  result = ( 1 + 1.4/(Zi+1.) ) * input;
  result -= 1.9/(Zi+1.) * input*input;
  result += 0.3/(Zi+1.) * input*input*input;
  result -= 0.2/(Zi+1.) * input*input*input*input;

  return(result);
}

const Field3D F32ee(const Field3D input)
{
  Field3D result;
  result.allocate();
  
  result = (0.05+0.62*Zi)/(Zi*(1+0.44*Zi))*(input-input*input*input*input);
  result +=1./(1.+0.22*Zi)*( input*input-input*input*input*input-1.2*(input*input*input-input*input*input*input) );
  result += 1.2/(1.+0.5*Zi)*input*input*input*input;

  return(result);
}

const Field3D F32ei(const Field3D input)
{
  Field3D result;
  result.allocate();
  
  result = (0.56+1.93*Zi)/(Zi*(1+0.44*Zi))*(input-input*input*input*input);
  result +=4.95/(1.+2.48*Zi)*( input*input-input*input*input*input-0.55*(input*input*input-input*input*input*input) );
  result -= 1.2/(1.+0.5*Zi)*input*input*input*input;

  return(result);
}

