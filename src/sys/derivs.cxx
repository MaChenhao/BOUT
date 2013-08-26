/**************************************************************************
 * Basic derivative methods
 *
 * 
 * Four kinds of differencing methods:
 * 
 * 1. First derivative DD*
 *    Central differencing e.g. Div(f)
 *
 * 2. Second derivatives D2D*2
 *    Central differencing e.g. Delp2(f)
 *
 * 3. Upwinding VDD*
 *    Terms like v*Grad(f)
 *
 * 4. Flux methods FDD* (e.g. flux conserving, limiting)
 *    Div(v*f)
 *
 **************************************************************************
 * Copyright 2010 B.D.Dudson, S.Farley, M.V.Umansky, X.Q.Xu
 *
 * Contact: Ben Dudson, bd512@york.ac.uk
 * 
 * This file is part of BOUT++.
 *
 * BOUT++ is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * BOUT++ is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with BOUT++.  If not, see <http://www.gnu.org/licenses/>.
 * 
 **************************************************************************/

#include <globals.hxx>
#include <derivs.hxx>
#include <stencils.hxx>
#include <utils.hxx>
#include <fft.hxx>
#include <interpolation.hxx>
#include <bout/constants.hxx>
#include <msg_stack.hxx>

#include <cmath>
#include <string.h>
#include <stdlib.h>

#include <output.hxx>

//#undef _OPENMP

#ifdef _OPENMP
#include <omp.h>
#endif

typedef BoutReal (*deriv_func)(stencil &); // f
typedef BoutReal (*upwind_func)(stencil &, stencil &); // v, f

/*******************************************************************************
 * Limiters
 *******************************************************************************/

/// Van Leer limiter. Used in TVD code
BoutReal VANLEER(BoutReal r) {
  return r + fabs(r)/(1.0 + fabs(r));
}

// Superbee limiter
BoutReal SUPERBEE(BoutReal r) {
  return BOUTMAX(0.0, BOUTMIN(2.*r, 1.0), BOUTMIN(r, 2.));
}

/*******************************************************************************
 * Basic derivative methods.
 * All expect to have an input grid cell at the same location as the output
 * Hence convert cell centred values -> centred values, or left -> left
 *******************************************************************************/

const BoutReal WENO_SMALL = 1.0e-8; // Small number for WENO schemes

////////////////////// FIRST DERIVATIVES /////////////////////

/// central, 2nd order
BoutReal DDX_C2(stencil &f) {
  return 0.5*(f.p - f.m);
}

/// central, 4th order
BoutReal DDX_C4(stencil &f) {
  return (8.*f.p - 8.*f.m + f.mm - f.pp)/12.;
}

/// Central WENO method, 2nd order (reverts to 1st order near shocks)
BoutReal DDX_CWENO2(stencil &f) {
  BoutReal isl, isr, isc; // Smoothness indicators
  BoutReal al, ar, ac, sa; // Un-normalised weights
  BoutReal dl, dr, dc; // Derivatives using different stencils
  
  dc = 0.5*(f.p - f.m);
  dl = f.c - f.m;
  dr = f.p - f.c;
  
  isl = SQ(dl);
  isr = SQ(dr);
  isc = (13./3.)*SQ(f.p - 2.*f.c + f.m) + 0.25*SQ(f.p-f.m);
  
  al = 0.25/SQ(WENO_SMALL + isl);
  ar = 0.25/SQ(WENO_SMALL + isr);
  ac = 0.5/SQ(WENO_SMALL + isc);
  sa = al + ar + ac;

  return (al*dl + ar*dr + ac*dc)/sa;
}

// Smoothing 2nd order derivative
BoutReal DDX_S2(stencil &f) {
  
  // 4th-order differencing
  BoutReal result = (8.*f.p - 8.*f.m + f.mm - f.pp)/12.;

  result += SIGN(f.c)*(f.pp - 4.*f.p + 6.*f.c - 4.*f.m + f.mm)/12.;
  
  return result;
}

///////////////////// SECOND DERIVATIVES ////////////////////

/// Second derivative: Central, 2nd order
BoutReal D2DX2_C2(stencil &f) {
  return f.p + f.m - 2.*f.c;
}

/// Second derivative: Central, 4th order
BoutReal D2DX2_C4(stencil &f) {
  return (-f.pp + 16.*f.p - 30.*f.c + 16.*f.m - f.mm)/12.;
}

//////////////////////// UPWIND METHODS ///////////////////////

/// Upwinding: Central, 2nd order
BoutReal VDDX_C2(stencil &v, stencil &f) {
  return v.c*0.5*(f.p - f.m);
}

/// Upwinding: Central, 4th order
BoutReal VDDX_C4(stencil &v, stencil &f) {
  return v.c*(8.*f.p - 8.*f.m + f.mm - f.pp)/12.;
}

/// upwind, 1st order
BoutReal VDDX_U1(stencil &v, stencil &f) {
  return v.c>=0.0 ? v.c*(f.c - f.m): v.c*(f.p - f.c);
}

/// upwind, 4th order
BoutReal VDDX_U4(stencil &v, stencil &f) {
  return v.c >= 0.0 ? v.c*(4.*f.p - 12.*f.m + 2.*f.mm + 6.*f.c)/12.
    : v.c*(-4.*f.m + 12.*f.p - 2.*f.pp - 6.*f.c)/12.;
}

/// TVD upwinding (2nd order)
/// WARNING WARNING : THIS TVD IMPLEMENTATION DOESN'T WORK PROPERLY
BoutReal VDDX_TVD(BoutReal vc, BoutReal vm, BoutReal vp, BoutReal fc, BoutReal fm, BoutReal fp, BoutReal fmm, BoutReal fpp) {
  BoutReal denom, res, ri, ri1, ri_1, fluxLeft, fluxRight;

  if (vc>=0.0){

    /*-smoothness indicator*/
    denom=fp-fc;
    if (fabs(denom) < 1e-20) denom=1e-20;
    ri=(fc-fm)/denom;

    denom=fc-fm;
    if (fabs(denom) < 1e-20) denom=1e-20;
    ri_1=(fm-fmm)/denom;

    /*;-nonlinear TVD flux*/
    fluxRight=fc + 0.5*VANLEER(ri)*(fp-fc);
    fluxLeft=fm + 0.5*VANLEER(ri_1)*(fc-fm);
    res = vc*(fluxRight-fluxLeft); /*divide by dx outside*/
    
  }
  else{

    /*;-smoothness indicator*/
    denom=fm-fc;
    if (fabs(denom) < 1e-20) denom=1e-20;
    ri=(fc-fp)/denom;
    
    denom=(fc-fp);
    if (fabs(denom) < 1e-20) denom=1e-20;
    ri1=(fp-fpp)/denom;

    /*;-nonlinear TVD flux*/
    fluxRight=(fp - 0.5*VANLEER(ri1)*(fp-fc));
    fluxLeft=(fc - 0.5*VANLEER(ri)*(fc-fm));
    res = vc*(fluxRight-fluxLeft); /*divide by dx outside*/    
  }
  return res;
}

/// 3rd-order WENO scheme
BoutReal VDDX_WENO3(stencil &v, stencil &f) {
  BoutReal deriv, w, r;

  if(v.c > 0.0) {
    // Left-biased stencil
    
    r = (WENO_SMALL + SQ(f.c - 2.0*f.m + f.mm)) / (WENO_SMALL + SQ(f.p - 2.0*f.c + f.m));
    w = 1.0 / (1.0 + 2.0*r*r);

    deriv = 0.5*(f.p - f.m) - 0.5*w*(-f.mm + 3.*f.m - 3.*f.c + f.p);
    
  }else {
    // Right-biased
    
    r = (WENO_SMALL + SQ(f.pp - 2.0*f.p + f.c)) / (WENO_SMALL + SQ(f.p - 2.0*f.c + f.m));
    w = 1.0 / (1.0 + 2.0*r*r);
    
    deriv = 0.5*(f.p - f.m) - 0.5*w*( -f.m + 3.*f.c - 3.*f.p + f.pp );
  }

  return v.c*deriv;
}

/// 3rd-order CWENO. Uses the upwinding code and split flux
BoutReal DDX_CWENO3(stencil &f) {
  BoutReal a, ma = fabs(f.c);
  // Split flux
  a = fabs(f.m); if(a > ma) ma = a;
  a = fabs(f.p); if(a > ma) ma = a;
  a = fabs(f.mm); if(a > ma) ma = a;
  a = fabs(f.pp); if(a > ma) ma = a;
  
  stencil sp, vp, sm, vm;
  
  vp.c = 0.5; vm.c = -0.5;
  
  sp = f + ma;
  sm = ma - f;
  
  return VDDX_WENO3(vp, sp) + VDDX_WENO3(vm, sm);
}


BoutReal VDDX_PPM(stencil &v, stencil &f) {

   return 0;

}
//////////////////////// FLUX METHODS ///////////////////////

BoutReal FDDX_U1(stencil &v, stencil &f) {
  // Velocity at lower end
  BoutReal vs = 0.5*(v.m + v.c);
  BoutReal result = (vs >= 0.0) ? vs * f.m : vs * f.c;
  // and at upper 
  vs = 0.5*(v.c + v.p);
  result -= (vs >= 0.0) ? vs * f.c : vs * f.p;

  return result;
}

BoutReal FDDX_C2(stencil &v, stencil &f) {
  return 0.5*(v.p*f.p - v.m*f.m);
}

BoutReal FDDX_C4(stencil &v, stencil &f) {
  return (8.*v.p*f.p - 8.*v.m*f.m + v.mm*f.mm - v.pp*f.pp)/12.;
}

/// Non-oscillatory, containing No free parameters and Dissipative (NND) scheme
/// http://arxiv.org/abs/1010.4135v1
BoutReal FDDX_NND(stencil &v, stencil &f) {
  // f{+-} i
  BoutReal fp = 0.5*(v.c + fabs(v.c))*f.c;
  BoutReal fm = 0.5*(v.c - fabs(v.c))*f.c;
  
  // f{+-} i+1
  BoutReal fp1 = 0.5*(v.p + fabs(v.p))*f.p;
  BoutReal fm1 = 0.5*(v.p - fabs(v.p))*f.p;
  
  // f{+-} i+2
  BoutReal fm2 = 0.5*(v.pp - fabs(v.pp))*f.pp;

  // f{+-} i-1
  BoutReal fp_1 = 0.5*(v.m + fabs(v.m))*f.m;
  BoutReal fm_1 = 0.5*(v.m - fabs(v.m))*f.m;
  
  // f{+-} i-2
  BoutReal fp_2 = 0.5*(v.mm + fabs(v.mm))*f.mm;

  // f^{LR} {i+1/2}
  BoutReal flp = fp  + 0.5*MINMOD(fp1 - fp, fp - fp_1);
  BoutReal frp = fm1 - 0.5*MINMOD(fm1 - fm, fm2 - fm1);
  
  // f^{LR} {i-1/2}
  BoutReal flm = fp_1  + 0.5*MINMOD(fp - fp_1, fp_1 - fp_2);
  BoutReal frm = fm - 0.5*MINMOD(fm - fm_1, fm1 - fm);
    
  // h{+-}
  BoutReal hp = flp + frp;
  BoutReal hm = flm + frm; 
  
  return hp - hm;
}

//////////////////////// MUSCL scheme ///////////////////////

void DDX_KT_LR(const stencil &f, BoutReal &fLp, BoutReal &fRp, BoutReal &fLm, BoutReal &fRm) {
  // Limiter functions
  BoutReal phi   = SUPERBEE( (f.c - f.m) / (f.p - f.c) );
  BoutReal phi_m = SUPERBEE( (f.m - f.mm) / (f.c - f.m) );
  BoutReal phi_p = SUPERBEE( (f.p - f.c) / (f.pp - f.p) );

  fLp = f.c + 0.5*phi*(f.p - f.c);
  fRp = f.p - 0.5*phi_p*(f.pp - f.p);

  fLm = f.m + 0.5*phi_m*(f.c - f.m);
  fRm = f.c - 0.5*phi*(f.p - f.c);
}

// du/dt = d/dx(f)  with maximum local velocity Vmax
BoutReal DDX_KT(const stencil &f, const stencil &u, const BoutReal Vmax) {
  BoutReal uLp, uRp, uLm, uRm;
  BoutReal fLp, fRp, fLm, fRm;
  
  DDX_KT_LR(u, uLp, uRp, uLm, uRm);
  DDX_KT_LR(f, fLp, fRp, fLm, fRm);
  
  BoutReal Fm = 0.5*( fRm + fLm - Vmax*(uRm - uLm));
  BoutReal Fp = 0.5*( fRp + fLp - Vmax*(uRp - uLp));
  
  return Fm - Fp;
}

/*******************************************************************************
 * Staggered differencing methods
 * These expect the output grid cell to be at a different location to the input
 * 
 * The stencil no longer has a value in 'C' (centre)
 * instead, points are shifted as follows:
 * 
 * mm  -> -3/2 h
 * m   -> -1/2 h
 * p   -> +1/2 h
 * pp  -? +3/2 h
 *
 * NOTE: Cell widths (dx, dy, dz) are currently defined as centre->centre
 * for the methods above. This is currently not taken account of, so large
 * variations in cell size will cause issues.
 *******************************************************************************/

/////////////////////// FIRST DERIVATIVES //////////////////////
// Map Centre -> Low or Low -> Centre

// Second order differencing (staggered)
BoutReal DDX_C2_stag(stencil &f) {
  return f.p - f.m;
}

BoutReal DDX_C4_stag(stencil &f) {
  return ( 27.*(f.p - f.m) - (f.pp - f.mm) ) / 24.;
}

/////////////////////// SECOND DERIVATIVES //////////////////////
// Map Centre -> Low or Low -> Centre

BoutReal D2DX2_C4_stag(stencil &f) {
  return ( f.pp + f.mm - f.p - f.m ) / 2.;
}

/////////////////////////// UPWINDING ///////////////////////////
// Map (Low, Centre) -> Centre  or (Centre, Low) -> Low
// Hence v contains only (mm, m, p, pp) fields whilst f has 'c' too
//
// v.p is v at +1/2, v.m is at -1/2

BoutReal VDDX_U1_stag(stencil &v, stencil &f) {
  // Lower cell boundary
  BoutReal result = (v.m >= 0) ? v.m * f.m : v.m * f.c;
  
  // Upper cell boundary
  result -= (v.p >= 0) ? v.p * f.c : v.p * f.p;
  
  result *= -1;
  
  // result is now d/dx(v*f), but want v*d/dx(f) so subtract f*d/dx(v)
  result -= f.c*(v.p - v.m);
  
  return result;
}

/////////////////////////// FLUX ///////////////////////////
// Map (Low, Centre) -> Centre  or (Centre, Low) -> Low
// Hence v contains only (mm, m, p, pp) fields whilst f has 'c' too
//
// v.p is v at +1/2, v.m is at -1/2

BoutReal FDDX_U1_stag(stencil &v, stencil &f) {
  // Lower cell boundary
  BoutReal result = (v.m >= 0) ? v.m * f.m : v.m * f.c;
  
  // Upper cell boundary
  result -= (v.p >= 0) ? v.p * f.c : v.p * f.p;

  return result;
}

/*******************************************************************************
 * Lookup tables of functions. Map between names, codes and functions
 *******************************************************************************/

/// Translate between DIFF_METHOD codes, and functions
struct DiffLookup {
  DIFF_METHOD method;
  deriv_func func;     // Single-argument differencing function
  upwind_func up_func; // Upwinding function
};

/// Translate between short names, long names and DIFF_METHOD codes
struct DiffNameLookup {
  DIFF_METHOD method;
  const char* label; // Short name
  const char* name;  // Long name
};

/// Differential function name/code lookup
static DiffNameLookup DiffNameTable[] = { {DIFF_U1, "U1", "First order upwinding"},
					  {DIFF_C2, "C2", "Second order central"},
					  {DIFF_W2, "W2", "Second order WENO"},
					  {DIFF_W3, "W3", "Third order WENO"},
					  {DIFF_C4, "C4", "Fourth order central"},
					  {DIFF_U4, "U4", "Fourth order upwinding"},
                                          {DIFF_S2, "S2", "Smoothing 2nd order"},
					  {DIFF_FFT, "FFT", "FFT"},
                                          {DIFF_NND, "NND", "NND"},
                                          {DIFF_SPLIT, "SPLIT", "Split into upwind and central"},
					  {DIFF_PPM, "PPM", "Piecewise Parabolic Method"},
					  {DIFF_DEFAULT}}; // Use to terminate the list

/// First derivative lookup table
static DiffLookup FirstDerivTable[] = { {DIFF_C2, DDX_C2,     NULL},
					{DIFF_W2, DDX_CWENO2, NULL},
					{DIFF_W3, DDX_CWENO3, NULL},
					{DIFF_C4, DDX_C4,     NULL},
                                        {DIFF_S2, DDX_S2,     NULL},
					{DIFF_FFT, NULL,      NULL},
					{DIFF_DEFAULT}};

/// Second derivative lookup table
static DiffLookup SecondDerivTable[] = { {DIFF_C2, D2DX2_C2, NULL},
					{DIFF_C4, D2DX2_C4, NULL},
					{DIFF_FFT, NULL,    NULL},
					{DIFF_DEFAULT}};

/// Upwinding functions lookup table
static DiffLookup UpwindTable[] = { {DIFF_U1, NULL, VDDX_U1},
				    {DIFF_C2, NULL, VDDX_C2},
				    {DIFF_U4, NULL, VDDX_U4},
				    {DIFF_W3, NULL, VDDX_WENO3},
				    {DIFF_C4, NULL, VDDX_C4},
				    {DIFF_PPM, NULL, VDDX_PPM},
				    {DIFF_DEFAULT}};

/// Flux functions lookup table
static DiffLookup FluxTable[] = { {DIFF_SPLIT, NULL, NULL},
                                  {DIFF_U1, NULL, FDDX_U1},
                                  {DIFF_C2, NULL, FDDX_C2},
                                  {DIFF_C4, NULL, FDDX_C4},
                                  {DIFF_NND, NULL, FDDX_NND},
                                  {DIFF_DEFAULT}};

/// First staggered derivative lookup
static DiffLookup FirstStagDerivTable[] = { {DIFF_C2, DDX_C2_stag, NULL}, 
					    {DIFF_C4, DDX_C4_stag, NULL},
					    {DIFF_DEFAULT}};

/// Second staggered derivative lookup
static DiffLookup SecondStagDerivTable[] = { {DIFF_C4, D2DX2_C4_stag, NULL},
					     {DIFF_DEFAULT}};

/// Upwinding staggered lookup
static DiffLookup UpwindStagTable[] = { {DIFF_U1, NULL, VDDX_U1_stag},
					{DIFF_DEFAULT} };

/// Flux staggered lookup
static DiffLookup FluxStagTable[] = { {DIFF_SPLIT, NULL, NULL},
                                      {DIFF_U1, NULL, FDDX_U1_stag},
                                      {DIFF_DEFAULT}};

/*******************************************************************************
 * Routines to use the above tables to map between function codes, names
 * and pointers
 *******************************************************************************/

deriv_func lookupFunc(DiffLookup* table, DIFF_METHOD method) {
  int i = 0;
  do {
    if(table[i].method == method)
      return table[i].func;
    i++;
  }while(table[i].method != DIFF_DEFAULT);
  // Not found in list. Return the first 
  
  return table[0].func;
}

upwind_func lookupUpwindFunc(DiffLookup* table, DIFF_METHOD method) {
  int i = 0;
  do {
    if(table[i].method == method)
      return table[i].up_func;
    i++;
  }while(table[i].method != DIFF_DEFAULT);
  // Not found in list. Return the first 
  
  return table[0].up_func;
}

/// Test if a given DIFF_METHOD exists in a table
bool isImplemented(DiffLookup* table, DIFF_METHOD method) {
  int i = 0;
  do {
    if(table[i].method == method)
      return true;
    i++;
  }while(table[i].method != DIFF_DEFAULT);
  
  return false;
}

/// This function is used during initialisation only (i.e. doesn't need to be particularly fast)
/// Returns DIFF_METHOD, rather than function so can be applied to central and upwind tables
DIFF_METHOD lookupFunc(DiffLookup *table, const string &label) {
  DIFF_METHOD matchtype; // code which matches just the first letter ('C', 'U' or 'W')

  if(label.empty())
    return table[0].method;

  matchtype = DIFF_DEFAULT;
  int typeind;
  
  // Loop through the name lookup table
  int i = 0;
  do {
    if((toupper(DiffNameTable[i].label[0]) == toupper(label[0])) && isImplemented(table, DiffNameTable[i].method)) {
      matchtype = DiffNameTable[i].method;
      typeind = i;
      
      if(strcasecmp(label.c_str(), DiffNameTable[i].label) == 0) {// Whole match
	return matchtype;
      }
    }
    i++;
  }while(DiffNameTable[i].method != DIFF_DEFAULT);
  
  // No exact match, so return matchtype.

  if(matchtype == DIFF_DEFAULT) {
    // No type match either. Return the first value in the table
    matchtype = table[0].method;
    output << " No match for '" << label << "' -> ";
  }else
    output << " Type match for '" << label << "' ->";

  return matchtype;
}

void printFuncName(DIFF_METHOD method) {
  // Find this entry

  int i = 0;
  do {
    if(DiffNameTable[i].method == method) {
      output.write(" %s (%s)\n", DiffNameTable[i].name, DiffNameTable[i].label);
      return;
    }
    i++;
  }while(DiffNameTable[i].method != DIFF_DEFAULT);

  // None
  output.write(" == INVALID DIFFERENTIAL METHOD ==\n");
}

/*******************************************************************************
 * Default functions
 *
 *
 *******************************************************************************/

// Central -> Central (or Left -> Left) functions
deriv_func fDDX, fDDY, fDDZ;        ///< Differencing methods for each dimension
deriv_func fD2DX2, fD2DY2, fD2DZ2;  ///< second differential operators
upwind_func fVDDX, fVDDY, fVDDZ;    ///< Upwind functions in the three directions
upwind_func fFDDX, fFDDY, fFDDZ;    ///< Default flux functions

// Central -> Left (or Left -> Central) functions
deriv_func sfDDX, sfDDY, sfDDZ;
deriv_func sfD2DX2, sfD2DY2, sfD2DZ2;
upwind_func sfVDDX, sfVDDY, sfVDDZ;
upwind_func sfFDDX, sfFDDY, sfFDDZ;

/*******************************************************************************
 * Initialisation
 *******************************************************************************/

/// Set the derivative method, given a table and option name
void derivs_set(Options *options, DiffLookup *table, const char* name, deriv_func &f) {
  string label;
  options->get(name, label, "", false);

  DIFF_METHOD method = lookupFunc(table, label); // Find the function
  printFuncName(method); // Print differential function name
  f = lookupFunc(table, method); // Find the function pointer
}

void derivs_set(Options *options, DiffLookup *table, const char* name, upwind_func &f) {
  string label;
  options->get(name, label, "", false);

  DIFF_METHOD method = lookupFunc(table, label); // Find the function
  printFuncName(method); // Print differential function name
  f = lookupUpwindFunc(table, method);
}

/// Initialise derivatives from options
void derivs_init(Options *options, bool StaggerGrids,
                 deriv_func &fdd, deriv_func &sfdd, 
                 deriv_func &fd2d, deriv_func &sfd2d, 
                 upwind_func &fu, upwind_func &sfu,
                 upwind_func &ff, upwind_func &sff) {
  output.write("\tFirst       : ");
  derivs_set(options, FirstDerivTable, "first",  fdd);
  if(StaggerGrids) {
    output.write("\tStag. First : ");
    derivs_set(options, FirstStagDerivTable, "first",  sfdd);
  }
  output.write("\tSecond      : ");
  derivs_set(options, SecondDerivTable, "second", fd2d);
  if(StaggerGrids) {
    output.write("\tStag. Second: ");
    derivs_set(options, SecondStagDerivTable, "second", sfd2d);
  }
  output.write("\tUpwind      : ");
  derivs_set(options, UpwindTable,     "upwind", fu);
  if(StaggerGrids) {
    output.write("\tStag. Upwind: ");
    derivs_set(options, UpwindStagTable,     "upwind", sfu);
  }
  output.write("\tFlux        : ");
  derivs_set(options, FluxTable,     "flux", ff);
  if(StaggerGrids) {
    output.write("\tStag. Flux  : ");
    derivs_set(options, FluxStagTable,     "flux", sff);
  }
}

/// Initialise the derivative methods. Must be called before any derivatives are used
int derivs_init() {
#ifdef CHECK
  msg_stack.push("Initialising derivatives");
#endif

  /// NOTE: StaggerGrids is also in Mesh, but derivs_init needs to come before Mesh
  bool StaggerGrids;
  
  // Get the options
  Options *options = Options::getRoot();
  OPTION(options, StaggerGrids,   false);

  output.write("Setting X differencing methods\n");
  derivs_init(options->getSection("ddx"), 
              StaggerGrids,
              fDDX, sfDDX, 
              fD2DX2, sfD2DX2,
              fVDDX, sfVDDX,
              fFDDX, sfFDDX);
  
  if((fDDX == NULL) || (fD2DX2 == NULL)) {
    output.write("\t***Error: FFT cannot be used in X\n");
    return 1;
  }
  
  output.write("Setting Y differencing methods\n");
  derivs_init(options->getSection("ddy"), 
              StaggerGrids,
              fDDY, sfDDY, 
              fD2DY2, sfD2DY2,
              fVDDY, sfVDDY,
              fFDDY, sfFDDY);
  
  if((fDDY == NULL) || (fD2DY2 == NULL)) {
    output.write("\t***Error: FFT cannot be used in Y\n");
    return 1;
  }
  
  output.write("Setting Z differencing methods\n");
  derivs_init(options->getSection("ddz"), 
              StaggerGrids,
              fDDZ, sfDDZ, 
              fD2DZ2, sfD2DZ2,
              fVDDZ, sfVDDZ,
              fFDDZ, sfFDDZ);
  
#ifdef CHECK
  msg_stack.pop();
#endif

  return 0;
}

/*******************************************************************************
 * Apply differential operators. These are fairly brain-dead functions
 * which apply a derivative function to a field (sort of like map). Decisions
 * of what to apply are made in the DDX,DDY and DDZ functions lower down.
 *
 * loc  is the cell location of the result
 *******************************************************************************/

// X derivative

const Field2D applyXdiff(const Field2D &var, deriv_func func, const Field2D &dd, CELL_LOC loc = CELL_DEFAULT) {
  Field2D result;
  result.allocate(); // Make sure data allocated

  bindex bx;

  BoutReal **r = result.getData();

  start_index(&bx, RGN_NOX);
#ifdef _OPENMP
  // Parallel version. Needs another variable for each thread
  
  bindex bxstart = bx; // Copy to avoid race condition on first index
  bool workToDoGlobal; // Shared loop control
  #pragma omp parallel
  {
    bindex bxlocal; // Index for each thread
    stencil s;
    bool workToDo;  // Does this thread have work to do?
    
    #pragma omp single
    {
      // First index done once
      var.setXStencil(s, bxstart, loc);
      r[bxstart.jx][bxstart.jy] = func(s) / dd(bxstart.jx, bxstart.jy);
    }
    
    do {
      #pragma omp critical
      {
        // Get the next index
        workToDo = next_index2(&bx);
        bxlocal = bx; // Make a local copy
        workToDoGlobal = workToDo;
      }
      if(workToDo) { // Here workToDo could be different to workToDoGlobal
        var.setXStencil(s, bxlocal, loc);
        r[bxlocal.jx][bxlocal.jy] = func(s) / dd(bxlocal.jx, bxlocal.jy);
      }
    }while(workToDoGlobal);
  }
#else
  // Serial version
  
  stencil s;
  do {
    var.setXStencil(s, bx, loc);
    r[bx.jx][bx.jy] = func(s) / dd[bx.jx][bx.jy];
  }while(next_index2(&bx));
#endif // _OPENMP

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif

  return result;
}

const Field3D applyXdiff(const Field3D &var, deriv_func func, const Field2D &dd, CELL_LOC loc = CELL_DEFAULT) {
  Field3D result;
  result.allocate(); // Make sure data allocated

  Field3D vs = var;
  if(mesh->ShiftXderivs && (mesh->ShiftOrder == 0)) {
    // Shift in Z using FFT
    vs = var.shiftZ(true); // Shift into real space
  }
  
  bindex bx;
  BoutReal ***r = result.getData();
  
  start_index(&bx, RGN_NOX);
#ifdef _OPENMP
  bindex bxstart = bx; // Copy to avoid race condition on first index
  bool workToDoGlobal; // Shared loop control
  #pragma omp parallel
  {
    bindex bxlocal; // Index for each thread
    stencil s;
    bool workToDo;  // Does this thread have work to do?
    
    #pragma omp single
    {
      // First index done by single thread
      for(bxstart.jz=0;bxstart.jz<mesh->ngz-1;bxstart.jz++) {
        vs.setXStencil(s, bxstart, loc);
        r[bxstart.jx][bxstart.jy][bxstart.jz] = func(s) / dd(bxstart.jx, bxstart.jy);
      }
    }
    
    do {
      #pragma omp critical
      {
        // Get the next index
        workToDo = next_index2(&bx); // Only in 2D
        bxlocal = bx; // Make a local copy
        workToDoGlobal = workToDo;
      }
      if(workToDo) { // Here workToDo could be different to workToDoGlobal
        for(bxlocal.jz=0;bxlocal.jz<mesh->ngz-1;bxlocal.jz++) {
          vs.setXStencil(s, bxlocal, loc);
          r[bxlocal.jx][bxlocal.jy][bxlocal.jz] = func(s) / dd(bxlocal.jx, bxlocal.jy);
        }
      }
    }while(workToDoGlobal);
  }
#else
  stencil s;
  do {
    for(bx.jz=0;bx.jz<mesh->ngz-1;bx.jz++) {
      vs.setXStencil(s, bx, loc);
      r[bx.jx][bx.jy][bx.jz] = func(s) / dd(bx.jx, bx.jy);
    }
  }while(next_index2(&bx));
#endif  

  if(mesh->ShiftXderivs && (mesh->ShiftOrder == 0))
    result = result.shiftZ(false); // Shift back

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif

  return result;
}

// Y derivative

const Field2D applyYdiff(const Field2D &var, deriv_func func, const Field2D &dd, CELL_LOC loc = CELL_DEFAULT) {
  Field2D result;
  result.allocate(); // Make sure data allocated
  BoutReal **r = result.getData();
  
  bindex bx;
  
  start_index(&bx, RGN_NOBNDRY);
  /*
#ifdef _OPENMP
  bindex bxstart = bx; // Copy to avoid race condition on first index
  bool workToDoGlobal; // Shared loop control
  #pragma omp parallel
  {
    bindex bxlocal; // Index for each thread
    stencil s;
    bool workToDo;  // Does this thread have work to do?
    
    #pragma omp single
    {
      // First index done once
      var.setYStencil(s, bxstart, loc);
      r[bxstart.jx][bxstart.jy] = func(s) / dd[bxstart.jx][bxstart.jy];
    }
    
    do {
      #pragma omp critical
      {
        // Get the next index
        workToDo = next_index2(&bx);
        bxlocal = bx; // Make a local copy
        workToDoGlobal = workToDo;
      }
      if(workToDo) { // Here workToDo could be different to workToDoGlobal
        var.setYStencil(s, bxlocal, loc);
        r[bxlocal.jx][bxlocal.jy] = func(s) / dd[bxlocal.jx][bxlocal.jy];
      }
    }while(workToDoGlobal);
  }
  #else */
  stencil s;
  do{
    var.setYStencil(s, bx, loc);
    r[bx.jx][bx.jy] = func(s) / dd(bx.jx, bx.jy);
  }while(next_index2(&bx));
  //#endif  

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_yup = result.bndry_ydown = false;
#endif

  return result;
}

const Field3D applyYdiff(const Field3D &var, deriv_func func, const Field2D &dd, CELL_LOC loc = CELL_DEFAULT) {
  Field3D result;
  result.allocate(); // Make sure data allocated
  BoutReal ***r = result.getData();
  
  bindex bx;
  
  start_index(&bx, RGN_NOBNDRY);
  
  /*
#ifdef _OPENMP
  // Parallel version
  bindex bxstart = bx; // Copy to avoid race condition on first index
  bool workToDoGlobal; // Shared loop control
  #pragma omp parallel
  {
    bindex bxlocal; // Index for each thread
    stencil s;
    bool workToDo;  // Does this thread have work to do?
    
    #pragma omp single
    {
      // First index done by single thread
      for(bxstart.jz=0;bxstart.jz<mesh->ngz-1;bxstart.jz++) {
        var.setYStencil(s, bxstart, loc);
        r[bxstart.jx][bxstart.jy][bxstart.jz] = func(s) / dd[bxstart.jx][bxstart.jy];
      }
    }
    
    do {
      #pragma omp critical
      {
        // Get the next index
        workToDo = next_index2(&bx); // Only in 2D
        bxlocal = bx; // Make a local copy
        workToDoGlobal = workToDo;
      }
      if(workToDo) { // Here workToDo could be different to workToDoGlobal
        for(bxlocal.jz=0;bxlocal.jz<mesh->ngz-1;bxlocal.jz++) {
          var.setYStencil(s, bxlocal, loc);
          r[bxlocal.jx][bxlocal.jy][bxlocal.jz] = func(s) / dd[bxlocal.jx][bxlocal.jy];
        }
      }
    }while(workToDoGlobal);
  }
#else 
  */
  stencil s;
  do {
    //output.write("apply %d %d\n", bx.jx, bx.jy);
    for(bx.jz=0;bx.jz<mesh->ngz-1;bx.jz++) {
      var.setYStencil(s, bx, loc);
      r[bx.jx][bx.jy][bx.jz] = func(s) / dd(bx.jx, bx.jy);
    }
  }while(next_index2(&bx));
  //#endif

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif

  return result;
}

// Z derivative

const Field3D applyZdiff(const Field3D &var, deriv_func func, BoutReal dd, CELL_LOC loc = CELL_DEFAULT) {
  Field3D result;
  result.allocate(); // Make sure data allocated
  BoutReal ***r = result.getData();
  
#ifdef _OPENMP
  // Parallel version
  
  int ny = mesh->yend-mesh->ystart+1;
  int ncz = mesh->ngz-1;
  #pragma omp parallel for
  for(int j=0;j<mesh->ngx*ny*ncz;j++) {
    int jz = j % (mesh->ngz-1);
    int rem = j / (mesh->ngz-1);
    int jy = (rem % ny) + mesh->ystart; 
    int jx = rem / ny;
    
    bindex bx;
    bx.jx=jx; bx.jy=jy; bx.jz=jz;
    bx.jzp  = (bx.jz+1)%ncz;
    bx.jzm  = (bx.jz+ncz-1)%ncz;
    bx.jz2p = (bx.jzp+1)%ncz;
    bx.jz2m = (bx.jzm+ncz-1)%ncz;
    stencil s;
    var.setZStencil(s, bx, loc);
    r[jx][jy][jz] = func(s) / dd;
  }
#else
  bindex bx;

  start_index(&bx, RGN_NOZ);
  stencil s;
  do {
    var.setZStencil(s, bx, loc);
    r[bx.jx][bx.jy][bx.jz] = func(s) / dd;
  }while(next_index3(&bx));
#endif

  return result;
}

/*******************************************************************************
 * First central derivatives
 *******************************************************************************/

////////////// X DERIVATIVE /////////////////

const Field3D DDX(const Field3D &f, CELL_LOC outloc, DIFF_METHOD method) {
  deriv_func func = fDDX; // Set to default function
  DiffLookup *table = FirstDerivTable;
  
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result

  Field3D result;

  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }

  if(mesh->StaggerGrids && (outloc != inloc)) {
    // Shifting to a new location
    
    if(((inloc == CELL_CENTRE) && (outloc == CELL_XLOW)) ||
       ((inloc == CELL_XLOW) && (outloc == CELL_CENTRE))) {
      // Shifting in X. Centre -> Xlow, or Xlow -> Centre
      
      func = sfDDX; // Set default
      table = FirstStagDerivTable; // Set table for others
      diffloc = (inloc == CELL_CENTRE) ? CELL_XLOW : CELL_CENTRE;
      
    }else {
      // A more complicated shift. Get a result at cell centre, then shift.
      if(inloc == CELL_XLOW) {
	// Shifting
	
	func = sfDDX; // Set default
	table = FirstStagDerivTable; // Set table for others
	diffloc = CELL_CENTRE;

      }else if(inloc != CELL_CENTRE) {
	// Interpolate then (centre -> centre) then interpolate
	return DDX(interp_to(f, CELL_CENTRE), outloc, method);
      }
    }
  }
  
  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupFunc(table, method);
    if(func == NULL)
      bout_error("Cannot use FFT for X derivatives");
  }
  
  result = applyXdiff(f, func, mesh->dx, diffloc);
  result.setLocation(diffloc); // Set the result location

  result = interp_to(result, outloc); // Interpolate if necessary
  
  if(mesh->ShiftXderivs && mesh->IncIntShear) {
    // Using BOUT-06 style shifting
    result += mesh->IntShiftTorsion * DDZ(f, outloc);
  }

  return result;
}

const Field3D DDX(const Field3D &f, DIFF_METHOD method, CELL_LOC outloc) {
  return DDX(f, outloc, method);
}

const Field3D DDX(const Field3D &f, DIFF_METHOD method) {
  return DDX(f, CELL_DEFAULT, method);
}

const Field2D DDX(const Field2D &f) {
  return applyXdiff(f, fDDX, mesh->dx);
}

////////////// Y DERIVATIVE /////////////////

const Field3D DDY(const Field3D &f, CELL_LOC outloc, DIFF_METHOD method) {
  deriv_func func = fDDY; // Set to default function
  DiffLookup *table = FirstDerivTable;
  
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result

  Field3D result;

  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }

  if(mesh->StaggerGrids && (outloc != inloc)) {
    // Shifting to a new location
    
    //output.write("\nSHIFTING %s -> %s\n", strLocation(inloc), strLocation(outloc));

    if(((inloc == CELL_CENTRE) && (outloc == CELL_YLOW)) ||
       ((inloc == CELL_YLOW) && (outloc == CELL_CENTRE))) {
      // Shifting in Y. Centre -> Ylow, or Ylow -> Centre
      
      //output.write("SHIFT");

      func = sfDDY; // Set default
      table = FirstStagDerivTable; // Set table for others
      diffloc = (inloc == CELL_CENTRE) ? CELL_YLOW : CELL_CENTRE;
      
    }else {
      // A more complicated shift. Get a result at cell centre, then shift.
      if(inloc == CELL_YLOW) {
	// Shifting
	
	func = sfDDY; // Set default
	table = FirstStagDerivTable; // Set table for others
	diffloc = CELL_CENTRE;

      }else if(inloc != CELL_CENTRE) {
	// Interpolate to centre then call DDY again
	return DDY(interp_to(f, CELL_CENTRE), outloc, method);
      }
    }
  }
  
  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupFunc(table, method);
    if(func == NULL)
      bout_error("Cannot use FFT for Y derivatives");
  }
  
  result = applyYdiff(f, func, mesh->dy, diffloc);
  //output.write("SETTING LOC %s -> %s\n", strLocation(diffloc), strLocation(outloc));
  result.setLocation(diffloc); // Set the result location
  
  return interp_to(result, outloc); // Interpolate if necessary
}

const Field3D DDY(const Field3D &f, DIFF_METHOD method, CELL_LOC outloc) {
  return DDY(f, outloc, method);
}

const Field3D DDY(const Field3D &f, DIFF_METHOD method) {
  return DDY(f, CELL_DEFAULT, method);
}

const Field2D DDY(const Field2D &f) {
  return applyYdiff(f, fDDY, mesh->dy);
}

const Field3D DDY_MUSCL(const Field3D &F, const Field3D &u, const Field2D &Vmax) {
  Field3D result;
  result.allocate(); // Make sure data allocated
  BoutReal ***r = result.getData();
  
  bindex bx;
  start_index(&bx, RGN_NOBNDRY);

  stencil fs, us;
  do {
    for(bx.jz=0;bx.jz<mesh->ngz-1;bx.jz++) {
      F.setYStencil(fs, bx);
      u.setYStencil(us, bx);
      
      r[bx.jx][bx.jy][bx.jz] = DDX_KT(fs, us, Vmax[bx.jx][bx.jy]) / mesh->dy(bx.jx, bx.jy);
    }
  }while(next_index2(&bx));
  
  return result;
}

////////////// Z DERIVATIVE /////////////////

const Field3D DDZ(const Field3D &f, CELL_LOC outloc, DIFF_METHOD method, bool inc_xbndry) {
  deriv_func func = fDDZ; // Set to default function
  DiffLookup *table = FirstDerivTable;
 
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result

  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }

  if(mesh->StaggerGrids && (outloc != inloc)) {
    // Shifting to a new location
    
    if(((inloc == CELL_CENTRE) && (outloc == CELL_ZLOW)) ||
       ((inloc == CELL_ZLOW) && (outloc == CELL_CENTRE))) {
      // Shifting in Z. Centre -> Zlow, or Zlow -> Centre
      
      func = sfDDZ; // Set default
      table = FirstStagDerivTable; // Set table for others
      diffloc = (inloc == CELL_CENTRE) ? CELL_ZLOW : CELL_CENTRE;
      
    }else {
      // A more complicated shift. Get a result at cell centre, then shift.
      if(inloc == CELL_ZLOW) {
	// Shifting
	
	func = sfDDZ; // Set default
	table = FirstStagDerivTable; // Set table for others
	diffloc = CELL_CENTRE;

      }else if(inloc != CELL_CENTRE) {
	// Interpolate then (centre -> centre) then interpolate
	return DDZ(interp_to(f, CELL_CENTRE), outloc, method);
      }
    }
  }
  
  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupFunc(table, method);
  }

  Field3D result;

  if(func == NULL) {
    // Use FFT
    
    BoutReal shift = 0.; // Shifting result in Z?
    if(mesh->StaggerGrids) {
      if((inloc == CELL_CENTRE) && (diffloc == CELL_ZLOW)) {
	// Shifting down - multiply by exp(-0.5*i*k*dz)
	shift = -1.;
      }else if((inloc == CELL_ZLOW) && (diffloc == CELL_CENTRE)) {
	// Shifting up
	shift = 1.;
      }
    }

    result.allocate(); // Make sure data allocated

    int xge = mesh->xstart, xlt = mesh->xend+1;
    if(inc_xbndry) { // Include x boundary region (for mixed XZ derivatives)
      xge = 0;
      xlt = mesh->ngx;
    }
    
    int ncz = mesh->ngz-1;
    
#ifndef _OPENMP
    static dcomplex *cv = (dcomplex*) NULL;
#else
    static dcomplex *globalcv;
    static int nthreads = 0;
#endif 

    #pragma omp parallel
    {
#ifndef _OPENMP
      // Serial, so can have a single static array
      if(cv == (dcomplex*) NULL)
        cv = new dcomplex[ncz/2 + 1];
#else
      // Parallel, so allocate a separate array for each thread
      
      int th_id = omp_get_thread_num(); // thread ID

      if(th_id == 0) {
        int n_th = omp_get_num_threads();
        if(nthreads < n_th) {
          // Allocate memory in thread zero
          if(nthreads > 0)
            delete[] globalcv;
          globalcv = new dcomplex[n_th*(ncz/2 + 1)];
          nthreads = n_th;
        }
      }
      // Wait for memory to be allocated
      #pragma omp barrier
      
      dcomplex *cv = globalcv + th_id*(ncz/2 + 1); // Separate array for each thread
#endif
       #pragma omp for
      for(int jy=0;jy<mesh->ngy*(xlt-xge);jy++) {
        rfft(f[xge][jy], ncz, cv); // Forward FFT
          
        for(int jz=0;jz<=ncz/2;jz++) {
          BoutReal kwave=jz*2.0*PI/mesh->zlength; // wave number is 1/[rad]
            
          BoutReal flt;
          if (jz>0.4*ncz) flt=1e-10; else flt=1.0;
          cv[jz] *= dcomplex(0.0, kwave) * flt;
          if(mesh->StaggerGrids)
            cv[jz] *= exp(Im * (shift * kwave * mesh->dz));
        }
          
        irfft(cv, ncz, result[xge][jy]); // Reverse FFT
         
        result[xge][jy][ncz] = result[xge][jy][0];
      }
    }
    
#ifdef CHECK
    // Mark boundaries as invalid
    result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif
    
  }else {
    // All other (non-FFT) functions 
    result = applyZdiff(f, func, mesh->dz);
  }
  
  result.setLocation(diffloc);

  return interp_to(result, outloc);
}

const Field3D DDZ(const Field3D &f, DIFF_METHOD method, CELL_LOC outloc, bool inc_xbndry) {
  return DDZ(f, outloc, method, inc_xbndry);
}

const Field3D DDZ(const Field3D &f, DIFF_METHOD method, bool inc_xbndry) {
  return DDZ(f, CELL_DEFAULT, method, inc_xbndry);
}

const Field3D DDZ(const Field3D &f, bool inc_xbndry) {
  return DDZ(f, CELL_DEFAULT, DIFF_DEFAULT, inc_xbndry);
}

const Field2D DDZ(const Field2D &f) {
  Field2D result;
  result = 0.0;
  return result;
}

const Vector3D DDZ(const Vector3D &v, CELL_LOC outloc, DIFF_METHOD method) {
  Vector3D result;

  result.covariant = v.covariant;

  result.x = DDZ(v.x, outloc, method);
  result.y = DDZ(v.y, outloc, method);
  result.z = DDZ(v.z, outloc, method);

  return result;
}

const Vector3D DDZ(const Vector3D &v, DIFF_METHOD method, CELL_LOC outloc) {
  return DDZ(v, outloc, method);
}

const Vector2D DDZ(const Vector2D &v) {
  Vector2D result;

  result.covariant = v.covariant;

  result.x = 0.;
  result.y = 0.;
  result.z = 0.;

  return result;
}

/*******************************************************************************
 * 2nd derivative
 *******************************************************************************/

////////////// X DERIVATIVE /////////////////

const Field3D D2DX2(const Field3D &f, CELL_LOC outloc, DIFF_METHOD method) {
  deriv_func func = fD2DX2; // Set to default function
  DiffLookup *table = SecondDerivTable;
  
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result
  
  Field3D result;
  
  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }

  if(mesh->StaggerGrids && (outloc != inloc)) {
    // Shifting to a new location
    
    if(((inloc == CELL_CENTRE) && (outloc == CELL_XLOW)) ||
       ((inloc == CELL_XLOW) && (outloc == CELL_CENTRE))) {
      // Shifting in X. Centre -> Xlow, or Xlow -> Centre
      
      func = sfD2DX2; // Set default
      table = SecondStagDerivTable; // Set table for others
      diffloc = (inloc == CELL_CENTRE) ? CELL_XLOW : CELL_CENTRE;
      
    }else {
      // A more complicated shift. Get a result at cell centre, then shift.
      if(inloc == CELL_XLOW) {
	// Shifting
	
	func = sfD2DX2; // Set default
	table = SecondStagDerivTable; // Set table for others
	diffloc = CELL_CENTRE;

      }else if(inloc != CELL_CENTRE) {
	// Interpolate then (centre -> centre) then interpolate
	return D2DX2(interp_to(f, CELL_CENTRE), outloc, method);
      }
    }
  }

  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupFunc(table, method);
    if(func == NULL)
      bout_error("Cannot use FFT for X derivatives");
  }

  result = applyXdiff(f, func, (mesh->dx*mesh->dx));
  result.setLocation(diffloc);

  if(mesh->non_uniform) {
    // Correction for non-uniform mesh
    result += mesh->d1_dx*applyXdiff(f, fDDX, mesh->dx);
  }

  result = interp_to(result, outloc);

  if(mesh->ShiftXderivs && mesh->IncIntShear) {
    mesh->IncIntShear = false; // So DDX doesn't try to include I again
    // Add I^2 d^2/dz^2 term
    result += mesh->IntShiftTorsion^2 * D2DZ2(f, outloc);
    // Mixed derivative
    result += 2.*mesh->IntShiftTorsion * D2DXDZ(f);
    // DDZ term
    result += DDX(mesh->IntShiftTorsion) * DDZ(f, outloc);
    mesh->IncIntShear = true;
  }
  
  return result;
}

const Field3D D2DX2(const Field3D &f, DIFF_METHOD method, CELL_LOC outloc) {
  return D2DX2(f, outloc, method);
}

const Field2D D2DX2(const Field2D &f) {
  Field2D result;

  result = applyXdiff(f, fD2DX2, (mesh->dx*mesh->dx));

  if(mesh->non_uniform) {
    // Correction for non-uniform mesh
    result += mesh->d1_dx * applyXdiff(f, fDDX, mesh->dx);
  }
  
  return(result);
}

////////////// Y DERIVATIVE /////////////////

const Field3D D2DY2(const Field3D &f, CELL_LOC outloc, DIFF_METHOD method) {
  deriv_func func = fD2DY2; // Set to default function
  DiffLookup *table = SecondDerivTable;
  
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result
  
  Field3D result;
  
  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }

  if(mesh->StaggerGrids && (outloc != inloc)) {
    // Shifting to a new location
    
    if(((inloc == CELL_CENTRE) && (outloc == CELL_YLOW)) ||
       ((inloc == CELL_YLOW) && (outloc == CELL_CENTRE))) {
      // Shifting in Y. Centre -> Ylow, or Ylow -> Centre
      
      func = sfD2DY2; // Set default
      table = SecondStagDerivTable; // Set table for others
      diffloc = (inloc == CELL_CENTRE) ? CELL_YLOW : CELL_CENTRE;
      
    }else {
      // A more complicated shift. Get a result at cell centre, then shift.
      if(inloc == CELL_YLOW) {
	// Shifting
	
	func = sfD2DY2; // Set default
	table = SecondStagDerivTable; // Set table for others
	diffloc = CELL_CENTRE;

      }else if(inloc != CELL_CENTRE) {
	// Interpolate then (centre -> centre) then interpolate
	return D2DY2(interp_to(f, CELL_CENTRE), outloc, method);
      }
    }
  }

  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupFunc(table, method);
    if(func == NULL)
      bout_error("Cannot use FFT for Y derivatives");
  }

  result = applyYdiff(f, func, (mesh->dy*mesh->dy));
  result.setLocation(diffloc);

  if(mesh->non_uniform) {
    // Correction for non-uniform mesh
    result += mesh->d1_dy * applyYdiff(f, fDDY, mesh->dy);
  }

  return interp_to(result, outloc);
}

const Field3D D2DY2(const Field3D &f, DIFF_METHOD method, CELL_LOC outloc) {
  return D2DY2(f, outloc, method);
}

const Field2D D2DY2(const Field2D &f) {
  Field2D result;

  result = applyYdiff(f, fD2DY2, (mesh->dy*mesh->dy));

  if(mesh->non_uniform) {
    // Correction for non-uniform mesh
    result += mesh->d1_dy * applyYdiff(f, fDDY, mesh->dy);
  }

  return(result);
}

////////////// Z DERIVATIVE /////////////////

const Field3D D2DZ2(const Field3D &f, CELL_LOC outloc, DIFF_METHOD method) {
  deriv_func func = fD2DZ2; // Set to default function
  DiffLookup *table = SecondDerivTable;
  
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result
  
  Field3D result;

  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }

  if(mesh->StaggerGrids && (outloc != inloc)) {
    // Shifting to a new location
    
    if(((inloc == CELL_CENTRE) && (outloc == CELL_ZLOW)) ||
       ((inloc == CELL_ZLOW) && (outloc == CELL_CENTRE))) {
      // Shifting in Z. Centre -> Zlow, or Zlow -> Centre
      
      func = sfD2DZ2; // Set default
      table = SecondStagDerivTable; // Set table for others
      diffloc = (inloc == CELL_CENTRE) ? CELL_ZLOW : CELL_CENTRE;
      
    }else {
      // A more complicated shift. Get a result at cell centre, then shift.
      if(inloc == CELL_ZLOW) {
	// Shifting
	
	func = sfD2DZ2; // Set default
	table = SecondStagDerivTable; // Set table for others
	diffloc = CELL_CENTRE;

      }else if(inloc != CELL_CENTRE) {
	// Interpolate then (centre -> centre) then interpolate
	return D2DZ2(interp_to(f, CELL_CENTRE), outloc, method);
      }
    }
  }

  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupFunc(table, method);
  }

  if(func == NULL) {
    // Use FFT

    BoutReal shift = 0.; // Shifting result in Z?
    if(mesh->StaggerGrids) {
      if((inloc == CELL_CENTRE) && (diffloc == CELL_ZLOW)) {
	// Shifting down - multiply by exp(-0.5*i*k*dz)
	shift = -1.;
      }else if((inloc == CELL_ZLOW) && (diffloc == CELL_CENTRE)) {
	// Shifting up
	shift = 1.;
      }
    }
    
    result.allocate(); // Make sure data allocated

    int ncz = mesh->ngz-1;
    
#ifndef _OPENMP
    static dcomplex *cv = (dcomplex*) NULL;
#else
    static dcomplex *globalcv;
    static int nthreads = 0;
#endif
    
    #pragma omp parallel
    {
#ifndef _OPENMP
      // Serial, so can have a single static array
      if(cv == (dcomplex*) NULL)
        cv = new dcomplex[ncz/2 + 1];
#else
      // Parallel, so allocate a separate array for each thread
      
      int th_id = omp_get_thread_num(); // thread ID
      int n_th = omp_get_num_threads();
      if(th_id == 0) {
        if(nthreads < n_th) {
          // Allocate memory in thread zero
          if(nthreads > 0)
            delete[] globalcv;
          globalcv = new dcomplex[n_th*(ncz/2 + 1)];
          nthreads = n_th;
        }
      }
      // Wait for memory to be allocated
      #pragma omp barrier
      
      dcomplex *cv = globalcv + th_id*(ncz/2 + 1); // Separate array for each thread
#endif
      #pragma omp for
      for(int jx=mesh->xstart;jx<=mesh->xend;jx++) {
        for(int jy=mesh->ystart;jy<=mesh->yend;jy++) {
          
          rfft(f[jx][jy], ncz, cv); // Forward FFT
	
          for(int jz=0;jz<=ncz/2;jz++) {
            BoutReal kwave=jz*2.0*PI/mesh->zlength; // wave number is 1/[rad]
            
            BoutReal flt;
            if (jz>0.4*ncz) flt=1e-10; else flt=1.0;

            cv[jz] *= -SQ(kwave) * flt;
            if(mesh->StaggerGrids)
              cv[jz] *= exp(Im * (shift * kwave * mesh->dz));
          }

          irfft(cv, ncz, result[jx][jy]); // Reverse FFT
	
          result[jx][jy][ncz] = result[jx][jy][0];
        }
      }
    } // End of parallel section

#ifdef CHECK
    // Mark boundaries as invalid
    result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif

  }else {
    // All other (non-FFT) functions
    
    result = applyZdiff(f, func, SQ(mesh->dz));
  }

  result.setLocation(diffloc);

  return interp_to(result, outloc);
}

const Field3D D2DZ2(const Field3D &f, DIFF_METHOD method, CELL_LOC outloc) {
  return D2DZ2(f, outloc, method);
}


const Field2D D2DZ2(const Field2D &f) {
  Field2D result;
  result = 0.0;
  return result;
}

/*******************************************************************************
 * Fourth derivatives
 *******************************************************************************/

BoutReal D4DX4_C2(stencil &f) {
  return (f.pp - 4.*f.p + 6.*f.c - 4.*f.m + f.mm);
}

const Field3D D4DX4(const Field3D &f) {
  return applyXdiff(f, D4DX4_C2, SQ(SQ(mesh->dx)));
}

const Field2D D4DX4(const Field2D &f) {
  return applyXdiff(f, D4DX4_C2, SQ(SQ(mesh->dx)));
}

const Field3D D4DY4(const Field3D &f) {
  return applyYdiff(f, D4DX4_C2, SQ(SQ(mesh->dy)));
}

const Field2D D4DY4(const Field2D &f) {
  return applyYdiff(f, D4DX4_C2, SQ(SQ(mesh->dy)));
}

const Field3D D4DZ4(const Field3D &f) {
  return applyZdiff(f, D4DX4_C2, SQ(SQ(mesh->dz)));
}

const Field2D D4DZ4(const Field2D &f) {
  return Field2D(0.0);
}

/*******************************************************************************
 * Mixed derivatives
 *******************************************************************************/

const Field2D D2DXDY(const Field2D &f) {
  return Field2D(0.0);
  /*
    Note: Missing corners, so following will break
  Field2D result;
  result.allocate();
  for(int i=mesh->xstart;i<=mesh->xend;i++)
    for(int j=mesh->ystart;j<=mesh->yend;j++) {
      result(i,j) = 0.25*( +(f(i+1,j+1) - f(i-1,j+1))/(mesh->dx(i,j+1))
                           -(f(i+1,j-1) - f(i-1,j-1))/(mesh->dx(i,j-1)) )
        / mesh->dy(i,j);
    }
  return result;
  */
}

const Field3D D2DXDY(const Field3D &f) {
  return Field3D(0.0);
  /*
    Note: Missing corners, so following will break
    
  Field3D result;
  result.allocate();
  for(int i=mesh->xstart;i<=mesh->xend;i++)
    for(int j=mesh->ystart;j<=mesh->yend;j++) 
      for(int k=0;k<mesh->ngz;k++) {
        result(i,j,k) = 0.25*( +(f(i+1,j+1,k) - f(i-1,j+1,k))/(mesh->dx(i,j+1))
                               -(f(i+1,j-1,k) - f(i-1,j-1,k))/(mesh->dx(i,j-1)) )
          / mesh->dy(i,j);
      }
  return result;
  */
}

const Field2D D2DXDZ(const Field2D &f) {
  return Field2D(0.0);
}

/// X-Z mixed derivative
const Field3D D2DXDZ(const Field3D &f) {
  Field3D result;
  
  // Take derivative in Z, including in X boundaries. Then take derivative in X
  // Maybe should average results of DDX(DDZ) and DDZ(DDX)?
  result = DDX(DDZ(f, true));
  
  return result;
}

const Field2D D2DYDZ(const Field2D &f) {
  return Field2D(0.0);
}

const Field3D D2DYDZ(const Field3D &f) {
  Field3D result;
  result.allocate();
  for(int i=mesh->xstart;i<=mesh->xend;i++)
    for(int j=mesh->ystart;j<=mesh->yend;j++) 
      for(int k=0;k<mesh->ngz-1;k++) {
        int kp = (k+1) % (mesh->ngz-1);
        int km = (k-1+mesh->ngz-1) % (mesh->ngz-1);
        result(i,j,k) = 0.25*( +(f(i,j+1,kp) - f(i,j-1,kp))/(mesh->dy(i,j+1))
                               -(f(i,j+1,km) - f(i,j-1,km))/(mesh->dy(i,j-1)) )
          / mesh->dz;
      }
  return result;
}

/*******************************************************************************
 * Advection schemes
 * 
 * Jan 2009  - Re-written to use Set*Stencil routines
 *******************************************************************************/

////////////// X DERIVATIVE /////////////////

/// Special case where both arguments are 2D. Output location ignored for now
const Field2D VDDX(const Field2D &v, const Field2D &f, CELL_LOC outloc, DIFF_METHOD method) {
  upwind_func func = fVDDX;

  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(UpwindTable, method);
  }

  Field2D result;
  result.allocate(); // Make sure data allocated
  BoutReal **d = result.getData();

  bindex bx;
  stencil vs, fs;
  start_index(&bx);
  do {
    f.setXStencil(fs, bx);
    v.setXStencil(vs, bx);
    
    d[bx.jx][bx.jy] = func(vs, fs) / mesh->dx(bx.jx, bx.jy);
  }while(next_index2(&bx));

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = false;
#endif

  return result;
}

const Field2D VDDX(const Field2D &v, const Field2D &f, DIFF_METHOD method) {
  return VDDX(v, f, CELL_DEFAULT, method);
}

#define MIN(X, Y) (((X)<(Y))?(X):(Y))
#define MAX(X, Y) (((X)>(Y))?(X):(Y))
#define SIGN(X) (((X)>0)?1:-1)
#define SQR(x) ((x)*(x))
#define C 1.25

Field3D ext_X(const Field &b, BoutReal dt, const Field &v)
{
   rvec a6, ap, am, f, a2;
   rvec a, u;
   Field3D ddta;
   int x, y, z, j;
   BoutReal sigma, D2a, D2aC, D2aL, D2aR, D2alim, s;
   BoutReal alphap, alpham, deltaf, deltaa;
   BoutReal h;

   a6.resize(mesh->ngx);
   ap.resize(mesh->ngx);
   am.resize(mesh->ngx);
   f.resize(mesh->ngx);
   a2.resize(mesh->ngx);

   ddta.allocate();
   ddta = 0;

   for(y=mesh->ystart; y<=mesh->yend; y++)
      for(z=0; z<mesh->ngz; z++)
      {
         b.getXArray(y, z, a);
         v.getXArray(y, z, u);
         for(x=mesh->xstart-2; x<=mesh->xend+1; x++)
         {
            h = mesh->dx[x][y];
            a2[x] = 37. / 60. * (a[x] + a[x+1]) - 2. / 15. * (a[x-1] + a[x+2]) + 1. / 60. * (a[x-2] + a[x+3]);

            if(!(a2[x] >= MIN(a[x],a[x+1]) && a2[x] <= MAX(a[x],a[x+1])))
            {
               D2a = 3 / SQR(h) * (a[x] - 2 * a2[x] + a[x+1]);
               D2aL = 1 / SQR(h) * (a[x-1] - 2 * a[x] + a[x+1]);
               D2aR = 1 / SQR(h) * (a[x] - 2 * a[x+1] + a[x+2]);
               if((D2a * D2aL > 0) && (D2a * D2aR > 0))
               {
                  s = SIGN(D2a);
                  D2alim = s * MIN(C * MIN(fabs(D2aL), fabs(D2aR)), fabs(D2a));
               }
               else
                  D2alim = 0;
               a2[x] = 0.5 * (a[x] + a[x+1]) - SQR(h) / 3 * D2alim;
            }
         }

         for(x=mesh->xstart-1; x<=mesh->xend+1; x++)
         {
            ap[x] = a2[x];
            am[x] = a2[x-1];

            if((ap[x]-a[x])*(a[x]-am[x])<=0 && (a[x-1]-a[x])*(a[x]-a[x+1])<=0)
            {
               D2a = - 2 / SQR(h) * (6 * a[x] - 3 * (ap[x] + am[x]));
               D2aC = 1 / SQR(h) * (a[x-1] - 2 * a[x] + a[x+1]);
               D2aL = 1 / SQR(h) * (a[x-2] - 2 * a[x-1] + a[x]);
               D2aR = 1 / SQR(h) * (a[x] - 2 * a[x+1] + a[x+2]);
               if(D2a * D2aC > 0 && D2a * D2aL > 0 && D2a * D2aR > 0)
               {
                  s = SIGN(D2a);
                  D2alim = s * MIN(C * MIN(fabs(D2aL), fabs(D2aR)), MIN(C*fabs(D2aC), fabs(D2a)));
               }
               else
                  D2alim = 0;
               ap[x] = a[x] + (ap[x] - a[x]) * D2alim / D2a;
               am[x] = a[x] + (am[x] - a[x]) * D2alim / D2a;
            }
            else
            {
               s = SIGN(a[x+1] - a[x-1]);
               alphap = ap[x] - a[x];
               alpham = am[x] - a[x];
               if(fabs(alphap) >= 2 * fabs(alpham))
               {
                  deltaf = -SQR(alphap) / (4 * (alphap + alpham));
                  deltaa = a[x+1] - a[x];
                  if(deltaf >= s * deltaa)
                  {
                     ap[x] = a[x] - (2 * deltaa + 2 * s * sqrt(fabs(SQR(deltaa) - deltaa * alpham)));
                  }
               }
               else if(fabs(alpham) >= 2 * fabs(alphap))
               {
                  deltaf = -SQR(alpham) / (4 * (alphap + alpham));
                  deltaa = a[x-1] - a[x];
                  if(deltaf >= s * deltaa)
                  {
                     am[x] = a[x] - (2 * deltaa + 2 * s * sqrt(fabs(SQR(deltaa) - deltaa * alphap)));
                  }
               }
            }

            a6[x] = 6 * a[x] - 3 * (ap[x] + am[x]);
            sigma = u[x] * dt / mesh->dx[x][y];
            if(u[x] >= 0)
               f[x] = ap[x] - sigma / 2. * (ap[x] - am[x] - (1. - 2. /3. * sigma) * a6[x]);
            else if(u[x-1] < 0)
               f[x-1] = am[x] + sigma / 2. * (ap[x] - am[x] + (1. - 2. /3. * sigma) * a6[x]);
         }
         for(x=mesh->xstart; x<=mesh->xend; x++)
            a[x] = u[x] / mesh->dx[x][y] * (f[x-1] - f[x]);
         for(x=0; x<mesh->ystart; x++)
            a[x] = 0;
         for(x=mesh->yend+1; x<mesh->ngy; x++)
            a[x] = 0;
         ddta.setXArray(y, z, a);
      }

   return -ddta;   
}

/// General version for 2 or 3-D objects
const Field3D VDDX(const Field &v, const Field &f, CELL_LOC outloc, DIFF_METHOD method) {
  upwind_func func = fVDDX;
  DiffLookup *table = UpwindTable;

  CELL_LOC vloc = v.getLocation();
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result
  
  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }
  
  if(mesh->StaggerGrids && (vloc != inloc)) {
    // Staggered grids enabled, and velocity at different location to value
    if(vloc == CELL_XLOW) {
      // V staggered w.r.t. variable
      func = sfVDDX;
      table = UpwindStagTable;
      diffloc = CELL_CENTRE;
    }else if((vloc == CELL_CENTRE) && (inloc == CELL_XLOW)) {
      // Shifted
      func = sfVDDX;
      table = UpwindStagTable;
      diffloc = CELL_XLOW;
    }else {
      // More complicated. Deciding what to do here isn't straightforward
      // For now, interpolate velocity to the same location as f.

      // Should be able to do something like:
      //return VDDX(interp_to(v, inloc), f, outloc, method);
      
      // Instead, pretend it's been shifted FIX THIS
      diffloc = vloc;
    }
  }

  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(table, method);
  }

  /// Clone inputs (for shifting)
  Field *vp = v.clone();
  Field *fp = f.clone();
  
  if(mesh->ShiftXderivs && (mesh->ShiftOrder == 0)) {
    // Shift in Z using FFT if needed
    vp->shiftToReal(true);
    fp->shiftToReal(true);
  }
  
  Field3D result;
  result.allocate(); // Make sure data allocated
  BoutReal ***d = result.getData();

  bindex bx;
  
  start_index(&bx);
#ifdef _OPENMP
  // Parallel version
  
  bindex bxstart = bx; // Copy to avoid race condition on first index
  bool workToDoGlobal; // Shared loop control
  #pragma omp parallel
  {
    bindex bxlocal; // Index for each thread
    stencil vval, fval;
    bool workToDo;  // Does this thread have work to do?
    #pragma omp single
    {
      // First index done by a single thread
      for(bxstart.jz=0;bxstart.jz<mesh->ngz-1;bxstart.jz++) {
        vp->setXStencil(vval, bxstart, diffloc);
        fp->setXStencil(fval, bxstart); // Location is always the same as input
    
        d[bxstart.jx][bxstart.jy][bxstart.jz] = func(vval, fval) / mesh->dx(bxstart.jx, bxstart.jy);
      }
    }
    
    do {
      #pragma omp critical
      {
        // Get the next index
        workToDo = next_index2(&bx);
        bxlocal = bx; // Make a local copy
        workToDoGlobal = workToDo;
      }
      if(workToDo) { // Here workToDo could be different to workToDoGlobal
        for(bxlocal.jz=0;bxlocal.jz<mesh->ngz-1;bxlocal.jz++) {
          vp->setXStencil(vval, bxlocal, diffloc);
          fp->setXStencil(fval, bxlocal); // Location is always the same as input
    
          d[bxlocal.jx][bxlocal.jy][bxlocal.jz] = func(vval, fval) / mesh->dx(bxlocal.jx, bxlocal.jy);
        }
      }
    }while(workToDoGlobal);
  }
#else
  // Serial version
  stencil vval, fval;
  if(func == VDDX_PPM)
  {
     result = ext_X(f, 0, v); 
  }
  else
  {
     do {
        vp->setXStencil(vval, bx, diffloc);
        fp->setXStencil(fval, bx); // Location is always the same as input

        d[bx.jx][bx.jy][bx.jz] = func(vval, fval) / mesh->dx(bx.jx, bx.jy);
     }while(next_index3(&bx));
  }
#endif
  
  if(mesh->ShiftXderivs && (mesh->ShiftOrder == 0))
    result = result.shiftZ(false); // Shift back
  
  result.setLocation(inloc);

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif
  
  // Delete clones
  delete vp;
  delete fp;
  
  return interp_to(result, outloc);
}

const Field3D VDDX(const Field &v, const Field &f, DIFF_METHOD method, CELL_LOC outloc) {
  return VDDX(v, f, outloc, method);
}

////////////// Y DERIVATIVE /////////////////

// special case where both are 2D
const Field2D VDDY(const Field2D &v, const Field2D &f, CELL_LOC outloc, DIFF_METHOD method) {
  upwind_func func = fVDDY;
  DiffLookup *table = UpwindTable;

  CELL_LOC vloc = v.getLocation();
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result
  
  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }

  if(mesh->StaggerGrids && (vloc != inloc)) {
    // Staggered grids enabled, and velocity at different location to value
    if(vloc == CELL_YLOW) {
      // V staggered w.r.t. variable
      func = sfVDDY;
      table = UpwindStagTable;
      diffloc = CELL_CENTRE;
    }else if((vloc == CELL_CENTRE) && (inloc == CELL_YLOW)) {
      // Shifted
      func = sfVDDY;
      table = UpwindStagTable;
      diffloc = CELL_YLOW;
    }else {
      // More complicated. Deciding what to do here isn't straightforward
      // For now, interpolate velocity to the same location as f.

      // Should be able to do something like:
      //return VDDY(interp_to(v, inloc), f, outloc, method);
      
      // Instead, pretend it's been shifted FIX THIS
      diffloc = vloc;
    }
  }

  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(table, method);
  }

  bindex bx;
  stencil fval, vval;
  
  Field2D result;
  result.allocate(); // Make sure data allocated
  BoutReal **d = result.getData();

  start_index(&bx);
  do {
    f.setYStencil(fval, bx);
    v.setYStencil(vval, bx, diffloc);
    d[bx.jx][bx.jy] = func(vval,fval)/mesh->dy(bx.jx, bx.jy);
  }while(next_index2(&bx));

  result.setLocation(inloc);
  
#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif

  return interp_to(result, outloc);
}

const Field2D VDDY(const Field2D &v, const Field2D &f, DIFF_METHOD method) {
  return VDDY(v, f, CELL_DEFAULT, method);
}


Field3D ext_Y(const Field &b, BoutReal dt, const Field &v)
{
   rvec a6, ap, am, f, a2;
   rvec a, u;
   Field3D ddta;
   int x, y, z, j;
   BoutReal sigma, D2a, D2aC, D2aL, D2aR, D2alim, s;
   BoutReal alphap, alpham, deltaf, deltaa;
   BoutReal h;

   a6.resize(mesh->ngy);
   ap.resize(mesh->ngy);
   am.resize(mesh->ngy);
   f.resize(mesh->ngy);
   a2.resize(mesh->ngy);

   ddta.allocate();
   ddta = 0;

   for(x=mesh->xstart; x<=mesh->xend; x++)
      for(z=0; z<mesh->ngz; z++)
      {
         b.getYArray(x, z, a);
         v.getYArray(x, z, u);
         for(y=mesh->ystart-2; y<=mesh->yend+1; y++)
         {
            h = mesh->dy[x][y];
            a2[y] = 37. / 60. * (a[y] + a[y+1]) - 2. / 15. * (a[y-1] + a[y+2]) + 1. / 60. * (a[y-2] + a[y+3]);

            if(!(a2[y] >= MIN(a[y],a[y+1]) && a2[y] <= MAX(a[y],a[y+1])))
            {
               D2a = 3 / SQR(h) * (a[y] - 2 * a2[y] + a[y+1]);
               D2aL = 1 / SQR(h) * (a[y-1] - 2 * a[y] + a[y+1]);
               D2aR = 1 / SQR(h) * (a[y] - 2 * a[y+1] + a[y+2]);
               if((D2a * D2aL > 0) && (D2a * D2aR > 0))
               {
                  s = SIGN(D2a);
                  D2alim = s * MIN(C * MIN(fabs(D2aL), fabs(D2aR)), fabs(D2a));
               }
               else
                  D2alim = 0;
               a2[y] = 0.5 * (a[y] + a[y+1]) - SQR(h) / 3 * D2alim;
            }
         }

         for(y=mesh->ystart-1; y<=mesh->yend+1; y++)
         {
            ap[y] = a2[y];
               am[y] = a2[y-1];

            if((ap[y]-a[y])*(a[y]-am[y])<=0 && (a[y-1]-a[y])*(a[y]-a[y+1])<=0)
            {
               D2a = - 2 / SQR(h) * (6 * a[y] - 3 * (ap[y] + am[y]));
               D2aC = 1 / SQR(h) * (a[y-1] - 2 * a[y] + a[y+1]);
               D2aL = 1 / SQR(h) * (a[y-2] - 2 * a[y-1] + a[y]);
               D2aR = 1 / SQR(h) * (a[y] - 2 * a[y+1] + a[y+2]);
               if(D2a * D2aC > 0 && D2a * D2aL > 0 && D2a * D2aR > 0)
               {
                  s = SIGN(D2a);
                  D2alim = s * MIN(C * MIN(fabs(D2aL), fabs(D2aR)), MIN(C*fabs(D2aC), fabs(D2a)));
               }
               else
                  D2alim = 0;
               ap[y] = a[y] + (ap[y] - a[y]) * D2alim / D2a;
               am[y] = a[y] + (am[y] - a[y]) * D2alim / D2a;
            }
            else
            {
               s = SIGN(a[y+1] - a[y-1]);
               alphap = ap[y] - a[y];
               alpham = am[y] - a[y];
               if(fabs(alphap) >= 2 * fabs(alpham))
               {
                  deltaf = -SQR(alphap) / (4 * (alphap + alpham));
                  deltaa = a[y+1] - a[y];
                  if(deltaf >= s * deltaa)
                  {
                     ap[y] = a[y] - (2 * deltaa + 2 * s * sqrt(fabs(SQR(deltaa) - deltaa * alpham)));
                  }
               }
               else if(fabs(alpham) >= 2 * fabs(alphap))
               {
                  deltaf = -SQR(alpham) / (4 * (alphap + alpham));
                  deltaa = a[y-1] - a[y];
                  if(deltaf >= s * deltaa)
                  {
                     am[y] = a[y] - (2 * deltaa + 2 * s * sqrt(fabs(SQR(deltaa) - deltaa * alphap)));
                  }
               }
            }

            a6[y] = 6 * a[y] - 3 * (ap[y] + am[y]);
            sigma = u[y] * dt / mesh->dy[x][y];
            if(u[y] >= 0)
               f[y] = ap[y] - sigma / 2. * (ap[y] - am[y] - (1. - 2. /3. * sigma) * a6[y]);
            else if(u[y-1] < 0)
               f[y-1] = am[y] + sigma / 2. * (ap[y] - am[y] + (1. - 2. /3. * sigma) * a6[y]);
         }
         for(y=mesh->ystart; y<=mesh->yend; y++)
            a[y] = u[y] / mesh->dy[x][y] * (f[y-1] - f[y]);
         for(y=0; y<mesh->ystart; y++)
            a[y] = 0;
         for(y=mesh->yend+1; y<mesh->ngy; y++)
            a[y] = 0;
         ddta.setYArray(x, z, a);
      }

   return -ddta;   
}


Field3D ext_Z(const Field &b, BoutReal dt, const Field &v)
{
   rvec a6, ap, am, f, a2, ta, tu;
   rvec a, u;
   Field3D ddta;
   int x, y, z, j;
   BoutReal sigma, D2a, D2aC, D2aL, D2aR, D2alim, s;
   BoutReal alphap, alpham, deltaf, deltaa;
   BoutReal h;
   BoutReal zstart, zend;

   a6.resize(mesh->ngz+8);
   ap.resize(mesh->ngz+8);
   am.resize(mesh->ngz+8);
   f.resize(mesh->ngz+8);
   a2.resize(mesh->ngz+8);
   a.resize(mesh->ngz+8);
   u.resize(mesh->ngz+8);
   zstart = 4;
   zend = mesh->ngz+3;

   ddta.allocate();
   ddta = 0;

   for(x=mesh->xstart; x<=mesh->xend; x++)
      for(y=mesh->ystart; y<mesh->yend; y++)
      {
         b.getZArray(x, y, ta);
         v.getZArray(x, y, tu);
         for(z=0; z<mesh->ngz; z++)
         {
            a[z+4] = ta[z];
            u[z+4] = tu[z];
         }
         for(z=0; z<zstart; z++)
         {
            a[z] = a[z + mesh->ngz];
            u[z] = u[z + mesh->ngz];
         }
         for(z=zend+1; z<mesh->ngz+8; z++)
         {
            a[z] = a[z - mesh->ngz];
            u[z] = u[z - mesh->ngz];
         }

         for(z=zstart-2; z<=zstart+1; z++)
         {
            h = mesh->dz;
            a2[z] = 37. / 60. * (a[z] + a[z+1]) - 2. / 15. * (a[z-1] + a[z+2]) + 1. / 60. * (a[z-2] + a[z+3]);

            if(!(a2[z] >= MIN(a[z],a[z+1]) && a2[z] <= MAX(a[z],a[z+1])))
            {
               D2a = 3 / SQR(h) * (a[z] - 2 * a2[z] + a[z+1]);
               D2aL = 1 / SQR(h) * (a[z-1] - 2 * a[z] + a[z+1]);
               D2aR = 1 / SQR(h) * (a[z] - 2 * a[z+1] + a[z+2]);
               if((D2a * D2aL > 0) && (D2a * D2aR > 0))
               {
                  s = SIGN(D2a);
                  D2alim = s * MIN(C * MIN(fabs(D2aL), fabs(D2aR)), fabs(D2a));
               }
               else
                  D2alim = 0;
               a2[z] = 0.5 * (a[z] + a[z+1]) - SQR(h) / 3 * D2alim;
            }
         }

         for(z=zstart-1; z<=zend+1; z++)
         {
            ap[z] = a2[z];
            am[z] = a2[z-1];

            if((ap[z]-a[z])*(a[z]-am[z])<=0 && (a[z-1]-a[z])*(a[z]-a[z+1])<=0)
            {
               D2a = - 2 / SQR(h) * (6 * a[z] - 3 * (ap[z] + am[z]));
               D2aC = 1 / SQR(h) * (a[z-1] - 2 * a[z] + a[z+1]);
               D2aL = 1 / SQR(h) * (a[z-2] - 2 * a[z-1] + a[z]);
               D2aR = 1 / SQR(h) * (a[z] - 2 * a[z+1] + a[z+2]);
               if(D2a * D2aC > 0 && D2a * D2aL > 0 && D2a * D2aR > 0)
               {
                  s = SIGN(D2a);
                  D2alim = s * MIN(C * MIN(fabs(D2aL), fabs(D2aR)), MIN(C*fabs(D2aC), fabs(D2a)));
               }
               else
                  D2alim = 0;
               ap[z] = a[z] + (ap[z] - a[z]) * D2alim / D2a;
               am[z] = a[z] + (am[z] - a[z]) * D2alim / D2a;
            }
            else
            {
               s = SIGN(a[z+1] - a[z-1]);
               alphap = ap[z] - a[z];
               alpham = am[z] - a[z];
               if(fabs(alphap) >= 2 * fabs(alpham))
               {
                  deltaf = -SQR(alphap) / (4 * (alphap + alpham));
                  deltaa = a[z+1] - a[z];
                  if(deltaf >= s * deltaa)
                  {
                     ap[z] = a[z] - (2 * deltaa + 2 * s * sqrt(fabs(SQR(deltaa) - deltaa * alpham)));
                  }
               }
               else if(fabs(alpham) >= 2 * fabs(alphap))
               {
                  deltaf = -SQR(alpham) / (4 * (alphap + alpham));
                  deltaa = a[z-1] - a[z];
                  if(deltaf >= s * deltaa)
                  {
                     am[z] = a[z] - (2 * deltaa + 2 * s * sqrt(fabs(SQR(deltaa) - deltaa * alphap)));
                  }
               }
            }

            a6[z] = 6 * a[z] - 3 * (ap[z] + am[z]);
            sigma = u[z] * dt / mesh->dz;
            if(u[z] >= 0)
               f[z] = ap[z] - sigma / 2. * (ap[z] - am[z] - (1. - 2. /3. * sigma) * a6[z]);
            else if(u[z-1] < 0)
               f[z-1] = am[z] + sigma / 2. * (ap[z] - am[z] + (1. - 2. /3. * sigma) * a6[z]);
         }
         for(z=zstart; z<=zend; z++)
            a[z] = u[z] / mesh->dz * (f[z-1] - f[z]);
         for(z=0; z<zstart; z++)
            a[z] = 0;
         for(z=zend+1; z<mesh->ngz+8; z++)
            a[z] = 0;
         ddta.setZArray(x, y, a);
      }

   return -ddta;   
}

// general case
const Field3D VDDY(const Field &v, const Field &f, CELL_LOC outloc, DIFF_METHOD method) {
  upwind_func func = fVDDY;
  DiffLookup *table = UpwindTable;

  CELL_LOC vloc = v.getLocation();
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result
  
  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }

  if(mesh->StaggerGrids && (vloc != inloc)) {
    // Staggered grids enabled, and velocity at different location to value
    if(vloc == CELL_YLOW) {
      // V staggered w.r.t. variable
      func = sfVDDY;
      table = UpwindStagTable;
      diffloc = CELL_CENTRE;
    }else if((vloc == CELL_CENTRE) && (inloc == CELL_YLOW)) {
      // Shifted
      func = sfVDDY;
      table = UpwindStagTable;
      diffloc = CELL_YLOW;
    }else {
      // More complicated. Deciding what to do here isn't straightforward
      // For now, interpolate velocity to the same location as f.

      // Should be able to do something like:
      //return VDDY(interp_to(v, inloc), f, outloc, method);
      
      // Instead, pretend it's been shifted FIX THIS
      diffloc = vloc;
    }
  }
  
  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(table, method);
  }
  
  bindex bx;
  
  Field3D result;
  result.allocate(); // Make sure data allocated
  BoutReal ***d = result.getData();

  start_index(&bx);
#ifdef _OPENMP
  // Parallel version
  
  bindex bxstart = bx; // Copy to avoid race condition on first index
  bool workToDoGlobal; // Shared loop control

  #pragma omp parallel
  {
    bindex bxlocal; // Index for each thread
    stencil vval, fval;
    bool workToDo;  // Does this thread have work to do?
    #pragma omp single
    {
      // First index done by a single thread
      for(bxstart.jz=0;bxstart.jz<mesh->ngz-1;bxstart.jz++) {
        v.setYStencil(vval, bxstart, diffloc);
        f.setYStencil(fval, bxstart);
    
        d[bxstart.jx][bxstart.jy][bxstart.jz] = func(vval, fval)/mesh->dy(bxstart.jx, bxstart.jy);
      }
    }
    
    do {
      #pragma omp critical
      {
        // Get the next index
        workToDo = next_index2(&bx);
        bxlocal = bx; // Make a local copy
        workToDoGlobal = workToDo;
      }
      if(workToDo) { // Here workToDo could be different to workToDoGlobal
        for(bxlocal.jz=0;bxlocal.jz<mesh->ngz-1;bxlocal.jz++) {
          v.setYStencil(vval, bxlocal, diffloc);
          f.setYStencil(fval, bxlocal);
    
          d[bxlocal.jx][bxlocal.jy][bxlocal.jz] = func(vval, fval)/mesh->dy(bxlocal.jx, bxlocal.jy);
        }
      }
    }while(workToDoGlobal);
  }
#else
  // Serial version
  stencil vval, fval;
  if(func == VDDX_PPM)
  {
     result = ext_Y(f, 0, v); 
  }
  else
  {
     do {
        v.setYStencil(vval, bx, diffloc);
        f.setYStencil(fval, bx);

        d[bx.jx][bx.jy][bx.jz] = func(vval, fval)/mesh->dy(bx.jx, bx.jy);
     }while(next_index3(&bx));
  }
#endif
  
  result.setLocation(inloc);

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif

  return interp_to(result, outloc);
}

const Field3D VDDY(const Field &v, const Field &f, DIFF_METHOD method, CELL_LOC outloc) {
  return VDDY(v, f, outloc, method);
}

////////////// Z DERIVATIVE /////////////////

// special case where both are 2D
const Field2D VDDZ(const Field2D &v, const Field2D &f) {
  Field2D result;
  result = 0.0;
  return result;
}

// Note that this is zero because no compression is included
const Field2D VDDZ(const Field3D &v, const Field2D &f) {
  Field2D result;
  result = 0.0;
  return result;
}

// general case
const Field3D VDDZ(const Field &v, const Field &f, CELL_LOC outloc, DIFF_METHOD method) {
  upwind_func func = fVDDZ;
  DiffLookup *table = UpwindTable;

  CELL_LOC vloc = v.getLocation();
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result
  
  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }

  if(mesh->StaggerGrids && (vloc != inloc)) {
    // Staggered grids enabled, and velocity at different location to value
    if(vloc == CELL_ZLOW) {
      // V staggered w.r.t. variable
      func = sfVDDZ;
      table = UpwindStagTable;
      diffloc = CELL_CENTRE;
    }else if((vloc == CELL_CENTRE) && (inloc == CELL_ZLOW)) {
      // Shifted
      func = sfVDDZ;
      table = UpwindStagTable;
      diffloc = CELL_ZLOW;
    }else {
      // More complicated. Deciding what to do here isn't straightforward
      // For now, interpolate velocity to the same location as f.

      // Should be able to do something like:
      //return VDDY(interp_to(v, inloc), f, outloc, method);
      
      // Instead, pretend it's been shifted FIX THIS
      diffloc = vloc;
    }
  }

  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(table, method);
  }

  bindex bx;
  
  
  Field3D result;
  result.allocate(); // Make sure data allocated
  BoutReal ***d = result.getData();
  
  start_index(&bx);
#ifdef _OPENMP
  // Parallel version

  bindex bxstart = bx; // Copy to avoid race condition on first index
  bool workToDoGlobal; // Shared loop control
  
  #pragma omp parallel
  {
    bindex bxlocal; // Index for each thread
    stencil vval, fval;
    bool workToDo;  // Does this thread have work to do?
    #pragma omp single
    {
      // First index done by a single thread
      for(bxstart.jz=0;bxstart.jz<mesh->ngz-1;bxstart.jz++) {
        v.setZStencil(vval, bxstart, diffloc);
        f.setZStencil(fval, bxstart);
    
        d[bxstart.jx][bxstart.jy][bxstart.jz] = func(vval, fval)/mesh->dz;
      }
    }
    
    do {
      #pragma omp critical
      {
        // Get the next index
        workToDo = next_index2(&bx);
        bxlocal = bx; // Make a local copy
        workToDoGlobal = workToDo;
      }
      if(workToDo) { // Here workToDo could be different to workToDoGlobal
        for(bxlocal.jz=0;bxlocal.jz<mesh->ngz-1;bxlocal.jz++) {
          v.setZStencil(vval, bxlocal, diffloc);
          f.setZStencil(fval, bxlocal);
    
          d[bxlocal.jx][bxlocal.jy][bxlocal.jz] = func(vval, fval)/mesh->dz;
        }
      }
    }while(workToDoGlobal);
  }
#else 
  stencil vval, fval;
  if(func == VDDX_PPM)
  {
     result = ext_Z(f, 0, v); 
  }
  else
  {
     do {
        v.setZStencil(vval, bx, diffloc);
        f.setZStencil(fval, bx);

        d[bx.jx][bx.jy][bx.jz] = func(vval, fval)/mesh->dz;
     }while(next_index3(&bx));
  }
#endif

  result.setLocation(inloc);

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif

  return interp_to(result, outloc);
}

const Field3D VDDZ(const Field &v, const Field &f, DIFF_METHOD method, CELL_LOC outloc) {
  return VDDZ(v, f, outloc, method);
}

/*******************************************************************************
 * Flux conserving schemes
 *******************************************************************************/

const Field2D FDDX(const Field2D &v, const Field2D &f) {
  return FDDX(v, f, DIFF_DEFAULT, CELL_DEFAULT);
}

const Field2D FDDX(const Field2D &v, const Field2D &f, CELL_LOC outloc, DIFF_METHOD method) {
  return FDDX(v, f, method, outloc);
}

const Field2D FDDX(const Field2D &v, const Field2D &f, DIFF_METHOD method, CELL_LOC outloc) {
  if( (method == DIFF_SPLIT) || ((method == DIFF_DEFAULT) && (fFDDX == NULL)) ) {
    // Split into an upwind and a central differencing part
    // d/dx(v*f) = v*d/dx(f) + f*d/dx(v)
    return VDDX(v, f) + f * DDX(v);
  }
  
  upwind_func func = fFDDX;
  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(FluxTable, method);
  }
  Field2D result;
  result.allocate(); // Make sure data allocated
  BoutReal **d = result.getData();

  bindex bx;
  stencil vs, fs;
  start_index(&bx);
  do {
    f.setXStencil(fs, bx);
    v.setXStencil(vs, bx);
    
    d[bx.jx][bx.jy] = func(vs, fs) / mesh->dx(bx.jx, bx.jy);
  }while(next_index2(&bx));

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = false;
#endif
  
  return result;
}

const Field3D FDDX(const Field3D &v, const Field3D &f) {
  return FDDX(v, f, DIFF_DEFAULT, CELL_DEFAULT);
}

const Field3D FDDX(const Field3D &v, const Field3D &f, CELL_LOC outloc, DIFF_METHOD method) {
  return FDDX(v, f, method, outloc);
}

const Field3D FDDX(const Field3D &v, const Field3D &f, DIFF_METHOD method, CELL_LOC outloc) {
  if( (method == DIFF_SPLIT) || ((method == DIFF_DEFAULT) && (fFDDX == NULL)) ) {
    // Split into an upwind and a central differencing part
    // d/dx(v*f) = v*d/dx(f) + f*d/dx(v)
    return VDDX(v, f, outloc) + DDX(v, outloc) * f;
  }
  
  upwind_func func = fFDDX;
  DiffLookup *table = FluxTable;

  CELL_LOC vloc = v.getLocation();
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result
  
  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }
  
  if(mesh->StaggerGrids && (vloc != inloc)) {
    // Staggered grids enabled, and velocity at different location to value
    if(vloc == CELL_XLOW) {
      // V staggered w.r.t. variable
      func = sfFDDX;
      table = FluxStagTable;
      diffloc = CELL_CENTRE;
    }else if((vloc == CELL_CENTRE) && (inloc == CELL_XLOW)) {
      // Shifted
      func = sfFDDX;
      table = FluxStagTable;
      diffloc = CELL_XLOW;
    }else {
      // More complicated. Deciding what to do here isn't straightforward
      // For now, interpolate velocity to the same location as f.

      // Should be able to do something like:
      //return VDDX(interp_to(v, inloc), f, outloc, method);
      
      // Instead, pretend it's been shifted FIX THIS
      diffloc = vloc;
    }
  }

  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(table, method);
  }

  /// Clone inputs (for shifting)
  Field3D *vp = v.clone();
  Field3D *fp = f.clone();
  
  if(mesh->ShiftXderivs && (mesh->ShiftOrder == 0)) {
    // Shift in Z using FFT if needed
    vp->shiftToReal(true);
    fp->shiftToReal(true);
  }
  
  Field3D result;
  result.allocate(); // Make sure data allocated
  BoutReal ***d = result.getData();

  bindex bx;
  stencil vval, fval;
  
  start_index(&bx);
  do {
    vp->setXStencil(vval, bx, diffloc);
    fp->setXStencil(fval, bx); // Location is always the same as input
    
    d[bx.jx][bx.jy][bx.jz] = func(vval, fval) / mesh->dx[bx.jx][bx.jy];
  }while(next_index3(&bx));
  
  if(mesh->ShiftXderivs && (mesh->ShiftOrder == 0))
    result = result.shiftZ(false); // Shift back
  
  result.setLocation(inloc);

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif
  
  // Delete clones
  delete vp;
  delete fp;
  
  return interp_to(result, outloc);
}

/////////////////////////////////////////////////////////////////////////

const Field2D FDDY(const Field2D &v, const Field2D &f) {
  return FDDY(v, f, DIFF_DEFAULT, CELL_DEFAULT);
}

const Field2D FDDY(const Field2D &v, const Field2D &f, CELL_LOC outloc, DIFF_METHOD method) {
  return FDDY(v, f, method, outloc);
}

const Field2D FDDY(const Field2D &v, const Field2D &f, DIFF_METHOD method, CELL_LOC outloc) {
  if( (method == DIFF_SPLIT) || ((method == DIFF_DEFAULT) && (fFDDY == NULL)) ) {
    // Split into an upwind and a central differencing part
    // d/dx(v*f) = v*d/dx(f) + f*d/dx(v)
    return VDDY(v, f) + f * DDY(v);
  }
  
  upwind_func func = fFDDY;
  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(FluxTable, method);
  }
  Field2D result;
  result.allocate(); // Make sure data allocated
  BoutReal **d = result.getData();

  bindex bx;
  stencil vs, fs;
  start_index(&bx);
  do {
    f.setYStencil(fs, bx);
    v.setYStencil(vs, bx);
    
    d[bx.jx][bx.jy] = func(vs, fs) / mesh->dy(bx.jx, bx.jy);
  }while(next_index2(&bx));

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = false;
#endif
  
  return result;
}

const Field3D FDDY(const Field3D &v, const Field3D &f) {
  return FDDY(v, f, DIFF_DEFAULT, CELL_DEFAULT);
}

const Field3D FDDY(const Field3D &v, const Field3D &f, CELL_LOC outloc, DIFF_METHOD method) {
  return FDDY(v, f, method, outloc);
}

const Field3D FDDY(const Field3D &v, const Field3D &f, DIFF_METHOD method, CELL_LOC outloc) {
  //output.write("fddy: %d %d %d : %u %u \n", v.getLocation(), f.getLocation(), method, fFDDY, sfFDDY);
  
  if( (method == DIFF_SPLIT) || ((method == DIFF_DEFAULT) && (fFDDY == NULL)) ) {
    // Split into an upwind and a central differencing part
    // d/dx(v*f) = v*d/dx(f) + f*d/dx(v)
    return VDDY(v, f, outloc) + DDY(v, outloc) * f;
  }
  upwind_func func = fFDDY;
  DiffLookup *table = FluxTable;

  CELL_LOC vloc = v.getLocation();
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result
  
  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }
  
  if(mesh->StaggerGrids && (vloc != inloc)) {
    // Staggered grids enabled, and velocity at different location to value    
    if(vloc == CELL_YLOW) {
      // V staggered w.r.t. variable
      func = sfFDDY;
      table = FluxStagTable;
      diffloc = CELL_CENTRE;
    }else if((vloc == CELL_CENTRE) && (inloc == CELL_YLOW)) {
      // Shifted
      func = sfFDDY;
      table = FluxStagTable;
      diffloc = CELL_YLOW;
    }else {
      // More complicated. Deciding what to do here isn't straightforward
      // For now, interpolate velocity to the same location as f.
      
      // Should be able to do something like:
      //return VDDX(interp_to(v, inloc), f, outloc, method);
      
      // Instead, pretend it's been shifted FIX THIS
      diffloc = vloc;
    }
  }

  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(table, method);
  }
  
  if(func == NULL) {
    // To catch when no function
    return VDDY(v, f, outloc) + DDY(v, outloc) * f;
  }

  Field3D result;
  result.allocate(); // Make sure data allocated
  BoutReal ***d = result.getData();

  bindex bx;
  stencil vval, fval;
  
  start_index(&bx);
  do {
    v.setYStencil(vval, bx, diffloc);
    f.setYStencil(fval, bx); // Location is always the same as input
    
    d[bx.jx][bx.jy][bx.jz] = func(vval, fval) / mesh->dy(bx.jx, bx.jy);

  }while(next_index3(&bx));

  result.setLocation(inloc);

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif

  return interp_to(result, outloc);
}

/////////////////////////////////////////////////////////////////////////

const Field2D FDDZ(const Field2D &v, const Field2D &f) {
  return FDDZ(v, f, DIFF_DEFAULT, CELL_DEFAULT);
}

const Field2D FDDZ(const Field2D &v, const Field2D &f, CELL_LOC outloc, DIFF_METHOD method) {
  return FDDZ(v, f, method, outloc);
}

const Field2D FDDZ(const Field2D &v, const Field2D &f, DIFF_METHOD method, CELL_LOC outloc) {
  if( (method == DIFF_SPLIT) || ((method == DIFF_DEFAULT) && (fFDDZ == NULL)) ) {
    // Split into an upwind and a central differencing part
    // d/dx(v*f) = v*d/dx(f) + f*d/dx(v)
    return VDDZ(v, f) + f * DDZ(v);
  }
  
  upwind_func func = fFDDZ;
  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(FluxTable, method);
  }
  Field2D result;
  result.allocate(); // Make sure data allocated
  BoutReal **d = result.getData();

  bindex bx;
  stencil vs, fs;
  start_index(&bx);
  do {
    f.setZStencil(fs, bx);
    v.setZStencil(vs, bx);
    
    d[bx.jx][bx.jy] = func(vs, fs) / mesh->dz;
  }while(next_index2(&bx));

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = false;
#endif
  
  return result;
}

const Field3D FDDZ(const Field3D &v, const Field3D &f) {
  return FDDZ(v, f, DIFF_DEFAULT, CELL_DEFAULT);
}

const Field3D FDDZ(const Field3D &v, const Field3D &f, CELL_LOC outloc, DIFF_METHOD method) {
  return FDDZ(v, f, method, outloc);
}

const Field3D FDDZ(const Field3D &v, const Field3D &f, DIFF_METHOD method, CELL_LOC outloc) {
  if( (method == DIFF_SPLIT) || ((method == DIFF_DEFAULT) && (fFDDZ == NULL)) ) {
    // Split into an upwind and a central differencing part
    // d/dx(v*f) = v*d/dx(f) + f*d/dx(v)
    return VDDZ(v, f, outloc) + DDZ(v, outloc) * f;
  }
  
  upwind_func func = fFDDZ;
  DiffLookup *table = FluxTable;

  CELL_LOC vloc = v.getLocation();
  CELL_LOC inloc = f.getLocation(); // Input location
  CELL_LOC diffloc = inloc; // Location of differential result
  
  if(mesh->StaggerGrids && (outloc == CELL_DEFAULT)) {
    // Take care of CELL_DEFAULT case
    outloc = diffloc; // No shift (i.e. same as no stagger case)
  }
  
  if(mesh->StaggerGrids && (vloc != inloc)) {
    // Staggered grids enabled, and velocity at different location to value
    if(vloc == CELL_ZLOW) {
      // V staggered w.r.t. variable
      func = sfFDDZ;
      table = FluxStagTable;
      diffloc = CELL_CENTRE;
    }else if((vloc == CELL_CENTRE) && (inloc == CELL_ZLOW)) {
      // Shifted
      func = sfFDDZ;
      table = FluxStagTable;
      diffloc = CELL_ZLOW;
    }else {
      // More complicated. Deciding what to do here isn't straightforward
      // For now, interpolate velocity to the same location as f.

      // Should be able to do something like:
      //return VDDX(interp_to(v, inloc), f, outloc, method);
      
      // Instead, pretend it's been shifted FIX THIS
      diffloc = vloc;
    }
  }

  if(method != DIFF_DEFAULT) {
    // Lookup function
    func = lookupUpwindFunc(table, method);
  }
  
  Field3D result;
  result.allocate(); // Make sure data allocated
  BoutReal ***d = result.getData();

  bindex bx;
  stencil vval, fval;
  
  start_index(&bx);
  do {
    v.setZStencil(vval, bx, diffloc);
    f.setZStencil(fval, bx); // Location is always the same as input
    
    d[bx.jx][bx.jy][bx.jz] = func(vval, fval) / mesh->dz;
  }while(next_index3(&bx));
  
  result.setLocation(inloc);

#ifdef CHECK
  // Mark boundaries as invalid
  result.bndry_xin = result.bndry_xout = result.bndry_yup = result.bndry_ydown = false;
#endif
  
  return interp_to(result, outloc);
}
