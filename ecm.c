/* Elliptic Curve Method: toplevel and stage 1 routines.

Copyright 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011,
2012 Paul Zimmermann, Alexander Kruppa, Cyril Bouvier, David Cleaver.

This file is part of the ECM Library.

The ECM Library is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

The ECM Library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
License for more details.

You should have received a copy of the GNU Lesser General Public License
along with the ECM Library; see the file COPYING.LIB.  If not, see
http://www.gnu.org/licenses/ or write to the Free Software Foundation, Inc.,
51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA. */

#include <stdio.h>
#include <stdlib.h>
#include "ecm-impl.h"
#include <math.h>

#ifdef HAVE_LIMITS_H
# include <limits.h>
#else
# define ULONG_MAX __GMP_ULONG_MAX
#endif

#ifdef TIMING_CRT
extern int mpzspv_from_mpzv_slow_time, mpzspv_to_mpzv_time,
  mpzspv_normalise_time;
#endif

/* the following factor takes into account the smaller expected smoothness
   for Montgomery's curves (batch mode) with respect to Suyama's curves */
/* For param 1 we use A=4d-2 with d a square (see main.c). In that
   case, Cyril Bouvier and Razvan Barbulescu have shown that the average
   expected torsion is that of a generic Suyama curve multiplied by the
   constant 2^(1/3)/(3*3^(1/128)) */
#define EXTRA_SMOOTHNESS_SQUARE 0.416384512396064
/* For A=4d-2 (param 3) for d a random integer, the average expected torsion 
   is that of a generic Suyama curve multiplied by the constant 
   1/(3*3^(1/128)) */
#define EXTRA_SMOOTHNESS_32BITS_D 0.330484606500389
/******************************************************************************
*                                                                             *
*                            Elliptic Curve Method                            *
*                                                                             *
******************************************************************************/

void duplicate (mpres_t, mpres_t, mpres_t, mpres_t, mpmod_t, mpres_t, 
                mpres_t, mpres_t, mpres_t) ATTRIBUTE_HOT;
void add3 (mpres_t, mpres_t, mpres_t, mpres_t, mpres_t, mpres_t, mpres_t, 
           mpres_t, mpmod_t, mpres_t, mpres_t, mpres_t) ATTRIBUTE_HOT;

#define mpz_mulmod5(r,s1,s2,m,t) { mpz_mul(t,s1,s2); mpz_mod(r, t, m); }

/* switch from Montgomery's form g*y^2 = x^3 + a*x^2 + x
   to Weierstrass' form          Y^2 = X^3 + A*X + B
   by change of variables x -> g*X-a/3, y -> g*Y.
   We have A = (3-a^2)/(3g^2), X = (3x+a)/(3g), Y = y/g.
   If a factor is found during the modular inverse, returns 
   ECM_FACTOR_FOUND_STEP1 and the factor in f, otherwise returns
   ECM_NO_FACTOR_FOUND.
*/
int 
montgomery_to_weierstrass (mpz_t f, mpres_t x, mpres_t y, mpres_t A, mpmod_t n)
{
  mpres_t g;
  
  mpres_init (g, n);
  mpres_add (g, x, A, n);
  mpres_mul (g, g, x, n);
  mpres_add_ui (g, g, 1, n);
  mpres_mul (g, g, x, n);    /* g = x^3+a*x^2+x (y=1) */
  mpres_mul_ui (y, g, 3, n);
  mpres_mul (y, y, g, n);    /* y = 3g^2 */
  if (!mpres_invert (y, y, n)) /* y = 1/(3g^2) temporarily */
    {
      mpres_gcd (f, y, n);
      mpres_clear (g, n);
      return ECM_FACTOR_FOUND_STEP1;
    }
  
  /* update x */
  mpres_mul_ui (x, x, 3, n); /* 3x */
  mpres_add (x, x, A, n);    /* 3x+a */
  mpres_mul (x, x, g, n);    /* (3x+a)*g */
  mpres_mul (x, x, y, n);    /* (3x+a)/(3g) */

  /* update A */
  mpres_sqr (A, A, n);    /* a^2 */
  mpres_sub_ui (A, A, 3, n);
  mpres_neg (A, A, n);       /* 3-a^2 */
  mpres_mul (A, A, y, n);    /* (3-a^2)/(3g^2) */

  /* update y */
  mpres_mul_ui (g, g, 3, n); /* 3g */
  mpres_mul (y, y, g, n);    /* (3g)/(3g^2) = 1/g */
  
  mpres_clear (g, n);
  return ECM_NO_FACTOR_FOUND;
}

/* adds Q=(x2:z2) and R=(x1:z1) and puts the result in (x3:z3),
     using 6 muls (4 muls and 2 squares), and 6 add/sub.
   One assumes that Q-R=P or R-Q=P where P=(x:z).
     - n : number to factor
     - u, v, w : auxiliary variables
   Modifies: x3, z3, u, v, w.
   (x3,z3) may be identical to (x2,z2) and to (x,z)
*/
void
add3 (mpres_t x3, mpres_t z3, mpres_t x2, mpres_t z2, mpres_t x1, mpres_t z1, 
      mpres_t x, mpres_t z, mpmod_t n, mpres_t u, mpres_t v, mpres_t w)
{
  mpres_sub (u, x2, z2, n);
  mpres_add (v, x1, z1, n);      /* u = x2-z2, v = x1+z1 */

  mpres_mul (u, u, v, n);        /* u = (x2-z2)*(x1+z1) */

  mpres_add (w, x2, z2, n);
  mpres_sub (v, x1, z1, n);      /* w = x2+z2, v = x1-z1 */

  mpres_mul (v, w, v, n);        /* v = (x2+z2)*(x1-z1) */

  mpres_add (w, u, v, n);        /* w = 2*(x1*x2-z1*z2) */
  mpres_sub (v, u, v, n);        /* v = 2*(x2*z1-x1*z2) */

  mpres_sqr (w, w, n);           /* w = 4*(x1*x2-z1*z2)^2 */
  mpres_sqr (v, v, n);           /* v = 4*(x2*z1-x1*z2)^2 */

  if (x == x3) /* same variable: in-place variant */
    {
      /* x3 <- w * z mod n
	 z3 <- x * v mod n */
      mpres_mul (z3, w, z, n);
      mpres_mul (x3, x, v, n);
      mpres_swap (x3, z3, n);
    }
  else
    {
      mpres_mul (x3, w, z, n);   /* x3 = 4*z*(x1*x2-z1*z2)^2 mod n */
      mpres_mul (z3, x, v, n);   /* z3 = 4*x*(x2*z1-x1*z2)^2 mod n */
    }
  /* mul += 6; */
}

/* computes 2P=(x2:z2) from P=(x1:z1), with 5 muls (3 muls and 2 squares)
   and 4 add/sub.
     - n : number to factor
     - b : (a+2)/4 mod n
     - t, u, v, w : auxiliary variables
*/
void
duplicate (mpres_t x2, mpres_t z2, mpres_t x1, mpres_t z1, mpmod_t n, 
           mpres_t b, mpres_t u, mpres_t v, mpres_t w)
{
  mpres_add (u, x1, z1, n);
  mpres_sqr (u, u, n);      /* u = (x1+z1)^2 mod n */
  mpres_sub (v, x1, z1, n);
  mpres_sqr (v, v, n);      /* v = (x1-z1)^2 mod n */
  mpres_mul (x2, u, v, n);  /* x2 = u*v = (x1^2 - z1^2)^2 mod n */
  mpres_sub (w, u, v, n);   /* w = u-v = 4*x1*z1 */
  mpres_mul (u, w, b, n);   /* u = w*b = ((A+2)/4*(4*x1*z1)) mod n */
  mpres_add (u, u, v, n);   /* u = (x1-z1)^2+(A+2)/4*(4*x1*z1) */
  mpres_mul (z2, w, u, n);  /* z2 = ((4*x1*z1)*((x1-z1)^2+(A+2)/4*(4*x1*z1))) mod n */
}

/* multiply P=(x:z) by e and puts the result in (x:z). */
void
ecm_mul (mpres_t x, mpres_t z, mpz_t e, mpmod_t n, mpres_t b)
{
  size_t l;
  int negated = 0;
  mpres_t x0, z0, x1, z1, u, v, w;

  /* In Montgomery coordinates, the point at infinity is (0::0) */
  if (mpz_sgn (e) == 0)
    {
      mpz_set_ui (x, 0);
      mpz_set_ui (z, 0);
      return;
    }

  /* The negative of a point (x:y:z) is (x:-y:u). Since we do not compute
     y, e*(x::z) == (-e)*(x::z). */
  if (mpz_sgn (e) < 0)
    {
      negated = 1;
      mpz_neg (e, e);
    }

  if (mpz_cmp_ui (e, 1) == 0)
    goto ecm_mul_end;

  mpres_init (x0, n);
  mpres_init (z0, n);
  mpres_init (x1, n);
  mpres_init (z1, n);
  mpres_init (u, n);
  mpres_init (v, n);
  mpres_init (w, n);

  l = mpz_sizeinbase (e, 2) - 1; /* l >= 1 */

  mpres_set (x0, x, n);
  mpres_set (z0, z, n);
  duplicate (x1, z1, x0, z0, n, b, u, v, w);

  /* invariant: (P1,P0) = ((k+1)P, kP) where k = floor(e/2^l) */

  while (l-- > 0)
    {
      if (mpz_tstbit (e, l)) /* k, k+1 -> 2k+1, 2k+2 */
        {
          add3 (x0, z0, x0, z0, x1, z1, x, z, n, u, v, w); /* 2k+1 */
          duplicate (x1, z1, x1, z1, n, b, u, v, w); /* 2k+2 */
        }
      else /* k, k+1 -> 2k, 2k+1 */
        {
          add3 (x1, z1, x1, z1, x0, z0, x, z, n, u, v, w); /* 2k+1 */
          duplicate (x0, z0, x0, z0, n, b, u, v, w); /* 2k */
        }
    }

  mpres_set (x, x0, n);
  mpres_set (z, z0, n);

  mpres_clear (x0, n);
  mpres_clear (z0, n);
  mpres_clear (x1, n);
  mpres_clear (z1, n);
  mpres_clear (u, n);
  mpres_clear (v, n);
  mpres_clear (w, n);

ecm_mul_end:

  /* Undo negation to avoid changing the caller's e value */
  if (negated)
    mpz_neg (e, e);
}

#define ADD 6.0 /* number of multiplications in an addition */
#define DUP 5.0 /* number of multiplications in a duplicate */

/* returns the number of modular multiplications for computing
   V_n from V_r * V_{n-r} - V_{n-2r}.
   ADD is the cost of an addition
   DUP is the cost of a duplicate
*/
static double
lucas_cost (ecm_uint n, double v)
{
  ecm_uint d, e, r;
  double c; /* cost */

  d = n;
  r = (ecm_uint) ((double) d * v + 0.5);
  if (r >= n)
    return (ADD * (double) n);
  d = n - r;
  e = 2 * r - n;
  c = DUP + ADD; /* initial duplicate and final addition */
  while (d != e)
    {
      if (d < e)
        {
          r = d;
          d = e;
          e = r;
        }
      if (d - e <= e / 4 && ((d + e) % 3) == 0)
        { /* condition 1 */
          d = (2 * d - e) / 3;
          e = (e - d) / 2;
          c += 3.0 * ADD; /* 3 additions */
        }
      else if (d - e <= e / 4 && (d - e) % 6 == 0)
        { /* condition 2 */
          d = (d - e) / 2;
          c += ADD + DUP; /* one addition, one duplicate */
        }
      else if ((d + 3) / 4 <= e)
        { /* condition 3 */
          d -= e;
          c += ADD; /* one addition */
        }
      else if ((d + e) % 2 == 0)
        { /* condition 4 */
          d = (d - e) / 2;
          c += ADD + DUP; /* one addition, one duplicate */
        }
      /* now d+e is odd */
      else if (d % 2 == 0)
        { /* condition 5 */
          d /= 2;
          c += ADD + DUP; /* one addition, one duplicate */
        }
      /* now d is odd and e is even */
      else if (d % 3 == 0)
        { /* condition 6 */
          d = d / 3 - e;
          c += 3.0 * ADD + DUP; /* three additions, one duplicate */
        }
      else if ((d + e) % 3 == 0)
        { /* condition 7 */
          d = (d - 2 * e) / 3;
          c += 3.0 * ADD + DUP; /* three additions, one duplicate */
        }
      else if ((d - e) % 3 == 0)
        { /* condition 8 */
          d = (d - e) / 3;
          c += 3.0 * ADD + DUP; /* three additions, one duplicate */
        }
      else /* necessarily e is even: catches all cases */
        { /* condition 9 */
          e /= 2;
          c += ADD + DUP; /* one addition, one duplicate */
        }
    }
  
  return c;
}


/* computes kP from P=(xA:zA) and puts the result in (xA:zA). Assumes k>2. 
   WARNING! The calls to add3() assume that the two input points are distinct,
   which is not neccessarily satisfied. The result can be that in rare cases
   the point at infinity (z==0) results when it shouldn't. A test case is 
   echo 33554520197234177 | ./ecm -sigma 2046841451 373 1
   which finds the prime even though it shouldn't (23^2=529 divides order).
   This is not a problem for ECM since at worst we'll find a factor we 
   shouldn't have found. For other purposes (i.e. primality proving) this 
   would have to be fixed first.
*/

static void
prac (mpres_t xA, mpres_t zA, ecm_uint k, mpmod_t n, mpres_t b,
      mpres_t u, mpres_t v, mpres_t w, mpres_t xB, mpres_t zB, mpres_t xC, 
      mpres_t zC, mpres_t xT, mpres_t zT, mpres_t xT2, mpres_t zT2)
{
  ecm_uint d, e, r, i = 0, nv;
  double c, cmin;
  __mpz_struct *tmp;
#define NV 10  
  /* 1/val[0] = the golden ratio (1+sqrt(5))/2, and 1/val[i] for i>0
     is the real number whose continued fraction expansion is all 1s
     except for a 2 in i+1-st place */
  static double val[NV] =
    { 0.61803398874989485, 0.72360679774997897, 0.58017872829546410,
      0.63283980608870629, 0.61242994950949500, 0.62018198080741576,
      0.61721461653440386, 0.61834711965622806, 0.61791440652881789,
      0.61807966846989581};

  /* for small n, it makes no sense to try 10 different Lucas chains */
  nv = mpz_size ((mpz_ptr) n);
  if (nv > NV)
    nv = NV;

  if (nv > 1)
    {
      /* chooses the best value of v */
      for (d = 0, cmin = ADD * (double) k; d < nv; d++)
        {
          c = lucas_cost (k, val[d]);
          if (c < cmin)
            {
              cmin = c;
              i = d;
            }
        }
    }

  d = k;
  r = (ecm_uint) ((double) d * val[i] + 0.5);
  
  /* first iteration always begins by Condition 3, then a swap */
  d = k - r;
  e = 2 * r - k;
  mpres_set (xB, xA, n);
  mpres_set (zB, zA, n); /* B=A */
  mpres_set (xC, xA, n);
  mpres_set (zC, zA, n); /* C=A */
  duplicate (xA, zA, xA, zA, n, b, u, v, w); /* A = 2*A */
  while (d != e)
    {
      if (d < e)
        {
          r = d;
          d = e;
          e = r;
          mpres_swap (xA, xB, n);
          mpres_swap (zA, zB, n);
        }
      /* do the first line of Table 4 whose condition qualifies */
      if (d - e <= e / 4 && ((d + e) % 3) == 0)
        { /* condition 1 */
          d = (2 * d - e) / 3;
          e = (e - d) / 2;
          add3 (xT, zT, xA, zA, xB, zB, xC, zC, n, u, v, w); /* T = f(A,B,C) */
          add3 (xT2, zT2, xT, zT, xA, zA, xB, zB, n, u, v, w); /* T2 = f(T,A,B) */
          add3 (xB, zB, xB, zB, xT, zT, xA, zA, n, u, v, w); /* B = f(B,T,A) */
          mpres_swap (xA, xT2, n);
          mpres_swap (zA, zT2, n); /* swap A and T2 */
        }
      else if (d - e <= e / 4 && (d - e) % 6 == 0)
        { /* condition 2 */
          d = (d - e) / 2;
          add3 (xB, zB, xA, zA, xB, zB, xC, zC, n, u, v, w); /* B = f(A,B,C) */
          duplicate (xA, zA, xA, zA, n, b, u, v, w); /* A = 2*A */
        }
      else if ((d + 3) / 4 <= e)
        { /* condition 3 */
          d -= e;
          add3 (xT, zT, xB, zB, xA, zA, xC, zC, n, u, v, w); /* T = f(B,A,C) */
          /* circular permutation (B,T,C) */
          tmp = xB;
          xB = xT;
          xT = xC;
          xC = tmp;
          tmp = zB;
          zB = zT;
          zT = zC;
          zC = tmp;
        }
      else if ((d + e) % 2 == 0)
        { /* condition 4 */
          d = (d - e) / 2;
          add3 (xB, zB, xB, zB, xA, zA, xC, zC, n, u, v, w); /* B = f(B,A,C) */
          duplicate (xA, zA, xA, zA, n, b, u, v, w); /* A = 2*A */
        }
      /* now d+e is odd */
      else if (d % 2 == 0)
        { /* condition 5 */
          d /= 2;
          add3 (xC, zC, xC, zC, xA, zA, xB, zB, n, u, v, w); /* C = f(C,A,B) */
          duplicate (xA, zA, xA, zA, n, b, u, v, w); /* A = 2*A */
        }
      /* now d is odd, e is even */
      else if (d % 3 == 0)
        { /* condition 6 */
          d = d / 3 - e;
          duplicate (xT, zT, xA, zA, n, b, u, v, w); /* T = 2*A */
          add3 (xT2, zT2, xA, zA, xB, zB, xC, zC, n, u, v, w); /* T2 = f(A,B,C) */
          add3 (xA, zA, xT, zT, xA, zA, xA, zA, n, u, v, w); /* A = f(T,A,A) */
          add3 (xT, zT, xT, zT, xT2, zT2, xC, zC, n, u, v, w); /* T = f(T,T2,C) */
          /* circular permutation (C,B,T) */
          tmp = xC;
          xC = xB;
          xB = xT;
          xT = tmp;
          tmp = zC;
          zC = zB;
          zB = zT;
          zT = tmp;
        }
      else if ((d + e) % 3 == 0)
        { /* condition 7 */
          d = (d - 2 * e) / 3;
          add3 (xT, zT, xA, zA, xB, zB, xC, zC, n, u, v, w); /* T = f(A,B,C) */
          add3 (xB, zB, xT, zT, xA, zA, xB, zB, n, u, v, w); /* B = f(T,A,B) */
          duplicate (xT, zT, xA, zA, n, b, u, v, w);
          add3 (xA, zA, xA, zA, xT, zT, xA, zA, n, u, v, w); /* A = 3*A */
        }
      else if ((d - e) % 3 == 0)
        { /* condition 8 */
          d = (d - e) / 3;
          add3 (xT, zT, xA, zA, xB, zB, xC, zC, n, u, v, w); /* T = f(A,B,C) */
          add3 (xC, zC, xC, zC, xA, zA, xB, zB, n, u, v, w); /* C = f(A,C,B) */
          mpres_swap (xB, xT, n);
          mpres_swap (zB, zT, n); /* swap B and T */
          duplicate (xT, zT, xA, zA, n, b, u, v, w);
          add3 (xA, zA, xA, zA, xT, zT, xA, zA, n, u, v, w); /* A = 3*A */
        }
      else /* necessarily e is even here */
        { /* condition 9 */
          e /= 2;
          add3 (xC, zC, xC, zC, xB, zB, xA, zA, n, u, v, w); /* C = f(C,B,A) */
          duplicate (xB, zB, xB, zB, n, b, u, v, w); /* B = 2*B */
        }
    }
  
  add3 (xA, zA, xA, zA, xB, zB, xC, zC, n, u, v, w);

  ASSERT(d == 1);
}


/* Input: x is initial point
          A is curve parameter in Montgomery's form:
          g*y^2*z = x^3 + a*x^2*z + x*z^2
          n is the number to factor
	  B1 is the stage 1 bound
   Output: If a factor is found, it is returned in f.
           Otherwise, x contains the x-coordinate of the point computed
           in stage 1 (with z coordinate normalized to 1).
	   B1done is set to B1 if stage 1 completed normally,
	   or to the largest prime processed if interrupted, but never
	   to a smaller value than B1done was upon function entry.
   Return value: ECM_FACTOR_FOUND_STEP1 if a factor, otherwise 
           ECM_NO_FACTOR_FOUND
*/
static int
ecm_stage1 (mpz_t f, mpres_t x, mpres_t A, mpmod_t n, double B1, 
            double *B1done, mpz_t go, int (*stop_asap)(void), 
            char *chkfilename)
{
  mpres_t b, z, u, v, w, xB, zB, xC, zC, xT, zT, xT2, zT2;
  double p, r, last_chkpnt_p;
  int ret = ECM_NO_FACTOR_FOUND;
  long last_chkpnt_time;

  mpres_init (b, n);
  mpres_init (z, n);
  mpres_init (u, n);
  mpres_init (v, n);
  mpres_init (w, n);
  mpres_init (xB, n);
  mpres_init (zB, n);
  mpres_init (xC, n);
  mpres_init (zC, n);
  mpres_init (xT, n);
  mpres_init (zT, n);
  mpres_init (xT2, n);
  mpres_init (zT2, n);
  
  last_chkpnt_time = cputime ();

  mpres_set_ui (z, 1, n);

  mpres_add_ui (b, A, 2, n);
  mpres_div_2exp (b, b, 2, n); /* b == (A0+2)*B/4, where B=2^(k*GMP_NUMB_BITS)
                                  for MODMULN or REDC, B=1 otherwise */
  /* preload group order */
  if (go != NULL)
    ecm_mul (x, z, go, n, b);

  /* prac() wants multiplicands > 2 */
  for (r = 2.0; r <= B1; r *= 2.0)
    if (r > *B1done)
      duplicate (x, z, x, z, n, b, u, v, w);
  
  /* We'll do 3 manually, too (that's what ecm4 did..) */
  for (r = 3.0; r <= B1; r *= 3.0)
    if (r > *B1done)
      {
        duplicate (xB, zB, x, z, n, b, u, v, w);
        add3 (x, z, x, z, xB, zB, x, z, n, u, v, w);
      }
  
  last_chkpnt_p = 3.;
  p = getprime (); /* Puts 3.0 into p. Next call gives 5.0 */
  for (p = getprime (); p <= B1; p = getprime ())
    {
      for (r = p; r <= B1; r *= p)
	if (r > *B1done)
	  prac (x, z, (ecm_uint) p, n, b, u, v, w, xB, zB, xC, zC, xT,
		zT, xT2, zT2);

      if (mpres_is_zero (z, n))
        {
          outputf (OUTPUT_VERBOSE, "Reached point at infinity, %.0f divides "
                   "group order\n", p);
          break;
        }

      if (stop_asap != NULL && (*stop_asap) ())
        {
          outputf (OUTPUT_NORMAL, "Interrupted at prime %.0f\n", p);
          break;
        }

      if (chkfilename != NULL && p > last_chkpnt_p + 10000. && 
          elltime (last_chkpnt_time, cputime ()) > CHKPNT_PERIOD)
        {
	  writechkfile (chkfilename, ECM_ECM, MAX(p, *B1done), n, A, x, NULL, z);
          last_chkpnt_p = p;
          last_chkpnt_time = cputime ();
        }
    }
  
  /* If stage 1 finished normally, p is the smallest prime >B1 here.
     In that case, set to B1 */
  if (p > B1)
      p = B1;
  
  if (p > *B1done)
      *B1done = p;

  if (chkfilename != NULL)
    writechkfile (chkfilename, ECM_ECM, *B1done, n, A, x, NULL, z);
  getprime_clear (); /* free the prime tables, and reinitialize */

  if (!mpres_invert (u, z, n)) /* Factor found? */
    {
      mpres_gcd (f, z, n);
      ret = ECM_FACTOR_FOUND_STEP1;
    }
  mpres_mul (x, x, u, n);

  mpres_clear (zT2, n);
  mpres_clear (xT2, n);
  mpres_clear (zT, n);
  mpres_clear (xT, n);
  mpres_clear (zC, n);
  mpres_clear (xC, n);
  mpres_clear (zB, n);
  mpres_clear (xB, n);
  mpres_clear (w, n);
  mpres_clear (v, n);
  mpres_clear (u, n);
  mpres_clear (z, n);
  mpres_clear (b, n);

  return ret;
}

/******************** Weierstrass section ********************/

#define DEBUG_EC_W 0

void
pt_w_set_to_zero(ec_point_t P, mpmod_t n)
{
    mpres_set_ui (P->x, 0, n);
    mpres_set_ui (P->y, 1, n);
    mpres_set_ui (P->z, 0, n);
}

int
pt_w_is_zero(mpres_t z, mpmod_t n)
{
    return mpres_is_zero(z, n);
}

#if EC_W_LAW == EC_W_LAW_AFFINE
int
pt_w_is_equal(mpres_t x0, mpres_t y0, mpres_t z0,
	      mpres_t x1, mpres_t y1, mpres_t z1)
{
    return (mpz_cmp(x0, x1) == 0 && mpz_cmp(y0, y1) == 0 && mpz_cmp(z0, z1));
}
#elif EC_W_LAW == EC_W_LAW_PROJECTIVE
/* if need be, this is easy to write */
#endif

void
pt_w_set(mpres_t x0, mpres_t y0, mpres_t z0,
	    mpres_t x, mpres_t y, mpres_t z,
	    ATTRIBUTE_UNUSED mpmod_t n)
{
  mpres_set (x0, x, n);
  mpres_set (y0, y, n);
  mpres_set (z0, z, n);
}

static void
print_mpz_from_mpres(mpres_t x, mpmod_t n)
{
    mpz_t tmp;

    mpz_init(tmp);
    mpres_get_z(tmp, x, n);
    gmp_printf("%Zd", tmp);
    mpz_clear(tmp);
}

void
pt_w_print(mpres_t x, mpres_t y, mpres_t z, mpmod_t n)
{
    printf("[");
    print_mpz_from_mpres(x, n);
    printf(", ");
    print_mpz_from_mpres(y, n);
    printf(", ");
#if EC_W_LAW == EC_W_LAW_AFFINE
    gmp_printf("%Zd", z);
#else
    print_mpz_from_mpres(z, n);
#endif
    printf("]");
}

#if EC_W_LAW == EC_W_LAW_AFFINE
int
pt_w_common(mpres_t x0, mpres_t y0, mpres_t z0,
	    mpres_t x1, mpres_t y1,
	    mpres_t x2,
	    mpmod_t n, mpres_t num, mpres_t den, mpres_t inv)
{
    if(mpres_invert(inv, den, n) == 0){
	mpres_gcd(x0, den, n);
	return 0;
    }
    mpres_mul(inv, inv, num, n);
    mpres_mul(num, inv, inv, n);
    mpres_sub(den, num, x1, n);
    mpres_sub(den, den, x2, n);
    mpres_sub(num, x1, den, n);
    mpres_mul(num, num, inv, n);
    mpres_sub(y0, num, y1, n);
    mpres_set(x0, den, n);
    mpz_set_ui(z0, 1); /* just in case */
    return 1;
}
#endif

/* [x0, y0, z0] <- [2] * [x, y, z] */
int
pt_w_duplicate(mpres_t x3, mpres_t y3, mpres_t z3,
	       mpres_t x1, mpres_t y1, mpres_t z1,
	       mpmod_t n, ec_curve_t E)
{
    if(pt_w_is_zero(z1, n))
      {
	pt_w_set(x3, y3, z3, x1, y1, z1, n);
	return 1;
      }
#if EC_W_LAW == EC_W_LAW_AFFINE
    /* buf[1] <- 2*y1 */
    mpres_add(E->buf[1], y1, y1, n);
    if(mpres_is_zero(y1, n))
      {
	/* y1 = 0 <=> P is a [2]-torsion point */
	pt_w_set(x3, y3, z3, x1, y1, z1, n);
	return 1;
      }
    /* buf[0] <- 3*x^2+A */
    mpres_mul_ui(E->buf[0], x1, 3, n);
    mpres_mul(E->buf[0], E->buf[0], x1, n);
    mpres_add(E->buf[0], E->buf[0], A, n);
    return pt_w_common(x3, y3, z3, x1, y1, x1, n, E->buf[0], E->buf[1], E->buf[2]);
#elif EC_W_LAW == EC_W_LAW_PROJECTIVE
    /* source is dbl-2007-bl: 5M + 6S + 1*a + 7add + 3*2 + 1*3 */
    /* mapping: h = buf[0], w = buf[1], s = buf[2], RR = buf[3], B = buf[4];*/
    /*  h:=X1^2 mod p;	    # S*/
    mpres_sqr(E->buf[0], x1, n);
    /*	w:=Z1^2 mod p;*/
    mpres_sqr(E->buf[1], z1, n);
    /*	w:=a*w mod p;*/
    mpres_mul(E->buf[1], E->buf[1], E->A, n);
    /*	s:=3*h mod p;	    # *3*/
    mpres_mul_ui(E->buf[2], E->buf[0], 3, n);
    /*	w:=w+s mod p;*/
    mpres_add(E->buf[1], E->buf[1], E->buf[2], n);
    /*	s:=Y1*Z1 mod p;*/
    mpres_mul(E->buf[2], y1, z1, n);
    /*	s:=2*s mod p;*/
    mpres_mul_ui(E->buf[2], E->buf[2], 2, n);
    /*	Z3:=s^2 mod p;*/
    mpres_sqr(z3, E->buf[2], n);
    /*	Z3:=s*Z3 mod p;*/
    mpres_mul(z3, z3, E->buf[2], n);
    /*	RR:=Y1*s mod p;	    # M*/
    mpres_mul(E->buf[3], y1, E->buf[2], n);
    /*	B:=X1+RR mod p;    # add*/
    mpres_add(E->buf[4], x1, E->buf[3], n);
    /*	B:=B^2 mod p;*/
    mpres_sqr(E->buf[4], E->buf[4], n);
    /*	RR:=RR^2 mod p;	    # S*/
    mpres_sqr(E->buf[3], E->buf[3], n);
    /*	B:=B-h mod p;*/
    mpres_sub(E->buf[4], E->buf[4], E->buf[0], n);
    /*	B:=B-RR mod p;*/
    mpres_sub(E->buf[4], E->buf[4], E->buf[3], n);
    /*	h:=w^2 mod p;*/
    mpres_sqr(E->buf[0], E->buf[1], n);
    /*	X3:=2*B mod p;*/
    mpres_mul_ui(x3, E->buf[4], 2, n);
    /*	h:=h-X3 mod p;*/
    mpres_sub(E->buf[0], E->buf[0], x3, n);
    /*	X3:=h*s mod p;	    # M*/
    mpres_mul(x3, E->buf[0], E->buf[2], n);
    /*	s:=B-h mod p;*/
    mpres_sub(E->buf[2], E->buf[4], E->buf[0], n);
    /*	s:=w*s mod p;*/
    mpres_mul(E->buf[2], E->buf[2], E->buf[1], n);
    /*	Y3:=2*RR mod p;*/
    mpres_mul_ui(y3, E->buf[3], 2, n);
    /*	Y3:=s-Y3 mod p;*/
    mpres_sub(y3, E->buf[2], y3, n);
    return 1;
#endif
}

/* [x3, y3, z3] <- [x1, y1, z1] + [x2, y2, z2]; P3 can be either P1 or P2. */
int
pt_w_add(mpres_t x3, mpres_t y3, mpres_t z3,
	 mpres_t x1, mpres_t y1, mpres_t z1,
	 mpres_t x2, mpres_t y2, mpres_t z2,
	 mpmod_t n, ec_curve_t E)
{
    if(pt_w_is_zero(z1, n)){
	pt_w_set(x3, y3, z3, x2, y2, z2, n);
	return 1;
    }
    else if(pt_w_is_zero(z2, n)){
	pt_w_set(x3, y3, z3, x1, y1, z1, n);
	return 1;
    }
#if EC_W_LAW == EC_W_LAW_AFFINE
    else if(pt_w_is_equal(x1, y1, z1, x2, y2, z2))
	return pt_w_duplicate(x3, y3, z3, x1, y1, z1, n, E);
    else{
	mpres_sub(E->buf[0], y1, y2, n);
	mpres_sub(E->buf[1], x1, x2, n);
	return pt_w_common(x3, y3, z3, x1, y1, x2, n, 
			   E->buf[0], E->buf[1], E->buf[2]);
    }
#elif EC_W_LAW == EC_W_LAW_PROJECTIVE
    /* Cohen-Miyaji-Ono: 12M+2S+6add+1*2 */
    /* mapping: y1z2 = buf, AA = buf+1, u = buf+2, v = buf+3, R = buf+4, */
    /* vvv = buf+5; */
    /*  Y1Z2:=Y1*Z2 mod p;	# M*/
    mpres_mul(E->buf[0], y1, z2, n);
    /*	A:=X1*Z2 mod p;	# M*/
    mpres_mul(E->buf[1], x1, z2, n);
    /*	u:=Y2*Z1 mod p;*/
    mpres_mul(E->buf[2], y2, z1, n);
    /*	u:=u-Y1Z2 mod p;*/
    mpres_sub(E->buf[2], E->buf[2], E->buf[0], n);
    /*	v:=X2*Z1 mod p;*/
    mpres_mul(E->buf[3], x2, z1, n);
    /*	v:=v-A mod p;*/
    mpres_sub(E->buf[3], E->buf[3], E->buf[1], n);
    if(mpz_sgn(E->buf[2]) == 0 && mpz_sgn(E->buf[3]) == 0){
	/* u = 0 <=> Y2*Z1 = Y1*Z2 <=> Y2/Z2 = Y1/Z1*/
	/* v = 0 <=> X2*Z1 = X1*Z2 <=> X2/Z2 = X1/Z1*/
	return pt_w_duplicate(x3, y3, z3, x1, y1, z1, n, E);
    }
    /*	Z3:=Z1*Z2 mod p;	# M*/
    mpres_mul(z3, z1, z2, n);
    /*	X3:=u^2 mod p;*/
    mpres_sqr(x3, E->buf[2], n);
    /*	X3:=X3*Z3 mod p;*/
    mpres_mul(x3, x3, z3, n);
    /*	R:=v^2 mod p;*/
    mpres_sqr(E->buf[4], E->buf[3], n);
    /*	vvv:=v*R mod p;*/
    mpres_mul(E->buf[5], E->buf[3], E->buf[4], n);
    /*	R:=R*A mod p;*/
    mpres_mul(E->buf[4], E->buf[4], E->buf[1], n);
    /*	Y3:=2*R mod p; 		# *2*/
    mpres_mul_ui(y3, E->buf[4], 2, n);
    /*	A:=X3-vvv mod p;*/
    mpres_sub(E->buf[1], x3, E->buf[5], n);
    /*	A:=A-Y3 mod p;*/
    mpres_sub(E->buf[1], E->buf[1], y3, n);
    /*	X3:=v*A mod p;		# M*/
    mpres_mul(x3, E->buf[3], E->buf[1], n);
    /*	Y3:=R-A mod p;*/
    mpres_sub(y3, E->buf[4], E->buf[1], n);
    /*	Y3:=u*Y3 mod p;*/
    mpres_mul(y3, y3, E->buf[2], n);
    /*	A:=vvv*Y1Z2 mod p;*/
    mpres_mul(E->buf[1], E->buf[5], E->buf[0], n);
    /*	Y3:=Y3-A mod p;*/
    mpres_sub(y3, y3, E->buf[1], n);
    /*	Z3:=vvv*Z3 mod p;	# M*/
    mpres_mul(z3, z3, E->buf[5], n);
    return 1;
#endif
}

/* [x3, y3, z3] <- [x1, y1, z1] - [x2, y2, z2]; P3 != P1, P3 != P1. */
int
pt_w_sub(mpres_t x3, mpres_t y3, mpres_t z3,
	 mpres_t x1, mpres_t y1, mpres_t z1,
	 mpres_t x2, mpres_t y2, mpres_t z2,
	 mpmod_t n, ec_curve_t E)
{
    int res;

    mpres_neg(y2, y2, n);
    res = pt_w_add(x3, y3, z3, x1, y1, z1, x2, y2, z2, n, E);
    mpres_neg(y2, y2, n);
    return res;
}

#if 0 /* TODO: make this work again? */
/* multiply P=(x:y:z) by e and puts the result in (x:y:z).
   Return value: 0 if a factor is found, and the factor is in x,
                 1 otherwise.
*/
int
pt_w_mul (mpres_t x, mpres_t y, mpres_t z, mpz_t e, ec_curve_t E, mpmod_t n)
{
  size_t l;
  int negated = 0, status = 1;
  mpres_t x0, y0, z0;

  if(pt_w_is_zero(z, n))
    return 1;

  if (mpz_sgn (e) == 0)
    {
      pt_w_set_to_zero(x, y, z, n);
      return 1;
    }

  /* The negative of a point (x:y:z) is (x:-y:z) */
  if (mpz_sgn (e) < 0)
    {
      negated = 1;
      mpz_neg (e, e);
      mpres_neg (y, y, n); /* since the point is non-zero */
    }

  if (mpz_cmp_ui (e, 1) == 0)
    goto pt_w_mul_end;

  l = mpz_sizeinbase (e, 2) - 1; /* l >= 1 */

  mpres_init (x0, n);
  mpres_init (y0, n);
  mpres_init (z0, n);
  pt_w_set(x0, y0, z0, x, y, z, n);

  while (l-- > 0)
    {
	if(pt_w_duplicate (x0, y0, z0, x0, y0, z0, n, E) == 0)
	  {
	    status = 0;
	    break;
	  }
#if DEBUG_EC_W >= 2
	printf("Rdup:="); pt_w_print(x0, y0, z0, n); printf(";\n");
#endif
	if (mpz_tstbit (e, l))
	  {
	    if(pt_w_add (x0, y0, z0, x0, y0, z0, x, y, z, n, E) == 0)
	      {
		status = 0;
		break;
	      }
#if DEBUG_EC_W >= 2
	    printf("Radd:="); pt_w_print(x0, y0, z0, n); printf(";\n");
#endif
	  }
    }

#if 0
#if EC_W_LAW == EC_W_LAW_PROJECTIVE
  /* not clear! Perhaps it has sense to normalize P? */
  mpres_gcd(z, z0, n); /* very costly */
  if(mpz_cmp_ui(z, 1) != 0){
      /* factor found, even n... */
      mpres_set(x0, z, n);
      status = 0;
  }
#endif
#endif
  mpres_set (x, x0, n);
  mpres_set (y, y0, n);
  mpres_set (z, z0, n);

  mpres_clear (x0, n);
  mpres_clear (y0, n);
  mpres_clear (z0, n);

pt_w_mul_end:

  /* Undo negation to avoid changing the caller's e value */
  if (negated)
    mpz_neg (e, e);
  return status;
}
#endif

/* build NAF_w(c) = (u_{ell-1}, ..., u_0).
   When w = 2, we have 2^ell < 3*e < 2^(ell+1).
   TODO: have this work for e > long.
*/
int
build_NAF(long *S, mpz_t e, int w)
{
    long u, twowm1 = ((long)1 << w)-1;
    long hwm1 = ((long)1)<<(w-1), hw = ((long)1)<<w;
    int iS = 0;
#if 1
    long c;

    c = mpz_get_si(e);
    while(c > 0){
	if((c & 1) == 1){
	    /* c is odd */
	    /* u <- mods(c, 2^w) */
	    u = c & (long)twowm1;
	    if(u >= hwm1)
		u -= hw;
	    c -= u;
	}
	else
	    u = 0;
	S[iS++] = u;
	c >>= 1;
    }
#else
    mpz_t c;

    mpz_init_set(c, e);
    /* we operate on windows of size w, starting from lower order bits.
       This version copies the long-version one... Some more bit-fiddling
       might be used to replace the shift at the end of the loop.
     */
    while(mpz_sgn(c) != 0){
	if(mpz_tstbit(c) == 1){
	    /* c is odd */
	    /* u <- mods(c, 2^w) */
	    u = (long)(mpz_get_si(c) & (unsigned long)twowm1);
	    if(u >= hwm1)
		u -= hw;
	    mpz_sub_si(c, c, u);
	    /* at this point, c = 0 mod 2^w */
	}
	else
	    u = 0;
	S[iS++] = u;
	/* c >>= 1; */
	mpz_cdiv_q_2exp(c, c, 1);
    }
#endif
    return iS;
}

/* Checks that x1/z1 = x2/z2 and y1/z1 = y2/z2.
   OUTPUT: 1 if equals, 0 otherwise.
 */
int
pt_w_cmp(mpres_t x1, mpres_t y1, mpres_t z1,
	 mpres_t x2, mpres_t y2, mpres_t z2,
	 mpmod_t n)
{
    if(pt_w_is_zero(z1, n))
	return pt_w_is_zero(z2, n);
    else if(pt_w_is_zero(z2, n))
	return pt_w_is_zero(z1, n);
    else{
	mpres_t tmp1, tmp2;
	int cmp = 1;

	mpres_init(tmp1, n);
	mpres_init(tmp2, n);
	mpres_mul(tmp1, x1, z2, n);
	mpres_mul(tmp2, x2, z1, n);
	mpres_sub(tmp1, tmp1, tmp2, n);
	if(mpres_is_zero(tmp1, n) == 0)
	    cmp = 0;
	else{
	    mpres_mul(tmp1, y1, z2, n);
	    mpres_mul(tmp2, y2, z1, n);
	    mpres_sub(tmp1,tmp1, tmp2, n);
	    cmp = mpres_is_zero(tmp1, n);
	}
	mpres_clear(tmp1, n);
	mpres_clear(tmp2, n);
	return cmp;
    }
}

/******************** generic ec's ********************/

int
ec_point_is_zero(ec_point_t P, ec_curve_t E, mpmod_t n)
{
    if(E->type == ECM_EC_TYPE_WEIERSTRASS)
	return pt_w_is_zero(P->z, n);
    return 0;
}

void
ec_point_set_to_zero(ec_point_t P, ec_curve_t E, mpmod_t n)
{
    if(E->type == ECM_EC_TYPE_WEIERSTRASS)
	pt_w_set_to_zero(P, n);
}

int
ec_point_add(ec_point_t R, ec_point_t P, ec_point_t Q, ec_curve_t E, mpmod_t n)
{
    if(E->type == ECM_EC_TYPE_WEIERSTRASS)
	return pt_w_add(R->x, R->y, R->z, P->x, P->y, P->z, Q->x, Q->y, Q->z,
			n, E);
}

int
ec_point_sub(ec_point_t R, ec_point_t P, ec_point_t Q, ec_curve_t E, mpmod_t n)
{
    if(E->type == ECM_EC_TYPE_WEIERSTRASS)
	return pt_w_sub(R->x, R->y, R->z, P->x, P->y, P->z, Q->x, Q->y, Q->z,
			n, E);
}

int
ec_point_duplicate(ec_point_t R, ec_point_t P, ec_curve_t E, mpmod_t n)
{
    if(E->type == ECM_EC_TYPE_WEIERSTRASS)
	return pt_w_duplicate(R->x, R->y, R->z, P->x, P->y, P->z, n, E);
}

void
ec_point_negate(ec_point_t P, ec_curve_t E, mpmod_t n)
{
    if(ec_point_is_zero(P, E, n) != 0){
	if(E->type == ECM_EC_TYPE_WEIERSTRASS)
	    mpres_neg(P->y, P->y, n);
    }
}

#define EC_ADD_SUB_WMAX 6
#define EC_ADD_SUB_2_WMAX (1 << EC_ADD_SUB_WMAX)

/* multiply P=(x:y:z) by e and puts the result in Q.
   Return value: 0 if a factor is found, and the factor is in Q->x,
                 1 otherwise.
   See Solinas 2000 for the most plug-and-play presentation.		 
*/
int
ec_point_mul_add_sub (ec_point_t Q, mpz_t e, ec_point_t P,
		      ec_curve_t E, mpmod_t n)
{
    int negated = 0, status = 1, iS = 0, j, w, k, i;
    ec_point_t P0;
    ec_point_t iP[EC_ADD_SUB_2_WMAX];
    long S[64];
    
    if(ec_point_is_zero(P, E, n)){
	ec_point_set(Q, P, E, n);
	return 1;
    }
    
    if(mpz_sgn(e) == 0){
	ec_point_set_to_zero(Q, E, n);
	return 1;
    }
    
    /* The negative of a point (x:y:z) is (x:-y:z) */
    if(mpz_sgn (e) < 0){
	negated = 1;
	mpz_neg (e, e);
	ec_point_negate(P, E, n);
    }
    
    if (mpz_cmp_ui (e, 1) == 0){
	ec_point_set(Q, P, E, n);
	return 1;
    }

    ec_point_init(P0, E, n);

    w = 3; /* TODO: do better? */

    k = (1 << (w-2)) - 1;
    for(i = 0; i <= k; i++)
	ec_point_init(iP[i], E, n);
    ec_point_set(iP[0], P, E, n);
    if(k > 0){
	/* P[k] <- [2]*P */
	if(ec_point_duplicate(iP[k], P, E, n) == 0){
	    mpres_set(P0->x, iP[k]->x, n);
            status = 0;
	    goto ec_point_mul_add_sub_end;
        }
	for(i = 1; i <= k; i++){
	    if(ec_point_add(iP[i], iP[i-1], iP[k], E, n) == 0){
		mpres_set(P0->x, iP[i]->x, n);
                status = 0;
		goto ec_point_mul_add_sub_end;
	    }
	}
	/* at this point, P[i] = (2*i+1) P */
    }
  
    iS = build_NAF(S, e, w);
    ec_point_set_to_zero(P0, E, n);

    for(j = iS-1; j >= 0; j--){
	if(ec_point_duplicate(P0, P0, E, n) == 0){
	    status = 0;
	    break;
	}
#if DEBUG_EC_W >= 2
	printf("Rdup:="); pt_w_print(x0, y0, z0, n); printf(";\n");
#endif
	if(S[j] != 0){
	    i = abs(S[j]) >> 1; /* (abs(S[j])-1)/2, S[j] is always odd */
	    if(S[j] > 0){
		if(ec_point_add(P0, P0, iP[i], E, n) == 0){
		    status = 0;
		    break;
		}
#if DEBUG_EC_W >= 2
		printf("Radd:="); pt_w_print(x0, y0, z0, n); printf(";\n");
#endif
	    }
	    else{
		/* add(-P) = sub(P) */
		if(ec_point_sub(P0, P0, iP[i], E, n) == 0){
		    status = 0;
		    break;
		}
#if DEBUG_EC_W >= 2
		printf("Rsub:="); pt_w_print(x0, y0, z0, n); printf(";\n");
#endif
	    }
	}
    }
#if 0
#if EC_W_LAW == EC_W_LAW_PROJECTIVE
    /* not clear! Perhaps it has sense to normalize P? */
    mpres_gcd(z, z0, n); /* very costly */
    if(mpz_cmp_ui(z, 1) != 0){
	/* factor found, even n... */
	mpres_set(x0, z, n);
	status = 0;
    }
#endif
#endif
#if DEBUG_EC_W >= 2
    {
	mpres_t xx, yy, zz;
	mpz_t e;

	mpz_init_set_si(e, c);
	mpres_init(xx, n); mpres_set(xx, x, n);
	mpres_init(yy, n); mpres_set(yy, y, n);
	mpres_init(zz, n); mpres_set(zz, z, n);
	pt_w_mul(xx, yy, zz, e, E, n);
	if(pt_w_cmp(x0, y0, z0, xx, yy, zz, n) != 1){
	    printf("PB\n");
	    printf("as:="); pt_w_print(x0, y0, z0, n); printf(";\n");
	    printf("lo:="); pt_w_print(xx, yy, zz, n); printf(";\n");
	    exit(-1);
	}
	mpz_clear(e);
	mpres_clear(xx, n);
	mpres_clear(yy, n);
	mpres_clear(zz, n);
    }
#endif
 ec_point_mul_add_sub_end:
    ec_point_set(Q, P0, E, n);
    ec_point_clear(P0, E, n);
    for(i = 0; i <= k; i++)
	ec_point_clear(iP[i], E, n);

    /* Undo negation to avoid changing the caller's e value */
    if (negated)
	mpz_neg (e, e);
    return status;
}

/* TODO: change this according to E->type */
void
ec_point_init(ec_point_t P, ec_curve_t E, mpmod_t n)
{
    mpres_init(P->x, n);
    mpres_init(P->y, n);
    mpres_init(P->z, n);
    if(E->type == ECM_EC_TYPE_WEIERSTRASS){    
#if EC_W_LAW == EC_W_LAW_AFFINE
	mpz_set_ui (P->z, 1);
#else
	mpres_set_ui(P->z, 1, n);
#endif
    }
}

/* TODO: change this according to E->type */
void
ec_point_clear(ec_point_t P, ec_curve_t E, mpmod_t n)
{
    mpres_clear(P->x, n);
    mpres_clear(P->y, n);
    mpres_clear(P->z, n);
}

void
ec_point_print(ec_point_t P, ec_curve_t E, mpmod_t n)
{
    if(E->type == ECM_EC_TYPE_WEIERSTRASS){
	pt_w_print(P->x, P->y, P->z, n);
    }
    else if(E->type == ECM_EC_TYPE_HESSIAN){
	printf("u:="); print_mpz_from_mpres(P->x, n); printf(";\n");
	printf("v:="); print_mpz_from_mpres(P->y, n); printf(";\n");
	printf("w:="); print_mpz_from_mpres(P->z, n); printf(";\n");
	printf("P:=[u, v, w];\n");
    }
}

/* TODO: should depend on E->type... */
void
ec_point_set(ec_point_t Q, ec_point_t P, ec_curve_t E, mpmod_t n)
{
    mpres_set(Q->x, P->x, n);
    mpres_set(Q->y, P->y, n);
    mpres_set(Q->z, P->z, n);
}

int
ec_point_mul(ec_point_t Q, mpz_t e, ec_point_t P, ec_curve_t E, mpmod_t n)
{
    return ec_point_mul_add_sub(Q, e, P, E, n);
}

void
ec_curve_init(ec_curve_t E, mpmod_t n)
{
    int i;

    E->type = ECM_EC_TYPE_MONTGOMERY; /* default */
    mpres_init(E->A, n);
    for(i = 0; i < EC_W_NBUFS; i++)
	mpres_init (E->buf[i], n);
}

void
ec_curve_init_set(ec_curve_t E, mpres_t A, int type, mpmod_t n)
{
    int i;

    ec_curve_init(E, n);
    E->type = type;
    mpres_set(E->A, A, n);
}

void
ec_curve_clear(ec_curve_t E, mpmod_t n)
{
    int i;

    mpres_clear(E->A, n);
    for(i = 0; i < EC_W_NBUFS; i++)
	mpres_clear (E->buf[i], n);
}

void
ec_curve_print(ec_curve_t E, mpmod_t n)
{
    if(E->type == ECM_EC_TYPE_WEIERSTRASS){    
	printf("A:="); print_mpz_from_mpres(E->A, n); printf(";\n");
	printf("E:=[A, y0^2-x0^3-A*x0];\n");
    }
    else if(E->type == ECM_EC_TYPE_HESSIAN){
	printf("D:="); print_mpz_from_mpres(E->A, n); printf(";\n");
	printf("E:=[D];\n");
    }
}

/* Input: when Etype == ECM_EC_TYPE_WEIERSTRASS:
            (x, y) is initial point
            A is curve parameter in Weierstrass's form:
            Y^2 = X^3 + A*X + B, where B = y^2-(x^3+A*x) is implicit
	  when Etype == ECM_EC_TYPE_HESSIAN:
	    (x, y) is initial point
	    A is curve parameter in Hessian form: X^3+Y^3+Z^3=3*A*X*Y*Z
          n is the number to factor
	  B1 is the stage 1 bound
   Output: If a factor is found, it is returned in f.
           Otherwise, (x, y) contains the point computed in stage 1.
	   B1done is set to B1 if stage 1 completed normally,
	   or to the largest prime processed if interrupted, but never
	   to a smaller value than B1done was upon function entry.
   Return value: ECM_FACTOR_FOUND_STEP1 if a factor is found, otherwise 
           ECM_NO_FACTOR_FOUND
*/
static int
ecm_stage1_W (mpz_t f, int Etype, mpres_t x, mpres_t y, mpres_t A, mpmod_t n, 
	      double B1, double *B1done, mpz_t go, int (*stop_asap)(void), 
	      char *chkfilename)
{
    mpres_t xB;
    ec_curve_t E;
    ec_point_t P, Q;
    double p = 0.0, r, last_chkpnt_p;
    int ret = ECM_NO_FACTOR_FOUND;
    long last_chkpnt_time;
    
    mpres_init (xB, n);

    ec_curve_init_set(E, A, Etype, n);
    ec_point_init(P, E, n);
    mpres_set(P->x, x, n);
    mpres_set(P->y, y, n);
    ec_point_init(Q, E, n);
    
    last_chkpnt_time = cputime ();

#if DEBUG_EC_W >= 1
    ec_curve_print(E, n);
    printf("P:="); ec_point_print(P, E, n); printf(";\n");
#endif
    /* preload group order */
    if (go != NULL){
	if (ec_point_mul (Q, go, P, E, n) == 0){
	    mpz_set (f, x);
	    ret = ECM_FACTOR_FOUND_STEP1;
	    goto end_of_stage1_w;
        }
	ec_point_set(P, Q, E, n);
    }
#if DEBUG_EC_W >= 1
    gmp_printf("go:=%Zd;\n", go);
    printf("goP:="); ec_point_print(P, E, n); printf(";\n");
#endif
    for (r = 2.0; r <= B1; r *= 2.0)
	if (r > *B1done){
	    if(ec_point_duplicate (Q, P, E, n) == 0){
		mpz_set(f, Q->x);
		ret = ECM_FACTOR_FOUND_STEP1;
		goto end_of_stage1_w;
	    }
	    ec_point_set(P, Q, E, n);
#if DEBUG_EC_W >= 1
	    printf("P%ld:=", (long)r); ec_point_print(P, E, n); printf(";\n");
	    printf("Q:=EcmMult(2, Q, E, N);\n");
	    printf("(Q[1]*P%ld[3]-Q[3]*P%ld[1]) mod N;\n",(long)r,(long)r);
#endif
	}

    last_chkpnt_p = 3.;
    for (p = getprime (); p <= B1; p = getprime ()){
	for (r = p; r <= B1; r *= p){
	    if (r > *B1done){
		mpz_set_ui(f, (ecm_uint)p);
		if(ec_point_mul_add_sub (Q, f, P, E, n) == 0){
		    mpz_set(f, Q->x);
		    ret = ECM_FACTOR_FOUND_STEP1;
		    goto end_of_stage1_w;
		}
#if DEBUG_EC_W >= 1
		printf("R%ld:=", (long)r); ec_point_print(P, E, n); printf(";\n");
		printf("Q:=EcmMult(%ld, Q, E, N);\n", (long)r);
		printf("(Q[1]*R%ld[3]-Q[3]*R%ld[1]) mod N;\n",(long)r,(long)r);
#endif
	    }
	}
	ec_point_set(P, Q, E, n);
	if (mpres_is_zero (P->z, n)){
	    outputf (OUTPUT_VERBOSE, "Reached point at infinity, %.0f divides "
		     "group order\n", p);
	    break;
	}
	
	if (stop_asap != NULL && (*stop_asap) ()){
	    outputf (OUTPUT_NORMAL, "Interrupted at prime %.0f\n", p);
	    break;
	}
	
	if (chkfilename != NULL && p > last_chkpnt_p + 10000. && 
	    elltime (last_chkpnt_time, cputime ()) > CHKPNT_PERIOD){
	    writechkfile (chkfilename, ECM_ECM, MAX(p, *B1done), 
			  n, E->A, P->x, P->y, P->z);
	    last_chkpnt_p = p;
	    last_chkpnt_time = cputime ();
	}
    }
 end_of_stage1_w:
    /* If stage 1 finished normally, p is the smallest prime > B1 here.
       In that case, set to B1 */
    if (p > B1)
	p = B1;
    
    if (p > *B1done)
	*B1done = p;
    
    if (chkfilename != NULL)
	writechkfile (chkfilename, ECM_ECM, *B1done, n, E->A, P->x, P->y,P->z);
    getprime_clear (); /* free the prime tables, and reinitialize */
    
#if EC_W_LAW == EC_W_LAW_PROJECTIVE
    if(mpz_sgn(P->z) == 0){ /* TODO: not for Hessian stuff */
	/* too bad */
	ec_point_set_to_zero(P, E, n);
	mpz_set(f, n->orig_modulus);
	ret = ECM_FACTOR_FOUND_STEP1;
    }
    else if (!mpres_invert (xB, P->z, n)){ /* Factor found? */
	mpres_gcd (f, P->z, n);
	ret = ECM_FACTOR_FOUND_STEP1;
    }
    else{
	/* normalize to get (x:y:1) */
	mpres_mul (P->x, P->x, xB, n);
	mpres_mul (P->y, P->y, xB, n);
    }
#endif

    mpres_set(x, P->x, n);
    mpres_set(y, P->y, n);
    
    mpres_clear (xB, n);
    ec_point_clear(P, E, n);
    ec_point_clear(Q, E, n);
    ec_curve_clear(E, n);
    
    return ret;
}

/* choose "optimal" S according to step 2 range B2 */
int
choose_S (mpz_t B2len)
{
  if (mpz_cmp_d (B2len, 1e7) < 0)
    return 1;   /* x^1 */
  else if (mpz_cmp_d (B2len, 1e8) < 0)
    return 2;   /* x^2 */
  else if (mpz_cmp_d (B2len, 1e9) < 0)
    return -3;  /* Dickson(3) */
  else if (mpz_cmp_d (B2len, 1e10) < 0)
    return -6;  /* Dickson(6) */
  else if (mpz_cmp_d (B2len, 3e11) < 0)
    return -12; /* Dickson(12) */
  else
    return -30; /* Dickson(30) */
}

#define DIGITS_START 35
#define DIGITS_INCR   5
#define DIGITS_END   80

void
print_expcurves (double B1, const mpz_t B2, unsigned long dF, unsigned long k, 
                 int S, int param)
{
  double prob;
  int i, j;
  char sep, outs[128];
  double smoothness_correction;
  
  if (param == ECM_PARAM_SUYAMA || param == ECM_PARAM_BATCH_2)
      smoothness_correction = 1.0; 
  else if (param == ECM_PARAM_BATCH_SQUARE)
      smoothness_correction = EXTRA_SMOOTHNESS_SQUARE;
  else if (param == ECM_PARAM_BATCH_32BITS_D)
      smoothness_correction = EXTRA_SMOOTHNESS_32BITS_D;
  else /* This case should never happen */
      smoothness_correction = 0.0; 

  for (i = DIGITS_START, j = 0; i <= DIGITS_END; i += DIGITS_INCR, j += 3)
    sprintf (outs + j, "%2u%c", i, (i < DIGITS_END) ? '\t' : '\n');
  outs[j] = '\0';
  outputf (OUTPUT_VERBOSE, "Expected number of curves to find a factor "
           "of n digits:\n%s", outs);
  for (i = DIGITS_START; i <= DIGITS_END; i += DIGITS_INCR)
    {
      sep = (i < DIGITS_END) ? '\t' : '\n';
      prob = ecmprob (B1, mpz_get_d (B2),
                      /* smoothness depends on the parametrization */
                      pow (10., i - .5) / smoothness_correction,
                      (double) dF * dF * k, S);
      if (prob > 1. / 10000000)
        outputf (OUTPUT_VERBOSE, "%.0f%c", floor (1. / prob + .5), sep);
      else if (prob > 0.)
        outputf (OUTPUT_VERBOSE, "%.2g%c", floor (1. / prob + .5), sep);
      else
        outputf (OUTPUT_VERBOSE, "Inf%c", sep);
    }
}

void
print_exptime (double B1, const mpz_t B2, unsigned long dF, unsigned long k, 
               int S, double tottime, int param)
{
  double prob, exptime;
  int i, j;
  char sep, outs[128];
  double smoothness_correction;
  
  if (param == ECM_PARAM_SUYAMA || param == ECM_PARAM_BATCH_2)
      smoothness_correction = 1.0; 
  else if (param == ECM_PARAM_BATCH_SQUARE)
      smoothness_correction = EXTRA_SMOOTHNESS_SQUARE;
  else if (param == ECM_PARAM_BATCH_32BITS_D)
      smoothness_correction = EXTRA_SMOOTHNESS_32BITS_D;
  else /* This case should never happen */
      smoothness_correction = 0.0; 
  
  for (i = DIGITS_START, j = 0; i <= DIGITS_END; i += DIGITS_INCR, j += 3)
    sprintf (outs + j, "%2u%c", i, (i < DIGITS_END) ? '\t' : '\n');
  outs[j] = '\0';
  outputf (OUTPUT_VERBOSE, "Expected time to find a factor of n digits:\n%s",
           outs);
  for (i = DIGITS_START; i <= DIGITS_END; i += DIGITS_INCR)
    {
      sep = (i < DIGITS_END) ? '\t' : '\n';
      prob = ecmprob (B1, mpz_get_d (B2),
                      /* in batch mode, the extra smoothness is smaller */
                      pow (10., i - .5) / smoothness_correction,
                      (double) dF * dF * k, S);
      exptime = (prob > 0.) ? tottime / prob : HUGE_VAL;
      outputf (OUTPUT_TRACE, "Digits: %d, Total time: %.0f, probability: "
               "%g, expected time: %.0f\n", i, tottime, prob, exptime);
      if (exptime < 1000.)
        outputf (OUTPUT_VERBOSE, "%.0fms%c", exptime, sep);
      else if (exptime < 60000.) /* One minute */
        outputf (OUTPUT_VERBOSE, "%.2fs%c", exptime / 1000., sep);
      else if (exptime < 3600000.) /* One hour */
        outputf (OUTPUT_VERBOSE, "%.2fm%c", exptime / 60000., sep);
      else if (exptime < 86400000.) /* One day */
        outputf (OUTPUT_VERBOSE, "%.2fh%c", exptime / 3600000., sep);
      else if (exptime < 31536000000.) /* One year */
        outputf (OUTPUT_VERBOSE, "%.2fd%c", exptime / 86400000., sep);
      else if (exptime < 31536000000000.) /* One thousand years */
        outputf (OUTPUT_VERBOSE, "%.2fy%c", exptime / 31536000000., sep);
      else if (exptime < 31536000000000000.) /* One million years */
        outputf (OUTPUT_VERBOSE, "%.0fy%c", exptime / 31536000000., sep);
      else if (prob > 0.)
        outputf (OUTPUT_VERBOSE, "%.1gy%c", exptime / 31536000000., sep);
      else 
        outputf (OUTPUT_VERBOSE, "Inf%c", sep);
    }
}

/* y should be NULL for P+1, and P-1, it contains the y coordinate for the
   Weierstrass form for ECM (when sigma_is_A = -1). */
/* if gpu != 0 then it contains the number of curves that will be computed on
   the GPU */
void
print_B1_B2_poly (int verbosity, int method, double B1, double B1done, 
		  mpz_t B2min_param, mpz_t B2min, mpz_t B2, int S, mpz_t sigma,
		  int sigma_is_A, mpz_t y, int param, unsigned int gpu)
{
  ASSERT ((method == ECM_ECM) || (y == NULL));
  ASSERT ((-1 <= sigma_is_A) && (sigma_is_A <= 1));
  ASSERT (param != ECM_PARAM_DEFAULT || sigma_is_A == 1 || sigma_is_A == -1);

  if (test_verbose (verbosity))
  {
      outputf (verbosity, "Using ");
      if (ECM_IS_DEFAULT_B1_DONE(B1done))
	  outputf (verbosity, "B1=%1.0f, ", B1);
      else
	  outputf (verbosity, "B1=%1.0f-%1.0f, ", B1done, B1);
      if (mpz_sgn (B2min_param) < 0)
	  outputf (verbosity, "B2=%Zd", B2);
      else
	  outputf (verbosity, "B2=%Zd-%Zd", B2min, B2);
      
      if (S > 0)
	  outputf (verbosity, ", polynomial x^%u", S);
      else if (S < 0)
	  outputf (verbosity, ", polynomial Dickson(%u)", -S);
      
      /* don't print in resume case, since x0 is saved in resume file */
      if (method == ECM_ECM)
        {
	        if (sigma_is_A == 1)
	            outputf (verbosity, ", A=%Zd", sigma);
	        else if (sigma_is_A == 0)
            {
              if (gpu) /* if not 0, contains number_of_curves */
                {
                  outputf (verbosity, ", sigma=%d:%Zd", param, sigma);
                  mpz_add_ui (sigma, sigma, gpu-1);
                  outputf (verbosity, "-%d:%Zd", param, sigma);
                  mpz_sub_ui (sigma, sigma, gpu-1);
                  outputf (verbosity, " (%u curves)", gpu);
                }
              else
                  outputf (verbosity, ", sigma=%d:%Zd", param, sigma);
	          }
          else /* sigma_is_A = -1: curve was given in Weierstrass form */
            outputf (verbosity, ", Weierstrass(A=%Zd,y=%Zd)", sigma, y);
        }
      else if (ECM_IS_DEFAULT_B1_DONE(B1done))
        /* in case of P-1 or P+1, we store the initial point in sigma */
        outputf (verbosity, ", x0=%Zd", sigma);
      
      outputf (verbosity, "\n");
  }
}

/* Compute parameters for stage 2*/
int
set_stage_2_params (mpz_t B2, mpz_t B2_parm, mpz_t B2min, mpz_t B2min_parm, 
                    root_params_t *root_params, double B1, double B2scale,
                    unsigned long *k, const int S, int use_ntt, int *po2,
                    unsigned long *dF, char *TreeFilename, double maxmem, 
                    int Fermat, mpmod_t modulus)
{
  mpz_init_set (B2min, B2min_parm);
  mpz_init_set (B2, B2_parm);
  
  mpz_init (root_params->i0);

  /* set second stage bound B2: when using polynomial multiplication of
     complexity n^alpha, stage 2 has complexity about B2^(alpha/2), and
     we want stage 2 to take about half of stage 1, thus we choose
     B2 = (c*B1)^(2/alpha). Experimentally, c=1/4 seems to work well.
     For Toom-Cook 3, this gives alpha=log(5)/log(3), and B2 ~ (c*B1)^1.365.
     For Toom-Cook 4, this gives alpha=log(7)/log(4), and B2 ~ (c*B1)^1.424. */

  /* We take the cost of P+1 stage 1 to be about twice that of P-1.
     Since nai"ve P+1 and ECM cost respectively 2 and 11 multiplies per
     addition and duplicate, and both are optimized with PRAC, we can
     assume the ratio remains about 11/2. */

  /* Also scale B2 by what the user said (or by the default scaling of 1.0) */

  if (ECM_IS_DEFAULT_B2(B2))
    mpz_set_d (B2, B2scale * pow (ECM_COST * B1, DEFAULT_B2_EXPONENT));

  /* set B2min */
  if (mpz_sgn (B2min) < 0)
    mpz_set_d (B2min, B1);

  /* Let bestD determine parameters for root generation and the 
     effective B2 */

  if (use_ntt)
    *po2 = 1;

  root_params->d2 = 0; /* Enable automatic choice of d2 */
  if (bestD (root_params, k, dF, B2min, B2, *po2, use_ntt, maxmem, 
             (TreeFilename != NULL), modulus) == ECM_ERROR)
    {
      return ECM_ERROR;
    }

  /* Set default degree for Brent-Suyama extension */
  /* We try to keep the time used by the Brent-Suyama extension
     at about 10% of the stage 2 time */
  /* Degree S Dickson polys and x^S are equally fast for ECM, so we go for
     the better Dickson polys whenever possible. For S == 1, 2, they behave
     identically. */

  root_params->S = S;
  if (root_params->S == ECM_DEFAULT_S)
    {
      if (Fermat > 0)
        {
          /* For Fermat numbers, default is 1 (no Brent-Suyama) */
          root_params->S = 1;
        }
      else
        {
          mpz_t t;
          mpz_init (t);
          mpz_sub (t, B2, B2min);
          root_params->S = choose_S (t);
          mpz_clear (t);
        }
    }
  return ECM_NO_FACTOR_FOUND;
}

/* Input: x is starting point or zero
          y is used for Weierstrass curves (even in Step1)
          sigma is sigma value (if x is set to zero) or 
            A parameter (if x is non-zero) of curve
          n is the number to factor
          go is the initial group order to preload  
          B1, B2 are the stage 1/stage 2 bounds, respectively
          B2min the lower bound for stage 2
          B2scale is the stage 2 scale factor
          k is the number of blocks to do in stage 2
          S is the degree of the Suyama-Brent extension for stage 2
          verbose is verbosity level: 0 no output, 1 normal output,
            2 diagnostic output.
	  sigma_is_A: If true, the sigma parameter contains the curve's A value
	  Etype
   Output: f is the factor found.
   Return value: ECM_FACTOR_FOUND_STEPn if a factor was found,
                 ECM_NO_FACTOR_FOUND if no factor was found,
		 ECM_ERROR in case of error.
   (x, y) contains the new point at the end of Stage 1.
*/
int
ecm (mpz_t f, mpz_t x, mpz_t y, int *param, mpz_t sigma, mpz_t n, mpz_t go, 
     double *B1done, double B1, mpz_t B2min_parm, mpz_t B2_parm, double B2scale,
     unsigned long k, const int S, int verbose, int repr, int nobase2step2, 
     int use_ntt, int sigma_is_A, int Etype, 
     FILE *os, FILE* es, char *chkfilename, char
     *TreeFilename, double maxmem, double stage1time, gmp_randstate_t rng, int
     (*stop_asap)(void), mpz_t batch_s, double *batch_last_B1_used,
     ATTRIBUTE_UNUSED double gw_k, ATTRIBUTE_UNUSED unsigned long gw_b,
     ATTRIBUTE_UNUSED unsigned long gw_n, ATTRIBUTE_UNUSED signed long gw_c)
{
  int youpi = ECM_NO_FACTOR_FOUND;
  int base2 = 0;  /* If n is of form 2^n[+-]1, set base to [+-]n */
  int Fermat = 0; /* If base2 > 0 is a power of 2, set Fermat to base2 */
  int po2 = 0;    /* Whether we should use power-of-2 poly degree */
  long st;
  mpmod_t modulus;
  curve P;
  mpz_t B2min, B2; /* Local B2, B2min to avoid changing caller's values */
  unsigned long dF;
  root_params_t root_params;

  /*  1: sigma contains A from Montgomery form By^2 = x^3 + Ax^2 + x
      0: sigma contains sigma
     -1: sigma contains A from Weierstrass form y^2 = x^3 + Ax + B,
         and y contains y */
  ASSERT((-1 <= sigma_is_A) && (sigma_is_A <= 1));
  ASSERT((GMP_NUMB_BITS == 32) || (GMP_NUMB_BITS == 64));

  set_verbose (verbose);
  ECM_STDOUT = (os == NULL) ? stdout : os;
  ECM_STDERR = (es == NULL) ? stdout : es;

#ifdef MPRESN_NO_ADJUSTMENT
  /* When no adjustment is made in mpresn_ functions, N should be smaller
     than B^n/16 */
  if (mpz_sizeinbase (n, 2) > mpz_size (n) * GMP_NUMB_BITS - 4)
    {
      outputf (OUTPUT_ERROR, "Error, N should be smaller than B^n/16\n");
      return ECM_ERROR;
    }
#endif
 
  /* If the parametrization is not given, choose it.*/
  if (*param == ECM_PARAM_DEFAULT)
      *param = get_default_param (sigma_is_A, sigma, B1, *B1done);

  /* In batch mode, 
        we force repr=MODMULN, 
        B1done should either the default value or greater than B1 
        x should be either 0 (undetermined) or 2 */
  if (IS_BATCH_MODE(*param))
    {
      if (repr == ECM_MOD_DEFAULT || repr == ECM_MOD_NOBASE2)
        repr = ECM_MOD_MODMULN;
      else if (repr != ECM_MOD_MODMULN)
        {
          outputf (OUTPUT_ERROR, "Error, with param %d, repr should be " 
                                 "ECM_MOD_MODMULN.\n", *param);
          return ECM_ERROR;
        }

      if (!ECM_IS_DEFAULT_B1_DONE(*B1done) && *B1done < B1)
        {
          outputf (OUTPUT_ERROR, "Error, cannot resume with param %d, except " 
                                 "for doing only stage 2\n");
          return ECM_ERROR;
        }

      if (sigma_is_A >= 0 && mpz_sgn (x) != 0 && mpz_cmp_ui (x, 2) != 0)
        {
          if (ECM_IS_DEFAULT_B1_DONE(*B1done))
            {
              outputf (OUTPUT_ERROR, "Error, x0 should be equal to 2 with this "
                                     "parametrization\n");
              return ECM_ERROR;
            }
        }
    }

  /* check that if ECM_PARAM_BATCH_SQUARE is used, GMP_NUMB_BITS == 64*/
  if (*param == ECM_PARAM_BATCH_SQUARE && GMP_NUMB_BITS == 32)
    {
      outputf (OUTPUT_ERROR, "Error, parametrization ECM_PARAM_BATCH_SQUARE "
                             "works only with GMP_NUMB_BITS=64\n");
      return ECM_ERROR;
    }

  /* check that B1 is not too large */
  if (B1 > (double) ECM_UINT_MAX)
    {
      outputf (OUTPUT_ERROR, "Error, maximal step 1 bound for ECM is %lu.\n", 
               ECM_UINT_MAX);
      return ECM_ERROR;
    }

  /* Compute s for the batch mode */
  if (IS_BATCH_MODE(*param) && ECM_IS_DEFAULT_B1_DONE(*B1done) &&
      (B1 != *batch_last_B1_used || mpz_cmp_ui (batch_s, 1) <= 0))
    {
      *batch_last_B1_used = B1;

      st = cputime ();
      /* construct the batch exponent */
      compute_s (batch_s, B1);
      outputf (OUTPUT_VERBOSE, "Computing batch product (of %zu bits) of "
                               "primes below B1=%1.0f took %ldms\n", 
               mpz_sizeinbase (batch_s, 2), B1, cputime () - st);
    }

  st = cputime ();

  if (mpmod_init (modulus, n, repr) != 0)
    return ECM_ERROR;

  /* See what kind of number we have as that may influence optimal parameter 
     selection. Test for base 2 number. Note: this was already done by
     mpmod_init. */

  if (modulus->repr == ECM_MOD_BASE2)
    base2 = modulus->bits;

  /* For a Fermat number (base2 a positive power of 2) */
  for (Fermat = base2; Fermat > 0 && (Fermat & 1) == 0; Fermat >>= 1);
  if (Fermat == 1) 
    {
      Fermat = base2;
      po2 = 1;
    }
  else
      Fermat = 0;

  mpres_init (P.x, modulus);
  mpres_init (P.y, modulus);
  mpres_init (P.A, modulus);

  youpi = set_stage_2_params (B2, B2_parm, B2min, B2min_parm, &root_params, 
                              B1, B2scale, &k, S, use_ntt, &po2, &dF, 
                              TreeFilename, maxmem, Fermat, modulus);
  if (youpi == ECM_ERROR)
      goto end_of_ecm;
  
  if (sigma_is_A == 0)
    {
      if (mpz_sgn (sigma) == 0)
        {
          youpi = get_curve_from_random_parameter (f, P.A, P.x, sigma, *param,
                                                   modulus, rng);

          if (youpi == ECM_ERROR)
            {
              outputf (OUTPUT_ERROR, "Error, invalid parametrization.\n");
	            goto end_of_ecm;
            }
        }
      else /* Compute A and x0 from given sigma values */
        {
          if (*param == ECM_PARAM_SUYAMA)
              youpi = get_curve_from_param0 (f, P.A, P.x, sigma, modulus);
          else if (*param == ECM_PARAM_BATCH_SQUARE)
              youpi = get_curve_from_param1 (P.A, P.x, sigma, modulus);
          else if (*param == ECM_PARAM_BATCH_2)
              youpi = get_curve_from_param2 (f, P.A, P.x, sigma, modulus);
          else if (*param == ECM_PARAM_BATCH_32BITS_D)
              youpi = get_curve_from_param3 (P.A, P.x, sigma, modulus);
          else
            {
              outputf (OUTPUT_ERROR, "Error, invalid parametrization.\n");
              youpi = ECM_ERROR;
	            goto end_of_ecm;
            }
      
          /* If x != 0 we use this value for the starting point */ 
          if (mpz_sgn(x) != 0) /* humf */
              mpres_set_z (P.x, x, modulus);
      
          if (youpi != ECM_NO_FACTOR_FOUND)
            {
              if (youpi == ECM_ERROR)
                  outputf (OUTPUT_ERROR, "Error, invalid value of sigma.\n");

	            goto end_of_ecm;
            }
        }
    }
  else if (sigma_is_A == 1)
    {
      /* sigma contains the A value */
      mpres_set_z (P.A, sigma, modulus);
      /* TODO: make a valid, random starting point in case none was given */
      /* Problem: this may be as hard as factoring as we'd need to determine
         whether x^3 + a*x^2 + x is a quadratic residue or not */
      /* For now, we'll just chicken out. */
      /* Except for batch mode where we know that x0=2 */
      if (mpz_sgn (x) == 0)
        {
          if (IS_BATCH_MODE(*param))
            mpres_set_ui (P.x, 2, modulus);
          else
            {
              outputf (OUTPUT_ERROR, 
                          "Error, -A requires a starting point (-x0 x).\n");
              youpi = ECM_ERROR;
	            goto end_of_ecm;
            }
        }
      else
          mpres_set_z (P.x, x, modulus);
    }

  /* Print B1, B2, polynomial and sigma */
  print_B1_B2_poly (OUTPUT_NORMAL, ECM_ECM, B1, *B1done, B2min_parm, B2min, 
		    B2, root_params.S, sigma, sigma_is_A, y, *param, 0);

#if 0
  outputf (OUTPUT_VERBOSE, "b2=%1.0f, dF=%lu, k=%lu, d=%lu, d2=%lu, i0=%Zd\n", 
           b2, dF, k, root_params.d1, root_params.d2, root_params.i0);
#else
  outputf (OUTPUT_VERBOSE, "dF=%lu, k=%lu, d=%lu, d2=%lu, i0=%Zd\n", 
           dF, k, root_params.d1, root_params.d2, root_params.i0);
#endif

  if (sigma_is_A == -1) /* Weierstrass or Hessian form. 
			   It is known that all curves in Weierstrass form do
			   not admit a Montgomery form. However, we could
			   be interested in performing some plain Step 1
			   on a some special curves.
			*/
    {
      mpres_set_z (P.A, sigma, modulus); /* sigma contains A */
      mpres_set_z (P.x, x,    modulus);
      mpres_set_z (P.y, y,    modulus);
#if 0 /* FM */
      /* FIXME: this is not a good test */
      if (mpz_sgn (x) == 0 || mpz_sgn (go) == 0)
        {
          outputf (OUTPUT_ERROR, "Error, sigma_is_A=-1 requires x and y.\n");
	  youpi = ECM_ERROR;
	  goto end_of_ecm;
        }
      goto hecm;
#endif
    }

  if (test_verbose (OUTPUT_RESVERBOSE))
    {
      mpz_t t;

      mpz_init (t);
      mpres_get_z (t, P.A, modulus);
      outputf (OUTPUT_RESVERBOSE, "A=%Zd\n", t);
      mpres_get_z (t, P.x, modulus);
      outputf (OUTPUT_RESVERBOSE, "starting point: x0=%Zd\n", t);
      if (Etype == ECM_EC_TYPE_WEIERSTRASS)
	{
          mpres_get_z (t, P.y, modulus);
	  outputf (OUTPUT_RESVERBOSE, " y0=%Zd\n", t);
	}
      mpz_clear (t);
    }

  if (go != NULL && mpz_cmp_ui (go, 1) > 0)
    outputf (OUTPUT_VERBOSE, "initial group order: %Zd\n", go);

  if (test_verbose (OUTPUT_VERBOSE))
    {
      if (mpz_cmp_d (B2min, B1) != 0)
        {
          outputf (OUTPUT_VERBOSE, 
            "Can't compute success probabilities for B1 <> B2min\n");
        }
      else if (*param == ECM_PARAM_DEFAULT)
        {
          outputf (OUTPUT_VERBOSE, "Can't compute success probabilities " 
                                   "for unknown parametrization.\n");
        }
      else
        {
          rhoinit (256, 10);
          print_expcurves (B1, B2, dF, k, root_params.S, *param);
        }
    }

#ifdef HAVE_GWNUM
  /* We will only use GWNUM for numbers of the form k*b^n+c */

  if (gw_b != 0 && B1 >= *B1done && param == ECM_PARAM_SUYAMA)
      youpi = gw_ecm_stage1 (f, &P, modulus, B1, B1done, go, gw_k, gw_b, gw_n, gw_c);

  /* At this point B1 == *B1done unless interrupted, or no GWNUM ecm_stage1
     is available */

  if (youpi != ECM_NO_FACTOR_FOUND)
    goto end_of_ecm_rhotable;
#endif

  if (B1 > *B1done)
    {
        if (IS_BATCH_MODE(*param))
        /* FIXME: go, stop_asap and chkfilename are ignored in batch mode */
        youpi = ecm_stage1_batch (f, P.x, P.A, modulus, B1, B1done, *param, 
                                  batch_s);
        else{
	    if(Etype == ECM_EC_TYPE_MONTGOMERY)
		youpi = ecm_stage1 (f, P.x, P.A, modulus, B1, B1done, go, 
				    stop_asap, chkfilename);
	    else
		youpi = ecm_stage1_W (f, Etype, P.x, P.y, P.A, modulus, 
				      B1, B1done, go, stop_asap, chkfilename);
	}
    }
  
  if (stage1time > 0.)
    {
      const long st2 = elltime (st, cputime ());
      const long s1t = (long) (stage1time * 1000.);
      outputf (OUTPUT_NORMAL, 
               "Step 1 took %ldms (%ld in this run, %ld from previous runs)\n", 
               st2 + s1t, st2, s1t);
    }
  else
    outputf (OUTPUT_NORMAL, "Step 1 took %ldms\n", elltime (st, cputime ()));

  /* Store end-of-stage-1 residue in x in case we write it to a save file, 
     before P.x is (perhaps) converted to Weierstrass form */
  
  mpres_get_z (x, P.x, modulus);
  if (Etype == ECM_EC_TYPE_WEIERSTRASS)
    mpres_get_z (y, P.y, modulus);  

  if (youpi != ECM_NO_FACTOR_FOUND)
    goto end_of_ecm_rhotable;

  if (test_verbose (OUTPUT_RESVERBOSE)) 
    {
      mpz_t t;
      
      mpz_init (t);
      mpres_get_z (t, P.x, modulus);
      outputf (OUTPUT_RESVERBOSE, "x=%Zd\n", t);
      if (Etype == ECM_EC_TYPE_WEIERSTRASS)
	{
	  mpres_get_z (t, P.y, modulus);
	  outputf (OUTPUT_RESVERBOSE, "y=%Zd\n", t);
	}
      mpz_clear (t);
    }

  /* In case of a signal, we'll exit after the residue is printed. If no save
     file is specified, the user may still resume from the residue */
  if (stop_asap != NULL && (*stop_asap) ())
    goto end_of_ecm_rhotable;

  /* If using 2^k +/-1 modulus and 'nobase2step2' flag is set,
     set default (-nobase2) modular method and remap P.x, P.y, and P.A */
  if (modulus->repr == ECM_MOD_BASE2 && nobase2step2)
    {
      mpz_t x_t, y_t, A_t;

      mpz_init (x_t);
      mpz_init (y_t);
      mpz_init (A_t);

      mpz_mod (x_t, P.x, modulus->orig_modulus);
      mpz_mod (y_t, P.y, modulus->orig_modulus);
      mpz_mod (A_t, P.A, modulus->orig_modulus);

      mpmod_clear (modulus);

      repr = ECM_MOD_NOBASE2;
      if (mpmod_init (modulus, n, repr) != 0) /* reset modulus for nobase2 */
        return ECM_ERROR;

      /* remap x, y, and A for new modular method */
      mpres_set_z (P.x, x_t, modulus);
      mpres_set_z (P.y, y_t, modulus);
      mpres_set_z (P.A, A_t, modulus);

      mpz_clear (x_t);
      mpz_clear (y_t);
      mpz_clear (A_t);
    }

  if(Etype == ECM_EC_TYPE_MONTGOMERY)
      youpi = montgomery_to_weierstrass (f, P.x, P.y, P.A, modulus);
  /* hecm:*/
  
  if (test_verbose (OUTPUT_RESVERBOSE) && youpi == ECM_NO_FACTOR_FOUND && 
      mpz_cmp (B2, B2min) >= 0)
    {
      mpz_t t;

      mpz_init (t);
      mpres_get_z (t, P.x, modulus);
      outputf (OUTPUT_RESVERBOSE, "After switch to Weierstrass form, "
      "P=(%Zd", t);
      mpres_get_z (t, P.y, modulus);
      outputf (OUTPUT_RESVERBOSE, ", %Zd)\n", t);
      mpres_get_z (t, P.A, modulus);
      outputf (OUTPUT_RESVERBOSE, "on curve Y^2 = X^3 + %Zd * X + b\n", t);
      mpz_clear (t);
    }
  
  if (youpi == ECM_NO_FACTOR_FOUND && mpz_cmp (B2, B2min) >= 0)
    youpi = stage2 (f, &P, modulus, dF, k, &root_params, ECM_ECM, 
                    use_ntt, TreeFilename, stop_asap);
#ifdef TIMING_CRT
  printf ("mpzspv_from_mpzv_slow: %dms\n", mpzspv_from_mpzv_slow_time);
  printf ("mpzspv_to_mpzv: %dms\n", mpzspv_to_mpzv_time);
  printf ("mpzspv_normalise: %dms\n", mpzspv_normalise_time);
#endif
  
end_of_ecm_rhotable:
  if (test_verbose (OUTPUT_VERBOSE))
    {
      if (mpz_cmp_d (B2min, B1) == 0 && *param != ECM_PARAM_DEFAULT)
        {
          if (youpi == ECM_NO_FACTOR_FOUND && 
              (stop_asap == NULL || !(*stop_asap)()))
            print_exptime (B1, B2, dF, k, root_params.S, 
                           (long) (stage1time * 1000.) + 
                           elltime (st, cputime ()), *param);
          rhoinit (1, 0); /* Free memory of rhotable */
        }
    }

end_of_ecm:
  mpres_clear (P.A, modulus);
  mpres_clear (P.y, modulus);
  mpres_clear (P.x, modulus);
  mpmod_clear (modulus);
  mpz_clear (root_params.i0);
  mpz_clear (B2);
  mpz_clear (B2min);

  return youpi;
}
